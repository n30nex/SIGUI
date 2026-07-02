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

try:
    from artifact_metadata import stamp_report
except ImportError:  # pragma: no cover - package import path used by pytest
    from scripts.artifact_metadata import stamp_report


SMOKE_COMMANDS = [
    "version",
    "board",
    "settings get",
    "settings onboarding status",
    "identity status",
    "i2c",
    "display test",
    "touch test",
    "button",
    "ui status",
    "radiohw",
    "radio get",
    "map center",
    "mesh status",
    "companion status",
    "wifi status",
    "wifi scan",
    "ble status",
    "rp2040 status",
    "storage status",
    "storage map-policy",
    "storage setup",
    "packets",
    "packets filter any any",
    "packets search test",
    "messages public",
    "messages public search test",
    "messages dm",
    "messages unread",
    "nodes",
    "contacts",
    "contacts export",
    "routes",
    "routes trace 0BF0A701D5AE2DB6",
    "signal",
    "roomservers",
    "repeaters",
    "crashlog",
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


def read_command_result(ser, command: str, timeout: float) -> dict:
    deadline = time.time() + timeout
    while time.time() < deadline:
        raw = ser.readline().decode("utf-8", errors="replace")
        parsed = parse_jsonl_line(raw)
        if parsed and parsed.get("cmd") == command:
            return parsed
    return {
        "schema": 1,
        "ok": False,
        "cmd": command,
        "code": "TIMEOUT",
    }


def expected_command_name(command: str) -> str:
    if command.startswith("messages public search "):
        return "messages public"
    if command.startswith("storage setup confirm "):
        return "storage setup"
    if command == "messages dm clear":
        return command
    for prefix in [
        "settings set name ",
        "settings set pathhash ",
        "settings onboarding complete ",
        "contacts add ",
        "contacts export ",
        "contacts rename ",
        "contacts delete ",
        "messages dm ",
        "mesh send dm ",
        "routes trace ",
        "packets filter ",
        "packets search ",
        "packets raw ",
        "ui tab ",
        "radio set freq ",
        "radio set bw ",
        "radio set sf ",
        "radio set cr ",
        "radio set txpower ",
        "radio set rxboost ",
        "storage map-tile-canary ",
        "storage map-tile-check ",
        "storage export-canary ",
        "storage export-diagnostics ",
        "storage export-data ",
        "storage retained-canary ",
        "backlight ",
        "mesh send public ",
        "mesh send dm ",
    ]:
        if command.startswith(prefix):
            return prefix.strip()
    return command


def send_console_command(ser, command: str, timeout: float) -> dict:
    ser.write((command + "\n").encode("utf-8"))
    return read_command_result(ser, expected_command_name(command), timeout)


def wait_after_reboot(ser, settle_seconds: float) -> None:
    time.sleep(settle_seconds)
    ser.reset_input_buffer()


def run_persistence_check(ser, timeout: float) -> dict:
    test_name = "D1L Smoke Persist"
    steps: list[dict] = []

    for command in [
        "settings reset",
        "wifi off",
        "ble off",
        f"settings set name {test_name}",
        "settings set pathhash 3",
    ]:
        steps.append(send_console_command(ser, command, timeout))

    before = send_console_command(ser, "settings get", timeout)
    steps.append(before)

    reboot = send_console_command(ser, "reboot", timeout)
    steps.append(reboot)
    if reboot.get("ok"):
        wait_after_reboot(ser, max(2.5, timeout / 2.0))

    after = send_console_command(ser, "settings get", timeout)
    steps.append(after)

    reset = send_console_command(ser, "settings reset", timeout)
    steps.append(reset)

    cleanup_reboot = send_console_command(ser, "reboot", timeout)
    steps.append(cleanup_reboot)
    if cleanup_reboot.get("ok"):
        wait_after_reboot(ser, max(2.5, timeout / 2.0))

    cleanup = send_console_command(ser, "settings get", timeout)
    steps.append(cleanup)

    before_ok = (
        before.get("node_name") == test_name
        and before.get("path_hash_bytes") == 3
        and before.get("wifi_enabled") is False
        and before.get("ble_companion_enabled") is False
    )
    after_ok = (
        after.get("node_name") == test_name
        and after.get("path_hash_bytes") == 3
        and after.get("wifi_enabled") is False
        and after.get("ble_companion_enabled") is False
    )
    cleanup_ok = (
        cleanup.get("node_name") == "D1L Desk"
        and cleanup.get("path_hash_bytes") == 1
        and cleanup.get("wifi_enabled") is False
        and cleanup.get("ble_companion_enabled") is False
    )

    return {
        "schema": 1,
        "ok": all(step.get("ok") for step in steps) and before_ok and after_ok and cleanup_ok,
        "test": "settings_persistence_reboot",
        "temporary_node_name": test_name,
        "before_reboot_ok": before_ok,
        "after_reboot_ok": after_ok,
        "cleanup_ok": cleanup_ok,
        "steps": steps,
    }


def run_serial_smoke(port: str, baud: int, timeout: float, manual_touch: bool, persistence_test: bool) -> dict:
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("pyserial is required for hardware smoke: python -m pip install pyserial") from exc

    results: list[dict] = []
    with serial.Serial(port=port, baudrate=baud, timeout=timeout) as ser:
        time.sleep(1.0)
        ser.reset_input_buffer()
        for command in SMOKE_COMMANDS:
            results.append(send_console_command(ser, command, timeout))
        persistence = run_persistence_check(ser, timeout) if persistence_test else None

    if manual_touch:
        print("Manual check: confirm the display showed color bars and touch coordinates changed.")

    report = {
        "schema": 1,
        "mode": "hardware",
        "port": port,
        "baud": baud,
        "ok": all(item.get("ok") for item in results if item.get("cmd") != "touch test"),
        "results": results,
    }
    if persistence is not None:
        report["persistence"] = persistence
        report["ok"] = report["ok"] and persistence.get("ok", False)
    return report


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default=os.environ.get("D1L_PORT"))
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--list-commands", action="store_true")
    parser.add_argument("--manual-touch", action="store_true")
    parser.add_argument("--persistence-test", action="store_true")
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
        report = run_serial_smoke(args.port, args.baud, args.timeout, args.manual_touch, args.persistence_test)

    root = Path(__file__).resolve().parents[1]
    if args.out:
        out_path = Path(args.out)
        if not out_path.is_absolute():
            out_path = root / out_path
    else:
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        out_path = root / "artifacts" / "smoke" / f"d1l-smoke-{stamp}.json"
    stamp_report(report, root)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(report, indent=2), encoding="ascii")
    print(json.dumps(report))
    return 0 if report.get("ok") else 1


if __name__ == "__main__":
    raise SystemExit(main())
