#!/usr/bin/env python3
"""Full D1L-port-only RF acceptance runner for MeshCore DeskOS D1L."""

from __future__ import annotations

import argparse
import json
import os
import time
from datetime import datetime, timezone
from pathlib import Path

try:
    from artifact_metadata import stamp_report
    from smoke_d1l import send_console_command
except ImportError:  # pragma: no cover - package import path used by pytest
    from scripts.artifact_metadata import stamp_report
    from scripts.smoke_d1l import send_console_command


DEFAULT_TARGET_FINGERPRINT = "0BF0A701D5AE2DB6"
DEFAULT_D1L_PUBLIC_KEY = "ba14729e8588e30b44b36ff9c6c5511b9d88bf787196c6a46de102af6ebafa07"
DEFAULT_BOT_STATUS_PATH = r"F:\Meshcorebot\logs\meshcorebot.status.json"
DEFAULT_BOT_PORT = "COM" + "11"


def utc_stamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def default_token(commit: str | None = None) -> str:
    prefix = f"rf_accept_{commit[:7]}" if commit else "rf_accept"
    return f"{prefix}_{utc_stamp()}"


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


def status_snapshot(status: dict | None) -> dict:
    if not status:
        return {}
    counters = status.get("counters") if isinstance(status.get("counters"), dict) else {}
    return {
        "serial": status.get("serial") if isinstance(status.get("serial"), dict) else {},
        "discord": status.get("discord") if isinstance(status.get("discord"), dict) else {},
        "mesh": status.get("mesh") if isinstance(status.get("mesh"), dict) else {},
        "counters": {
            "rx_contact_total": counters.get("rx_contact_total"),
            "rx_log_total": counters.get("rx_log_total"),
            "relay_success_total": counters.get("relay_success_total"),
            "discord_send_success_total": counters.get("discord_send_success_total"),
        },
        "status_written_at": status.get("status_written_at"),
    }


def contains_token(value, token: str) -> bool:
    return token in json.dumps(value, sort_keys=True)


def public_key_fingerprint(public_key: str) -> str | None:
    key = str(public_key or "").strip().upper()
    if len(key) < 16:
        return None
    prefix = key[:16]
    if not all(char in "0123456789ABCDEF" for char in prefix):
        return None
    return prefix


def entries(value: dict | None) -> list[dict]:
    rows = value.get("entries") if isinstance(value, dict) else None
    return rows if isinstance(rows, list) else []


def entry_matches_fingerprint(value: dict | None, entry: dict, fingerprint: str) -> bool:
    top_level = value.get("fingerprint") if isinstance(value, dict) else None
    entry_fingerprint = entry.get("fingerprint")
    return (not top_level or top_level == fingerprint) and (not entry_fingerprint or entry_fingerprint == fingerprint)


def messages_have_inbound_token(value: dict | None, token: str, fingerprint: str) -> bool:
    for entry in entries(value):
        text = entry.get("text")
        if (
            entry.get("direction") == "rx"
            and isinstance(text, str)
            and token in text
            and entry_matches_fingerprint(value, entry, fingerprint)
        ):
            return True
    return False


def messages_have_tx_token(value: dict | None, token: str, fingerprint: str) -> bool:
    for entry in entries(value):
        text = entry.get("text")
        if (
            entry.get("direction") == "tx"
            and isinstance(text, str)
            and token in text
            and entry_matches_fingerprint(value, entry, fingerprint)
        ):
            return True
    return False


def messages_have_acked_tx(value: dict | None, token: str, fingerprint: str) -> bool:
    for entry in entries(value):
        ack_hash = entry.get("ack_hash")
        one_entry = {
            "fingerprint": value.get("fingerprint") if isinstance(value, dict) else None,
            "entries": [entry],
        }
        if (
            messages_have_tx_token(one_entry, token, fingerprint)
            and entry.get("acked") is True
            and ack_hash not in (None, "", 0, "0", "0x0", "0X0")
        ):
            return True
    return False


def packets_have_ack_or_path(value: dict | None) -> bool:
    for entry in entries(value):
        kind = str(entry.get("kind", "")).lower()
        if kind.startswith("dm_ack") or kind.startswith("path_return"):
            return True
    return False


def route_has_direct_path(value: dict | None, fingerprint: str) -> bool:
    if not isinstance(value, dict) or value.get("ok") is not True:
        return False
    if value.get("fingerprint") != fingerprint:
        return False
    for entry in entries(value):
        if (
            entry.get("target") == fingerprint
            and entry.get("kind") == "dm_text"
            and entry.get("direction") == "tx"
            and entry.get("route") == "direct"
        ):
            return True
    return False


def command_has_public_tx(command: str) -> bool:
    return command.strip().lower().startswith("mesh send public ")


def command_step(steps: list[dict], command: str) -> dict | None:
    for step in steps:
        if step.get("command") == command:
            return step
    return None


def latest_command_step(steps: list[dict], command: str) -> dict | None:
    for step in reversed(steps):
        if step.get("command") == command:
            return step
    return None


def latest_step_with_prefix(steps: list[dict], prefix: str) -> dict | None:
    for step in reversed(steps):
        if str(step.get("command", "")).startswith(prefix):
            return step
    return None


def discord_command(public_key: str, inbound_token: str) -> str:
    return f"+dm {public_key} {inbound_token}"


def dry_run_report(
    *,
    port: str,
    bot_status_path: Path,
    bot_port: str,
    fingerprint: str,
    public_key: str,
    token: str,
    send_outbound: bool,
) -> dict:
    outbound_token = f"{token}_out"
    inbound_token = f"{token}_in"
    direct_token = f"{token}_direct"
    commands = [
        "identity status",
        "contacts",
    ]
    if send_outbound:
        commands.extend(
            [
                f"mesh send dm {fingerprint} {outbound_token}",
                f"packets search {outbound_token}",
            ]
        )
    commands.append(f"messages dm {fingerprint}")
    if send_outbound:
        commands.extend(
            [
                "packets",
                f"routes trace {fingerprint}",
                f"mesh send dm {fingerprint} {direct_token}",
                f"packets search {direct_token}",
                f"messages dm {fingerprint}",
            ]
        )
    commands.extend(["packets", f"routes trace {fingerprint}", "health"])
    return {
        "schema": 1,
        "mode": "dry-run-rf-full-acceptance",
        "hardware_required": False,
        "port": port,
        "meshbot_status_path": str(bot_status_path),
        "meshbot_expected_port": bot_port,
        "target_fingerprint": fingerprint,
        "d1l_public_key": public_key,
        "token": token,
        "outbound_token": outbound_token,
        "inbound_token": inbound_token,
        "direct_token": direct_token,
        "expected_identity_fingerprint": public_key_fingerprint(public_key),
        "discord_command": discord_command(public_key, inbound_token),
        "public_rf_transmit": False,
        "commands": commands,
        "ok": True,
    }


def build_report(
    *,
    port: str,
    baud: int,
    bot_status_path: Path,
    bot_port: str,
    fingerprint: str,
    public_key: str,
    token: str,
    send_outbound: bool,
    steps: list[dict],
    meshbot_before: dict | None,
    meshbot_after: dict | None,
    inbound_seen_at: str | None,
) -> dict:
    outbound_token = f"{token}_out"
    inbound_token = f"{token}_in"
    direct_token = f"{token}_direct"
    outbound_command = f"mesh send dm {fingerprint} {outbound_token}"
    direct_command = f"mesh send dm {fingerprint} {direct_token}"
    identity_step = latest_command_step(steps, "identity status")
    outbound_step = command_step(steps, outbound_command)
    outbound_packets = command_step(steps, f"packets search {outbound_token}")
    direct_step = command_step(steps, direct_command)
    latest_messages = latest_step_with_prefix(steps, f"messages dm {fingerprint}")
    latest_packets = latest_command_step(steps, "packets")
    latest_route = latest_command_step(steps, f"routes trace {fingerprint}")
    latest_health = latest_command_step(steps, "health")
    identity_result = identity_step.get("result", {}) if identity_step else {}
    route_result = latest_route.get("result", {}) if latest_route else {}
    packets_result = latest_packets.get("result", {}) if latest_packets else {}
    messages_result = latest_messages.get("result", {}) if latest_messages else {}
    health_result = latest_health.get("result", {}) if latest_health else {}
    commands = [str(step.get("command", "")) for step in steps]
    identity_fingerprint = str(identity_result.get("fingerprint") or "").upper()
    expected_identity = public_key_fingerprint(public_key)
    meshbot_ok = (
        get_path(meshbot_before, "serial", "active_port") == bot_port
        and get_path(meshbot_before, "serial", "meshcore_connected") is True
        and get_path(meshbot_before, "discord", "connected") is True
    ) or (
        get_path(meshbot_after, "serial", "active_port") == bot_port
        and get_path(meshbot_after, "serial", "meshcore_connected") is True
        and get_path(meshbot_after, "discord", "connected") is True
    )
    outbound_ok = not send_outbound or bool(
        outbound_step
        and outbound_step.get("result", {}).get("ok") is True
        and outbound_packets
        and contains_token(outbound_packets.get("result"), outbound_token)
    )
    checks = {
        "identity_public_key_matches": bool(
            identity_result.get("ok") is True
            and expected_identity
            and identity_fingerprint == expected_identity
        ),
        "meshbot_on_expected_port": meshbot_ok,
        "outbound_dm": outbound_ok,
        "inbound_dm": bool(
            latest_messages and messages_have_inbound_token(messages_result, inbound_token, fingerprint)
        ),
        "ack_path": bool(
            messages_have_acked_tx(messages_result, outbound_token, fingerprint)
            and packets_have_ack_or_path(packets_result)
        ),
        "direct_route": bool(
            (not send_outbound or (direct_step and direct_step.get("result", {}).get("ok") is True))
            and messages_have_tx_token(messages_result, direct_token, fingerprint)
            and route_has_direct_path(route_result, fingerprint)
        ),
        "health_ready": bool(
            health_result.get("ok") is True
            and health_result.get("board_ready") is True
            and health_result.get("ui_ready") is True
        ),
        "no_public_commands": not any(command_has_public_tx(command) for command in commands),
    }
    return {
        "schema": 1,
        "mode": "rf-full-acceptance",
        "port": port,
        "baud": baud,
        "meshbot_status_path": str(bot_status_path),
        "meshbot_expected_port": bot_port,
        "target_fingerprint": fingerprint,
        "d1l_public_key": public_key,
        "expected_identity_fingerprint": expected_identity,
        "identity_fingerprint": identity_fingerprint,
        "token": token,
        "outbound_token": outbound_token,
        "inbound_token": inbound_token,
        "direct_token": direct_token,
        "discord_command": discord_command(public_key, inbound_token),
        "public_rf_transmit": False,
        "inbound_seen_at": inbound_seen_at,
        "meshbot_before": status_snapshot(meshbot_before),
        "meshbot_after": status_snapshot(meshbot_after),
        "checks": checks,
        "steps": steps,
        "ok": all(checks.values()),
    }


def command_can_retry(command: str) -> bool:
    return not command.strip().lower().startswith("mesh send ")


def send_acceptance_command(ser, command: str, timeout: float, retries: int = 1) -> dict:
    result = send_console_command(ser, command, timeout)
    for _ in range(max(0, retries)):
        if result.get("code") != "TIMEOUT" or not command_can_retry(command):
            break
        time.sleep(0.2)
        ser.reset_input_buffer()
        result = send_console_command(ser, command, timeout)
    return result


def run_hardware(
    *,
    port: str,
    baud: int,
    timeout: float,
    wait_sec: float,
    poll_sec: float,
    bot_status_path: Path,
    bot_port: str,
    fingerprint: str,
    public_key: str,
    token: str,
    send_outbound: bool,
) -> dict:
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("pyserial is required: python -m pip install pyserial") from exc

    outbound_token = f"{token}_out"
    inbound_token = f"{token}_in"
    direct_token = f"{token}_direct"
    steps: list[dict] = []
    meshbot_before = read_json(bot_status_path)
    inbound_seen_at = None

    def run_command(ser, command: str, command_timeout: float | None = None) -> dict:
        result = send_acceptance_command(ser, command, command_timeout or timeout)
        steps.append({"command": command, "result": result})
        return result

    with serial.Serial(port=port, baudrate=baud, timeout=timeout) as ser:
        time.sleep(1.0)
        ser.reset_input_buffer()
        run_command(ser, "identity status")
        run_command(ser, "contacts")
        if send_outbound:
            run_command(ser, f"mesh send dm {fingerprint} {outbound_token}", max(timeout, 8.0))
            time.sleep(2.0)
            run_command(ser, f"packets search {outbound_token}")
        deadline = time.time() + wait_sec
        while time.time() < deadline:
            messages = run_command(ser, f"messages dm {fingerprint}")
            if messages_have_inbound_token(messages, inbound_token, fingerprint):
                inbound_seen_at = datetime.now(timezone.utc).isoformat()
                break
            time.sleep(max(0.25, poll_sec))
        if send_outbound:
            run_command(ser, "packets")
            run_command(ser, f"routes trace {fingerprint}")
            run_command(ser, f"mesh send dm {fingerprint} {direct_token}", max(timeout, 8.0))
            time.sleep(2.0)
            run_command(ser, f"packets search {direct_token}")
            run_command(ser, f"messages dm {fingerprint}")
        run_command(ser, "packets")
        run_command(ser, f"routes trace {fingerprint}")
        run_command(ser, "health")

    meshbot_after = read_json(bot_status_path)
    return build_report(
        port=port,
        baud=baud,
        bot_status_path=bot_status_path,
        bot_port=bot_port,
        fingerprint=fingerprint,
        public_key=public_key,
        token=token,
        send_outbound=send_outbound,
        steps=steps,
        meshbot_before=meshbot_before,
        meshbot_after=meshbot_after,
        inbound_seen_at=inbound_seen_at,
    )


def default_out_path(report: dict) -> Path:
    if report.get("mode") == "dry-run-rf-full-acceptance":
        return Path("artifacts") / "smoke" / f"d1l-rf-full-acceptance-dry-run-{utc_stamp()}.json"
    port = str(report.get("port") or "unknown").lower()
    token = str(report.get("token") or utc_stamp()).replace(":", "_")
    return Path("artifacts") / "hardware" / port / f"rf_full_acceptance_{token}.json"


def write_report(report: dict, out_path: Path | None) -> Path:
    root = Path(__file__).resolve().parents[1]
    path = out_path or default_out_path(report)
    if not path.is_absolute():
        path = root / path
    stamp_report(report, root)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default=os.environ.get("D1L_PORT"))
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--wait-sec", type=float, default=90.0)
    parser.add_argument("--poll-sec", type=float, default=3.0)
    parser.add_argument("--fingerprint", default=os.environ.get("D1L_DM_TARGET", DEFAULT_TARGET_FINGERPRINT))
    parser.add_argument("--d1l-public-key", default=os.environ.get("D1L_PUBLIC_KEY", DEFAULT_D1L_PUBLIC_KEY))
    parser.add_argument("--bot-status", default=os.environ.get("MESHCOREBOT_STATUS_PATH", DEFAULT_BOT_STATUS_PATH))
    parser.add_argument("--bot-port", default=os.environ.get("MESHCOREBOT_PORT", DEFAULT_BOT_PORT))
    parser.add_argument("--token", default=None)
    parser.add_argument("--commit", default=None)
    parser.add_argument("--skip-outbound", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--out")
    args = parser.parse_args()

    token = args.token or default_token(args.commit)
    bot_status_path = Path(args.bot_status)
    if args.dry_run:
        report = dry_run_report(
            port=args.port or "<D1L_PORT>",
            bot_status_path=bot_status_path,
            bot_port=args.bot_port,
            fingerprint=args.fingerprint,
            public_key=args.d1l_public_key,
            token=token,
            send_outbound=not args.skip_outbound,
        )
    else:
        if not args.port:
            parser.error("No D1L port supplied. Set D1L_PORT or pass --port.")
        if not bot_status_path.exists():
            parser.error(f"Meshcorebot status file not found: {bot_status_path}")
        report = run_hardware(
            port=args.port,
            baud=args.baud,
            timeout=args.timeout,
            wait_sec=args.wait_sec,
            poll_sec=args.poll_sec,
            bot_status_path=bot_status_path,
            bot_port=args.bot_port,
            fingerprint=args.fingerprint,
            public_key=args.d1l_public_key,
            token=token,
            send_outbound=not args.skip_outbound,
        )

    written = write_report(report, Path(args.out) if args.out else None)
    print(json.dumps({"ok": report["ok"], "out": str(written), "mode": report["mode"]}, indent=2))
    if report.get("mode") == "dry-run-rf-full-acceptance":
        print(f"Discord command to trigger inbound proof: {report['discord_command']}")
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
