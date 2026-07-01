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
MOUNT_POLL_ATTEMPTS = 10
MOUNT_POLL_INTERVAL_SECONDS = 2.0


def command_plan(scenario: str, *, allow_format_confirm: bool = False) -> list[str]:
    if scenario == "rp2040-unavailable":
        return ["rp2040 ping", "storage status", "health"]

    commands = ["rp2040 ping", "storage status", "storage mount", "storage status"]
    if scenario in FILE_GATE_SCENARIOS:
        commands.append("storage filecanary")
    if scenario in SETUP_SCENARIOS:
        commands.append("storage setup")
    if scenario == "unformatted" and allow_format_confirm:
        commands.extend([
            "storage setup confirm FORMAT-DESKOS-SD",
            "storage status",
            "storage filecanary",
        ])
    commands.append("health")
    return commands


def dry_run_report(scenario: str, *, allow_format_confirm: bool = False) -> dict:
    if scenario == "all":
        return {
            "schema": 1,
            "mode": "dry-run",
            "hardware_required": False,
            "scenarios": {
                name: {
                    "commands": command_plan(
                        name,
                        allow_format_confirm=allow_format_confirm and name == "unformatted",
                    ),
                    "public_rf_tx": False,
                    "formats_sd": allow_format_confirm and name == "unformatted",
                }
                for name in SCENARIOS
            },
            "public_rf_tx": False,
            "formats_sd": allow_format_confirm,
            "ok": True,
        }
    return {
        "schema": 1,
        "mode": "dry-run",
        "hardware_required": False,
        "scenario": scenario,
        "commands": command_plan(scenario, allow_format_confirm=allow_format_confirm),
        "public_rf_tx": False,
        "formats_sd": allow_format_confirm and scenario == "unformatted",
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
    *,
    scenario: str,
    allow_format_confirm: bool,
) -> bool:
    format_confirm_allowed = (
        allow_format_confirm
        and scenario == "unformatted"
        and command == "storage setup confirm FORMAT-DESKOS-SD"
    )
    return (
        ("FORMAT-DESKOS-SD" in command and not format_confirm_allowed)
        or command.startswith("mesh send public")
    )


def setup_confirmation_guarded(setup: dict | None) -> bool:
    if not isinstance(setup, dict):
        return False
    return (
        setup.get("cmd") == "storage setup"
        and setup.get("ok") is True
        and setup.get("will_format") is False
        and setup.get("format_performed") is False
        and setup.get("confirmation_phrase") == "FORMAT-DESKOS-SD"
        and setup.get("fallback") == "nvs"
    )


def boot_prepare_passed(
    scenario: str,
    *,
    storage_after: dict | None,
    storage_setup: dict | None,
    format_result: dict | None,
    filecanary: dict | None,
    health: dict | None,
    public_rf_tx: bool,
    formats_sd: bool,
    allow_format_confirm: bool,
) -> tuple[bool, str]:
    if public_rf_tx:
        return False, "public_rf_tx_seen"
    if formats_sd and not allow_format_confirm:
        return False, "format_seen"
    if not isinstance(health, dict) or health.get("ok") is not True:
        return False, "health_failed"

    state = sd_state(storage_after)
    setup_action = storage_after.get("setup_action") if isinstance(storage_after, dict) else None
    format_action = storage_after.get("format_action") if isinstance(storage_after, dict) else None

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
            and format_action == "not_needed"
            and filecanary_passed(filecanary)
        ):
            return True, "ready_sd_file_gate"
        return False, "ready_sd_file_gate_missing"

    if scenario == "unformatted":
        if allow_format_confirm:
            if (
                isinstance(format_result, dict)
                and format_result.get("ok") is True
                and storage_file_gate_ready(storage_after)
                and filecanary_passed(filecanary)
            ):
                return True, "format_confirmed_ready"
            return False, "format_confirmed_not_ready"
        if state == "ready" and storage_file_gate_ready(storage_after):
            return True, "prepared_ready_without_runner_format"
        if setup_confirmation_guarded(storage_setup) and (
            setup_action in {"format_confirmation_required", "manual_format_required"}
            or format_action in {"confirm_required", "not_available"}
            or state in {"setup_required", "unformatted"}
        ):
            return True, "setup_requires_confirmation"
        return False, "unformatted_policy_not_reported"

    if scenario == "existing-data":
        if state == "ready" and sd_status(storage_after).get("data_root_ready") is True:
            return True, "existing_data_preserved_ready"
        if setup_confirmation_guarded(storage_setup) and (
            setup_action in {"format_confirmation_required", "manual_format_required"}
            or format_action in {"confirm_required", "not_available"}
            or state in {"setup_required", "error", "bridge_reported"}
        ):
            return True, "existing_data_not_wiped"
        return False, "existing_data_policy_not_reported"

    return False, "unknown_scenario"


def send_with_timeout(ser, command: str, timeout: float) -> dict:
    command_timeout = max(timeout, 15.0) if command in {"storage mount", "storage filecanary"} else timeout
    return send_console_command(ser, command, command_timeout)


def run_acceptance(
    *,
    port: str,
    baud: int,
    timeout: float,
    scenario: str,
    allow_format_confirm: bool = False,
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
            scenario=scenario,
            allow_format_confirm=allow_format_confirm,
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
        format_result = None
        filecanary = None

        if scenario != "rp2040-unavailable":
            run_command(ser, "storage mount")
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
            if scenario == "unformatted" and allow_format_confirm:
                format_result = run_command(ser, "storage setup confirm FORMAT-DESKOS-SD")
                storage_after = run_command(ser, "storage status")
                if storage_file_gate_ready(storage_after):
                    filecanary = run_command(ser, "storage filecanary")

        health = run_command(ser, "health")

    public_rf_tx = any_flag(results, "public_rf_tx")
    formats_sd = any_flag(results, "formats_sd") or any(
        result.get("format_performed") is True for result in results
    )
    scenario_ok, classification = boot_prepare_passed(
        scenario,
        storage_after=storage_after,
        storage_setup=storage_setup,
        format_result=format_result,
        filecanary=filecanary,
        health=health,
        public_rf_tx=public_rf_tx,
        formats_sd=formats_sd,
        allow_format_confirm=allow_format_confirm,
    )
    commands_safe = not any(
        command_is_destructive(
            command,
            scenario=scenario,
            allow_format_confirm=allow_format_confirm,
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
        "format_allowed": allow_format_confirm,
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
        "format_result": format_result,
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
    parser.add_argument(
        "--allow-format-confirm",
        action="store_true",
        help="For --scenario unformatted only, send the guarded FORMAT-DESKOS-SD confirmation to the sacrificial D1L SD card.",
    )
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
            for command in command_plan(
                scenario,
                allow_format_confirm=args.allow_format_confirm and scenario == "unformatted",
            ):
                print(command)
        return 0

    if args.dry_run:
        report = dry_run_report(args.scenario, allow_format_confirm=args.allow_format_confirm)
    else:
        if args.scenario == "all":
            parser.error("--scenario all is dry-run only; hardware scenarios require card swaps")
        if args.allow_format_confirm and args.scenario != "unformatted":
            parser.error("--allow-format-confirm is only valid with --scenario unformatted")
        if not args.port:
            parser.error("No D1L port supplied. Set D1L_PORT or pass --port.")
        report = run_acceptance(
            port=args.port,
            baud=args.baud,
            timeout=args.timeout,
            scenario=args.scenario,
            allow_format_confirm=args.allow_format_confirm,
            mount_poll_attempts=args.mount_poll_attempts,
            mount_poll_interval_sec=args.mount_poll_interval_sec,
        )

    written = write_report(report, Path(args.out) if args.out else None)
    print(json.dumps({"ok": report["ok"], "out": str(written), "mode": report["mode"]}))
    return 0 if report.get("ok") else 1


if __name__ == "__main__":
    raise SystemExit(main())
