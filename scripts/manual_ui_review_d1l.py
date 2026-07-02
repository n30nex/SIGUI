#!/usr/bin/env python3
"""Create a commit-stamped manual physical UI review artifact."""

from __future__ import annotations

import argparse
import json
import os
from datetime import datetime, timezone
from pathlib import Path

try:
    from artifact_metadata import stamp_report
except ImportError:  # pragma: no cover - package import path used by pytest
    from scripts.artifact_metadata import stamp_report


PHOTO_SUFFIXES = {".jpg", ".jpeg", ".png", ".webp"}
REQUIRED_CONFIRMATIONS = (
    "display_stable",
    "touch_accurate",
    "bottom_tabs",
    "messages_public",
    "dm_workflow",
    "nodes_contacts_routes",
    "map_storage",
    "settings_sheets",
    "photos_current",
)


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def photo_files(photo_dir: Path) -> list[Path]:
    if not photo_dir.is_dir():
        return []
    return sorted(path for path in photo_dir.iterdir() if path.is_file() and path.suffix.lower() in PHOTO_SUFFIXES)


def build_review(
    *,
    port: str,
    reviewer: str,
    photo_dir: Path,
    confirmations: dict[str, bool],
    notes: str | None = None,
) -> dict:
    photos = photo_files(photo_dir)
    missing = [name for name in REQUIRED_CONFIRMATIONS if confirmations.get(name) is not True]
    ok = not missing and bool(photos)
    return {
        "schema": 1,
        "mode": "manual-physical-ui-review",
        "created_at": utc_now(),
        "port": port,
        "reviewer": reviewer,
        "photo_dir": str(photo_dir),
        "photo_count": len(photos),
        "photos": [str(path) for path in photos],
        "required_confirmations": list(REQUIRED_CONFIRMATIONS),
        "confirmations": confirmations,
        "missing_confirmations": missing,
        "notes": notes or "",
        "ok": ok,
    }


def default_hardware_dir(port: str) -> Path:
    root = Path(__file__).resolve().parents[1]
    return root / "artifacts" / "hardware" / port.lower()


def default_out_path(hardware_dir: Path, report: dict) -> Path:
    commit = str(report.get("commit") or report.get("git", {}).get("short_commit") or "unknown")
    short = commit[:7] if commit != "unknown" else "unknown"
    return hardware_dir / f"manual_touch_review_{short}.json"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default=os.environ.get("D1L_PORT") or "COM" + "12")
    parser.add_argument("--reviewer", default=os.environ.get("USERNAME") or "operator")
    parser.add_argument("--photo-dir")
    parser.add_argument("--hardware-dir")
    parser.add_argument("--notes", default="")
    for name in REQUIRED_CONFIRMATIONS:
        flag = "--confirm-" + name.replace("_", "-")
        parser.add_argument(flag, action="store_true", dest=name)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--out")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = Path(__file__).resolve().parents[1]
    hardware_dir = Path(args.hardware_dir) if args.hardware_dir else default_hardware_dir(args.port)
    if not hardware_dir.is_absolute():
        hardware_dir = root / hardware_dir
    photo_dir = Path(args.photo_dir) if args.photo_dir else hardware_dir / "photos"
    if not photo_dir.is_absolute():
        photo_dir = root / photo_dir
    confirmations = {name: bool(getattr(args, name)) for name in REQUIRED_CONFIRMATIONS}
    report = build_review(
        port=args.port,
        reviewer=args.reviewer,
        photo_dir=photo_dir,
        confirmations=confirmations,
        notes=args.notes,
    )
    report["hardware_required"] = not args.dry_run
    stamp_report(report, root)

    out_path = Path(args.out) if args.out else default_out_path(hardware_dir, report)
    if not out_path.is_absolute():
        out_path = root / out_path
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(report, indent=2) + "\n", encoding="ascii")
    print(json.dumps({"ok": report["ok"], "out": str(out_path), "mode": report["mode"]}, indent=2))
    return 0 if report.get("ok") else 1


if __name__ == "__main__":
    raise SystemExit(main())
