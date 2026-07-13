#!/usr/bin/env python3
"""Capture direct RP2040 SD raw diagnostics for MeshCore DeskOS D1L."""

from __future__ import annotations

import argparse
import json
import os
import time
from datetime import datetime, timezone
from pathlib import Path

try:
    from artifact_metadata import stamp_report
    from smoke_d1l import open_d1l_serial, send_console_command
except ImportError:  # pragma: no cover - package import path used by pytest
    from scripts.artifact_metadata import stamp_report
    from scripts.smoke_d1l import open_d1l_serial, send_console_command


DIAG_COMMAND = "storage diag raw"
SAFE_COMMANDS = (DIAG_COMMAND, DIAG_COMMAND, "storage status", "rp2040 ping")
DEFAULT_DIAG_SETTLE_SECONDS = 20.0
DEFAULT_DIAG_POLL_SECONDS = 2.0
DEFAULT_DIAG_DEADLINE_SECONDS = 60.0


def utc_stamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def parse_fields(line: str) -> dict[str, str]:
    fields: dict[str, str] = {}
    for token in line.split():
        if "=" not in token:
            continue
        key, value = token.split("=", 1)
        fields[key] = value
    return fields


def dry_run_report(port: str | None,
                   diag_poll_seconds: float = DEFAULT_DIAG_POLL_SECONDS,
                   diag_deadline_seconds: float = DEFAULT_DIAG_DEADLINE_SECONDS) -> dict:
    return {
        "schema": 1,
        "mode": "dry-run",
        "hardware_required": False,
        "port": port,
        "commands": list(SAFE_COMMANDS),
        "diag_poll_seconds": diag_poll_seconds,
        "diag_deadline_seconds": diag_deadline_seconds,
        "public_rf_tx": False,
        "formats_sd": False,
        "ok": True,
    }


def raw_diag_is_pending(fields: dict[str, str]) -> bool:
    return fields.get("selected_power") == "pending" or fields.get("selected_mode") == "pending"


def raw_diag_is_complete(reply: dict) -> bool:
    raw_line = reply.get("raw_line") if isinstance(reply.get("raw_line"), str) else ""
    return (
        reply.get("ok") is True
        and raw_line.startswith("DESKOS_SD_DIAG")
        and reply.get("response_truncated") is not True
        and not raw_diag_is_pending(parse_fields(raw_line))
    )


def sleep_bounded(deadline: float, seconds: float) -> None:
    remaining = deadline - time.monotonic()
    if remaining > 0 and seconds > 0:
        time.sleep(min(seconds, remaining))


def capture_diag(port: str, baud: int, timeout: float,
                 diag_settle_seconds: float = DEFAULT_DIAG_SETTLE_SECONDS,
                 diag_poll_seconds: float = DEFAULT_DIAG_POLL_SECONDS,
                 diag_deadline_seconds: float = DEFAULT_DIAG_DEADLINE_SECONDS) -> dict:
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("pyserial is required: python -m pip install pyserial") from exc

    replies: dict[str, dict] = {}
    diag_attempts: list[dict] = []
    with open_d1l_serial(serial, port=port, baudrate=baud, timeout=timeout) as ser:
        time.sleep(1.0)
        ser.reset_input_buffer()
        deadline = time.monotonic() + diag_deadline_seconds
        diag_attempts.append(send_console_command(ser, DIAG_COMMAND, timeout))
        if not raw_diag_is_complete(diag_attempts[-1]):
            sleep_bounded(deadline, diag_settle_seconds)
        while not raw_diag_is_complete(diag_attempts[-1]) and time.monotonic() < deadline:
            remaining = deadline - time.monotonic()
            command_timeout = min(timeout, max(0.1, remaining))
            diag_attempts.append(send_console_command(ser, DIAG_COMMAND, command_timeout))
            if not raw_diag_is_complete(diag_attempts[-1]):
                sleep_bounded(deadline, diag_poll_seconds)
        replies[DIAG_COMMAND] = diag_attempts[-1]
        replies["storage status"] = send_console_command(ser, "storage status", timeout)
        replies["rp2040 ping"] = send_console_command(ser, "rp2040 ping", timeout)

    initial_storage_diag = diag_attempts[0]
    storage_diag = replies.get("storage diag raw", {})
    storage_status = replies.get("storage status", {})
    rp2040_ping = replies.get("rp2040 ping", {})
    raw_line = storage_diag.get("raw_line") if isinstance(storage_diag.get("raw_line"), str) else ""
    fields = parse_fields(raw_line)
    ok = raw_diag_is_complete(storage_diag)
    return {
        "schema": 1,
        "mode": "hardware",
        "kind": "rp2040_raw_diag_capture",
        "port": port,
        "baud": baud,
        "commands": [DIAG_COMMAND] * len(diag_attempts) + ["storage status", "rp2040 ping"],
        "public_rf_tx": False,
        "formats_sd": False,
        "diag_deadline_seconds": diag_deadline_seconds,
        "diag_poll_seconds": diag_poll_seconds,
        "diag_attempt_count": len(diag_attempts),
        "diag_completed": ok,
        "diag_deadline_exhausted": not ok and time.monotonic() >= deadline,
        "response_truncated": storage_diag.get("response_truncated") is True,
        "ok": ok,
        "raw_line": raw_line,
        "fields": fields,
        "initial_storage_diag": initial_storage_diag,
        "storage_diag": storage_diag,
        "diag_attempts": diag_attempts,
        "storage_status": storage_status,
        "rp2040_ping": rp2040_ping,
        "results": [*diag_attempts, replies["storage status"], replies["rp2040 ping"]],
    }


def write_report(report: dict, out_path: Path | None) -> Path:
    root = Path(__file__).resolve().parents[1]
    if out_path is None:
        out_path = root / "artifacts" / "rp2040-preflight" / f"rp2040_raw_diag_{utc_stamp()}.json"
    elif not out_path.is_absolute():
        out_path = root / out_path
    stamp_report(report, root)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(report, indent=2) + "\n", encoding="ascii")
    return out_path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default=os.environ.get("D1L_RP2040_PORT") or os.environ.get("D1L_RP2040_UF2_PORT"))
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--diag-settle-sec", type=float, default=DEFAULT_DIAG_SETTLE_SECONDS)
    parser.add_argument("--diag-poll-sec", type=float, default=DEFAULT_DIAG_POLL_SECONDS)
    parser.add_argument("--diag-deadline-sec", type=float, default=DEFAULT_DIAG_DEADLINE_SECONDS)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--out")
    args = parser.parse_args()

    if args.dry_run:
        report = dry_run_report(args.port, args.diag_poll_sec, args.diag_deadline_sec)
    else:
        if not args.port:
            parser.error("No RP2040 port supplied. Set D1L_RP2040_PORT or pass --port.")
        if args.timeout <= 0 or args.diag_settle_sec < 0 or args.diag_poll_sec <= 0 or args.diag_deadline_sec <= 0:
            parser.error("timeouts/deadline must be positive and --diag-settle-sec must be non-negative")
        report = capture_diag(
            args.port,
            args.baud,
            args.timeout,
            args.diag_settle_sec,
            args.diag_poll_sec,
            args.diag_deadline_sec,
        )

    written = write_report(report, Path(args.out) if args.out else None)
    print(json.dumps({"ok": report["ok"], "out": str(written), "mode": report["mode"]}))
    return 0 if report.get("ok") else 1


if __name__ == "__main__":
    raise SystemExit(main())
