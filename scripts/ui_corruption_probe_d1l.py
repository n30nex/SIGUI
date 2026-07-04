#!/usr/bin/env python3
"""Targeted serial UI corruption probe for MeshCore DeskOS D1L."""

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
except ModuleNotFoundError:  # pragma: no cover - package import path used by pytest
    from scripts.artifact_metadata import stamp_report
    from scripts.smoke_d1l import send_console_command


TAB_SEQUENCE = ("home", "messages", "nodes", "map", "packets", "settings")
RELEASE_MIN_ROUNDS = 20
TELEMETRY_FIELDS = (
    "heap_free",
    "heap_min_free",
    "heap_largest_free",
    "lvgl_free_bytes",
    "lvgl_largest_free_bytes",
    "lvgl_used_pct",
    "ui_task_stack_free_words",
    "reset_reason",
)


def utc_stamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def token_for_round(round_number: int, run_prefix: str = "uiRx") -> str:
    return f"{run_prefix}{round_number:04d}"


def token_prefix_for_run(started_at: datetime) -> str:
    return f"uic{int(started_at.timestamp()) % 1000000:06d}"


def dry_run_report(
    rounds: int,
    settle_sec: float,
    clear_crashlog_before_start: bool,
    skip_data_canary: bool,
) -> dict:
    commands = [
        "ui status",
        *[f"ui tab {tab}" for tab in TAB_SEQUENCE],
        f"ui data-canary {token_for_round(1)}",
        f"packets search {token_for_round(1)}",
        f"messages public search {token_for_round(1)}",
        "health",
        "crashlog",
    ]
    return {
        "schema": 1,
        "kind": "ui_corruption_probe",
        "mode": "dry-run",
        "ok": True,
        "hardware_required": False,
        "explicit_port_required": True,
        "rounds": rounds,
        "release_min_rounds": RELEASE_MIN_ROUNDS,
        "settle_sec": settle_sec,
        "clear_crashlog_before_start": clear_crashlog_before_start,
        "skip_data_canary": skip_data_canary,
        "tabs": list(TAB_SEQUENCE),
        "public_rf_tx": False,
        "formats_sd": False,
        "telemetry_fields": list(TELEMETRY_FIELDS),
        "commands": commands,
        "checks": {
            "tab_switches_settle": True,
            "button_refreshes_deferred": True,
            "data_refresh_exercised": not skip_data_canary,
            "data_refreshes_pass": not skip_data_canary,
            "no_public_rf": True,
            "no_formatting": True,
            "no_stuck_pending": True,
            "final_active_tab_known": True,
        },
    }


def crashlog_has_entries(row: dict) -> bool:
    entries = row.get("entries")
    return isinstance(entries, list) and len(entries) > 0


def result_entries_contain_token(row: dict, token: str) -> bool:
    entries = row.get("entries")
    if not isinstance(entries, list):
        return False
    return any(
        token in json.dumps(entry, sort_keys=True)
        for entry in entries
        if isinstance(entry, dict)
    )


def wait_for_tab(ser, tab: str, timeout: float, poll_sec: float) -> tuple[bool, list[dict]]:
    deadline = time.monotonic() + timeout
    statuses: list[dict] = []
    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break
        status = send_console_command(ser, "ui status", remaining)
        statuses.append(status)
        if status.get("ok") and status.get("active_tab") == tab and status.get("pending") is False:
            return True, statuses
        time.sleep(min(poll_sec, max(0.0, deadline - time.monotonic())))
    return False, statuses


def _int_values(rows: list[dict], field: str) -> list[int]:
    values: list[int] = []
    for row in rows:
        value = row.get(field)
        if isinstance(value, bool):
            continue
        if isinstance(value, int):
            values.append(value)
        elif isinstance(value, str) and value.isdigit():
            values.append(int(value))
    return values


def summarize_telemetry(events: list[dict]) -> dict:
    health_rows = [
        event.get("health", {})
        for event in events
        if isinstance(event.get("health"), dict) and event.get("health", {}).get("ok") is True
    ]
    status_rows = []
    for event in events:
        statuses = event.get("statuses") or []
        if statuses and isinstance(statuses[-1], dict):
            status_rows.append(statuses[-1])
        status = event.get("status")
        if isinstance(status, dict):
            status_rows.append(status)
        for nested_name in ("packet_tab", "messages_tab"):
            nested = event.get(nested_name)
            if isinstance(nested, dict):
                nested_statuses = nested.get("statuses") or []
                if nested_statuses and isinstance(nested_statuses[-1], dict):
                    status_rows.append(nested_statuses[-1])

    metrics = {}
    for field in TELEMETRY_FIELDS:
        if field == "reset_reason":
            continue
        values = _int_values(health_rows, field)
        if values:
            metrics[field] = {"min": min(values), "max": max(values), "last": values[-1]}

    uptimes = _int_values(health_rows, "uptime_ms")
    return {
        "health_sample_count": len(health_rows),
        "telemetry_fields": list(TELEMETRY_FIELDS),
        "metrics": metrics,
        "reset_reasons": sorted(
            {row.get("reset_reason") for row in health_rows if isinstance(row.get("reset_reason"), str)}
        ),
        "uptime_monotonic": bool(uptimes) and all(after >= before for before, after in zip(uptimes, uptimes[1:])),
        "final_active_tab": status_rows[-1].get("active_tab") if status_rows else None,
        "final_pending": status_rows[-1].get("pending") if status_rows else None,
    }


def tab_step(ser, tab: str, timeout: float, settle_sec: float, poll_sec: float, round_number: int) -> dict:
    request = send_console_command(ser, f"ui tab {tab}", timeout)
    time.sleep(settle_sec)
    active, statuses = wait_for_tab(ser, tab, timeout, poll_sec)
    health = send_console_command(ser, "health", timeout)
    crashlog = send_console_command(ser, "crashlog", timeout)
    return {
        "round": round_number,
        "kind": "tab",
        "tab": tab,
        "request": request,
        "active": active,
        "statuses": statuses,
        "health": health,
        "crashlog": crashlog,
    }


def data_refresh_step(ser, token: str, timeout: float, settle_sec: float, poll_sec: float, round_number: int) -> dict:
    canary = send_console_command(ser, f"ui data-canary {token}", timeout)
    packet_tab = tab_step(ser, "packets", timeout, settle_sec, poll_sec, round_number)
    packets = send_console_command(ser, f"packets search {token}", timeout)
    messages_tab = tab_step(ser, "messages", timeout, settle_sec, poll_sec, round_number)
    messages = send_console_command(ser, f"messages public search {token}", timeout)
    status = send_console_command(ser, "ui status", timeout)
    health = send_console_command(ser, "health", timeout)
    crashlog = send_console_command(ser, "crashlog", timeout)
    return {
        "round": round_number,
        "kind": "data_refresh",
        "token": token,
        "data_canary": canary,
        "packet_tab": packet_tab,
        "packets_search": packets,
        "messages_tab": messages_tab,
        "messages_search": messages,
        "status": status,
        "health": health,
        "crashlog": crashlog,
    }


def event_failures(events: list[dict], *, skip_data_canary: bool) -> list[dict]:
    failures: list[dict] = []
    for event in events:
        if event.get("kind") == "tab":
            request = event.get("request", {})
            health = event.get("health", {})
            crashlog = event.get("crashlog", {})
            if (
                not request.get("ok")
                or not event.get("active")
                or not health.get("ok")
                or crashlog_has_entries(crashlog)
            ):
                failures.append(
                    {
                        "round": event.get("round"),
                        "kind": "tab",
                        "tab": event.get("tab"),
                        "request_ok": request.get("ok"),
                        "active": event.get("active"),
                        "health_ok": health.get("ok"),
                        "crashlog_entries": crashlog_has_entries(crashlog),
                    }
                )
        elif event.get("kind") == "data_refresh" and not skip_data_canary:
            token = str(event.get("token") or "")
            canary = event.get("data_canary", {})
            packets = event.get("packets_search", {})
            messages = event.get("messages_search", {})
            health = event.get("health", {})
            crashlog = event.get("crashlog", {})
            if (
                not canary.get("ok")
                or not packets.get("ok")
                or not result_entries_contain_token(packets, token)
                or not messages.get("ok")
                or not result_entries_contain_token(messages, token)
                or not health.get("ok")
                or crashlog_has_entries(crashlog)
            ):
                failures.append(
                    {
                        "round": event.get("round"),
                        "kind": "data_refresh",
                        "token": token,
                        "data_canary_ok": canary.get("ok"),
                        "packets_ok": packets.get("ok"),
                        "packets_entries_contain_token": result_entries_contain_token(packets, token),
                        "messages_ok": messages.get("ok"),
                        "messages_entries_contain_token": result_entries_contain_token(messages, token),
                        "health_ok": health.get("ok"),
                        "crashlog_entries": crashlog_has_entries(crashlog),
                    }
                )
    return failures


def run_probe(
    *,
    port: str,
    baud: int,
    timeout: float,
    rounds: int,
    settle_sec: float,
    poll_sec: float,
    clear_crashlog_before_start: bool,
    skip_data_canary: bool,
) -> dict:
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("pyserial is required: python -m pip install pyserial") from exc

    events: list[dict] = []
    setup_events: list[dict] = []
    started_at = datetime.now(timezone.utc)
    run_prefix = token_prefix_for_run(started_at)

    with serial.Serial(port=port, baudrate=baud, timeout=timeout) as ser:
        time.sleep(1.0)
        ser.reset_input_buffer()
        setup_events.append({"cmd": "ui status", "result": send_console_command(ser, "ui status", timeout)})
        if clear_crashlog_before_start:
            setup_events.append(
                {"cmd": "crashlog clear", "result": send_console_command(ser, "crashlog clear", timeout)}
            )
        for round_number in range(1, rounds + 1):
            for tab in TAB_SEQUENCE:
                events.append(tab_step(ser, tab, timeout, settle_sec, poll_sec, round_number))
            if not skip_data_canary:
                events.append(
                    data_refresh_step(
                        ser,
                        token_for_round(round_number, run_prefix),
                        timeout,
                        settle_sec,
                        poll_sec,
                        round_number,
                    )
                )

    ended_at = datetime.now(timezone.utc)
    setup_failures = [event for event in setup_events if not event.get("result", {}).get("ok")]
    failures = event_failures(events, skip_data_canary=skip_data_canary)
    telemetry = summarize_telemetry(events)
    telemetry_failures = []
    if not telemetry.get("uptime_monotonic"):
        telemetry_failures.append({"check": "uptime_monotonic", "ok": False})
    if telemetry.get("final_pending") is not False:
        telemetry_failures.append({"check": "no_stuck_pending", "ok": False})
    if telemetry.get("final_active_tab") not in TAB_SEQUENCE:
        telemetry_failures.append({"check": "final_active_tab_known", "ok": False})
    report = {
        "schema": 1,
        "kind": "ui_corruption_probe",
        "mode": "hardware",
        "port": port,
        "baud": baud,
        "started_at": started_at.isoformat().replace("+00:00", "Z"),
        "ended_at": ended_at.isoformat().replace("+00:00", "Z"),
        "rounds": rounds,
        "release_min_rounds": RELEASE_MIN_ROUNDS,
        "settle_sec": settle_sec,
        "poll_sec": poll_sec,
        "data_canary_prefix": run_prefix,
        "clear_crashlog_before_start": clear_crashlog_before_start,
        "skip_data_canary": skip_data_canary,
        "tabs": list(TAB_SEQUENCE),
        "public_rf_tx": False,
        "formats_sd": False,
        "data_refresh_events": sum(1 for event in events if event.get("kind") == "data_refresh"),
        "ok": not failures and not setup_failures and not telemetry_failures,
        "failure_count": len(failures) + len(setup_failures) + len(telemetry_failures),
        "failures": failures,
        "telemetry_failures": telemetry_failures,
        "telemetry": telemetry,
        "checks": {
            "tab_switches_settle": not any(failure.get("kind") == "tab" for failure in failures),
            "data_refresh_exercised": skip_data_canary
            or sum(1 for event in events if event.get("kind") == "data_refresh") == rounds,
            "data_refreshes_pass": not any(
                failure.get("kind") == "data_refresh" for failure in failures
            ),
            "no_public_rf": True,
            "no_formatting": True,
            "uptime_monotonic": telemetry.get("uptime_monotonic") is True,
            "no_stuck_pending": telemetry.get("final_pending") is False,
            "final_active_tab_known": telemetry.get("final_active_tab") in TAB_SEQUENCE,
        },
        "setup_events": setup_events,
        "events": events,
    }
    return report


def resolve_out_path(out_arg: str | None, mode: str) -> Path:
    root = Path(__file__).resolve().parents[1]
    if out_arg:
        out_path = Path(out_arg)
        if not out_path.is_absolute():
            out_path = root / out_path
        return out_path
    return root / "artifacts" / "ui-corruption-probe" / f"d1l-ui-corruption-probe-{mode}-{utc_stamp()}.json"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default=os.environ.get("D1L_PORT"))
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--rounds", type=int, default=RELEASE_MIN_ROUNDS)
    parser.add_argument("--settle-sec", type=float, default=0.1)
    parser.add_argument("--poll-sec", type=float, default=0.05)
    parser.add_argument("--clear-crashlog-before-start", action="store_true")
    parser.add_argument("--skip-data-canary", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--out", default=None)
    args = parser.parse_args()

    if args.rounds <= 0:
        parser.error("--rounds must be positive")
    if args.settle_sec < 0:
        parser.error("--settle-sec cannot be negative")
    if args.poll_sec <= 0:
        parser.error("--poll-sec must be positive")

    if args.dry_run:
        report = dry_run_report(
            args.rounds,
            args.settle_sec,
            args.clear_crashlog_before_start,
            args.skip_data_canary,
        )
        mode = "dry-run"
    else:
        if not args.port:
            parser.error("No D1L port supplied. Set D1L_PORT or pass --port.")
        report = run_probe(
            port=args.port,
            baud=args.baud,
            timeout=args.timeout,
            rounds=args.rounds,
            settle_sec=args.settle_sec,
            poll_sec=args.poll_sec,
            clear_crashlog_before_start=args.clear_crashlog_before_start,
            skip_data_canary=args.skip_data_canary,
        )
        mode = "hardware"

    out_path = resolve_out_path(args.out, mode)
    stamp_report(report, Path(__file__).resolve().parents[1])
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(report, indent=2), encoding="ascii")
    print(json.dumps(report))
    return 0 if report.get("ok") else 1


if __name__ == "__main__":
    raise SystemExit(main())
