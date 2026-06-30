#!/usr/bin/env python3
"""Serial-only SD map-tile cache canary for MeshCore DeskOS D1L."""

from __future__ import annotations

import argparse
import json
import os
import re
from datetime import datetime, timezone
from pathlib import Path

try:
    from smoke_d1l import send_console_command
except ImportError:  # pragma: no cover - package import path used by pytest
    from scripts.smoke_d1l import send_console_command


TOKEN_RE = re.compile(r"^[A-Za-z0-9_.-]{1,31}$")
READY_MAP_BACKENDS = {"sd_map_tiles_ready"}


def command_plan(token: str) -> list[str]:
    return [
        "storage status",
        f"storage map-tile-canary {token}",
        "storage status",
        "health",
    ]


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
        storage_status.get("ok") is True
        and sd.get("state") == "ready"
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


def map_tile_backend_ready(storage_status: dict) -> bool:
    stores = storage_status.get("stores")
    store_backends = stores if isinstance(stores, dict) else {}
    return (
        storage_status.get("map_tile_backend") in READY_MAP_BACKENDS
        or store_backends.get("map_tiles") in READY_MAP_BACKENDS
    )


def map_tile_canary_passed(canary: dict, token: str) -> bool:
    path = str(canary.get("path", ""))
    tmp_path = str(canary.get("tmp_path", ""))
    return (
        canary.get("ok") is True
        and canary.get("cmd") == "storage map-tile-canary"
        and canary.get("token") == token
        and canary.get("write_tmp") is True
        and canary.get("read_tmp") is True
        and canary.get("rename_replace") is True
        and canary.get("stat_final") is True
        and canary.get("read_final") is True
        and canary.get("public_rf_tx") is False
        and canary.get("formats_sd") is False
        and path.endswith(f"/y2-{token}.tile")
        and tmp_path.endswith(f"/y2-{token}.tmp")
    )


def map_tile_canary_unavailable(canary: dict) -> bool:
    return (
        canary.get("cmd") == "storage map-tile-canary"
        and canary.get("ok") is False
        and canary.get("code") == "ESP_ERR_NOT_SUPPORTED"
        and canary.get("step") == "preflight"
        and canary.get("public_rf_tx") is False
        and canary.get("formats_sd") is False
    )


def dry_run_report(token: str) -> dict:
    return {
        "schema": 1,
        "mode": "dry-run",
        "hardware_required": False,
        "token": token,
        "commands": command_plan(token),
        "public_rf_tx": False,
        "formats_sd": False,
        "ok": True,
    }


def run_canary(port: str, baud: int, timeout: float, token: str, allow_unavailable: bool) -> dict:
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("pyserial is required: python -m pip install pyserial") from exc

    commands = command_plan(token)
    results: list[dict] = []
    with serial.Serial(port=port, baudrate=baud, timeout=timeout) as ser:
        ser.reset_input_buffer()
        for command in commands:
            results.append(send_console_command(ser, command, timeout))

    before = results[0]
    canary = results[1]
    after = results[2]
    health = results[3]
    gate_ready_before = storage_file_gate_ready(before)
    gate_ready_after = storage_file_gate_ready(after)
    map_backend_ready_before = map_tile_backend_ready(before)
    map_backend_ready_after = map_tile_backend_ready(after)
    canary_ok = map_tile_canary_passed(canary, token)
    unavailable_ok = allow_unavailable and map_tile_canary_unavailable(canary)

    return {
        "schema": 1,
        "mode": "hardware",
        "port": port,
        "baud": baud,
        "token": token,
        "commands": commands,
        "public_rf_tx": False,
        "formats_sd": False,
        "allow_unavailable": allow_unavailable,
        "ok": (
            before.get("ok", False)
            and after.get("ok", False)
            and health.get("ok", False)
            and (
                (
                    canary_ok
                    and gate_ready_before
                    and gate_ready_after
                    and map_backend_ready_before
                    and map_backend_ready_after
                )
                or unavailable_ok
            )
        ),
        "canary_passed": canary_ok,
        "canary_unavailable_ok": unavailable_ok,
        "storage_file_gate_ready_before": gate_ready_before,
        "storage_file_gate_ready_after": gate_ready_after,
        "map_tile_backend_ready_before": map_backend_ready_before,
        "map_tile_backend_ready_after": map_backend_ready_after,
        "storage_before": before,
        "storage_after": after,
        "canary": canary,
        "health": health,
        "results": results,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default=os.environ.get("D1L_PORT"))
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--token", default=datetime.now(timezone.utc).strftime("map%Y%m%d%H%M%S"))
    parser.add_argument("--allow-unavailable", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--list-commands", action="store_true")
    parser.add_argument("--out")
    args = parser.parse_args()

    if not TOKEN_RE.match(args.token):
        parser.error("--token must be 1-31 chars: A-Z a-z 0-9 _ . -")

    if args.list_commands:
        for command in command_plan(args.token):
            print(command)
        return 0

    if args.dry_run:
        report = dry_run_report(args.token)
    else:
        if not args.port:
            parser.error("No D1L port supplied. Set D1L_PORT or pass --port.")
        report = run_canary(args.port, args.baud, args.timeout, args.token, args.allow_unavailable)

    root = Path(__file__).resolve().parents[1]
    if args.out:
        out_path = Path(args.out)
        if not out_path.is_absolute():
            out_path = root / out_path
    else:
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        out_path = root / "artifacts" / "sd-map-tile-canary" / f"d1l-sd-map-tile-canary-{stamp}.json"
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(report, indent=2), encoding="ascii")
    print(json.dumps(report))
    return 0 if report.get("ok") else 1


if __name__ == "__main__":
    raise SystemExit(main())
