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
    from artifact_metadata import stamp_report
    from smoke_d1l import (
        boot_transition_proven,
        health_boot_nonce,
        open_d1l_serial,
        reboot_command_passed,
        send_console_command,
        timeout_for_reboot_command,
    )
    from sd_file_canary_d1l import (
        retained_history_backends_ready,
        storage_file_gate_ready,
        wait_for_storage_ready,
    )
except ImportError:  # pragma: no cover - package import path used by pytest
    from scripts.artifact_metadata import stamp_report
    from scripts.smoke_d1l import (
        boot_transition_proven,
        health_boot_nonce,
        open_d1l_serial,
        reboot_command_passed,
        send_console_command,
        timeout_for_reboot_command,
    )
    from scripts.sd_file_canary_d1l import (
        retained_history_backends_ready,
        storage_file_gate_ready,
        wait_for_storage_ready,
    )


TOKEN_RE = re.compile(r"^[A-Za-z0-9_.-]{1,31}$")
RETAINED_CANARY_MIN_TIMEOUT_SECONDS = 20.0


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
        "storage mount",
        "storage status",
        "storage filecanary",
        f"storage retained-canary {token}",
        *retained_readback_commands(token),
    ]
    if include_reboot:
        commands.extend(["health", "reboot"])
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


def positive_int(value: object) -> int | None:
    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        return None
    return value


def retained_canary_metadata(result: dict | None, token: str, fingerprint: str) -> dict | None:
    if not isinstance(result, dict):
        return None
    backends = result.get("backends")
    if (
        result.get("ok") is not True
        or result.get("cmd") != "storage retained-canary"
        or result.get("token") != token
        or result.get("fingerprint") != fingerprint
        or result.get("public_rf_tx") is not False
        or result.get("formats_sd") is not False
        or not isinstance(backends, dict)
        or any(backends.get(name) != "sd" for name in ("messages", "dm", "routes", "packets"))
    ):
        return None

    sequences = {
        name: positive_int(result.get(f"{name}_seq"))
        for name in ("public", "dm", "route", "packet")
    }
    if any(value is None for value in sequences.values()):
        return None
    return {"token": token, "fingerprint": fingerprint, "sequences": sequences}


def result_safety_flags(results: list[dict]) -> tuple[bool, bool]:
    public_rf_tx = any(result.get("public_rf_tx") is True for result in results)
    formats_sd = any(
        result.get("formats_sd") is True or result.get("format_performed") is True
        for result in results
    )
    return public_rf_tx, formats_sd


def response_entries(result: dict | None) -> list[dict]:
    if not isinstance(result, dict):
        return []
    entries = result.get("entries")
    if not isinstance(entries, list) or not entries:
        return []
    return [entry for entry in entries if isinstance(entry, dict)]


def entry_matches(entries: list[dict], sequence: int, expected: dict) -> bool:
    return any(
        entry.get("seq") == sequence
        and all(entry.get(field) == value for field, value in expected.items())
        for entry in entries
    )


def exact_page_counts(result: dict, entries: list[dict], *, thread: bool = False) -> bool:
    page_count = positive_int(result.get("page_count"))
    total_matches = positive_int(result.get("total_matches"))
    if page_count != len(entries) or total_matches is None or total_matches < page_count:
        return False
    if thread and result.get("thread_count") != total_matches:
        return False
    return True


def readbacks_pass(
    results: dict[str, dict],
    token: str,
    fingerprint: str,
    canary_result: dict | None,
) -> bool:
    metadata = retained_canary_metadata(canary_result, token, fingerprint)
    if metadata is None:
        return False
    sequences = metadata["sequences"]
    text = f"sd-retained-canary {token}"

    public = results.get(f"messages public search {token}", {})
    public_entries = response_entries(public)
    public_ok = (
        public.get("ok") is True
        and public.get("cmd") == "messages public"
        and public.get("filtered") is True
        and public.get("search") == token
        and exact_page_counts(public, public_entries)
        and positive_int(public.get("count")) is not None
        and entry_matches(
            public_entries,
            sequences["public"],
            {"direction": "tx", "author": "SD Canary", "text": text, "delivered": True},
        )
    )

    dm = results.get(f"messages dm {fingerprint}", {})
    dm_entries = response_entries(dm)
    dm_ok = (
        dm.get("ok") is True
        and dm.get("cmd") == "messages dm"
        and dm.get("filtered") is True
        and dm.get("fingerprint") == fingerprint
        and exact_page_counts(dm, dm_entries, thread=True)
        and positive_int(dm.get("count")) is not None
        and entry_matches(
            dm_entries,
            sequences["dm"],
            {
                "fingerprint": fingerprint,
                "alias": "SD Canary",
                "direction": "tx",
                "text": text,
                "delivered": True,
                "acked": True,
                "ack_hash": retained_canary_hash(token),
            },
        )
    )

    route = results.get(f"routes trace {fingerprint}", {})
    route_entries = response_entries(route)
    route_ok = (
        route.get("ok") is True
        and route.get("cmd") == "routes trace"
        and route.get("fingerprint") == fingerprint
        and positive_int(route.get("route_count")) == len(route_entries)
        and entry_matches(
            route_entries,
            sequences["route"],
            {
                "target": fingerprint,
                "label": "SD Canary",
                "kind": "sd_canary",
                "route": "local",
                "direction": "tx",
                "payload_len": len(text),
            },
        )
    )

    packet = results.get(f"packets search {token}", {})
    packet_entries = response_entries(packet)
    packet_filter = packet.get("filter")
    packet_ok = (
        packet.get("ok") is True
        and packet.get("cmd") == "packets search"
        and positive_int(packet.get("count")) is not None
        and isinstance(packet_filter, dict)
        and packet_filter == {"direction": "any", "kind": "any", "search": token}
        and entry_matches(
            packet_entries,
            sequences["packet"],
            {
                "direction": "tx",
                "kind": "sd_canary",
                "payload_len": len(text),
                "note": f"sd-canary {token}",
            },
        )
    )
    return public_ok and dm_ok and route_ok and packet_ok


def retained_storage_ready(result: dict | None) -> bool:
    if not isinstance(result, dict):
        return False
    manager = result.get("manager")
    return (
        retained_history_backends_ready(result)
        and isinstance(manager, dict)
        and manager.get("running") is True
        and manager.get("state") == "READY_SD"
    )


def run_command(ser, command: str, timeout: float) -> dict:
    command_timeout = timeout_for_reboot_command(command, timeout)
    if command.startswith("storage retained-canary "):
        command_timeout = max(timeout, RETAINED_CANARY_MIN_TIMEOUT_SECONDS)
    return send_console_command(ser, command, command_timeout)


def run_commands(ser, commands: list[str], timeout: float) -> list[dict]:
    return [run_command(ser, command, timeout) for command in commands]


def run_acceptance(
    port: str,
    baud: int,
    timeout: float,
    token: str,
    *,
    include_reboot: bool,
    reboot_settle_sec: float,
    mount_wait_sec: float,
    allow_unavailable: bool,
) -> dict:
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("pyserial is required: python -m pip install pyserial") from exc

    fingerprint = fingerprint_for_token(token)
    retained_commands = [
        "storage filecanary",
        f"storage retained-canary {token}",
        *retained_readback_commands(token),
    ]
    if include_reboot:
        retained_commands.append("health")
    post_commands = ["storage status", *retained_readback_commands(token), "health"]
    results: list[dict] = []
    with open_d1l_serial(serial, port=port, baudrate=baud, timeout=timeout) as ser:
        ser.reset_input_buffer()
        storage_before, ready_wait_results, mount_attempt = wait_for_storage_ready(
            ser,
            timeout,
            mount_wait_sec=mount_wait_sec,
            allow_unavailable=allow_unavailable,
        )
        results.extend(ready_wait_results)
        results.extend(run_commands(ser, retained_commands, timeout))

    retained_start = len(ready_wait_results)
    filecanary_result = results[retained_start]
    retained_result = results[retained_start + 1]
    pre_commands = [row.get("cmd", "") for row in ready_wait_results] + retained_commands
    if allow_unavailable and allowed_unavailable(retained_result):
        public_rf_tx, formats_sd = result_safety_flags(results)
        report = {
            "schema": 1,
            "mode": "hardware",
            "port": port,
            "baud": baud,
            "token": token,
            "fingerprint": fingerprint,
            "commands": pre_commands,
            "public_rf_tx": public_rf_tx,
            "formats_sd": formats_sd,
            "allow_unavailable": allow_unavailable,
            "retained_canary_unavailable_ok": True,
            "filecanary_unavailable_ok": filecanary_unavailable(filecanary_result),
            "storage_file_gate_ready_before": storage_file_gate_ready(storage_before),
            "storage_file_gate_ready_after": False,
            "retained_history_sd_ready_before": retained_storage_ready(storage_before),
            "retained_history_sd_ready_after": False,
            "storage_before": storage_before,
            "storage_after": storage_before,
            "storage_ready_waited": len(ready_wait_results) > 1,
            "storage_ready_poll_count": sum(
                1 for result in ready_wait_results if result.get("cmd") == "storage status"
            ),
            "mount_attempt": mount_attempt,
            "ok": not public_rf_tx and not formats_sd,
            "results": results,
        }
        return report

    reboot_result: dict | None = None
    reboot_ok = not include_reboot
    post_commands_ran = False
    if include_reboot:
        with open_d1l_serial(serial, port=port, baudrate=baud, timeout=timeout) as ser:
            reboot_result = run_command(ser, "reboot", timeout)
            results.append(reboot_result)
        reboot_ok = reboot_command_passed(reboot_result)
        if reboot_ok:
            time.sleep(reboot_settle_sec)
            with open_d1l_serial(serial, port=port, baudrate=baud, timeout=timeout) as ser:
                ser.reset_input_buffer()
                results.extend(run_commands(ser, post_commands, timeout))
            post_commands_ran = True
        commands = [*pre_commands, "reboot"]
        if post_commands_ran:
            commands.extend(post_commands)
    else:
        with open_d1l_serial(serial, port=port, baudrate=baud, timeout=timeout) as ser:
            ser.reset_input_buffer()
            results.append(send_console_command(ser, "health", timeout))
        commands = [*pre_commands, "health"]

    by_command = {row.get("cmd", ""): row for row in results}
    pre_by_command = {cmd: result for cmd, result in zip(pre_commands, results[: len(pre_commands)])}
    post_start = len(pre_commands) + (1 if include_reboot else 0)
    post_by_command = {
        cmd: result for cmd, result in zip(post_commands, results[post_start: post_start + len(post_commands)])
    } if post_commands_ran else {}
    canary_ok = retained_canary_metadata(retained_result, token, fingerprint) is not None
    filecanary_ok = filecanary_result.get("ok") is True
    pre_readbacks_ok = readbacks_pass(pre_by_command, token, fingerprint, retained_result)
    pre_health = pre_by_command.get("health", {})
    post_health = post_by_command.get("health", {})
    reboot_proof_ok = (
        boot_transition_proven(pre_health, post_health)
        if include_reboot
        else True
    )
    post_readbacks_ok = (
        reboot_proof_ok and readbacks_pass(post_by_command, token, fingerprint, retained_result)
        if include_reboot
        else True
    )
    health_ok = (
        post_health.get("ok") is True
        if include_reboot
        else by_command.get("health", {}).get("ok") is True
    )
    storage_after = post_by_command.get("storage status", pre_by_command.get("storage status", {}))
    storage_file_gate_ready_before = storage_file_gate_ready(storage_before)
    storage_file_gate_ready_after = (
        reboot_proof_ok and storage_file_gate_ready(storage_after)
        if include_reboot
        else storage_file_gate_ready(storage_after)
    )
    retained_history_sd_ready_before = retained_storage_ready(storage_before)
    retained_history_sd_ready_after = (
        reboot_proof_ok and retained_storage_ready(storage_after)
        if include_reboot
        else retained_storage_ready(storage_after)
    )
    public_rf_tx, formats_sd = result_safety_flags(results)

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
        "allow_unavailable": allow_unavailable,
        "retained_canary_passed": canary_ok,
        "filecanary_passed": filecanary_ok,
        "storage_file_gate_ready_before": storage_file_gate_ready_before,
        "storage_file_gate_ready_after": storage_file_gate_ready_after,
        "retained_history_sd_ready_before": retained_history_sd_ready_before,
        "retained_history_sd_ready_after": retained_history_sd_ready_after,
        "storage_ready_waited": len(ready_wait_results) > 1,
        "storage_ready_poll_count": sum(
            1 for result in ready_wait_results if result.get("cmd") == "storage status"
        ),
        "mount_attempt": mount_attempt,
        "reboot_command_passed": reboot_ok if include_reboot else None,
        "reboot_route_flush": (
            reboot_result.get("route_flush")
            if include_reboot and isinstance(reboot_result, dict)
            else None
        ),
        "pre_reboot_boot_nonce": health_boot_nonce(pre_health) if include_reboot else None,
        "post_reboot_boot_nonce": health_boot_nonce(post_health) if include_reboot else None,
        "reboot_proven": reboot_proof_ok if include_reboot else None,
        "pre_reboot_readbacks_ok": pre_readbacks_ok,
        "post_reboot_readbacks_ok": post_readbacks_ok,
        "storage_before": storage_before,
        "storage_after": storage_after,
        "health_ok": health_ok,
        "ok": (
            not public_rf_tx
            and not formats_sd
            and canary_ok
            and filecanary_ok
            and storage_file_gate_ready_before
            and storage_file_gate_ready_after
            and retained_history_sd_ready_before
            and retained_history_sd_ready_after
            and reboot_ok
            and reboot_proof_ok
            and pre_readbacks_ok
            and post_readbacks_ok
            and health_ok
        ),
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
    parser.add_argument("--mount-wait-sec", type=float, default=30.0)
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
            mount_wait_sec=args.mount_wait_sec,
            allow_unavailable=args.allow_unavailable,
        )

    root = Path(__file__).resolve().parents[1]
    stamp_report(report, root)
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
