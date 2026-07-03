#!/usr/bin/env python3
"""Serial-driven UI tab abuse runner for MeshCore DeskOS D1L."""

from __future__ import annotations

import argparse
import json
import os
import time
from datetime import datetime, timezone
from pathlib import Path

try:
    from smoke_d1l import send_console_command
    from artifact_metadata import stamp_report
except ModuleNotFoundError:
    from scripts.artifact_metadata import stamp_report
    from scripts.smoke_d1l import send_console_command


TAB_SEQUENCE = ("home", "messages", "nodes", "map", "packets", "settings")
RELEASE_MIN_CYCLES = 500
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


def dry_run_report(cycles: int, settle_sec: float, clear_crashlog_before_start: bool) -> dict:
    return {
        "schema": 1,
        "mode": "dry-run",
        "ok": True,
        "hardware_required": False,
        "explicit_port_required": True,
        "cycles": cycles,
        "settle_sec": settle_sec,
        "clear_crashlog_before_start": clear_crashlog_before_start,
        "tabs": list(TAB_SEQUENCE),
        "release_min_cycles": RELEASE_MIN_CYCLES,
        "telemetry_fields": list(TELEMETRY_FIELDS),
        "commands": ["ui status", *[f"ui tab {tab}" for tab in TAB_SEQUENCE], "health", "crashlog"],
    }


def crashlog_has_entries(row: dict) -> bool:
    entries = row.get("entries")
    return isinstance(entries, list) and len(entries) > 0


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


def run_tab_abuse(
    *,
    port: str,
    baud: int,
    timeout: float,
    cycles: int,
    settle_sec: float,
    poll_sec: float,
    clear_crashlog_before_start: bool,
) -> dict:
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("pyserial is required for hardware tab abuse: python -m pip install pyserial") from exc

    events: list[dict] = []
    setup_events: list[dict] = []
    started_at = datetime.now(timezone.utc)

    with serial.Serial(port=port, baudrate=baud, timeout=timeout) as ser:
        time.sleep(1.0)
        ser.reset_input_buffer()

        setup_events.append({"cmd": "ui status", "result": send_console_command(ser, "ui status", timeout)})
        if clear_crashlog_before_start:
            setup_events.append(
                {"cmd": "crashlog clear", "result": send_console_command(ser, "crashlog clear", timeout)}
            )

        for cycle in range(cycles):
            for tab in TAB_SEQUENCE:
                request = send_console_command(ser, f"ui tab {tab}", timeout)
                time.sleep(settle_sec)
                active, statuses = wait_for_tab(ser, tab, timeout, poll_sec)
                health = send_console_command(ser, "health", timeout)
                crashlog = send_console_command(ser, "crashlog", timeout)
                events.append(
                    {
                        "cycle": cycle + 1,
                        "tab": tab,
                        "request": request,
                        "active": active,
                        "statuses": statuses,
                        "health": health,
                        "crashlog": crashlog,
                    }
                )

    ended_at = datetime.now(timezone.utc)
    failures = [
        {
            "cycle": event["cycle"],
            "tab": event["tab"],
            "request_ok": event["request"].get("ok"),
            "active": event["active"],
            "health_ok": event["health"].get("ok"),
            "crashlog_entries": crashlog_has_entries(event["crashlog"]),
        }
        for event in events
        if not event["request"].get("ok")
        or not event["active"]
        or not event["health"].get("ok")
        or crashlog_has_entries(event["crashlog"])
    ]
    setup_failures = [
        event for event in setup_events if not event.get("result", {}).get("ok")
    ]
    telemetry = summarize_telemetry(events)
    telemetry_failures = []
    if not telemetry.get("uptime_monotonic"):
        telemetry_failures.append({"check": "uptime_monotonic", "ok": False})
    return {
        "schema": 1,
        "mode": "hardware",
        "port": port,
        "baud": baud,
        "started_at": started_at.isoformat().replace("+00:00", "Z"),
        "ended_at": ended_at.isoformat().replace("+00:00", "Z"),
        "cycles": cycles,
        "release_min_cycles": RELEASE_MIN_CYCLES,
        "settle_sec": settle_sec,
        "poll_sec": poll_sec,
        "clear_crashlog_before_start": clear_crashlog_before_start,
        "tabs": list(TAB_SEQUENCE),
        "ok": not failures and not setup_failures and not telemetry_failures,
        "failure_count": len(failures) + len(setup_failures) + len(telemetry_failures),
        "failures": failures,
        "telemetry_failures": telemetry_failures,
        "telemetry": telemetry,
        "setup_events": setup_events,
        "events": events,
    }


def resolve_out_path(out_arg: str | None, mode: str) -> Path:
    root = Path(__file__).resolve().parents[1]
    if out_arg:
        out_path = Path(out_arg)
        if not out_path.is_absolute():
            out_path = root / out_path
        return out_path
    return root / "artifacts" / "ui-tab-abuse" / f"d1l-ui-tab-abuse-{mode}-{utc_stamp()}.json"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default=os.environ.get("D1L_PORT"))
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--cycles", type=int, default=RELEASE_MIN_CYCLES)
    parser.add_argument("--settle-sec", type=float, default=0.1)
    parser.add_argument("--poll-sec", type=float, default=0.05)
    parser.add_argument("--clear-crashlog-before-start", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--out", default=None)
    args = parser.parse_args()

    if args.cycles <= 0:
        parser.error("--cycles must be positive")
    if args.settle_sec < 0:
        parser.error("--settle-sec cannot be negative")
    if args.poll_sec <= 0:
        parser.error("--poll-sec must be positive")

    if args.dry_run:
        report = dry_run_report(args.cycles, args.settle_sec, args.clear_crashlog_before_start)
        mode = "dry-run"
    else:
        if not args.port:
            parser.error("No D1L port supplied. Set D1L_PORT or pass --port.")
        report = run_tab_abuse(
            port=args.port,
            baud=args.baud,
            timeout=args.timeout,
            cycles=args.cycles,
            settle_sec=args.settle_sec,
            poll_sec=args.poll_sec,
            clear_crashlog_before_start=args.clear_crashlog_before_start,
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
