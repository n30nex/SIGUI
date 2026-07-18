#!/usr/bin/env python3
"""Capture an exact-package Core install/recovery operator review."""

from __future__ import annotations

import argparse
import json
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

try:
    from artifact_metadata import stamp_report
    from manual_ui_review_d1l import (
        CORE_PORT,
        CORE_RELEASE_PROFILE,
        CORE_SD_HISTORY_MODE,
        exact_sha,
        file_receipt,
        positive_decimal,
        resolve_regular_file,
    )
    from verify_checksums import verify_checksum_tree
except ImportError:  # pragma: no cover - package import path used by pytest
    from scripts.artifact_metadata import stamp_report
    from scripts.manual_ui_review_d1l import (
        CORE_PORT,
        CORE_RELEASE_PROFILE,
        CORE_SD_HISTORY_MODE,
        exact_sha,
        file_receipt,
        positive_decimal,
        resolve_regular_file,
    )
    from scripts.verify_checksums import verify_checksum_tree


REQUIRED_INSTALL_CONFIRMATIONS = (
    "checksums_reviewed",
    "normal_usb_install_reviewed",
    "recovery_steps_reviewed",
    "com12_only",
    "non_erasing_normal_flash",
    "full_flash_data_loss_warning",
    "no_on_device_sd_format",
    "supported_features_reviewed",
)
REVIEWED_PACKAGE_FILES = (
    "manifest.json",
    "SHA256SUMS.txt",
    "README_RELEASE.md",
    "SUPPORTED_FEATURES.md",
    "docs/CORE_INSTALL_RECOVERY.md",
)


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError):
        return {}
    return value if isinstance(value, dict) else {}


def validate_package_manifest(
    manifest: dict[str, Any],
    *,
    commit: str,
    run_id: str,
    run_attempt: str,
) -> bool:
    workflow = manifest.get("workflow")
    install = manifest.get("install_recovery_guide")
    return (
        manifest.get("release_profile") == CORE_RELEASE_PROFILE
        and manifest.get("sd_history_mode") == CORE_SD_HISTORY_MODE
        and exact_sha(manifest.get("firmware_commit")) == commit
        and str(manifest.get("actions_run")) == run_id
        and manifest.get("full_feature_release_ready") is False
        and manifest.get("rp2040_artifacts") == []
        and manifest.get("update_image") is None
        and isinstance(workflow, dict)
        and exact_sha(workflow.get("sha")) == commit
        and str(workflow.get("run_id")) == run_id
        and str(workflow.get("run_attempt")) == run_attempt
        and workflow.get("repository") == "n30nex/SIGUI"
        and isinstance(install, dict)
        and install.get("usb_only") is True
        and install.get("normal_install_script") == "flash_project.ps1"
        and install.get("normal_install_port") == CORE_PORT
        and install.get("normal_install_preserves_unrelated_nvs") is True
        and install.get("normal_install_package_root_only") is True
        and install.get("normal_install_checksum_verified") is True
        and install.get("recovery_script") == "flash_full_8mb.ps1"
        and install.get("recovery_requires_typed_confirmation") is True
        and install.get("recovery_checksum_verified") is True
        and install.get("install_guide") == "docs/CORE_INSTALL_RECOVERY.md"
        and install.get("recovery_guide") == "docs/CORE_INSTALL_RECOVERY.md"
        and install.get("no_on_device_sd_format") is True
    )


def build_review(
    *,
    commit: str,
    run_id: str,
    run_attempt: str,
    operator: str,
    reviewer: str,
    package_manifest: dict[str, object],
    reviewed_files: list[dict[str, object]],
    package_manifest_valid: bool,
    checksum_tree_valid: bool,
    confirmations: dict[str, bool],
    notes: str = "",
) -> dict[str, Any]:
    normalized_commit = exact_sha(commit)
    normalized_run = positive_decimal(run_id)
    normalized_attempt = positive_decimal(run_attempt)
    confirmations_ok = (
        set(confirmations) == set(REQUIRED_INSTALL_CONFIRMATIONS)
        and all(
            confirmations.get(name) is True
            for name in REQUIRED_INSTALL_CONFIRMATIONS
        )
    )
    reviewed_paths = {
        row.get("package_path")
        for row in reviewed_files
        if isinstance(row, dict)
    }
    files_ok = reviewed_paths == set(REVIEWED_PACKAGE_FILES) and all(
        isinstance(row.get("path"), str)
        and isinstance(row.get("size"), int)
        and row["size"] > 0
        and isinstance(row.get("sha256"), str)
        and len(row["sha256"]) == 64
        for row in reviewed_files
    )
    identities_ok = (
        normalized_commit is not None
        and normalized_run is not None
        and normalized_attempt is not None
        and bool(operator.strip())
        and bool(reviewer.strip())
        and operator.strip().casefold() != reviewer.strip().casefold()
    )
    ok = (
        identities_ok
        and confirmations_ok
        and package_manifest_valid
        and checksum_tree_valid
        and files_ok
        and package_manifest.get("sha256")
        == next(
            (
                row.get("sha256")
                for row in reviewed_files
                if row.get("package_path") == "manifest.json"
            ),
            None,
        )
    )
    return {
        "schema": 1,
        "kind": "core_install_recovery_review",
        "mode": "manual-package-review",
        "created_at": utc_now(),
        "ok": bool(ok),
        "closure_eligible": bool(ok),
        "physical_observed": False,
        "hardware_required": False,
        "dry_run": False,
        "simulated": False,
        "source_only": False,
        "reused": False,
        "port": CORE_PORT,
        "release_profile": CORE_RELEASE_PROFILE,
        "sd_history_mode": CORE_SD_HISTORY_MODE,
        "commit": normalized_commit or commit,
        "github_actions_run": normalized_run or run_id,
        "workflow_run_attempt": normalized_attempt or run_attempt,
        "operator": operator.strip(),
        "reviewer": reviewer.strip(),
        "package_manifest": package_manifest,
        "package_manifest_sha256": package_manifest.get("sha256"),
        "package_manifest_valid": package_manifest_valid,
        "checksum_tree_valid": checksum_tree_valid,
        "reviewed_files": reviewed_files,
        "confirmations": confirmations,
        "required_confirmations": list(REQUIRED_INSTALL_CONFIRMATIONS),
        "missing_confirmations": [
            name
            for name in REQUIRED_INSTALL_CONFIRMATIONS
            if confirmations.get(name) is not True
        ],
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
        / f"core_install_recovery_review_{commit[:12]}_run_{run_id}.json"
    )


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--expected-firmware-commit", required=True)
    parser.add_argument("--github-run-id", required=True)
    parser.add_argument("--github-run-attempt", required=True)
    parser.add_argument("--package-root", required=True)
    parser.add_argument("--operator", required=True)
    parser.add_argument("--reviewer", required=True)
    parser.add_argument("--notes", default="")
    for name in REQUIRED_INSTALL_CONFIRMATIONS:
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

    package_root = Path(args.package_root)
    if not package_root.is_absolute():
        package_root = root / package_root
    try:
        package_root = package_root.resolve(strict=True)
        package_root.relative_to(root.resolve(strict=True))
        if not package_root.is_dir():
            raise ValueError("package root is not a directory")
        manifest_path = resolve_regular_file(root, package_root / "manifest.json")
        reviewed_files = []
        for package_path in REVIEWED_PACKAGE_FILES:
            path = resolve_regular_file(root, package_root / package_path)
            receipt = file_receipt(root, path)
            receipt["package_path"] = package_path
            reviewed_files.append(receipt)
    except (OSError, RuntimeError, ValueError) as exc:
        raise SystemExit(str(exc)) from exc

    manifest_data = read_json(manifest_path)
    manifest_valid = validate_package_manifest(
        manifest_data,
        commit=commit,
        run_id=run_id,
        run_attempt=run_attempt,
    )
    confirmations = {
        name: bool(getattr(args, name))
        for name in REQUIRED_INSTALL_CONFIRMATIONS
    }
    manifest_receipt = next(
        row for row in reviewed_files if row["package_path"] == "manifest.json"
    )
    report = build_review(
        commit=commit,
        run_id=run_id,
        run_attempt=run_attempt,
        operator=args.operator,
        reviewer=args.reviewer,
        package_manifest=manifest_receipt,
        reviewed_files=reviewed_files,
        package_manifest_valid=manifest_valid,
        checksum_tree_valid=verify_checksum_tree(package_root),
        confirmations=confirmations,
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
