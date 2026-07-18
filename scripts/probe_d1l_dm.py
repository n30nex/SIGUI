#!/usr/bin/env python3
"""Targeted DM-only hardware probe for MeshCore DeskOS D1L."""

from __future__ import annotations

import argparse
import json
import os
import re
import time
from datetime import datetime, timezone
from pathlib import Path

try:
    from artifact_metadata import stamp_report
    from smoke_d1l import open_d1l_serial, send_console_command
except ImportError:
    from scripts.artifact_metadata import stamp_report
    from scripts.smoke_d1l import open_d1l_serial, send_console_command


DEFAULT_TARGET_FINGERPRINT = "0BF0A701D5AE2DB6"
DM_PROBE_SCHEMA = 2
FORBIDDEN_PORTS = {"COM" + str(number) for number in (8, 11, 29)}
COUNTER_NAMES = (
    "rx_contact_total",
    "rx_log_total",
    "relay_success_total",
    "discord_send_success_total",
)


def default_token() -> str:
    stamp = datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S")
    return f"d1l_dm_probe_{stamp}"


def normalize_port(value: str | None) -> str | None:
    if not isinstance(value, str) or not value.strip():
        return None
    normalized = value.strip().upper().replace("/", "\\")
    for prefix in ("\\\\.\\", "\\\\?\\"):
        if normalized.startswith(prefix):
            normalized = normalized[len(prefix):]
            break
    return normalized


def enforce_port_policy(port: str, peer_port: str) -> tuple[str, str]:
    normalized_port = normalize_port(port)
    normalized_peer = normalize_port(peer_port)
    if normalized_port in FORBIDDEN_PORTS:
        raise ValueError(f"refusing forbidden D1L port {normalized_port}")
    if normalized_peer in FORBIDDEN_PORTS:
        raise ValueError(f"refusing forbidden controlled-peer port {normalized_peer}")
    if normalized_port is None or normalized_peer is None:
        raise ValueError("explicit D1L and controlled-peer ports are required")
    if re.fullmatch(r"COM[1-9][0-9]*", normalized_port) is None:
        raise ValueError(f"invalid D1L port {normalized_port}")
    if re.fullmatch(r"COM[1-9][0-9]*", normalized_peer) is None:
        raise ValueError(f"invalid controlled-peer port {normalized_peer}")
    if normalized_port == normalized_peer:
        raise ValueError("D1L and controlled-peer ports must be distinct")
    return normalized_port, normalized_peer


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


def dry_run_report(
    fingerprint: str,
    token: str,
    peer_status_path: Path,
    peer_port: str,
) -> dict:
    return {
        "schema": DM_PROBE_SCHEMA,
        "mode": "dry-run-dm-probe",
        "hardware_required": False,
        "physical_observed": False,
        "dry_run": True,
        "simulated": False,
        "simulation": False,
        "source_inspection": False,
        "execution_complete": False,
        "dm_rf_tx": False,
        "public_rf_tx": False,
        "formats_sd": False,
        "target_fingerprint": fingerprint,
        "token": token,
        "controlled_peer": {
            "fingerprint": fingerprint,
            "evidence_source": "explicit_peer_status",
            "port": peer_port,
            "status_path": str(peer_status_path),
        },
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
    peer_status_path: Path,
    peer_port: str,
    fingerprint: str,
    token: str,
    commands: list[str],
    steps: list[dict],
    peer_before: dict | None,
    peer_after_dm_wait: dict | None,
    peer_after: dict | None,
    min_contact_delta: int,
) -> dict:
    send_step = find_step(steps, "mesh send dm ")
    messages_step = find_step(steps, "messages dm")
    packets_step = find_step(steps, "packets search ")
    route_step = find_step(steps, "routes trace ")
    health_step = find_step(steps, "health")
    deltas = counter_deltas(peer_before, peer_after)
    wait_deltas = counter_deltas(peer_before, peer_after_dm_wait)
    peer_connected_before = (
        normalize_port(get_path(peer_before, "serial", "active_port")) == peer_port
        and get_path(peer_before, "serial", "meshcore_connected") is True
    )
    peer_connected_after = (
        normalize_port(get_path(peer_after, "serial", "active_port")) == peer_port
        and get_path(peer_after, "serial", "meshcore_connected") is True
    )

    route_result = route_step.get("result", {}) if route_step else {}
    health_result = health_step.get("result", {}) if health_step else {}
    checks = {
        "controlled_peer_status_connected": peer_connected_before or peer_connected_after,
        "send_ok": bool(send_step and send_step.get("result", {}).get("ok")),
        "messages_dm_has_token": bool(messages_step and contains_token(messages_step.get("result"), token)),
        "packets_search_has_token": bool(packets_step and contains_token(packets_step.get("result"), token)),
        "route_trace_has_target": bool(route_result.get("ok") and route_result.get("fingerprint") == fingerprint),
        "controlled_peer_rx_contact_delta": (deltas.get("rx_contact_total") or 0)
        >= min_contact_delta,
        "health_ready": bool(health_result.get("ok") and health_result.get("board_ready") and health_result.get("ui_ready")),
        "no_public_commands": not any(command_has_public_tx(command) for command in commands),
    }
    return {
        "schema": DM_PROBE_SCHEMA,
        "mode": "hardware-dm-probe",
        "hardware_required": True,
        "physical_observed": True,
        "dry_run": False,
        "simulated": False,
        "simulation": False,
        "source_inspection": False,
        "execution_complete": True,
        "dm_rf_tx": bool(send_step and send_step.get("result", {}).get("ok")),
        "public_rf_tx": False,
        "formats_sd": False,
        "port": port,
        "baud": baud,
        "controlled_peer": {
            "fingerprint": fingerprint,
            "evidence_source": "explicit_peer_status",
            "port": peer_port,
            "status_path": str(peer_status_path),
        },
        "target_fingerprint": fingerprint,
        "token": token,
        "public_rf_transmit": False,
        "commands_sent": commands,
        "controlled_peer_before": status_snapshot(peer_before),
        "controlled_peer_after_dm_wait": status_snapshot(peer_after_dm_wait),
        "controlled_peer_after": status_snapshot(peer_after),
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
    peer_status_path: Path,
    peer_port: str,
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
    peer_before = read_json(peer_status_path)
    steps: list[dict] = []
    peer_after_dm_wait: dict | None = None

    with open_d1l_serial(serial, port=port, baudrate=baud, timeout=timeout) as ser:
        time.sleep(1.0)
        ser.reset_input_buffer()
        for command in commands:
            result = send_probe_command(ser, command, timeout)
            steps.append({"command": command, "result": result})
            if command.startswith("mesh send dm "):
                peer_after_dm_wait = wait_for_bot_receive(
                    peer_status_path,
                    peer_before,
                    wait_sec,
                    poll_sec,
                    min_contact_delta,
                )

    peer_after = read_json(peer_status_path)
    return build_report(
        port=port,
        baud=baud,
        peer_status_path=peer_status_path,
        peer_port=peer_port,
        fingerprint=fingerprint,
        token=token,
        commands=commands,
        steps=steps,
        peer_before=peer_before,
        peer_after_dm_wait=peer_after_dm_wait or peer_after,
        peer_after=peer_after,
        min_contact_delta=min_contact_delta,
    )


def write_report(report: dict, out_path: Path | None) -> Path:
    root = Path(__file__).resolve().parents[1]
    if out_path is None:
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        out_path = root / "artifacts" / "smoke" / f"d1l-dm-probe-{stamp}.json"
    elif not out_path.is_absolute():
        out_path = root / out_path
    stamp_report(report, root)
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
    parser.add_argument(
        "--peer-status",
        "--bot-status",
        dest="peer_status",
        default=os.environ.get("MESH_PEER_STATUS_PATH"),
    )
    parser.add_argument(
        "--peer-port",
        "--bot-port",
        dest="peer_port",
        default=os.environ.get("MESH_PEER_PORT"),
    )
    parser.add_argument("--wait-sec", type=float, default=12.0)
    parser.add_argument("--poll-sec", type=float, default=0.75)
    parser.add_argument("--min-contact-delta", type=int, default=1)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--out", default=None)
    args = parser.parse_args()

    token = args.token or default_token()
    if not args.port:
        parser.error("No D1L port supplied. Set D1L_PORT or pass --port.")
    if not args.peer_port:
        parser.error("No controlled-peer port supplied. Set MESH_PEER_PORT or pass --peer-port.")
    if not args.peer_status:
        parser.error(
            "No controlled-peer status file supplied. "
            "Set MESH_PEER_STATUS_PATH or pass --peer-status."
        )
    try:
        port, peer_port = enforce_port_policy(args.port, args.peer_port)
    except ValueError as exc:
        parser.error(str(exc))
    peer_status_path = Path(args.peer_status)
    out_path = Path(args.out) if args.out else None

    if args.dry_run:
        report = dry_run_report(
            args.fingerprint,
            token,
            peer_status_path,
            peer_port,
        )
    else:
        if not peer_status_path.exists():
            parser.error(f"Controlled-peer status file not found: {peer_status_path}")
        report = run_serial_probe(
            port=port,
            baud=args.baud,
            timeout=args.timeout,
            peer_status_path=peer_status_path,
            peer_port=peer_port,
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
