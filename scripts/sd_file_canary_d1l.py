#!/usr/bin/env python3
"""Serial-only RP2040 SD file-operation canary for MeshCore DeskOS D1L."""

from __future__ import annotations

import argparse
import json
import os
import sys
from datetime import datetime, timezone
from pathlib import Path

try:
    from smoke_d1l import send_console_command
except ImportError:  # pragma: no cover - package import path used by pytest
    from scripts.smoke_d1l import send_console_command


CANARY_COMMANDS = [
    "storage status",
    "storage filecanary",
    "storage status",
    "packets",
    "health",
]

RETAINED_HISTORY_BACKENDS = (
    "message_store_backend",
    "dm_store_backend",
    "route_store_backend",
    "packet_log_backend",
)


def _number_at_least(value, minimum: int) -> bool:
    try:
        return int(value) >= minimum
    except (TypeError, ValueError):
        return False


def storage_file_gate_ready(storage_status: dict) -> bool:
    sd = storage_status.get("sd")
    if not isinstance(sd, dict):
        return False
    return (
        sd.get("state") == "ready"
        and sd.get("present") is True
        and sd.get("mounted") is True
        and sd.get("data_root_ready") is True
        and sd.get("rp2040_protocol_supported") is True
        and sd.get("file_ops") is True
        and sd.get("atomic_rename") is True
        and _number_at_least(sd.get("file_line_max"), 512)
        and _number_at_least(sd.get("file_chunk_max"), 192)
        and _number_at_least(sd.get("path_max"), 96)
    )


def retained_history_backends_ready(storage_status: dict) -> bool:
    stores = storage_status.get("stores")
    store_backends = stores if isinstance(stores, dict) else {}
    return (
        storage_status.get("ok") is True
        and storage_status.get("data_enabled") is True
        and storage_status.get("data_backend") == "mixed"
        and all(storage_status.get(field) == "sd" for field in RETAINED_HISTORY_BACKENDS)
        and store_backends.get("messages") == "sd"
        and store_backends.get("dm") == "sd"
        and store_backends.get("routes") == "sd"
        and store_backends.get("packets") == "sd"
        and storage_file_gate_ready(storage_status)
    )


def filecanary_passed(canary: dict) -> bool:
    return (
        canary.get("ok") is True
        and canary.get("rename_replace") is True
        and canary.get("read_final") is True
        and canary.get("delete_final") is True
        and canary.get("stat_deleted") is True
    )


def dry_run_report() -> dict:
    return {
        "schema": 1,
        "mode": "dry-run",
        "commands": CANARY_COMMANDS,
        "hardware_required": False,
        "public_rf_tx": False,
        "formats_sd": False,
        "ok": True,
    }


def run_canary(port: str, baud: int, timeout: float, allow_unavailable: bool) -> dict:
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("pyserial is required for hardware SD canary: python -m pip install pyserial") from exc

    results: list[dict] = []
    with serial.Serial(port=port, baudrate=baud, timeout=timeout) as ser:
        ser.reset_input_buffer()
        for command in CANARY_COMMANDS:
            results.append(send_console_command(ser, command, timeout))

    before = results[0]
    canary = results[1]
    after = results[2]
    packets = results[3]
    health = results[4]
    retained_history_sd_before = retained_history_backends_ready(before)
    retained_history_sd_after = retained_history_backends_ready(after)
    canary_ok = filecanary_passed(canary)
    unavailable_ok = (
        allow_unavailable
        and canary.get("ok") is False
        and canary.get("code") == "ESP_ERR_NOT_SUPPORTED"
        and canary.get("step") == "preflight"
    )

    return {
        "schema": 1,
        "mode": "hardware",
        "port": port,
        "baud": baud,
        "commands": CANARY_COMMANDS,
        "public_rf_tx": False,
        "formats_sd": False,
        "ok": (
            before.get("ok", False)
            and (canary_ok or unavailable_ok)
            and after.get("ok", False)
            and packets.get("ok", False)
            and health.get("ok", False)
        ),
        "allow_unavailable": allow_unavailable,
        "canary_passed": canary_ok,
        "canary_unavailable_ok": unavailable_ok,
        "storage_file_gate_ready_before": storage_file_gate_ready(before),
        "storage_file_gate_ready_after": storage_file_gate_ready(after),
        "retained_history_sd_ready_before": retained_history_sd_before,
        "retained_history_sd_ready_after": retained_history_sd_after,
        "storage_before": before,
        "storage_after": after,
        "canary": canary,
        "packets": packets,
        "health": health,
        "results": results,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default=os.environ.get("D1L_PORT"))
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--allow-unavailable", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--list-commands", action="store_true")
    parser.add_argument("--out", default=None)
    args = parser.parse_args()

    if args.list_commands:
        for command in CANARY_COMMANDS:
            print(command)
        return 0

    if args.dry_run:
        report = dry_run_report()
    else:
        if not args.port:
            parser.error("No D1L port supplied. Set D1L_PORT or pass --port.")
        report = run_canary(args.port, args.baud, args.timeout, args.allow_unavailable)

    root = Path(__file__).resolve().parents[1]
    if args.out:
        out_path = Path(args.out)
        if not out_path.is_absolute():
            out_path = root / out_path
    else:
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        out_path = root / "artifacts" / "sd-canary" / f"d1l-sd-file-canary-{stamp}.json"
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(report, indent=2), encoding="ascii")
    print(json.dumps(report))
    return 0 if report.get("ok") else 1


if __name__ == "__main__":
    raise SystemExit(main())
