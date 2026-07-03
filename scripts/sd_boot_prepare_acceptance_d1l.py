#!/usr/bin/env python3
"""Safe SD boot/use acceptance runner for MeshCore DeskOS D1L."""

from __future__ import annotations

import argparse
import json
import os
import time
from datetime import datetime, timezone
from pathlib import Path

try:
    from smoke_d1l import send_console_command
except ImportError:  # pragma: no cover - package import path used by pytest
    from scripts.smoke_d1l import send_console_command


SCENARIOS = (
    "no-card",
    "correct-structure",
    "missing-structure",
    "unformatted",
    "existing-data",
    "rp2040-unavailable",
)
FILE_GATE_SCENARIOS = {"correct-structure", "missing-structure"}
SETUP_SCENARIOS = {"no-card", "unformatted", "existing-data"}
FIRMWARE_MOUNT_ERROR_ACTION = "inspect_rp2040_sd_mount_error_firmware_path"
FALLBACK_SETUP_ACTIONS = {
    "prepare_fat32_on_computer",
    "retry_storage_mount",
    "use_nvs_fallback",
}
MOUNT_POLL_ATTEMPTS = 10
MOUNT_POLL_INTERVAL_SECONDS = 2.0


def command_plan(scenario: str) -> list[str]:
    if scenario == "rp2040-unavailable":
        return ["rp2040 ping", "storage status", "health"]

    commands = ["rp2040 ping", "storage status", "storage remount", "storage status"]
    if scenario in FILE_GATE_SCENARIOS:
        commands.append("storage filecanary")
    if scenario in SETUP_SCENARIOS:
        commands.append("storage setup")
    commands.append("health")
    return commands


def dry_run_report(scenario: str) -> dict:
    if scenario == "all":
        return {
            "schema": 1,
            "mode": "dry-run",
            "hardware_required": False,
            "scenarios": {
                name: {
                    "commands": command_plan(name),
                    "public_rf_tx": False,
                    "formats_sd": False,
                }
                for name in SCENARIOS
            },
            "public_rf_tx": False,
            "formats_sd": False,
            "ok": True,
        }
    return {
        "schema": 1,
        "mode": "dry-run",
        "hardware_required": False,
        "scenario": scenario,
        "commands": command_plan(scenario),
        "public_rf_tx": False,
        "formats_sd": False,
        "ok": True,
    }


def sd_status(result: dict | None) -> dict:
    if not isinstance(result, dict):
        return {}
    sd = result.get("sd")
    return sd if isinstance(sd, dict) else {}


def sd_state(result: dict | None) -> str | None:
    state = sd_status(result).get("state")
    return state if isinstance(state, str) else None


def _number_at_least(value, minimum: int) -> bool:
    try:
        return int(value) >= minimum
    except (TypeError, ValueError):
        return False


def storage_file_gate_ready(storage_status: dict | None) -> bool:
    sd = sd_status(storage_status)
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


def retained_store_gate_ready(storage_status: dict | None) -> bool:
    if not isinstance(storage_status, dict):
        return False
    stores = storage_status.get("stores")
    store_backends = stores if isinstance(stores, dict) else {}
    return (
        storage_status.get("data_enabled") is True
        and storage_status.get("data_backend") == "mixed"
        and storage_status.get("message_store_backend") == "sd"
        and storage_status.get("dm_store_backend") == "sd"
        and storage_status.get("route_store_backend") == "sd"
        and storage_status.get("packet_log_backend") == "sd"
        and store_backends.get("messages") == "sd"
        and store_backends.get("dm") == "sd"
        and store_backends.get("routes") == "sd"
        and store_backends.get("packets") == "sd"
    )


def filecanary_passed(result: dict | None) -> bool:
    if not isinstance(result, dict):
        return False
    return (
        result.get("ok") is True
        and result.get("rename_replace") is True
        and result.get("read_final") is True
    )


def any_flag(results: list[dict], flag: str) -> bool:
    return any(result.get(flag) is True for result in results)


def command_is_destructive(
    command: str,
) -> bool:
    return command.startswith("mesh send public")


def setup_policy_reported(setup: dict | None) -> bool:
    if not isinstance(setup, dict):
        return False
    return (
        setup.get("cmd") == "storage setup"
        and setup.get("ok") is True
        and setup.get("will_format") is False
        and setup.get("format_performed") is False
        and setup.get("policy") == "no_device_format"
        and setup.get("fallback") == "nvs"
    )


def sd_mount_error_present(report: dict | None) -> bool:
    sd = sd_status(report)
    return any(int(sd.get(key) or 0) != 0 for key in ("mount_error", "mount_data"))


def boot_prepare_passed(
    scenario: str,
    *,
    storage_after: dict | None,
    storage_setup: dict | None,
    filecanary: dict | None,
    health: dict | None,
    public_rf_tx: bool,
    formats_sd: bool,
) -> tuple[bool, str]:
    if public_rf_tx:
        return False, "public_rf_tx_seen"
    if formats_sd:
        return False, "format_seen"
    if not isinstance(health, dict) or health.get("ok") is not True:
        return False, "health_failed"

    state = sd_state(storage_after)
    setup_action = storage_after.get("setup_action") if isinstance(storage_after, dict) else None

    if scenario == "rp2040-unavailable":
        if state in {"rp2040_unavailable", "bridge_unavailable", "protocol_pending"}:
            return True, "bridge_unavailable_fallback"
        return False, "bridge_unavailable_not_reported"

    if not isinstance(storage_after, dict) or storage_after.get("ok") is not True:
        return False, "storage_status_failed"

    if scenario == "no-card":
        sd = sd_status(storage_after)
        if state == "no_card" or sd.get("present") is False or setup_action == "insert_card":
            return True, "no_card_fallback"
        return False, "no_card_not_reported"

    if scenario in FILE_GATE_SCENARIOS:
        if (
            storage_file_gate_ready(storage_after)
            and retained_store_gate_ready(storage_after)
            and filecanary_passed(filecanary)
        ):
            return True, "ready_sd_file_gate"
        return False, "ready_sd_file_gate_missing"

    if scenario == "unformatted":
        if state == "ready" and storage_file_gate_ready(storage_after):
            return True, "prepared_ready_without_runner_format"
        if setup_policy_reported(storage_setup) and (
            setup_action == FIRMWARE_MOUNT_ERROR_ACTION
            or sd_mount_error_present(storage_after)
        ):
            return True, "firmware_mount_error_fallback"
        if setup_policy_reported(storage_setup) and (
            setup_action in FALLBACK_SETUP_ACTIONS
            or state in {"not_fat32_or_unmountable", "deskos_manifest_invalid", "error"}
        ):
            return True, "computer_fat32_required"
        return False, "unformatted_policy_not_reported"

    if scenario == "existing-data":
        if state == "ready" and sd_status(storage_after).get("data_root_ready") is True:
            return True, "existing_data_preserved_ready"
        if setup_policy_reported(storage_setup) and (
            setup_action == FIRMWARE_MOUNT_ERROR_ACTION
            or sd_mount_error_present(storage_after)
        ):
            return True, "existing_data_firmware_mount_fallback"
        if setup_policy_reported(storage_setup) and (
            setup_action in FALLBACK_SETUP_ACTIONS
            or state in {"not_fat32_or_unmountable", "deskos_manifest_invalid", "error", "bridge_reported"}
        ):
            return True, "existing_data_not_wiped"
        return False, "existing_data_policy_not_reported"

    return False, "unknown_scenario"


def send_with_timeout(ser, command: str, timeout: float) -> dict:
    if command in {"storage mount", "storage remount", "storage filecanary"}:
        command_timeout = max(timeout, 15.0)
    else:
        command_timeout = timeout
    return send_console_command(ser, command, command_timeout)


def run_acceptance(
    *,
    port: str,
    baud: int,
    timeout: float,
    scenario: str,
    mount_poll_attempts: int = MOUNT_POLL_ATTEMPTS,
    mount_poll_interval_sec: float = MOUNT_POLL_INTERVAL_SECONDS,
) -> dict:
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("pyserial is required: python -m pip install pyserial") from exc

    commands: list[str] = []
    results: list[dict] = []
    by_command: dict[str, dict] = {}

    def run_command(ser, command: str) -> dict:
        if command_is_destructive(
            command,
        ):
            raise RuntimeError(f"refusing destructive command in SD boot acceptance: {command}")
        result = send_with_timeout(ser, command, timeout)
        commands.append(command)
        results.append(result)
        by_command[command] = result
        return result

    with serial.Serial(port=port, baudrate=baud, timeout=timeout) as ser:
        ser.reset_input_buffer()
        run_command(ser, "rp2040 ping")
        initial_storage = run_command(ser, "storage status")
        storage_after = initial_storage
        storage_setup = None
        filecanary = None

        if scenario != "rp2040-unavailable":
            run_command(ser, "storage remount")
            storage_after = run_command(ser, "storage status")
            for _attempt in range(mount_poll_attempts):
                if sd_state(storage_after) != "mount_pending":
                    break
                time.sleep(mount_poll_interval_sec)
                storage_after = run_command(ser, "storage status")

            if scenario in FILE_GATE_SCENARIOS:
                filecanary = run_command(ser, "storage filecanary")
            if scenario in SETUP_SCENARIOS:
                storage_setup = run_command(ser, "storage setup")

        health = run_command(ser, "health")

    public_rf_tx = any_flag(results, "public_rf_tx")
    format_command_sent = False
    format_confirmed = False
    formats_sd = any_flag(results, "formats_sd") or any(
        result.get("format_performed") is True for result in results
    )
    scenario_ok, classification = boot_prepare_passed(
        scenario,
        storage_after=storage_after,
        storage_setup=storage_setup,
        filecanary=filecanary,
        health=health,
        public_rf_tx=public_rf_tx,
        formats_sd=formats_sd,
    )
    commands_safe = not any(
        command_is_destructive(
            command,
        )
        for command in commands
    )

    return {
        "schema": 1,
        "mode": "hardware",
        "port": port,
        "baud": baud,
        "scenario": scenario,
        "commands": commands,
        "public_rf_tx": public_rf_tx,
        "formats_sd": formats_sd,
        "format_command_sent": format_command_sent,
        "format_confirmed": format_confirmed,
        "format_allowed": False,
        "commands_safe": commands_safe,
        "scenario_passed": scenario_ok,
        "classification": classification,
        "storage_file_gate_ready": storage_file_gate_ready(storage_after),
        "retained_store_gate_ready": retained_store_gate_ready(storage_after),
        "filecanary_passed": filecanary_passed(filecanary) if filecanary is not None else None,
        "ok": scenario_ok and commands_safe,
        "initial_storage": initial_storage,
        "storage_after": storage_after,
        "storage_setup": storage_setup,
        "format_result": None,
        "filecanary": filecanary,
        "health": health,
        "results": results,
    }


def write_report(report: dict, out_path: Path | None) -> Path:
    root = Path(__file__).resolve().parents[1]
    if out_path is None:
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        scenario = str(report.get("scenario", "all")).replace("-", "_")
        out_path = root / "artifacts" / "sd-boot-prepare" / f"d1l-sd-boot-prepare-{scenario}-{stamp}.json"
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
    parser.add_argument("--scenario", choices=(*SCENARIOS, "all"), default="correct-structure")
    parser.add_argument("--mount-poll-attempts", type=int, default=MOUNT_POLL_ATTEMPTS)
    parser.add_argument("--mount-poll-interval-sec", type=float, default=MOUNT_POLL_INTERVAL_SECONDS)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--list-commands", action="store_true")
    parser.add_argument("--out")
    args = parser.parse_args()

    if args.list_commands:
        scenario_names = SCENARIOS if args.scenario == "all" else (args.scenario,)
        for scenario in scenario_names:
            if args.scenario == "all":
                print(f"# {scenario}")
            for command in command_plan(scenario):
                print(command)
        return 0

    if args.dry_run:
        report = dry_run_report(args.scenario)
    else:
        if args.scenario == "all":
            parser.error("--scenario all is dry-run only; hardware scenarios require card swaps")
        if not args.port:
            parser.error("No D1L port supplied. Set D1L_PORT or pass --port.")
        report = run_acceptance(
            port=args.port,
            baud=args.baud,
            timeout=args.timeout,
            scenario=args.scenario,
            mount_poll_attempts=args.mount_poll_attempts,
            mount_poll_interval_sec=args.mount_poll_interval_sec,
        )

    written = write_report(report, Path(args.out) if args.out else None)
    print(json.dumps({"ok": report["ok"], "out": str(written), "mode": report["mode"]}))
    return 0 if report.get("ok") else 1


if __name__ == "__main__":
    raise SystemExit(main())
