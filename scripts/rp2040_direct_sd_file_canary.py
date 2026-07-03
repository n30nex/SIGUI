#!/usr/bin/env python3
"""Direct RP2040 USB SD file-protocol canary for D1L bring-up."""

from __future__ import annotations

import argparse
import base64
import binascii
import json
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


FILE_PREFIX = "DESKOS_SD_FILE"
PING_COMMAND = "DESKOS_SD_PING"
STATUS_COMMAND = "DESKOS_SD_STATUS"
PAYLOAD = b"DeskOS SD file canary direct v1"
TMP_PATH = "canary/filecanary.tmp"
FINAL_PATH = "canary/filecanary.bin"
ROOT_PROBE_PATH = "rootprobe.tmp"


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def utc_stamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def b64url(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).decode("ascii").rstrip("=")


def encode_path(path: str) -> str:
    return b64url(path.encode("ascii"))


def crc32_hex(data: bytes) -> str:
    return f"{binascii.crc32(data) & 0xFFFFFFFF:08X}"


def parse_tokens(line: str) -> dict[str, str]:
    parts = line.split()
    parsed = {"prefix": parts[0] if parts else ""}
    for token in parts[1:]:
        if "=" in token:
            key, value = token.split("=", 1)
            parsed[key] = value
    return parsed


def file_command(request_id: int, op: str, **tokens: object) -> str:
    merged = {"v": 1, "id": request_id, "op": op, **tokens}
    return " ".join([FILE_PREFIX, *(f"{key}={value}" for key, value in merged.items())])


def canary_commands(payload: bytes = PAYLOAD) -> list[tuple[str, str, str, int | None]]:
    return [
        ("ping", PING_COMMAND, PING_COMMAND, None),
        ("status", STATUS_COMMAND, STATUS_COMMAND, None),
        ("delete_root_missing", file_command(1, "delete", path=encode_path(ROOT_PROBE_PATH)), FILE_PREFIX, 1),
        ("delete_tmp", file_command(2, "delete", path=encode_path(TMP_PATH)), FILE_PREFIX, 2),
        ("delete_final", file_command(3, "delete", path=encode_path(FINAL_PATH)), FILE_PREFIX, 3),
        (
            "write_tmp",
            file_command(
                4,
                "write",
                path=encode_path(TMP_PATH),
                off=0,
                len=len(payload),
                trunc=1,
                data=b64url(payload),
                crc=crc32_hex(payload),
            ),
            FILE_PREFIX,
            4,
        ),
        ("read_tmp", file_command(5, "read", path=encode_path(TMP_PATH), off=0, len=len(payload)), FILE_PREFIX, 5),
        (
            "rename_final",
            file_command(6, "rename", path=encode_path(TMP_PATH), to=encode_path(FINAL_PATH), replace=1),
            FILE_PREFIX,
            6,
        ),
        ("stat_final", file_command(7, "stat", path=encode_path(FINAL_PATH)), FILE_PREFIX, 7),
        ("read_final", file_command(8, "read", path=encode_path(FINAL_PATH), off=0, len=len(payload)), FILE_PREFIX, 8),
        ("delete_final_after", file_command(9, "delete", path=encode_path(FINAL_PATH)), FILE_PREFIX, 9),
        ("stat_deleted", file_command(10, "stat", path=encode_path(FINAL_PATH)), FILE_PREFIX, 10),
        ("final_status", STATUS_COMMAND, STATUS_COMMAND, None),
    ]


def dry_run_report(port: str | None = None, baud: int = 115200) -> dict[str, Any]:
    return {
        "schema": 1,
        "kind": "rp2040_direct_sd_file_canary",
        "mode": "dry-run",
        "hardware_required": False,
        "explicit_port_required": True,
        "port": port,
        "baud": baud,
        "public_rf_tx": False,
        "formats_sd": False,
        "will_format": False,
        "manual_user_required": False,
        "commands": [command for _, command, _, _ in canary_commands()],
        "ok": True,
    }


def drain_serial(ser: Any, seconds: float) -> list[str]:
    lines: list[str] = []
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        text = raw.decode("utf-8", errors="replace").strip()
        if text:
            lines.append(text)
            deadline = time.monotonic() + 0.2
    return lines


def send_request(ser: Any, command: str, prefix: str, request_id: int | None, timeout: float) -> dict[str, Any]:
    step: dict[str, Any] = {
        "request": command,
        "expect_prefix": prefix,
        "request_id": request_id,
        "started_at": utc_now(),
        "pre_drain": drain_serial(ser, 0.25),
        "lines": [],
        "reply": None,
        "tokens": None,
        "ok": False,
    }
    ser.write((command + "\n").encode("ascii"))
    ser.flush()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        text = raw.decode("utf-8", errors="replace").strip()
        if not text:
            continue
        step["lines"].append(text)
        if not text.startswith(prefix):
            continue
        tokens = parse_tokens(text)
        if request_id is not None and tokens.get("id") != str(request_id):
            continue
        step["reply"] = text
        step["tokens"] = tokens
        step["ok"] = True
        break
    if not step["ok"]:
        step["error"] = "timeout"
    step["ended_at"] = utc_now()
    return step


def step_accepted(name: str, step: dict[str, Any], payload: bytes = PAYLOAD) -> bool:
    tokens = step.get("tokens") if isinstance(step.get("tokens"), dict) else {}
    if name == "ping":
        return step.get("ok") is True
    if name in {"status", "final_status"}:
        return (
            step.get("ok") is True
            and tokens.get("state") == "ready"
            and tokens.get("file_ops") == "1"
        )
    if name in {"delete_root_missing", "delete_tmp", "delete_final"}:
        return step.get("ok") is True and (tokens.get("ok") == "1" or tokens.get("err") == "not_found")
    if name == "stat_deleted":
        return step.get("ok") is True and tokens.get("ok") == "1" and tokens.get("exists") == "0"
    if name.startswith("read"):
        return (
            step.get("ok") is True
            and tokens.get("ok") == "1"
            and tokens.get("data") == b64url(payload)
            and tokens.get("crc") == crc32_hex(payload)
        )
    if name == "stat_final":
        return (
            step.get("ok") is True
            and tokens.get("ok") == "1"
            and tokens.get("exists") == "1"
            and tokens.get("size") == str(len(payload))
        )
    return step.get("ok") is True and tokens.get("ok") == "1"


def run_canary(
    port: str,
    baud: int,
    timeout: float,
    *,
    commit: str | None = None,
    github_run_id: int | None = None,
) -> dict[str, Any]:
    import serial

    report: dict[str, Any] = {
        "schema": 1,
        "kind": "rp2040_direct_sd_file_canary",
        "mode": "hardware",
        "port": port,
        "baud": baud,
        "commit": commit,
        "github_actions_run": github_run_id,
        "public_rf_tx": False,
        "formats_sd": False,
        "will_format": False,
        "manual_user_required": False,
        "started_at": utc_now(),
        "steps": [],
    }
    try:
        with serial.Serial(port, baud, timeout=0.1, write_timeout=1.0) as ser:
            ser.dtr = True
            ser.rts = True
            time.sleep(1.5)
            ser.reset_input_buffer()
            report["initial_drain"] = drain_serial(ser, 0.8)
            for name, command, prefix, request_id in canary_commands():
                step = send_request(ser, command, prefix, request_id, timeout)
                step["name"] = name
                step["accepted"] = step_accepted(name, step)
                report["steps"].append(step)
                if not step["accepted"]:
                    report["first_failed_step"] = name
                    break
    except Exception as exc:  # pragma: no cover - serial dependent
        report["error"] = str(exc)
    report["ended_at"] = utc_now()
    file_steps = [step for step in report["steps"] if step.get("expect_prefix") == FILE_PREFIX]
    report["canary_passed"] = bool(file_steps) and len(file_steps) == 10 and all(
        step.get("accepted") is True for step in file_steps
    )
    report["filecanary_passed"] = report["canary_passed"]
    report["storage_file_gate_ready_before"] = any(
        step.get("name") == "status" and step.get("accepted") is True
        for step in report["steps"]
    )
    report["storage_file_gate_ready_after"] = any(
        step.get("name") == "final_status" and step.get("accepted") is True
        for step in report["steps"]
    )
    report["ok"] = (
        report.get("first_failed_step") is None
        and report["canary_passed"] is True
        and report["storage_file_gate_ready_before"] is True
        and report["storage_file_gate_ready_after"] is True
    )
    return report


def write_report(path: Path, report: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2), encoding="utf-8")


def default_out() -> Path:
    return Path("artifacts") / "hardware" / "rp2040" / f"rp2040-direct-sd-file-canary-{utc_stamp()}.json"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", help="RP2040 USB CDC serial port")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=12.0)
    parser.add_argument("--commit")
    parser.add_argument("--github-run-id", type=int)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--out", type=Path, default=None)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.dry_run:
        report = dry_run_report(args.port, args.baud)
    else:
        if not args.port:
            raise SystemExit("--port is required unless --dry-run is used")
        report = run_canary(
            args.port,
            args.baud,
            args.timeout,
            commit=args.commit,
            github_run_id=args.github_run_id,
        )
    if args.out:
        write_report(args.out, report)
    print(json.dumps(report, indent=2))
    return 0 if report.get("ok") is True else 1


if __name__ == "__main__":
    raise SystemExit(main())
