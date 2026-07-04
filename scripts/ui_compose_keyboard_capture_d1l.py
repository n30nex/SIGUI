#!/usr/bin/env python3
"""Capture D1L compose keyboard states from the hardware framebuffer."""

from __future__ import annotations

import argparse
import json
import os
import re
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

try:
    from artifact_metadata import stamp_report
    from ui_capture_d1l import MAX_CHUNK_BYTES, capture_frame, write_png_from_rgb565_le
except ImportError:  # pragma: no cover - package import path used by pytest
    from scripts.artifact_metadata import stamp_report
    from scripts.ui_capture_d1l import MAX_CHUNK_BYTES, capture_frame, write_png_from_rgb565_le


DEFAULT_TARGETS = ["public", "public-long", "dm", "dm-long"]


def default_json_path(root: Path) -> Path:
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    return root / "artifacts" / "ui-capture" / f"d1l-compose-keyboard-capture-{stamp}.json"


def resolve_output_path(root: Path, out: str | None) -> Path:
    if not out:
        return default_json_path(root)
    path = Path(out)
    if not path.is_absolute():
        path = root / path
    return path


def slug(value: str) -> str:
    return re.sub(r"[^a-z0-9_]+", "_", value.lower().replace("-", "_")).strip("_")


def parse_targets(text: str | None) -> list[str]:
    if not text:
        return list(DEFAULT_TARGETS)
    targets = [item.strip().lower() for item in text.split(",") if item.strip()]
    allowed = set(DEFAULT_TARGETS)
    unknown = sorted(set(targets) - allowed)
    if unknown:
        raise ValueError(f"unknown compose capture target(s): {', '.join(unknown)}")
    return targets


def dry_run_report(targets: list[str], chunk_size: int) -> dict[str, Any]:
    commands: list[str] = []
    for target in targets:
        commands.extend(
            [
                f"ui compose-probe {target}",
                "ui capture status",
                "ui capture begin",
                "ui capture chunk 0 1024",
                "ui capture end",
            ]
        )
    return {
        "schema": 1,
        "kind": "ui_compose_keyboard_capture",
        "mode": "dry-run",
        "ok": True,
        "hardware_required": False,
        "explicit_port_required": True,
        "targets": targets,
        "commands": commands,
        "width": 480,
        "height": 480,
        "bytes_per_pixel": 2,
        "pixel_format": "rgb565-le",
        "chunk_size": min(chunk_size, MAX_CHUNK_BYTES),
        "public_rf_tx": False,
        "formats_sd": False,
    }


def compose_probe_event(frame: dict[str, Any]) -> dict[str, Any] | None:
    for event in frame.get("events", []):
        if isinstance(event, dict) and event.get("cmd") == "ui compose-probe":
            return event
    return None


def capture_target(
    target: str,
    port: str,
    baud: int,
    timeout: float,
    chunk_size: int,
    output_dir: Path,
    stem: str,
) -> dict[str, Any]:
    frame = capture_frame(
        port=port,
        baud=baud,
        timeout=timeout,
        chunk_size=chunk_size,
        prep_commands=[f"ui compose-probe {target}"],
        prep_ok_required=False,
        post_prep_settle_sec=0.25,
    )
    raw = bytes(frame.pop("raw_bytes", b""))
    target_slug = slug(target)
    raw_path = output_dir / f"{stem}-{target_slug}.rgb565"
    png_path = output_dir / f"{stem}-{target_slug}.png"
    if frame.get("ok") is True or frame.get("pixel_capture_ok") is True:
        raw_path.write_bytes(raw)
        write_png_from_rgb565_le(raw, png_path, int(frame["width"]), int(frame["height"]))
        frame["raw_path"] = str(raw_path)
        frame["png_path"] = str(png_path)
    probe = compose_probe_event(frame)
    target_visible = (
        frame.get("onboarding_visible") is False
        and bool(probe and probe.get("onboarding_visible") is False)
    )
    return {
        "target": target,
        "ok": frame.get("ok") is True and bool(probe and probe.get("ok") is True) and target_visible,
        "compose_probe": probe,
        "capture": frame,
        "png_path": frame.get("png_path"),
        "raw_path": frame.get("raw_path"),
        "target_visible": target_visible,
        "public_rf_tx": False,
        "formats_sd": False,
    }


def capture_all(
    port: str,
    baud: int,
    timeout: float,
    chunk_size: int,
    targets: list[str],
    json_path: Path,
) -> dict[str, Any]:
    json_path.parent.mkdir(parents=True, exist_ok=True)
    stem = json_path.stem
    captures = [
        capture_target(target, port, baud, timeout, chunk_size, json_path.parent, stem)
        for target in targets
    ]
    return {
        "schema": 1,
        "kind": "ui_compose_keyboard_capture",
        "mode": "hardware",
        "ok": all(capture.get("ok") is True for capture in captures),
        "port": port,
        "baud": baud,
        "targets": targets,
        "captures": captures,
        "capture_count": len(captures),
        "public_rf_tx": False,
        "formats_sd": False,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default=os.environ.get("D1L_PORT"))
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=15.0)
    parser.add_argument("--chunk-size", type=int, default=MAX_CHUNK_BYTES)
    parser.add_argument("--targets", default=",".join(DEFAULT_TARGETS))
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--out", default=None, help="JSON report path")
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    targets = parse_targets(args.targets)
    json_path = resolve_output_path(root, args.out)
    if args.dry_run:
        report = dry_run_report(targets, args.chunk_size)
    else:
        if not args.port:
            parser.error("No D1L port supplied. Set D1L_PORT or pass --port.")
        report = capture_all(
            port=args.port,
            baud=args.baud,
            timeout=args.timeout,
            chunk_size=args.chunk_size,
            targets=targets,
            json_path=json_path,
        )
    stamp_report(report, root)
    json_path.parent.mkdir(parents=True, exist_ok=True)
    json_path.write_text(json.dumps(report, indent=2), encoding="ascii")
    print(json.dumps({"ok": report.get("ok"), "path": str(json_path), "capture_count": report.get("capture_count")}))
    return 0 if report.get("ok") is True else 1


if __name__ == "__main__":
    raise SystemExit(main())
