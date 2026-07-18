#!/usr/bin/env python3
"""Capture a Core 1.0 manual display/touch review bound to exact UI evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import stat
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

try:
    from artifact_metadata import stamp_report
except ImportError:  # pragma: no cover - package import path used by pytest
    from scripts.artifact_metadata import stamp_report


CORE_PORT = "COM12"
CORE_RELEASE_PROFILE = "core_1_0"
CORE_SD_HISTORY_MODE = "disabled"
RELEASE_MIN_ROUNDS = 20
REQUIRED_CONFIRMATIONS = (
    "display_480x480_stable",
    "touch_accurate",
    "backlight_works",
    "home",
    "messages_public",
    "dm_workflow",
    "nodes",
    "packets",
    "settings",
    "unavailable_features_absent",
)
PHOTO_COVERAGE = frozenset(
    {
        "display_touch_backlight",
        "home",
        "messages",
        "nodes",
        "packets",
        "settings",
    }
)


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def exact_sha(value: object) -> str | None:
    if not isinstance(value, str):
        return None
    normalized = value.strip().lower()
    return normalized if re.fullmatch(r"[0-9a-f]{40}", normalized) else None


def positive_decimal(value: object) -> str | None:
    normalized = str(value).strip()
    return normalized if re.fullmatch(r"[1-9][0-9]*", normalized) else None


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def is_link_or_reparse(path: Path) -> bool:
    try:
        info = path.lstat()
    except OSError:
        return True
    return path.is_symlink() or bool(
        getattr(info, "st_file_attributes", 0)
        & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)
    )


def resolve_regular_file(root: Path, raw_path: str | Path) -> Path:
    candidate = Path(raw_path)
    if not candidate.is_absolute():
        candidate = root / candidate
    resolved = candidate.resolve(strict=True)
    resolved.relative_to(root.resolve(strict=True))
    if not resolved.is_file() or is_link_or_reparse(resolved):
        raise ValueError(f"evidence path is not a regular in-repository file: {raw_path}")
    return resolved


def file_receipt(root: Path, path: Path) -> dict[str, object]:
    return {
        "path": path.relative_to(root.resolve(strict=True)).as_posix(),
        "size": path.stat().st_size,
        "sha256": sha256_file(path),
    }


def read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError):
        return {}
    return value if isinstance(value, dict) else {}


def validate_ui_receipt(
    data: dict[str, Any],
    *,
    commit: str,
    run_id: str,
    run_attempt: str,
) -> bool:
    git_info = data.get("git")
    checks = data.get("checks")
    return (
        data.get("kind") == "core_ui_corruption_probe"
        and data.get("mode") == "hardware"
        and data.get("ok") is True
        and data.get("closure_eligible") is True
        and data.get("physical_observed") is True
        and data.get("port") == CORE_PORT
        and data.get("release_profile") == CORE_RELEASE_PROFILE
        and data.get("sd_history_mode") == CORE_SD_HISTORY_MODE
        and exact_sha(data.get("expected_firmware_commit")) == commit
        and exact_sha(data.get("device_build_commit")) == commit
        and str(data.get("github_actions_run")) == run_id
        and str(data.get("workflow_run_attempt")) == run_attempt
        and isinstance(data.get("rounds"), int)
        and not isinstance(data.get("rounds"), bool)
        and data["rounds"] >= RELEASE_MIN_ROUNDS
        and isinstance(checks, dict)
        and bool(checks)
        and all(value is True for value in checks.values())
        and isinstance(git_info, dict)
        and exact_sha(git_info.get("commit")) == commit
        and git_info.get("dirty") is False
        and git_info.get("dirty_entries") == []
        and data.get("public_rf_tx") is False
        and data.get("network_tx") is False
        and data.get("map_network_requests") is False
        and data.get("formats_sd") is False
        and all(
            data.get(flag) is not True
            for flag in ("dry_run", "simulated", "source_only", "reused")
        )
    )


def parse_photo_spec(root: Path, spec: str) -> dict[str, object]:
    if "=" not in spec:
        raise ValueError("--photo must use COVERAGE[,COVERAGE...]=PATH")
    raw_coverage, raw_path = spec.split("=", 1)
    coverage = [item.strip() for item in raw_coverage.split(",") if item.strip()]
    if not coverage or any(item not in PHOTO_COVERAGE for item in coverage):
        raise ValueError(f"invalid photo coverage: {raw_coverage}")
    path = resolve_regular_file(root, raw_path.strip())
    raw = path.read_bytes()
    if (
        path.suffix.lower() != ".png"
        or len(raw) <= 100
        or not raw.startswith(b"\x89PNG\r\n\x1a\n")
        or len(raw) < 24
        or int.from_bytes(raw[16:20], "big") <= 0
        or int.from_bytes(raw[20:24], "big") <= 0
    ):
        raise ValueError(f"photo is not a nonempty PNG: {raw_path}")
    receipt = file_receipt(root, path)
    receipt["coverage"] = coverage
    return receipt


def build_review(
    *,
    commit: str,
    run_id: str,
    run_attempt: str,
    port: str,
    operator: str,
    reviewer: str,
    confirmations: dict[str, bool],
    automated_ui_receipt: dict[str, object],
    automated_ui_receipt_valid: bool,
    photo_receipts: list[dict[str, object]] | None = None,
    notes: str = "",
) -> dict[str, Any]:
    normalized_commit = exact_sha(commit)
    normalized_run = positive_decimal(run_id)
    normalized_attempt = positive_decimal(run_attempt)
    exact_confirmations = set(confirmations) == set(REQUIRED_CONFIRMATIONS)
    confirmations_ok = exact_confirmations and all(
        confirmations.get(name) is True for name in REQUIRED_CONFIRMATIONS
    )
    photos = list(photo_receipts or [])
    photo_hashes = [row.get("sha256") for row in photos]
    photos_ok = (
        all(
            isinstance(row.get("path"), str)
            and isinstance(row.get("size"), int)
            and row["size"] > 100
            and isinstance(row.get("sha256"), str)
            and re.fullmatch(r"[0-9a-f]{64}", row["sha256"]) is not None
            and isinstance(row.get("coverage"), list)
            and bool(row["coverage"])
            and all(item in PHOTO_COVERAGE for item in row["coverage"])
            for row in photos
        )
        and len(photo_hashes) == len(set(photo_hashes))
    )
    identities_ok = (
        normalized_commit is not None
        and normalized_run is not None
        and normalized_attempt is not None
        and port.strip().upper() == CORE_PORT
        and bool(operator.strip())
        and bool(reviewer.strip())
        and operator.strip().casefold() != reviewer.strip().casefold()
    )
    ok = (
        identities_ok
        and confirmations_ok
        and automated_ui_receipt_valid
        and photos_ok
    )
    return {
        "schema": 2,
        "kind": "core_manual_ui_review",
        "mode": "manual-physical-ui-review",
        "created_at": utc_now(),
        "ok": ok,
        "closure_eligible": ok,
        "hardware_required": True,
        "physical_observed": True,
        "dry_run": False,
        "simulated": False,
        "source_only": False,
        "reused": False,
        "port": CORE_PORT if port.strip().upper() == CORE_PORT else port,
        "release_profile": CORE_RELEASE_PROFILE,
        "sd_history_mode": CORE_SD_HISTORY_MODE,
        "commit": normalized_commit or commit,
        "github_actions_run": normalized_run or run_id,
        "workflow_run_attempt": normalized_attempt or run_attempt,
        "operator": operator.strip(),
        "reviewer": reviewer.strip(),
        "confirmations": confirmations,
        "required_confirmations": list(REQUIRED_CONFIRMATIONS),
        "missing_confirmations": [
            name
            for name in REQUIRED_CONFIRMATIONS
            if confirmations.get(name) is not True
        ],
        "automated_ui_receipt": automated_ui_receipt,
        "automated_ui_receipt_valid": automated_ui_receipt_valid,
        "photo_receipts": photos,
        "photos_optional": True,
        "notes": notes,
        "public_rf_tx": False,
        "formats_sd": False,
    }


def default_out_path(root: Path, commit: str, run_id: str) -> Path:
    return (
        root
        / "artifacts"
        / "hardware"
        / "com12"
        / f"core_manual_ui_review_{commit[:12]}_run_{run_id}.json"
    )


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default=os.environ.get("D1L_PORT") or CORE_PORT)
    parser.add_argument("--expected-firmware-commit", required=True)
    parser.add_argument("--github-run-id", required=True)
    parser.add_argument("--github-run-attempt", required=True)
    parser.add_argument("--operator", required=True)
    parser.add_argument("--reviewer", required=True)
    parser.add_argument("--automated-ui-receipt", required=True)
    parser.add_argument(
        "--photo",
        action="append",
        default=[],
        metavar="COVERAGE[,COVERAGE...]=PATH",
        help="Optional current-candidate PNG evidence; repeat for more photos.",
    )
    parser.add_argument("--notes", default="")
    for name in REQUIRED_CONFIRMATIONS:
        parser.add_argument(
            "--confirm-" + name.replace("_", "-"),
            action="store_true",
            dest=name,
        )
    parser.add_argument("--out")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    root = Path(__file__).resolve().parents[1]
    commit = exact_sha(args.expected_firmware_commit)
    run_id = positive_decimal(args.github_run_id)
    run_attempt = positive_decimal(args.github_run_attempt)
    if commit is None:
        raise SystemExit("--expected-firmware-commit must be an exact 40-hex SHA")
    if run_id is None or run_attempt is None:
        raise SystemExit("GitHub run ID and attempt must be positive decimal values")
    if args.port.strip().upper() != CORE_PORT:
        raise SystemExit("Core manual UI evidence is restricted to COM12")

    try:
        ui_path = resolve_regular_file(root, args.automated_ui_receipt)
        ui_data = read_json(ui_path)
        ui_receipt = file_receipt(root, ui_path)
        photos = [parse_photo_spec(root, spec) for spec in args.photo]
    except (OSError, RuntimeError, ValueError) as exc:
        raise SystemExit(str(exc)) from exc
    ui_valid = validate_ui_receipt(
        ui_data,
        commit=commit,
        run_id=run_id,
        run_attempt=run_attempt,
    )
    confirmations = {
        name: bool(getattr(args, name)) for name in REQUIRED_CONFIRMATIONS
    }
    report = build_review(
        commit=commit,
        run_id=run_id,
        run_attempt=run_attempt,
        port=args.port,
        operator=args.operator,
        reviewer=args.reviewer,
        confirmations=confirmations,
        automated_ui_receipt=ui_receipt,
        automated_ui_receipt_valid=ui_valid,
        photo_receipts=photos,
        notes=args.notes,
    )
    stamp_report(report, root)
    git_info = report.get("git")
    source_ok = (
        isinstance(git_info, dict)
        and exact_sha(git_info.get("commit")) == commit
        and git_info.get("dirty") is False
        and git_info.get("dirty_entries") == []
    )
    report["candidate_source_clean"] = source_ok
    report["ok"] = report["ok"] is True and source_ok
    report["closure_eligible"] = report["ok"]

    out_path = (
        Path(args.out)
        if args.out
        else default_out_path(root, commit, run_id)
    )
    if not out_path.is_absolute():
        out_path = root / out_path
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(report, indent=2) + "\n", encoding="ascii")
    print(
        json.dumps(
            {"ok": report["ok"], "out": str(out_path), "kind": report["kind"]},
            indent=2,
        )
    )
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
