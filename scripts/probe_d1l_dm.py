#!/usr/bin/env python3
"""Targeted DM-only hardware probe for MeshCore DeskOS D1L."""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

try:
    from smoke_d1l import send_console_command
except ImportError:
    from scripts.smoke_d1l import send_console_command


DEFAULT_TARGET_FINGERPRINT = "0BF0A701D5AE2DB6"
DEFAULT_BOT_STATUS_PATH = r"F:\Meshcorebot\logs\meshcorebot.status.json"
COUNTER_NAMES = (
    "rx_contact_total",
    "rx_log_total",
    "relay_success_total",
    "discord_send_success_total",
)


def default_token() -> str:
    stamp = datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S")
    return f"d1l_dm_probe_{stamp}"


def build_probe_commands(fingerprint: str, token: str) -> list[str]:
    return [
        "identity status",
        "contacts",
        "mesh status",
        f"mesh send dm {fingerprint} {token}",
        "messages dm",
        f"packets search {token}",
        f"routes trace {fingerprint}",
        "mesh status",
        "health",
    ]


def dry_run_report(fingerprint: str, token: str, bot_status_path: Path, bot_port: str) -> dict:
    return {
        "schema": 1,
        "mode": "dry-run-dm-probe",
        "hardware_required": False,
        "target_fingerprint": fingerprint,
        "token": token,
        "bot_status_path": str(bot_status_path),
        "bot_port": bot_port,
        "public_rf_transmit": False,
        "commands": build_probe_commands(fingerprint, token),
        "ok": True,
    }


def read_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as fh:
        return json.load(fh)


def get_path(obj: dict | None, *parts: str, default=None):
    current = obj
    for part in parts:
        if not isinstance(current, dict) or part not in current:
            return default
        current = current[part]
    return current


def counter_subset(status: dict | None) -> dict:
    counters = get_path(status, "counters", default={}) or {}
    return {name: counters.get(name) for name in COUNTER_NAMES}


def status_snapshot(status: dict | None) -> dict:
    if not status:
        return {}
    return {
        "serial": get_path(status, "serial", default={}) or {},
        "mesh": get_path(status, "mesh", default={}) or {},
        "counters": counter_subset(status),
        "status_written_at": status.get("status_written_at"),
    }


def counter_deltas(before: dict | None, after: dict | None) -> dict:
    deltas: dict[str, int | None] = {}
    before_counts = counter_subset(before)
    after_counts = counter_subset(after)
    for name in COUNTER_NAMES:
        before_value = before_counts.get(name)
        after_value = after_counts.get(name)
        if isinstance(before_value, int) and isinstance(after_value, int):
            deltas[name] = after_value - before_value
        else:
            deltas[name] = None
    return deltas


def contains_token(value, token: str) -> bool:
    return token in json.dumps(value, sort_keys=True)


def command_has_public_tx(command: str) -> bool:
    return command.strip().lower().startswith("mesh send public ")


def wait_for_bot_receive(
    status_path: Path,
    before: dict | None,
    wait_sec: float,
    poll_sec: float,
    min_contact_delta: int,
) -> dict | None:
    deadline = time.time() + wait_sec
    last_status: dict | None = None
    while time.time() < deadline:
        try:
            last_status = read_json(status_path)
        except (OSError, json.JSONDecodeError):
            last_status = None
        deltas = counter_deltas(before, last_status)
        if (deltas.get("rx_contact_total") or 0) >= min_contact_delta:
            return last_status
        time.sleep(max(0.1, poll_sec))
    return last_status


def find_step(steps: list[dict], command_prefix: str) -> dict | None:
    for step in steps:
        if step["command"].startswith(command_prefix):
            return step
    return None


def command_can_retry(command: str) -> bool:
    return not command.strip().lower().startswith("mesh send ")


def send_probe_command(ser, command: str, timeout: float, retries: int = 1) -> dict:
    result = send_console_command(ser, command, timeout)
    for _ in range(max(0, retries)):
        if result.get("code") != "TIMEOUT" or not command_can_retry(command):
            break
        time.sleep(0.2)
        ser.reset_input_buffer()
        result = send_console_command(ser, command, timeout)
    return result


def build_report(
    *,
    port: str,
    baud: int,
    bot_status_path: Path,
    bot_port: str,
    fingerprint: str,
    token: str,
    commands: list[str],
    steps: list[dict],
    bot_before: dict | None,
    bot_after_dm_wait: dict | None,
    bot_after: dict | None,
    min_contact_delta: int,
) -> dict:
    send_step = find_step(steps, "mesh send dm ")
    messages_step = find_step(steps, "messages dm")
    packets_step = find_step(steps, "packets search ")
    route_step = find_step(steps, "routes trace ")
    health_step = find_step(steps, "health")
    deltas = counter_deltas(bot_before, bot_after)
    wait_deltas = counter_deltas(bot_before, bot_after_dm_wait)
    bot_connected_before = (
        get_path(bot_before, "serial", "active_port") == bot_port
        and get_path(bot_before, "serial", "meshcore_connected") is True
    )
    bot_connected_after = (
        get_path(bot_after, "serial", "active_port") == bot_port
        and get_path(bot_after, "serial", "meshcore_connected") is True
    )

    route_result = route_step.get("result", {}) if route_step else {}
    health_result = health_step.get("result", {}) if health_step else {}
    checks = {
        "meshbot_on_expected_port": bot_connected_before or bot_connected_after,
        "send_ok": bool(send_step and send_step.get("result", {}).get("ok")),
        "messages_dm_has_token": bool(messages_step and contains_token(messages_step.get("result"), token)),
        "packets_search_has_token": bool(packets_step and contains_token(packets_step.get("result"), token)),
        "route_trace_has_target": bool(route_result.get("ok") and route_result.get("fingerprint") == fingerprint),
        "meshbot_rx_contact_delta": (deltas.get("rx_contact_total") or 0) >= min_contact_delta,
        "health_ready": bool(health_result.get("ok") and health_result.get("board_ready") and health_result.get("ui_ready")),
        "no_public_commands": not any(command_has_public_tx(command) for command in commands),
    }
    return {
        "schema": 1,
        "mode": "hardware-dm-probe",
        "port": port,
        "baud": baud,
        "meshbot_status_path": str(bot_status_path),
        "meshbot_expected_port": bot_port,
        "target_fingerprint": fingerprint,
        "token": token,
        "public_rf_transmit": False,
        "commands_sent": commands,
        "bot_before": status_snapshot(bot_before),
        "bot_after_dm_wait": status_snapshot(bot_after_dm_wait),
        "bot_after": status_snapshot(bot_after),
        "dm_wait_deltas": wait_deltas,
        "deltas": deltas,
        "checks": checks,
        "steps": steps,
        "ok": all(checks.values()) and all(step.get("result", {}).get("ok") for step in steps),
    }


def run_serial_probe(
    *,
    port: str,
    baud: int,
    timeout: float,
    bot_status_path: Path,
    bot_port: str,
    fingerprint: str,
    token: str,
    wait_sec: float,
    poll_sec: float,
    min_contact_delta: int,
) -> dict:
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("pyserial is required for hardware DM probe: python -m pip install pyserial") from exc

    commands = build_probe_commands(fingerprint, token)
    bot_before = read_json(bot_status_path)
    steps: list[dict] = []
    bot_after_dm_wait: dict | None = None

    with serial.Serial(port=port, baudrate=baud, timeout=timeout) as ser:
        time.sleep(1.0)
        ser.reset_input_buffer()
        for command in commands:
            result = send_probe_command(ser, command, timeout)
            steps.append({"command": command, "result": result})
            if command.startswith("mesh send dm "):
                bot_after_dm_wait = wait_for_bot_receive(
                    bot_status_path,
                    bot_before,
                    wait_sec,
                    poll_sec,
                    min_contact_delta,
                )

    bot_after = read_json(bot_status_path)
    return build_report(
        port=port,
        baud=baud,
        bot_status_path=bot_status_path,
        bot_port=bot_port,
        fingerprint=fingerprint,
        token=token,
        commands=commands,
        steps=steps,
        bot_before=bot_before,
        bot_after_dm_wait=bot_after_dm_wait or bot_after,
        bot_after=bot_after,
        min_contact_delta=min_contact_delta,
    )


def write_report(report: dict, out_path: Path | None) -> Path:
    if out_path is None:
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        out_path = Path("artifacts") / "smoke" / f"d1l-dm-probe-{stamp}.json"
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return out_path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default=os.environ.get("D1L_PORT"))
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--fingerprint", default=os.environ.get("D1L_DM_TARGET", DEFAULT_TARGET_FINGERPRINT))
    parser.add_argument("--token", default=None)
    parser.add_argument("--bot-status", default=os.environ.get("MESHCOREBOT_STATUS_PATH", DEFAULT_BOT_STATUS_PATH))
    parser.add_argument("--bot-port", default=os.environ.get("MESHCOREBOT_PORT"))
    parser.add_argument("--wait-sec", type=float, default=12.0)
    parser.add_argument("--poll-sec", type=float, default=0.75)
    parser.add_argument("--min-contact-delta", type=int, default=1)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--out", default=None)
    args = parser.parse_args()

    token = args.token or default_token()
    bot_status_path = Path(args.bot_status)
    out_path = Path(args.out) if args.out else None

    if args.dry_run:
        report = dry_run_report(args.fingerprint, token, bot_status_path, args.bot_port or "<bot-port>")
    else:
        if not args.port:
            parser.error("No D1L port supplied. Set D1L_PORT or pass --port.")
        if not args.bot_port:
            parser.error("No Meshcorebot port supplied. Set MESHCOREBOT_PORT or pass --bot-port.")
        if not bot_status_path.exists():
            parser.error(f"Meshcorebot status file not found: {bot_status_path}")
        report = run_serial_probe(
            port=args.port,
            baud=args.baud,
            timeout=args.timeout,
            bot_status_path=bot_status_path,
            bot_port=args.bot_port,
            fingerprint=args.fingerprint,
            token=token,
            wait_sec=args.wait_sec,
            poll_sec=args.poll_sec,
            min_contact_delta=args.min_contact_delta,
        )

    written = write_report(report, out_path)
    print(json.dumps({"ok": report["ok"], "out": str(written), "mode": report["mode"]}, indent=2))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
