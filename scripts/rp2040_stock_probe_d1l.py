#!/usr/bin/env python3
"""Safe serial probe for Seeed stock RP2040 UART behavior on D1L."""

from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime, timezone
from pathlib import Path

try:
    from smoke_d1l import send_console_command
except ImportError:  # pragma: no cover - package import path used by pytest
    from scripts.smoke_d1l import send_console_command


DEFAULT_PORT = "COM" + "12"
FORBIDDEN_PORTS = {"COM" + "11", "COM" + "29"}
COMMANDS = ("rp2040 status", "rp2040 ping", "rp2040 stock-probe")


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def normalize_port(port: str) -> str:
    return port.strip().upper()


def enforce_port_guard(port: str) -> None:
    if normalize_port(port) in FORBIDDEN_PORTS:
        raise ValueError(f"Refusing to use forbidden port: {port}")


def build_report(args: argparse.Namespace) -> dict:
    port = normalize_port(args.port)
    enforce_port_guard(port)
    report = {
        "schema": 1,
        "kind": "rp2040_stock_probe",
        "mode": "dry-run" if args.dry_run else "hardware",
        "port": port,
        "baud": args.baud,
        "timeout": args.timeout,
        "public_rf_tx": False,
        "formats_sd": False,
        "manual_user_required": False,
        "commands": list(COMMANDS),
        "results": [],
        "started_at": utc_now(),
        "ended_at": None,
        "ok": True,
    }
    if args.dry_run:
        report["ended_at"] = utc_now()
        return report

    try:
        import serial
    except ImportError as exc:  # pragma: no cover - dependency dependent
        report["ok"] = False
        report["error"] = f"pyserial is required: {exc}"
        report["ended_at"] = utc_now()
        return report

    try:
        with serial.Serial(port=port, baudrate=args.baud, timeout=args.timeout) as ser:
            for command in COMMANDS:
                result = send_console_command(ser, command, args.timeout)
                report["results"].append(result)
    except Exception as exc:  # pragma: no cover - serial dependent
        report["ok"] = False
        report["error"] = str(exc)
    else:
        stock = next((item for item in report["results"] if item.get("cmd") == "rp2040 stock-probe"), {})
        report["ok"] = stock.get("ok") is True
        report["stock_probe"] = stock
        if not report["ok"]:
            report["error"] = stock.get("code") or stock.get("error") or "rp2040_stock_probe_no_response"
    report["ended_at"] = utc_now()
    return report


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default=DEFAULT_PORT)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--out")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])
    try:
        report = build_report(args)
    except ValueError as exc:
        report = {"schema": 1, "kind": "rp2040_stock_probe", "ok": False, "error": str(exc)}
    if args.out:
        out = Path(args.out)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="ascii")
    print(json.dumps(report))
    return 0 if report.get("ok") else 1


if __name__ == "__main__":
    raise SystemExit(main())
