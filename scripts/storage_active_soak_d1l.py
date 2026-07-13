#!/usr/bin/env python3
"""Run the source-bound WP-01 storage-active soak without RF transmission."""

from __future__ import annotations

import argparse
import json
import os
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

try:
    from artifact_metadata import stamp_report
    from sd_reboot_remount_acceptance_d1l import (
        MOUNT_POLL_ATTEMPTS,
        MOUNT_POLL_INTERVAL_SECONDS,
        PERSISTENCE_POLL_ATTEMPTS,
        PERSISTENCE_POLL_INTERVAL_SECONDS,
        dry_run_report as reboot_dry_run_report,
        run_acceptance,
    )
    from smoke_d1l import exact_commit
    from soak_d1l import dry_run_report as soak_dry_run_report
    from soak_d1l import run_serial_soak
except ImportError:  # pragma: no cover - package import path used by pytest
    from scripts.artifact_metadata import stamp_report
    from scripts.sd_reboot_remount_acceptance_d1l import (
        MOUNT_POLL_ATTEMPTS,
        MOUNT_POLL_INTERVAL_SECONDS,
        PERSISTENCE_POLL_ATTEMPTS,
        PERSISTENCE_POLL_INTERVAL_SECONDS,
        dry_run_report as reboot_dry_run_report,
        run_acceptance,
    )
    from scripts.smoke_d1l import exact_commit
    from scripts.soak_d1l import dry_run_report as soak_dry_run_report
    from scripts.soak_d1l import run_serial_soak


DEFAULT_SEGMENT_COUNT = 4
DEFAULT_SEGMENT_DURATION_SECONDS = 30 * 60
MIN_SEGMENT_COUNT = 2
MIN_HARDWARE_DURATION_SECONDS = 60 * 60
DEFAULT_SAMPLE_INTERVAL_SECONDS = 5 * 60


def _nested_true(value: object, name: str) -> bool:
    if isinstance(value, dict):
        return any(
            (key == name and child is True) or _nested_true(child, name)
            for key, child in value.items()
        )
    if isinstance(value, list):
        return any(_nested_true(child, name) for child in value)
    return False


def _retained_canary_results(report: dict) -> list[dict]:
    return [
        result
        for result in report.get("results", [])
        if isinstance(result, dict) and result.get("cmd") == "storage retained-canary"
    ]


def _canary_safety_explicit(report: dict) -> bool:
    rows = _retained_canary_results(report)
    return bool(
        len(rows) == 1
        and rows[0].get("public_rf_tx") is False
        and rows[0].get("dm_rf_tx") is False
        and rows[0].get("formats_sd") is False
    )


def _validate_shape(segment_count: int, segment_duration_sec: float) -> None:
    if segment_count < MIN_SEGMENT_COUNT:
        raise ValueError(f"segment_count must be at least {MIN_SEGMENT_COUNT}")
    if segment_duration_sec <= 0:
        raise ValueError("segment_duration_sec must be positive")
    if segment_count * segment_duration_sec < MIN_HARDWARE_DURATION_SECONDS:
        raise ValueError("storage-active hardware source must schedule at least 3600 seconds")


def _token(prefix: str, reboot_index: int) -> str:
    token = f"{prefix}{reboot_index:02d}"
    if not token or len(token) > 31 or not all(
        char.isalnum() or char in "_.-" for char in token
    ):
        raise ValueError("token prefix must produce a 1-31 character safe token")
    return token


def dry_run_report(
    *,
    port: str | None,
    baud: int,
    expected_firmware_commit: str,
    segment_count: int = DEFAULT_SEGMENT_COUNT,
    segment_duration_sec: float = DEFAULT_SEGMENT_DURATION_SECONDS,
    sample_interval_sec: float = DEFAULT_SAMPLE_INTERVAL_SECONDS,
    token_prefix: str = "wp01soak",
) -> dict:
    commit = exact_commit(expected_firmware_commit)
    if commit is None:
        raise ValueError("expected_firmware_commit must be an exact 40-character hexadecimal SHA")
    _validate_shape(segment_count, segment_duration_sec)
    events: list[dict[str, Any]] = []
    for index in range(1, segment_count + 1):
        events.append(
            {
                "kind": "segment",
                "index": index,
                "report": soak_dry_run_report(
                    duration_sec=segment_duration_sec,
                    sample_interval_sec=sample_interval_sec,
                    active_public_text=None,
                    active_interval_sec=120.0,
                    require_rx_delta=False,
                    min_rx_delta=0,
                    min_tx_delta=0,
                    clear_crashlog_before_start=False,
                    command_retries=0,
                    retry_delay_sec=0.0,
                    sample_storage=True,
                    sd_file_canary=True,
                    allow_sd_unavailable=False,
                    active_dm_fingerprint=None,
                    active_dm_text=None,
                    expected_firmware_commit=commit,
                ),
            }
        )
        if index < segment_count:
            token = _token(token_prefix, index)
            events.append(
                {
                    "kind": "reboot",
                    "index": index,
                    "token": token,
                    "report": reboot_dry_run_report(
                        token,
                        include_reboot=True,
                        expected_firmware_commit=commit,
                    ),
                }
            )
    return {
        "schema": 1,
        "kind": "d1l_storage_active_soak_source",
        "mode": "dry-run",
        "hardware_required": False,
        "ok": True,
        "port": port,
        "baud": baud,
        "expected_firmware_commit": commit,
        "segment_count": segment_count,
        "segment_duration_sec": segment_duration_sec,
        "scheduled_segment_duration_sec": segment_count * segment_duration_sec,
        "sample_interval_sec": sample_interval_sec,
        "event_topology": [event["kind"] for event in events],
        "events": events,
        "public_rf_tx": False,
        "dm_rf_tx": False,
        "formats_sd": False,
    }


def run_storage_active_soak(
    *,
    root: Path,
    port: str,
    baud: int,
    timeout: float,
    expected_firmware_commit: str,
    segment_count: int = DEFAULT_SEGMENT_COUNT,
    segment_duration_sec: float = DEFAULT_SEGMENT_DURATION_SECONDS,
    sample_interval_sec: float = DEFAULT_SAMPLE_INTERVAL_SECONDS,
    startup_settle_sec: float = 1.0,
    reboot_settle_sec: float = 8.0,
    mount_poll_attempts: int = MOUNT_POLL_ATTEMPTS,
    mount_poll_interval_sec: float = MOUNT_POLL_INTERVAL_SECONDS,
    persistence_poll_attempts: int = PERSISTENCE_POLL_ATTEMPTS,
    persistence_poll_interval_sec: float = PERSISTENCE_POLL_INTERVAL_SECONDS,
    token_prefix: str = "wp01soak",
) -> dict:
    commit = exact_commit(expected_firmware_commit)
    if commit is None:
        raise ValueError("expected_firmware_commit must be an exact 40-character hexadecimal SHA")
    _validate_shape(segment_count, segment_duration_sec)
    if sample_interval_sec <= 0:
        raise ValueError("sample_interval_sec must be positive")

    started_at = datetime.now(timezone.utc)
    events: list[dict[str, Any]] = []
    failure: str | None = None
    for index in range(1, segment_count + 1):
        segment = run_serial_soak(
            port=port,
            baud=baud,
            timeout=timeout,
            duration_sec=segment_duration_sec,
            sample_interval_sec=sample_interval_sec,
            active_public_text=None,
            active_interval_sec=120.0,
            startup_settle_sec=startup_settle_sec,
            require_rx_delta=False,
            min_rx_delta=0,
            min_tx_delta=0,
            clear_crashlog_before_start=False,
            command_retries=0,
            retry_delay_sec=0.0,
            sample_storage=True,
            sd_file_canary=True,
            allow_sd_unavailable=False,
            active_dm_fingerprint=None,
            active_dm_text=None,
            expected_firmware_commit=commit,
        )
        stamp_report(segment, root)
        events.append({"kind": "segment", "index": index, "report": segment})
        if segment.get("ok") is not True:
            failure = f"segment_{index}_failed"
            break
        if index == segment_count:
            continue

        token = _token(token_prefix, index)
        reboot = run_acceptance(
            port,
            baud,
            timeout,
            token,
            include_reboot=True,
            reboot_settle_sec=reboot_settle_sec,
            mount_poll_attempts=mount_poll_attempts,
            mount_poll_interval_sec=mount_poll_interval_sec,
            expected_firmware_commit=commit,
            persistence_poll_attempts=persistence_poll_attempts,
            persistence_poll_interval_sec=persistence_poll_interval_sec,
        )
        stamp_report(reboot, root)
        reboot["dm_rf_tx"] = False
        canary_safety_explicit = _canary_safety_explicit(reboot)
        events.append(
            {
                "kind": "reboot",
                "index": index,
                "token": token,
                "canary_safety_explicit": canary_safety_explicit,
                "report": reboot,
            }
        )
        if reboot.get("ok") is not True:
            failure = f"reboot_{index}_failed"
            break
        if not canary_safety_explicit:
            failure = f"reboot_{index}_canary_safety_missing"
            break

    public_rf_tx = _nested_true(events, "public_rf_tx")
    dm_rf_tx = _nested_true(events, "dm_rf_tx")
    formats_sd = _nested_true(events, "formats_sd") or _nested_true(
        events, "format_performed"
    )
    complete_topology = len(events) == segment_count * 2 - 1 and [
        event.get("kind") for event in events
    ] == ["segment" if index % 2 == 0 else "reboot" for index in range(segment_count * 2 - 1)]
    report = {
        "schema": 1,
        "kind": "d1l_storage_active_soak_source",
        "mode": "hardware",
        "port": port,
        "baud": baud,
        "expected_firmware_commit": commit,
        "segment_count": segment_count,
        "segment_duration_sec": segment_duration_sec,
        "scheduled_segment_duration_sec": segment_count * segment_duration_sec,
        "sample_interval_sec": sample_interval_sec,
        "event_topology": [event.get("kind") for event in events],
        "events": events,
        "started_at": started_at.isoformat().replace("+00:00", "Z"),
        "ended_at": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
        "public_rf_tx": public_rf_tx,
        "dm_rf_tx": dm_rf_tx,
        "formats_sd": formats_sd,
        "failure": failure,
        "ok": bool(
            failure is None
            and complete_topology
            and not public_rf_tx
            and not dm_rf_tx
            and not formats_sd
        ),
    }
    stamp_report(report, root)
    return report


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=".")
    parser.add_argument("--port", default=os.environ.get("D1L_PORT"))
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--expected-firmware-commit", required=True)
    parser.add_argument("--segment-count", type=int, default=DEFAULT_SEGMENT_COUNT)
    parser.add_argument(
        "--segment-duration-sec", type=float, default=DEFAULT_SEGMENT_DURATION_SECONDS
    )
    parser.add_argument(
        "--sample-interval-sec", type=float, default=DEFAULT_SAMPLE_INTERVAL_SECONDS
    )
    parser.add_argument("--startup-settle-sec", type=float, default=1.0)
    parser.add_argument("--reboot-settle-sec", type=float, default=8.0)
    parser.add_argument("--token-prefix", default="wp01soak")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--out")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    root = Path(args.root).resolve()
    try:
        if args.dry_run:
            report = dry_run_report(
                port=args.port,
                baud=args.baud,
                expected_firmware_commit=args.expected_firmware_commit,
                segment_count=args.segment_count,
                segment_duration_sec=args.segment_duration_sec,
                sample_interval_sec=args.sample_interval_sec,
                token_prefix=args.token_prefix,
            )
        else:
            if not args.port:
                raise ValueError("No D1L port supplied. Set D1L_PORT or pass --port.")
            report = run_storage_active_soak(
                root=root,
                port=args.port,
                baud=args.baud,
                timeout=args.timeout,
                expected_firmware_commit=args.expected_firmware_commit,
                segment_count=args.segment_count,
                segment_duration_sec=args.segment_duration_sec,
                sample_interval_sec=args.sample_interval_sec,
                startup_settle_sec=args.startup_settle_sec,
                reboot_settle_sec=args.reboot_settle_sec,
                token_prefix=args.token_prefix,
            )
    except ValueError as exc:
        raise SystemExit(str(exc)) from exc

    out = (
        Path(args.out).resolve()
        if args.out
        else root
        / "artifacts"
        / "storage-active-soak"
        / f"d1l-storage-active-soak-{datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')}.json"
    )
    try:
        out.relative_to(root)
    except ValueError as exc:
        raise SystemExit("output must remain within repository root") from exc
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({"ok": report.get("ok"), "mode": report.get("mode"), "out": str(out)}))
    return 0 if report.get("ok") else 1


if __name__ == "__main__":
    raise SystemExit(main())
