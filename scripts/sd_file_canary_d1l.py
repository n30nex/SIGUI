#!/usr/bin/env python3
"""Serial-only RP2040 SD file-operation canary for MeshCore DeskOS D1L."""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

try:
    from artifact_metadata import stamp_report
    from smoke_d1l import open_d1l_serial, send_console_command
except ImportError:  # pragma: no cover - package import path used by pytest
    from scripts.artifact_metadata import stamp_report
    from scripts.smoke_d1l import open_d1l_serial, send_console_command


CANARY_COMMANDS = [
    "storage status",
    "storage mount",
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


def storage_status_fresh(storage_status: dict) -> bool:
    sd = storage_status.get("sd")
    if not isinstance(sd, dict):
        return False
    try:
        refresh_failures = int(sd.get("refresh_failures"))
    except (TypeError, ValueError):
        return False
    return (
        sd.get("status_stale") is False
        and sd.get("presence_stale") is False
        and refresh_failures == 0
    )


def storage_file_gate_ready(storage_status: dict) -> bool:
    sd = storage_status.get("sd")
    if not isinstance(sd, dict):
        return False
    return (
        storage_status_fresh(storage_status)
        and sd.get("state") == "ready"
        and sd.get("filesystem") == "fat32"
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


def storage_manager_ready_for_file_ops(storage_status: dict) -> bool:
    manager = storage_status.get("manager")
    if not isinstance(manager, dict):
        return True
    return manager.get("state") == "READY_SD"


def storage_ready_for_filecanary(storage_status: dict) -> bool:
    return storage_file_gate_ready(storage_status) and storage_manager_ready_for_file_ops(storage_status)


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


def filecanary_transient_timeout(canary: dict) -> bool:
    return (
        canary.get("ok") is False
        and canary.get("cmd") == "storage filecanary"
        and (
            canary.get("code") in {"ESP_ERR_TIMEOUT", "TIMEOUT"}
            or canary.get("file_note") == "timeout"
        )
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


def wait_for_storage_ready(
    ser,
    timeout: float,
    *,
    mount_wait_sec: float,
    poll_interval_sec: float = 1.0,
    allow_unavailable: bool = False,
) -> tuple[dict, list[dict], dict | None]:
    initial = send_console_command(ser, "storage status", timeout)
    results = [initial]
    if storage_ready_for_filecanary(initial) or allow_unavailable:
        return initial, results, None

    mount = None
    if not storage_file_gate_ready(initial):
        mount = send_console_command(ser, "storage mount", max(timeout, 8.0))
        results.append(mount)

    deadline = time.monotonic() + mount_wait_sec
    latest = initial
    while time.monotonic() < deadline:
        time.sleep(poll_interval_sec)
        latest = send_console_command(ser, "storage status", timeout)
        results.append(latest)
        if storage_ready_for_filecanary(latest):
            return latest, results, mount

    return latest, results, mount


def run_canary(
    port: str,
    baud: int,
    timeout: float,
    allow_unavailable: bool,
    mount_wait_sec: float = 30.0,
) -> dict:
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("pyserial is required for hardware SD canary: python -m pip install pyserial") from exc

    results: list[dict] = []
    with open_d1l_serial(serial, port=port, baudrate=baud, timeout=timeout) as ser:
        ser.reset_input_buffer()
        before, ready_wait_results, mount_attempt = wait_for_storage_ready(
            ser,
            timeout,
            mount_wait_sec=mount_wait_sec,
            allow_unavailable=allow_unavailable,
        )
        results.extend(ready_wait_results)
        canary = send_console_command(ser, "storage filecanary", timeout)
        initial_canary = canary
        results.append(canary)
        canary_retry_status = None
        canary_retry = None
        if filecanary_transient_timeout(canary):
            canary_retry_status = send_console_command(ser, "storage status", timeout)
            results.append(canary_retry_status)
            if storage_ready_for_filecanary(canary_retry_status):
                time.sleep(1.0)
                canary_retry = send_console_command(ser, "storage filecanary", timeout)
                results.append(canary_retry)
                canary = canary_retry
        after = send_console_command(ser, "storage status", timeout)
        packets = send_console_command(ser, "packets", timeout)
        health = send_console_command(ser, "health", timeout)
        results.extend([after, packets, health])

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
        "mount_wait_sec": mount_wait_sec,
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
        "canary_retried": canary_retry is not None,
        "canary_retry_ready": (
            storage_ready_for_filecanary(canary_retry_status)
            if canary_retry_status is not None else None
        ),
        "storage_file_gate_ready_before": storage_file_gate_ready(before),
        "storage_file_gate_ready_after": storage_file_gate_ready(after),
        "storage_manager_ready_before": storage_manager_ready_for_file_ops(before),
        "storage_manager_ready_after": storage_manager_ready_for_file_ops(after),
        "storage_ready_waited": len(ready_wait_results) > 1,
        "storage_ready_poll_count": sum(
            1 for result in ready_wait_results if result.get("cmd") == "storage status"
        ),
        "retained_history_sd_ready_before": retained_history_sd_before,
        "retained_history_sd_ready_after": retained_history_sd_after,
        "initial_storage_status": ready_wait_results[0] if ready_wait_results else None,
        "mount_attempt": mount_attempt,
        "storage_before": before,
        "storage_after": after,
        "canary": canary,
        "initial_canary": initial_canary,
        "canary_retry_status": canary_retry_status,
        "canary_retry": canary_retry,
        "packets": packets,
        "health": health,
        "results": results,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default=os.environ.get("D1L_PORT"))
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--mount-wait-sec", type=float, default=30.0)
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
        report = run_canary(
            args.port,
            args.baud,
            args.timeout,
            args.allow_unavailable,
            mount_wait_sec=args.mount_wait_sec,
        )

    root = Path(__file__).resolve().parents[1]
    stamp_report(report, root)
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
