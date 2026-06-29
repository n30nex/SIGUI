#!/usr/bin/env python3
"""Hardware smoke runner for MeshCore DeskOS D1L."""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable


SMOKE_COMMANDS = [
    "version",
    "board",
    "settings get",
    "identity status",
    "i2c",
    "display test",
    "touch test",
    "button",
    "radiohw",
    "radio get",
    "mesh status",
    "companion status",
    "rp2040 status",
    "packets",
    "health",
]


def parse_jsonl_line(line: str) -> dict | None:
    line = line.strip()
    if not line.startswith("{"):
        return None
    try:
        obj = json.loads(line)
    except json.JSONDecodeError:
        return None
    if obj.get("schema") != 1:
        return None
    return obj


def parse_jsonl(lines: Iterable[str]) -> list[dict]:
    return [obj for line in lines if (obj := parse_jsonl_line(line)) is not None]


def dry_run_report() -> dict:
    return {
        "schema": 1,
        "mode": "dry-run",
        "commands": SMOKE_COMMANDS,
        "hardware_required": False,
        "ok": True,
    }


def run_serial_smoke(port: str, baud: int, timeout: float, manual_touch: bool) -> dict:
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("pyserial is required for hardware smoke: python -m pip install pyserial") from exc

    results: list[dict] = []
    with serial.Serial(port=port, baudrate=baud, timeout=timeout) as ser:
        time.sleep(1.0)
        ser.reset_input_buffer()
        for command in SMOKE_COMMANDS:
            ser.write((command + "\n").encode("utf-8"))
            deadline = time.time() + timeout
            command_result = None
            while time.time() < deadline:
                raw = ser.readline().decode("utf-8", errors="replace")
                parsed = parse_jsonl_line(raw)
                if parsed and parsed.get("cmd") == command:
                    command_result = parsed
                    break
            if command_result is None:
                command_result = {
                    "schema": 1,
                    "ok": False,
                    "cmd": command,
                    "code": "TIMEOUT",
                }
            results.append(command_result)

    if manual_touch:
        print("Manual check: confirm the display showed color bars and touch coordinates changed.")

    return {
        "schema": 1,
        "mode": "hardware",
        "port": port,
        "baud": baud,
        "ok": all(item.get("ok") for item in results if item.get("cmd") != "touch test"),
        "results": results,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default=os.environ.get("D1L_PORT"))
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--list-commands", action="store_true")
    parser.add_argument("--manual-touch", action="store_true")
    parser.add_argument("--out", default=None)
    args = parser.parse_args()

    if args.list_commands:
        for command in SMOKE_COMMANDS:
            print(command)
        return 0

    if args.dry_run:
        report = dry_run_report()
    else:
        if not args.port:
            parser.error("No D1L port supplied. Set D1L_PORT or pass --port.")
        report = run_serial_smoke(args.port, args.baud, args.timeout, args.manual_touch)

    root = Path(__file__).resolve().parents[1]
    if args.out:
        out_path = Path(args.out)
        if not out_path.is_absolute():
            out_path = root / out_path
    else:
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        out_path = root / "artifacts" / "smoke" / f"d1l-smoke-{stamp}.json"
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(report, indent=2), encoding="ascii")
    print(json.dumps(report))
    return 0 if report.get("ok") else 1


if __name__ == "__main__":
    raise SystemExit(main())
