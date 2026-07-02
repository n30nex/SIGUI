#!/usr/bin/env python3
"""Serial-only SD reboot/remount acceptance runner for MeshCore DeskOS D1L."""

from __future__ import annotations

import argparse
import json
import os
import re
import time
from datetime import datetime, timezone
from pathlib import Path

try:
    from smoke_d1l import send_console_command
    from sd_retained_history_acceptance_d1l import (
        fingerprint_for_token,
        readbacks_pass,
        retained_readback_commands,
    )
except ImportError:  # pragma: no cover - package import path used by pytest
    from scripts.smoke_d1l import send_console_command
    from scripts.sd_retained_history_acceptance_d1l import (
        fingerprint_for_token,
        readbacks_pass,
        retained_readback_commands,
    )


TOKEN_RE = re.compile(r"^[A-Za-z0-9_.-]{1,31}$")
MOUNT_POLL_ATTEMPTS = 10
MOUNT_POLL_INTERVAL_SECONDS = 2.0


def command_plan(token: str, *, include_reboot: bool = True) -> list[str]:
    commands = [
        "storage status",
        "storage mount",
        "storage status",
        "storage filecanary",
        f"storage retained-canary {token}",
        f"storage map-tile-canary {token}",
        *retained_readback_commands(token),
    ]
    if include_reboot:
        commands.extend(
            [
                "reboot",
                "storage status",
                "storage mount",
                "storage status",
                *retained_readback_commands(token),
                f"storage map-tile-check {token}",
                "health",
            ]
        )
    else:
        commands.append("health")
    return commands


def dry_run_report(token: str, *, include_reboot: bool = True) -> dict:
    return {
        "schema": 1,
        "mode": "dry-run",
        "hardware_required": False,
        "token": token,
        "fingerprint": fingerprint_for_token(token),
        "commands": command_plan(token, include_reboot=include_reboot),
        "public_rf_tx": False,
        "formats_sd": False,
        "ok": True,
    }


def sd_status(result: dict | None) -> dict:
    if not isinstance(result, dict):
        return {}
    sd = result.get("sd")
    return sd if isinstance(sd, dict) else {}


def sd_state(result: dict | None) -> str | None:
    state = sd_status(result).get("state")
    return state if isinstance(state, str) else None


def storage_ready(result: dict | None) -> bool:
    sd = sd_status(result)
    return (
        isinstance(result, dict)
        and result.get("ok") is True
        and sd.get("state") == "ready"
        and sd.get("present") is True
        and sd.get("mounted") is True
        and sd.get("data_root_ready") is True
        and sd.get("file_ops") is True
        and sd.get("atomic_rename") is True
    )


def canary_passed(result: dict | None) -> bool:
    return isinstance(result, dict) and result.get("ok") is True


def map_tile_write_passed(result: dict | None, token: str) -> bool:
    if not isinstance(result, dict):
        return False
    return (
        result.get("ok") is True
        and result.get("token") == token
        and result.get("rename_replace") is True
        and result.get("read_final") is True
        and result.get("public_rf_tx") is False
        and result.get("formats_sd") is False
    )


def map_tile_check_passed(result: dict | None, token: str) -> bool:
    if not isinstance(result, dict):
        return False
    return (
        result.get("ok") is True
        and result.get("token") == token
        and result.get("stat_final") is True
        and result.get("read_final") is True
        and result.get("public_rf_tx") is False
        and result.get("formats_sd") is False
    )


def any_flag(results: list[dict], flag: str) -> bool:
    return any(result.get(flag) is True for result in results)


def run_command(ser, command: str, timeout: float) -> dict:
    if command.startswith("mesh send public"):
        raise RuntimeError(f"refusing destructive/RF command in SD remount acceptance: {command}")
    command_timeout = (
        max(timeout, 15.0)
        if command in {"storage mount", "storage filecanary"}
        or command.startswith("storage map-tile-")
        else timeout
    )
    return send_console_command(ser, command, command_timeout)


def run_mount_sequence(
    ser,
    *,
    timeout: float,
    mount_poll_attempts: int,
    mount_poll_interval_sec: float,
) -> tuple[list[str], list[dict], dict]:
    commands = ["storage status", "storage mount", "storage status"]
    results = [
        run_command(ser, "storage status", timeout),
        run_command(ser, "storage mount", timeout),
        run_command(ser, "storage status", timeout),
    ]
    storage_after = results[-1]
    for _attempt in range(mount_poll_attempts):
        if sd_state(storage_after) != "mount_pending":
            break
        time.sleep(mount_poll_interval_sec)
        commands.append("storage status")
        storage_after = run_command(ser, "storage status", timeout)
        results.append(storage_after)
    return commands, results, storage_after


def run_acceptance(
    port: str,
    baud: int,
    timeout: float,
    token: str,
    *,
    include_reboot: bool,
    reboot_settle_sec: float,
    mount_poll_attempts: int,
    mount_poll_interval_sec: float,
) -> dict:
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("pyserial is required: python -m pip install pyserial") from exc

    fingerprint = fingerprint_for_token(token)
    results: list[dict] = []
    commands: list[str] = []

    with serial.Serial(port=port, baudrate=baud, timeout=timeout) as ser:
        ser.reset_input_buffer()
        pre_mount_commands, pre_mount_results, pre_storage = run_mount_sequence(
            ser,
            timeout=timeout,
            mount_poll_attempts=mount_poll_attempts,
            mount_poll_interval_sec=mount_poll_interval_sec,
        )
        commands.extend(pre_mount_commands)
        results.extend(pre_mount_results)
        pre_commands = [
            "storage filecanary",
            f"storage retained-canary {token}",
            f"storage map-tile-canary {token}",
            *retained_readback_commands(token),
        ]
        for command in pre_commands:
            commands.append(command)
            results.append(run_command(ser, command, timeout))

    pre_start = len(pre_mount_commands)
    pre_by_command = {
        command: result for command, result in zip(pre_commands, results[pre_start: pre_start + len(pre_commands)])
    }
    retained_result = pre_by_command.get(f"storage retained-canary {token}", {})
    pre_map_tile = pre_by_command.get(f"storage map-tile-canary {token}", {})

    post_storage = {}
    post_by_command: dict[str, dict] = {}
    post_map_tile = {}
    health = {}
    if include_reboot:
        with serial.Serial(port=port, baudrate=baud, timeout=timeout) as ser:
            commands.append("reboot")
            results.append(run_command(ser, "reboot", timeout))
        time.sleep(reboot_settle_sec)
        with serial.Serial(port=port, baudrate=baud, timeout=timeout) as ser:
            ser.reset_input_buffer()
            post_mount_commands, post_mount_results, post_storage = run_mount_sequence(
                ser,
                timeout=timeout,
                mount_poll_attempts=mount_poll_attempts,
                mount_poll_interval_sec=mount_poll_interval_sec,
            )
            commands.extend(post_mount_commands)
            results.extend(post_mount_results)
            post_commands = [
                *retained_readback_commands(token),
                f"storage map-tile-check {token}",
                "health",
            ]
            post_results = []
            for command in post_commands:
                commands.append(command)
                result = run_command(ser, command, timeout)
                results.append(result)
                post_results.append(result)
            post_by_command = {command: result for command, result in zip(post_commands, post_results)}
            post_map_tile = post_by_command.get(f"storage map-tile-check {token}", {})
            health = post_by_command.get("health", {})
    else:
        with serial.Serial(port=port, baudrate=baud, timeout=timeout) as ser:
            ser.reset_input_buffer()
            commands.append("health")
            health = run_command(ser, "health", timeout)
            results.append(health)
        post_storage = pre_storage
        post_by_command = pre_by_command
        post_map_tile = pre_map_tile

    public_rf_tx = any_flag(results, "public_rf_tx")
    formats_sd = any_flag(results, "formats_sd") or any(
        result.get("format_performed") is True for result in results
    )
    pre_readbacks_ok = readbacks_pass(pre_by_command, token, fingerprint)
    post_readbacks_ok = readbacks_pass(post_by_command, token, fingerprint) if include_reboot else True
    pre_ready = storage_ready(pre_storage)
    post_ready = storage_ready(post_storage)
    retained_ok = canary_passed(retained_result)
    pre_map_tile_ok = map_tile_write_passed(pre_map_tile, token)
    post_map_tile_ok = (
        map_tile_check_passed(post_map_tile, token)
        if include_reboot
        else map_tile_write_passed(post_map_tile, token)
    )
    health_ok = health.get("ok") is True

    return {
        "schema": 1,
        "mode": "hardware",
        "port": port,
        "baud": baud,
        "token": token,
        "fingerprint": fingerprint,
        "commands": commands,
        "public_rf_tx": public_rf_tx,
        "formats_sd": formats_sd,
        "pre_remount_ready": pre_ready,
        "post_remount_ready": post_ready,
        "retained_canary_passed": retained_ok,
        "pre_reboot_readbacks_ok": pre_readbacks_ok,
        "post_reboot_readbacks_ok": post_readbacks_ok,
        "pre_map_tile_canary_passed": pre_map_tile_ok,
        "post_map_tile_canary_passed": post_map_tile_ok,
        "health_ok": health_ok,
        "storage_before_reboot": pre_storage,
        "storage_after_reboot": post_storage,
        "health": health,
        "results": results,
        "ok": (
            not public_rf_tx
            and not formats_sd
            and pre_ready
            and post_ready
            and retained_ok
            and pre_readbacks_ok
            and post_readbacks_ok
            and pre_map_tile_ok
            and post_map_tile_ok
            and health_ok
        ),
    }


def write_report(report: dict, out_path: Path | None) -> Path:
    root = Path(__file__).resolve().parents[1]
    if out_path is None:
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        out_path = root / "artifacts" / "sd-reboot-remount" / f"d1l-sd-reboot-remount-{stamp}.json"
    elif not out_path.is_absolute():
        out_path = root / out_path
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(report, indent=2) + "\n", encoding="ascii")
    return out_path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default=os.environ.get("D1L_PORT"))
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--token", default=datetime.now(timezone.utc).strftime("remount%Y%m%d%H%M%S"))
    parser.add_argument("--no-reboot", action="store_true")
    parser.add_argument("--reboot-settle-sec", type=float, default=8.0)
    parser.add_argument("--mount-poll-attempts", type=int, default=MOUNT_POLL_ATTEMPTS)
    parser.add_argument("--mount-poll-interval-sec", type=float, default=MOUNT_POLL_INTERVAL_SECONDS)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--list-commands", action="store_true")
    parser.add_argument("--out")
    args = parser.parse_args()

    if not TOKEN_RE.match(args.token):
        parser.error("--token must be 1-31 chars: A-Z a-z 0-9 _ . -")
    include_reboot = not args.no_reboot

    if args.list_commands:
        for command in command_plan(args.token, include_reboot=include_reboot):
            print(command)
        return 0

    if args.dry_run:
        report = dry_run_report(args.token, include_reboot=include_reboot)
    else:
        if not args.port:
            parser.error("No D1L port supplied. Set D1L_PORT or pass --port.")
        report = run_acceptance(
            args.port,
            args.baud,
            args.timeout,
            args.token,
            include_reboot=include_reboot,
            reboot_settle_sec=args.reboot_settle_sec,
            mount_poll_attempts=args.mount_poll_attempts,
            mount_poll_interval_sec=args.mount_poll_interval_sec,
        )

    written = write_report(report, Path(args.out) if args.out else None)
    print(json.dumps({"ok": report["ok"], "out": str(written), "mode": report["mode"]}))
    return 0 if report.get("ok") else 1


if __name__ == "__main__":
    raise SystemExit(main())
