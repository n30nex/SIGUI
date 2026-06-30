#!/usr/bin/env python3
"""Serial-only retained-history SD acceptance runner for MeshCore DeskOS D1L."""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

try:
    from smoke_d1l import send_console_command
except ImportError:  # pragma: no cover - package import path used by pytest
    from scripts.smoke_d1l import send_console_command


TOKEN_RE = re.compile(r"^[A-Za-z0-9_.-]{1,31}$")


def retained_canary_hash(token: str) -> int:
    value = 2166136261
    for byte in token.encode("ascii"):
        value ^= byte
        value = (value * 16777619) & 0xFFFFFFFF
    return value or 1


def fingerprint_for_token(token: str) -> str:
    value = retained_canary_hash(token)
    return f"{value:08X}{(value ^ 0xA5A5F00D) & 0xFFFFFFFF:08X}"


def retained_readback_commands(token: str) -> list[str]:
    fingerprint = fingerprint_for_token(token)
    return [
        f"messages public search {token}",
        f"messages dm {fingerprint}",
        f"routes trace {fingerprint}",
        f"packets search {token}",
    ]


def command_plan(token: str, *, include_reboot: bool = True) -> list[str]:
    commands = [
        "storage status",
        "storage filecanary",
        f"storage retained-canary {token}",
        *retained_readback_commands(token),
    ]
    if include_reboot:
        commands.append("reboot")
        commands.extend(["storage status", *retained_readback_commands(token), "health"])
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


def allowed_unavailable(result: dict) -> bool:
    return (
        result.get("cmd") == "storage retained-canary"
        and result.get("ok") is False
        and result.get("code") == "SD_RETAINED_HISTORY_NOT_READY"
    )


def filecanary_unavailable(result: dict) -> bool:
    return (
        result.get("cmd") == "storage filecanary"
        and result.get("ok") is False
        and result.get("code") == "ESP_ERR_NOT_SUPPORTED"
        and result.get("step") == "preflight"
    )


def entries_contain_token(result: dict, token: str) -> bool:
    text = json.dumps(result, sort_keys=True)
    return token in text


def readbacks_pass(results: dict[str, dict], token: str, fingerprint: str) -> bool:
    return (
        results.get(f"messages public search {token}", {}).get("ok") is True
        and entries_contain_token(results.get(f"messages public search {token}", {}), token)
        and results.get(f"messages dm {fingerprint}", {}).get("ok") is True
        and entries_contain_token(results.get(f"messages dm {fingerprint}", {}), token)
        and results.get(f"routes trace {fingerprint}", {}).get("ok") is True
        and entries_contain_token(results.get(f"routes trace {fingerprint}", {}), fingerprint)
        and results.get(f"packets search {token}", {}).get("ok") is True
        and entries_contain_token(results.get(f"packets search {token}", {}), token)
    )


def run_commands(ser, commands: list[str], timeout: float) -> list[dict]:
    return [send_console_command(ser, command, timeout) for command in commands]


def run_acceptance(
    port: str,
    baud: int,
    timeout: float,
    token: str,
    *,
    include_reboot: bool,
    reboot_settle_sec: float,
    allow_unavailable: bool,
) -> dict:
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("pyserial is required: python -m pip install pyserial") from exc

    fingerprint = fingerprint_for_token(token)
    pre_commands = [
        "storage status",
        "storage filecanary",
        f"storage retained-canary {token}",
        *retained_readback_commands(token),
    ]
    post_commands = ["storage status", *retained_readback_commands(token), "health"]
    results: list[dict] = []
    with serial.Serial(port=port, baudrate=baud, timeout=timeout) as ser:
        ser.reset_input_buffer()
        results.extend(run_commands(ser, pre_commands, timeout))

    retained_result = results[2]
    if allow_unavailable and allowed_unavailable(retained_result):
        report = {
            "schema": 1,
            "mode": "hardware",
            "port": port,
            "baud": baud,
            "token": token,
            "fingerprint": fingerprint,
            "commands": pre_commands,
            "public_rf_tx": False,
            "formats_sd": False,
            "allow_unavailable": allow_unavailable,
            "retained_canary_unavailable_ok": True,
            "filecanary_unavailable_ok": filecanary_unavailable(results[1]),
            "ok": True,
            "results": results,
        }
        return report

    if include_reboot:
        with serial.Serial(port=port, baudrate=baud, timeout=timeout) as ser:
            reboot_result = send_console_command(ser, "reboot", timeout)
            results.append(reboot_result)
        time.sleep(reboot_settle_sec)
        with serial.Serial(port=port, baudrate=baud, timeout=timeout) as ser:
            ser.reset_input_buffer()
            results.extend(run_commands(ser, post_commands, timeout))
        commands = [*pre_commands, "reboot", *post_commands]
    else:
        with serial.Serial(port=port, baudrate=baud, timeout=timeout) as ser:
            ser.reset_input_buffer()
            results.append(send_console_command(ser, "health", timeout))
        commands = [*pre_commands, "health"]

    by_command = {row.get("cmd", ""): row for row in results}
    pre_by_command = {cmd: result for cmd, result in zip(pre_commands, results[: len(pre_commands)])}
    post_start = len(pre_commands) + (1 if include_reboot else 0)
    post_by_command = {
        cmd: result for cmd, result in zip(post_commands, results[post_start: post_start + len(post_commands)])
    }
    canary_ok = retained_result.get("ok") is True
    filecanary_ok = results[1].get("ok") is True
    pre_readbacks_ok = readbacks_pass(pre_by_command, token, fingerprint)
    post_readbacks_ok = readbacks_pass(post_by_command, token, fingerprint) if include_reboot else True
    health_ok = by_command.get("health", {}).get("ok") is True
    storage_after = post_by_command.get("storage status", pre_by_command.get("storage status", {}))

    return {
        "schema": 1,
        "mode": "hardware",
        "port": port,
        "baud": baud,
        "token": token,
        "fingerprint": fingerprint,
        "commands": commands,
        "public_rf_tx": False,
        "formats_sd": False,
        "allow_unavailable": allow_unavailable,
        "retained_canary_passed": canary_ok,
        "filecanary_passed": filecanary_ok,
        "pre_reboot_readbacks_ok": pre_readbacks_ok,
        "post_reboot_readbacks_ok": post_readbacks_ok,
        "storage_after": storage_after,
        "health_ok": health_ok,
        "ok": canary_ok and filecanary_ok and pre_readbacks_ok and post_readbacks_ok and health_ok,
        "results": results,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default=os.environ.get("D1L_PORT"))
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--token", default=datetime.now(timezone.utc).strftime("sd%Y%m%d%H%M%S"))
    parser.add_argument("--no-reboot", action="store_true")
    parser.add_argument("--reboot-settle-sec", type=float, default=8.0)
    parser.add_argument("--allow-unavailable", action="store_true")
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
            allow_unavailable=args.allow_unavailable,
        )

    root = Path(__file__).resolve().parents[1]
    if args.out:
        out_path = Path(args.out)
        if not out_path.is_absolute():
            out_path = root / out_path
    else:
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        out_path = root / "artifacts" / "sd-retained-history" / f"d1l-sd-retained-history-{stamp}.json"
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(report, indent=2), encoding="ascii")
    print(json.dumps(report))
    return 0 if report.get("ok") else 1


if __name__ == "__main__":
    raise SystemExit(main())
