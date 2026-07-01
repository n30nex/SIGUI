#!/usr/bin/env python3
"""Non-destructive RP2040 SD bridge bring-up preflight for MeshCore DeskOS D1L."""

from __future__ import annotations

import argparse
import json
import os
import time
from datetime import datetime, timezone
from pathlib import Path

try:
    from flash_rp2040_sd_bridge_uf2 import FlashGuardError, candidate_volumes, verify_artifact
    from smoke_d1l import send_console_command
except ImportError:  # pragma: no cover - package import path used by pytest
    from scripts.flash_rp2040_sd_bridge_uf2 import (
        FlashGuardError,
        candidate_volumes,
        verify_artifact,
    )
    from scripts.smoke_d1l import send_console_command


PREFLIGHT_COMMANDS = [
    "rp2040 status",
    "storage status",
    "storage diag",
    "health",
]


def dry_run_report(artifact_dir: str | None = None) -> dict:
    return {
        "schema": 1,
        "mode": "dry-run",
        "hardware_required": False,
        "artifact_dir": artifact_dir,
        "commands": PREFLIGHT_COMMANDS,
        "public_rf_tx": False,
        "formats_sd": False,
        "copies_uf2": False,
        "ok": True,
    }


def verify_optional_artifact(artifact_dir: str | None, expected_sha256: str | None) -> dict:
    if not artifact_dir:
        return {
            "checked": False,
            "ok": None,
            "reason": "artifact_dir_not_supplied",
        }
    try:
        artifact = verify_artifact(Path(artifact_dir), expected_sha256)
    except FlashGuardError as exc:
        return {
            "checked": True,
            "ok": False,
            "error": str(exc),
        }
    return {
        "checked": True,
        "ok": True,
        **artifact,
    }


def _number_at_least(value, minimum: int) -> bool:
    try:
        return int(value) >= minimum
    except (TypeError, ValueError):
        return False


def storage_file_gate_ready(storage_status: dict) -> bool:
    sd = storage_status.get("sd")
    if not isinstance(sd, dict):
        return False
    return (
        sd.get("state") == "ready"
        and sd.get("present") is True
        and sd.get("mounted") is True
        and sd.get("data_root_ready") is True
        and sd.get("rp2040_protocol_supported") is True
        and sd.get("file_ops") is True
        and sd.get("atomic_rename") is True
        and _number_at_least(sd.get("file_line_max"), 512)
        and _number_at_least(sd.get("file_chunk_max"), 192)
        and _number_at_least(sd.get("path_max"), 96)
    )


def classify_preflight(
    rp2040_status: dict,
    storage_status: dict,
    uf2_candidates: list[dict],
    artifact: dict,
) -> dict:
    sd = storage_status.get("sd") if isinstance(storage_status.get("sd"), dict) else {}
    artifact_ok = artifact.get("ok") is True or artifact.get("ok") is None
    bridge_uart_ready = bool(rp2040_status.get("uart_ready") or sd.get("rp2040_bridge_ready"))
    protocol_supported = sd.get("rp2040_protocol_supported") is True
    file_gate_ready = storage_file_gate_ready(storage_status)
    uf2_volume_available = len(uf2_candidates) == 1

    if file_gate_ready:
        state = "sd_bridge_ready"
        next_action = "run_sd_file_and_export_acceptance"
    elif bridge_uart_ready and not protocol_supported:
        state = "rp2040_protocol_pending"
        next_action = (
            "dry_run_then_copy_rp2040_uf2" if uf2_volume_available
            else "put_rp2040_in_uf2_bootloader"
        )
    elif not bridge_uart_ready:
        state = "rp2040_uart_unavailable"
        next_action = "check_d1l_power_cable_and_rp2040_uart"
    elif protocol_supported and sd.get("present") is not True:
        state = "sd_card_not_present"
        next_action = "insert_empty_or_sacrificial_sd_card"
    else:
        state = "sd_not_ready"
        next_action = "inspect_storage_status_and_run_sd_file_canary"

    return {
        "state": state,
        "next_action": next_action,
        "artifact_ready": artifact_ok,
        "uf2_volume_available": uf2_volume_available,
        "rp2040_uart_ready": bridge_uart_ready,
        "rp2040_protocol_supported": protocol_supported,
        "storage_file_gate_ready": file_gate_ready,
        "sd_state": sd.get("state"),
        "sd_last_error": sd.get("last_error"),
    }


def run_preflight(
    *,
    port: str,
    baud: int,
    timeout: float,
    artifact_dir: str | None,
    expected_sha256: str | None,
) -> dict:
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("pyserial is required for hardware preflight: python -m pip install pyserial") from exc

    artifact = verify_optional_artifact(artifact_dir, expected_sha256)
    uf2_candidates = candidate_volumes()
    results: list[dict] = []
    with serial.Serial(port=port, baudrate=baud, timeout=timeout) as ser:
        time.sleep(1.0)
        ser.reset_input_buffer()
        for command in PREFLIGHT_COMMANDS:
            results.append(send_console_command(ser, command, timeout))

    rp2040_status = results[0] if len(results) > 0 else {}
    storage_status = results[1] if len(results) > 1 else {}
    storage_diag = results[2] if len(results) > 2 else {}
    health = results[3] if len(results) > 3 else {}
    classification = classify_preflight(rp2040_status, storage_status, uf2_candidates, artifact)
    storage_ok = storage_status.get("ok") is True
    health_ok = health.get("ok") is True
    storage_diag_ok = storage_diag.get("ok") is True
    rp2040_status_ok = rp2040_status.get("ok") is True
    rp2040_status_optional_ok = rp2040_status_ok or classification["rp2040_uart_ready"]
    command_ok = rp2040_status_optional_ok and storage_ok and health_ok
    artifact_ok = artifact.get("ok") is not False
    return {
        "schema": 1,
        "mode": "hardware-preflight",
        "port": port,
        "baud": baud,
        "commands": PREFLIGHT_COMMANDS,
        "public_rf_tx": False,
        "formats_sd": False,
        "copies_uf2": False,
        "ok": command_ok and artifact_ok,
        "serial_commands_ok": command_ok,
        "rp2040_status_ok": rp2040_status_ok,
        "rp2040_status_optional_ok": rp2040_status_optional_ok,
        "storage_diag_ok": storage_diag_ok,
        "ready_for_sd_acceptance": classification["storage_file_gate_ready"],
        "classification": classification,
        "artifact": artifact,
        "candidate_volumes": uf2_candidates,
        "rp2040_status": rp2040_status,
        "storage_status": storage_status,
        "storage_diag": storage_diag,
        "health": health,
        "results": results,
    }


def write_report(report: dict, out_path: Path | None) -> Path:
    root = Path(__file__).resolve().parents[1]
    if out_path is None:
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        out_path = root / "artifacts" / "rp2040-preflight" / f"d1l-rp2040-sd-bridge-preflight-{stamp}.json"
    elif not out_path.is_absolute():
        out_path = root / out_path
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(report, indent=2) + "\n", encoding="ascii")
    return out_path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default=os.environ.get("D1L_PORT"))
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--artifact-dir")
    parser.add_argument("--expected-sha256")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--list-commands", action="store_true")
    parser.add_argument("--out")
    args = parser.parse_args()

    if args.list_commands:
        for command in PREFLIGHT_COMMANDS:
            print(command)
        return 0

    if args.dry_run:
        report = dry_run_report(args.artifact_dir)
    else:
        if not args.port:
            parser.error("No D1L port supplied. Set D1L_PORT or pass --port.")
        report = run_preflight(
            port=args.port,
            baud=args.baud,
            timeout=args.timeout,
            artifact_dir=args.artifact_dir,
            expected_sha256=args.expected_sha256,
        )

    written = write_report(report, Path(args.out) if args.out else None)
    print(json.dumps({"ok": report["ok"], "out": str(written), "mode": report["mode"]}))
    return 0 if report.get("ok") else 1


if __name__ == "__main__":
    raise SystemExit(main())
