#!/usr/bin/env python3
"""Short or long hardware soak runner for MeshCore DeskOS D1L."""

from __future__ import annotations

import argparse
import json
import os
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable

try:
    from smoke_d1l import send_console_command
except ModuleNotFoundError:
    from scripts.smoke_d1l import send_console_command


SOAK_COMMANDS = [
    "health",
    "mesh status",
    "signal",
    "messages unread",
    "packets",
    "crashlog",
]


def utc_stamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def number_or_none(value) -> int | None:
    return value if isinstance(value, int) and not isinstance(value, bool) else None


def command_results(samples: Iterable[dict], command: str) -> list[dict]:
    out: list[dict] = []
    for sample in samples:
        for result in sample.get("results", []):
            if result.get("cmd") == command:
                out.append(result)
    return out


def numeric_values(rows: Iterable[dict], field: str) -> list[int]:
    return [value for row in rows if (value := number_or_none(row.get(field))) is not None]


def crashlog_entries(rows: Iterable[dict]) -> list[dict]:
    entries: list[dict] = []
    for row in rows:
        row_entries = row.get("entries")
        if isinstance(row_entries, list):
            entries.extend(entry for entry in row_entries if isinstance(entry, dict))
    return entries


def first_last_delta(rows: list[dict], field: str) -> int | None:
    values = numeric_values(rows, field)
    if len(values) < 2:
        return None
    return values[-1] - values[0]


def monotonic_non_decreasing(values: list[int]) -> bool:
    return all(curr >= prev for prev, curr in zip(values, values[1:]))


def send_soak_command(ser, command: str, timeout: float, retries: int, retry_delay_sec: float) -> dict:
    attempts = []
    for attempt in range(max(0, retries) + 1):
        result = send_console_command(ser, command, timeout)
        attempts.append(result)
        if result.get("ok"):
            if attempt:
                result = dict(result)
                result["attempts"] = attempt + 1
                result["recovered_after_retry"] = True
                result["retry_failures"] = attempts[:-1]
            return result
        if attempt < retries:
            time.sleep(retry_delay_sec)

    result = dict(attempts[-1])
    result["attempts"] = len(attempts)
    result["recovered_after_retry"] = False
    result["retry_failures"] = attempts[:-1]
    return result


def collect_sample(
    ser,
    timeout: float,
    label: str,
    elapsed_sec: float,
    command_retries: int,
    retry_delay_sec: float,
) -> dict:
    results = [
        send_soak_command(ser, command, timeout, command_retries, retry_delay_sec)
        for command in SOAK_COMMANDS
    ]
    return {
        "label": label,
        "elapsed_sec": round(elapsed_sec, 3),
        "results": results,
    }


def summarize_soak(
    *,
    samples: list[dict],
    active_events: list[dict],
    require_rx_delta: bool,
    min_rx_delta: int,
    min_tx_delta: int,
) -> dict:
    health = command_results(samples, "health")
    mesh = command_results(samples, "mesh status")
    signal = command_results(samples, "signal")
    packet_rows = command_results(samples, "packets")
    crashlog = command_results(samples, "crashlog")

    failures = []
    for sample in samples:
        for result in sample.get("results", []):
            if not result.get("ok"):
                failures.append(
                    {
                        "sample": sample.get("label"),
                        "elapsed_sec": sample.get("elapsed_sec"),
                        "cmd": result.get("cmd"),
                        "code": result.get("code"),
                    }
                )
    for event in active_events:
        result = event.get("result", {})
        if not result.get("ok"):
            failures.append(
                {
                    "sample": "active_public_tx",
                    "elapsed_sec": event.get("elapsed_sec"),
                    "cmd": result.get("cmd"),
                    "code": result.get("code"),
                }
            )

    uptime_values = numeric_values(health, "uptime_ms")
    rx_delta = first_last_delta(mesh, "rx_packets")
    tx_delta = first_last_delta(mesh, "tx_packets")
    packet_written_delta = first_last_delta(packet_rows, "total_written")
    heap_free_delta = first_last_delta(health, "heap_free")
    psram_free_delta = first_last_delta(health, "psram_free")

    current_stack = numeric_values(health, "current_task_stack_free_words")
    ui_stack = numeric_values(health, "ui_task_stack_free_words")
    lvgl_used = numeric_values(health, "lvgl_used_pct")
    heap_min = numeric_values(health, "heap_min_free")
    psram_min = numeric_values(health, "psram_min_free")
    signal_samples = numeric_values(signal, "sample_count")
    crashlog_total_delta = first_last_delta(crashlog, "total_written")
    crash_entries = crashlog_entries(crashlog)
    crash_like_count = sum(1 for entry in crash_entries if entry.get("crash_like") is True)
    crashlog_count_values = numeric_values(crashlog, "count")
    retry_rows = [
        result
        for sample in samples
        for result in sample.get("results", [])
        if number_or_none(result.get("attempts")) is not None and result.get("attempts") > 1
    ]
    retry_rows.extend(
        event.get("result", {})
        for event in active_events
        if number_or_none(event.get("result", {}).get("attempts")) is not None
        and event.get("result", {}).get("attempts") > 1
    )

    board_ready_all = bool(health) and all(row.get("board_ready") is True for row in health)
    ui_ready_all = bool(health) and all(row.get("ui_ready") is True for row in health)
    mesh_ready_all = bool(mesh) and all(
        row.get("state") == "ready"
        and row.get("identity_ready") is True
        and row.get("radio_ready") is True
        for row in mesh
    )
    uptime_monotonic = monotonic_non_decreasing(uptime_values)
    active_tx_ok = all(event.get("result", {}).get("ok") for event in active_events)

    threshold_failures: list[str] = []
    if not board_ready_all:
        threshold_failures.append("board_not_ready")
    if not ui_ready_all:
        threshold_failures.append("ui_not_ready")
    if not mesh_ready_all:
        threshold_failures.append("mesh_not_ready")
    if not uptime_monotonic:
        threshold_failures.append("uptime_reset_or_reboot_seen")
    if current_stack and min(current_stack) <= 0:
        threshold_failures.append("current_task_stack_watermark_zero")
    if ui_stack and min(ui_stack) <= 0:
        threshold_failures.append("ui_task_stack_watermark_zero")
    if active_events and not active_tx_ok:
        threshold_failures.append("active_public_tx_failed")
    if crash_like_count > 0:
        threshold_failures.append("crash_like_reset_seen")
    if min_tx_delta > 0 and (tx_delta is None or tx_delta < min_tx_delta):
        threshold_failures.append("tx_delta_below_minimum")
    if require_rx_delta and (rx_delta is None or rx_delta < min_rx_delta):
        threshold_failures.append("rx_delta_below_minimum")

    ok = not failures and not threshold_failures

    return {
        "ok": ok,
        "sample_count": len(samples),
        "active_public_tx_count": len(active_events),
        "command_failure_count": len(failures),
        "command_failures": failures,
        "threshold_failures": threshold_failures,
        "board_ready_all": board_ready_all,
        "ui_ready_all": ui_ready_all,
        "mesh_ready_all": mesh_ready_all,
        "uptime_monotonic": uptime_monotonic,
        "mesh_rx_packet_delta": rx_delta,
        "mesh_tx_packet_delta": tx_delta,
        "packet_total_written_delta": packet_written_delta,
        "heap_free_delta": heap_free_delta,
        "psram_free_delta": psram_free_delta,
        "heap_min_free_floor": min(heap_min) if heap_min else None,
        "psram_min_free_floor": min(psram_min) if psram_min else None,
        "current_task_stack_free_words_floor": min(current_stack) if current_stack else None,
        "ui_task_stack_free_words_floor": min(ui_stack) if ui_stack else None,
        "lvgl_used_pct_peak": max(lvgl_used) if lvgl_used else None,
        "signal_sample_count_peak": max(signal_samples) if signal_samples else None,
        "crashlog_total_written_delta": crashlog_total_delta,
        "crashlog_count_peak": max(crashlog_count_values) if crashlog_count_values else None,
        "crashlog_crash_like_count": crash_like_count,
        "command_retry_count": len(retry_rows),
        "command_recovered_after_retry_count": sum(
            1 for row in retry_rows if row.get("recovered_after_retry") is True
        ),
        "command_retry_failure_count": sum(
            1 for row in retry_rows if row.get("recovered_after_retry") is False
        ),
    }


def dry_run_report(
    *,
    duration_sec: float,
    sample_interval_sec: float,
    active_public_text: str | None,
    active_interval_sec: float,
    require_rx_delta: bool,
    min_rx_delta: int,
    min_tx_delta: int,
    clear_crashlog_before_start: bool,
    command_retries: int,
    retry_delay_sec: float,
) -> dict:
    return {
        "schema": 1,
        "mode": "dry-run",
        "hardware_required": False,
        "ok": True,
        "duration_sec": duration_sec,
        "sample_interval_sec": sample_interval_sec,
        "commands": SOAK_COMMANDS,
        "active_public_text": active_public_text,
        "active_interval_sec": active_interval_sec,
        "require_rx_delta": require_rx_delta,
        "min_rx_delta": min_rx_delta,
        "min_tx_delta": min_tx_delta,
        "clear_crashlog_before_start": clear_crashlog_before_start,
        "command_retries": command_retries,
        "retry_delay_sec": retry_delay_sec,
    }


def run_serial_soak(
    *,
    port: str,
    baud: int,
    timeout: float,
    duration_sec: float,
    sample_interval_sec: float,
    active_public_text: str | None,
    active_interval_sec: float,
    startup_settle_sec: float,
    require_rx_delta: bool,
    min_rx_delta: int,
    min_tx_delta: int,
    clear_crashlog_before_start: bool,
    command_retries: int,
    retry_delay_sec: float,
) -> dict:
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("pyserial is required for hardware soak: python -m pip install pyserial") from exc

    if duration_sec <= 0:
        raise ValueError("duration_sec must be positive")
    if sample_interval_sec <= 0:
        raise ValueError("sample_interval_sec must be positive")
    if active_public_text and active_interval_sec <= 0:
        raise ValueError("active_interval_sec must be positive when active public TX is enabled")

    samples: list[dict] = []
    active_events: list[dict] = []
    setup_events: list[dict] = []
    started_at = datetime.now(timezone.utc)

    with serial.Serial(port=port, baudrate=baud, timeout=timeout) as ser:
        time.sleep(startup_settle_sec)
        ser.reset_input_buffer()

        if clear_crashlog_before_start:
            setup_events.append(
                {
                    "elapsed_sec": 0.0,
                    "cmd": "crashlog clear",
                    "result": send_soak_command(
                        ser, "crashlog clear", timeout, command_retries, retry_delay_sec
                    ),
                }
            )

        start = time.monotonic()
        deadline = start + duration_sec
        next_sample = start
        next_active = start if active_public_text else float("inf")
        sample_index = 0

        while True:
            now = time.monotonic()
            elapsed = now - start

            if now >= next_sample:
                label = "start" if sample_index == 0 else f"sample-{sample_index}"
                samples.append(
                    collect_sample(
                        ser, timeout, label, elapsed, command_retries, retry_delay_sec
                    )
                )
                sample_index += 1
                next_sample += sample_interval_sec

            now = time.monotonic()
            elapsed = now - start
            if active_public_text and now >= next_active and now < deadline:
                result = send_soak_command(
                    ser,
                    f"mesh send public {active_public_text}",
                    timeout,
                    command_retries,
                    retry_delay_sec,
                )
                active_events.append(
                    {
                        "elapsed_sec": round(elapsed, 3),
                        "text": active_public_text,
                        "result": result,
                    }
                )
                next_active += active_interval_sec

            if now >= deadline:
                break

            sleep_for = min(next_sample, next_active, deadline) - now
            time.sleep(max(0.1, min(sleep_for, 1.0)))

        final_elapsed = time.monotonic() - start
        samples.append(
            collect_sample(
                ser, timeout, "final", final_elapsed, command_retries, retry_delay_sec
            )
        )

    ended_at = datetime.now(timezone.utc)
    summary = summarize_soak(
        samples=samples,
        active_events=active_events,
        require_rx_delta=require_rx_delta,
        min_rx_delta=min_rx_delta,
        min_tx_delta=min_tx_delta,
    )
    setup_failures = [
        {
            "sample": "setup",
            "elapsed_sec": event.get("elapsed_sec"),
            "cmd": event.get("cmd"),
            "code": event.get("result", {}).get("code"),
        }
        for event in setup_events
        if not event.get("result", {}).get("ok")
    ]
    if setup_failures:
        summary["command_failures"].extend(setup_failures)
        summary["command_failure_count"] = len(summary["command_failures"])
        summary["ok"] = False

    return {
        "schema": 1,
        "mode": "hardware",
        "port": port,
        "baud": baud,
        "started_at": started_at.isoformat().replace("+00:00", "Z"),
        "ended_at": ended_at.isoformat().replace("+00:00", "Z"),
        "duration_sec": duration_sec,
        "sample_interval_sec": sample_interval_sec,
        "active_public_text": active_public_text,
        "active_interval_sec": active_interval_sec,
        "require_rx_delta": require_rx_delta,
        "min_rx_delta": min_rx_delta,
        "min_tx_delta": min_tx_delta,
        "clear_crashlog_before_start": clear_crashlog_before_start,
        "command_retries": command_retries,
        "retry_delay_sec": retry_delay_sec,
        "ok": summary["ok"],
        "summary": summary,
        "setup_events": setup_events,
        "active_events": active_events,
        "samples": samples,
    }


def resolve_out_path(out_arg: str | None, mode: str) -> Path:
    root = Path(__file__).resolve().parents[1]
    if out_arg:
        out_path = Path(out_arg)
        if not out_path.is_absolute():
            out_path = root / out_path
        return out_path
    return root / "artifacts" / "soak" / f"d1l-soak-{mode}-{utc_stamp()}.json"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default=os.environ.get("D1L_PORT"))
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--duration-sec", type=float, default=300.0)
    parser.add_argument("--sample-interval-sec", type=float, default=60.0)
    parser.add_argument("--active-public-text", default=None)
    parser.add_argument("--active-interval-sec", type=float, default=120.0)
    parser.add_argument("--startup-settle-sec", type=float, default=1.0)
    parser.add_argument("--require-rx-delta", action="store_true")
    parser.add_argument("--min-rx-delta", type=int, default=1)
    parser.add_argument("--min-tx-delta", type=int, default=0)
    parser.add_argument("--clear-crashlog-before-start", action="store_true")
    parser.add_argument("--command-retries", type=int, default=1)
    parser.add_argument("--retry-delay-sec", type=float, default=0.5)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--out", default=None)
    args = parser.parse_args()

    if args.dry_run:
        report = dry_run_report(
            duration_sec=args.duration_sec,
            sample_interval_sec=args.sample_interval_sec,
            active_public_text=args.active_public_text,
            active_interval_sec=args.active_interval_sec,
            require_rx_delta=args.require_rx_delta,
            min_rx_delta=args.min_rx_delta,
            min_tx_delta=args.min_tx_delta,
            clear_crashlog_before_start=args.clear_crashlog_before_start,
            command_retries=args.command_retries,
            retry_delay_sec=args.retry_delay_sec,
        )
        mode = "dry-run"
    else:
        if not args.port:
            parser.error("No D1L port supplied. Set D1L_PORT or pass --port.")
        try:
            report = run_serial_soak(
                port=args.port,
                baud=args.baud,
                timeout=args.timeout,
                duration_sec=args.duration_sec,
                sample_interval_sec=args.sample_interval_sec,
                active_public_text=args.active_public_text,
                active_interval_sec=args.active_interval_sec,
                startup_settle_sec=args.startup_settle_sec,
                require_rx_delta=args.require_rx_delta,
                min_rx_delta=args.min_rx_delta,
                min_tx_delta=args.min_tx_delta,
                clear_crashlog_before_start=args.clear_crashlog_before_start,
                command_retries=args.command_retries,
                retry_delay_sec=args.retry_delay_sec,
            )
        except ValueError as exc:
            parser.error(str(exc))
        mode = "hardware"

    out_path = resolve_out_path(args.out, mode)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(report, indent=2), encoding="ascii")
    print(json.dumps(report))
    return 0 if report.get("ok") else 1


if __name__ == "__main__":
    raise SystemExit(main())
