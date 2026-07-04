#!/usr/bin/env python3
"""Capture direct RP2040 SD raw diagnostics for MeshCore DeskOS D1L."""

from __future__ import annotations

import argparse
import json
import os
import time
from datetime import datetime, timezone
from pathlib import Path

try:
    from artifact_metadata import stamp_report
except ImportError:  # pragma: no cover - package import path used by pytest
    from scripts.artifact_metadata import stamp_report


SAFE_COMMANDS = ("DESKOS_SD_DIAG", "DESKOS_SD_STATUS", "DESKOS_SD_PING")


def utc_stamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def parse_fields(line: str) -> dict[str, str]:
    fields: dict[str, str] = {}
    for token in line.split():
        if "=" not in token:
            continue
        key, value = token.split("=", 1)
        fields[key] = value
    return fields


def dry_run_report(port: str | None) -> dict:
    return {
        "schema": 1,
        "mode": "dry-run",
        "hardware_required": False,
        "port": port,
        "commands": list(SAFE_COMMANDS),
        "public_rf_tx": False,
        "formats_sd": False,
        "ok": True,
    }


def read_reply(ser, command: str, timeout: float) -> tuple[str, list[str]]:
    deadline = time.monotonic() + timeout
    lines: list[str] = []
    while time.monotonic() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", errors="replace").strip()
        if not line:
            continue
        lines.append(line)
        if line.startswith(command):
            return line, lines
    return "", lines


def capture_diag(port: str, baud: int, timeout: float) -> dict:
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("pyserial is required: python -m pip install pyserial") from exc

    replies: dict[str, str] = {}
    captured_lines: list[str] = []
    with serial.Serial(port=port, baudrate=baud, timeout=timeout) as ser:
        time.sleep(1.0)
        ser.reset_input_buffer()
        for command in SAFE_COMMANDS:
            ser.write((command + "\n").encode("utf-8"))
            if hasattr(ser, "flush"):
                ser.flush()
            reply, lines = read_reply(ser, command, timeout)
            captured_lines.extend(lines)
            replies[command] = reply

    raw_line = replies.get("DESKOS_SD_DIAG", "")
    status_line = replies.get("DESKOS_SD_STATUS", "")
    ping_line = replies.get("DESKOS_SD_PING", "")
    fields = parse_fields(raw_line)
    status_fields = parse_fields(status_line)
    ping_fields = parse_fields(ping_line)
    ok = raw_line.startswith("DESKOS_SD_DIAG")
    return {
        "schema": 1,
        "mode": "hardware",
        "kind": "rp2040_raw_diag_capture",
        "port": port,
        "baud": baud,
        "commands": list(SAFE_COMMANDS),
        "public_rf_tx": False,
        "formats_sd": False,
        "response_truncated": False,
        "ok": ok,
        "raw_line": raw_line,
        "fields": fields,
        "status_line": status_line,
        "status_fields": status_fields,
        "ping_line": ping_line,
        "ping_fields": ping_fields,
        "lines": captured_lines,
    }


def write_report(report: dict, out_path: Path | None) -> Path:
    root = Path(__file__).resolve().parents[1]
    if out_path is None:
        out_path = root / "artifacts" / "rp2040-preflight" / f"rp2040_raw_diag_{utc_stamp()}.json"
    elif not out_path.is_absolute():
        out_path = root / out_path
    stamp_report(report, root)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(report, indent=2) + "\n", encoding="ascii")
    return out_path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default=os.environ.get("D1L_RP2040_PORT") or os.environ.get("D1L_RP2040_UF2_PORT"))
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--out")
    args = parser.parse_args()

    if args.dry_run:
        report = dry_run_report(args.port)
    else:
        if not args.port:
            parser.error("No RP2040 port supplied. Set D1L_RP2040_PORT or pass --port.")
        report = capture_diag(args.port, args.baud, args.timeout)

    written = write_report(report, Path(args.out) if args.out else None)
    print(json.dumps({"ok": report["ok"], "out": str(written), "mode": report["mode"]}))
    return 0 if report.get("ok") else 1


if __name__ == "__main__":
    raise SystemExit(main())
