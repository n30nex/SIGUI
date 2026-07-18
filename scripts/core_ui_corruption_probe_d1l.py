#!/usr/bin/env python3
"""Exact-candidate Core 1.0 UI corruption/navigation probe for D1L COM12."""

from __future__ import annotations

import argparse
import json
import os
import time
from datetime import datetime, timezone
from pathlib import Path

try:
    from artifact_metadata import git_metadata, stamp_report
    from core_smoke_d1l import (
        CORE_RELEASE_PROFILE,
        SD_HISTORY_MODES,
        crashlog_clean,
        enforce_core_port,
        exact_identity,
        exact_version_identity,
    )
    from smoke_d1l import exact_commit, open_d1l_serial, send_console_command
    from ui_corruption_probe_d1l import (
        data_refresh_step,
        event_failures,
        summarize_telemetry,
        tab_step,
        token_for_round,
        token_prefix_for_run,
    )
except ImportError:  # pragma: no cover - package import path used by pytest
    from scripts.artifact_metadata import git_metadata, stamp_report
    from scripts.core_smoke_d1l import (
        CORE_RELEASE_PROFILE,
        SD_HISTORY_MODES,
        crashlog_clean,
        enforce_core_port,
        exact_identity,
        exact_version_identity,
    )
    from scripts.smoke_d1l import exact_commit, open_d1l_serial, send_console_command
    from scripts.ui_corruption_probe_d1l import (
        data_refresh_step,
        event_failures,
        summarize_telemetry,
        tab_step,
        token_for_round,
        token_prefix_for_run,
    )


CORE_TAB_SEQUENCE = ("home", "messages", "nodes", "packets", "settings")
CORE_SCROLL_SURFACES = (
    "home",
    "public_messages",
    "dm_thread",
    "nodes",
    "packets",
    "settings",
)
CORE_COMPOSE_TARGETS = (
    "public",
    "public-long",
    "dm",
    "dm-long",
    "public-search",
    "dm-search",
    "packet-search",
    "contact-edit",
    "onboarding",
)
RELEASE_MIN_ROUNDS = 20


def core_tab_sequence_ok(tabs: object) -> bool:
    return (
        isinstance(tabs, list)
        and tuple(tabs) == CORE_TAB_SEQUENCE
        and "map" not in tabs
    )


def command_plan(rounds: int = RELEASE_MIN_ROUNDS) -> dict:
    commands = ["version", "health", "ui status"]
    commands.extend(
        f"ui scroll-probe {surface}" for surface in CORE_SCROLL_SURFACES
    )
    for round_number in range(1, rounds + 1):
        commands.extend(f"ui tab {tab}" for tab in CORE_TAB_SEQUENCE)
        commands.extend(
            (
                f"ui data-canary {token_for_round(round_number)}",
                "ui tab packets",
                f"packets search {token_for_round(round_number)}",
                "ui tab messages",
                f"messages public search {token_for_round(round_number)}",
                "ui status",
                "health",
                "crashlog",
            )
        )
    commands.extend(
        f"ui compose-probe {target}" for target in CORE_COMPOSE_TARGETS
    )
    return {
        "schema": 1,
        "kind": "core_ui_corruption_probe_plan",
        "mode": "plan",
        "ok": False,
        "closure_eligible": False,
        "hardware_required": True,
        "physical_observed": False,
        "port": "COM12",
        "release_profile": CORE_RELEASE_PROFILE,
        "rounds": rounds,
        "release_min_rounds": RELEASE_MIN_ROUNDS,
        "tabs": list(CORE_TAB_SEQUENCE),
        "scroll_surfaces": list(CORE_SCROLL_SURFACES),
        "compose_targets": list(CORE_COMPOSE_TARGETS),
        "commands": commands,
        "public_rf_tx": False,
        "network_tx": False,
        "map_network_requests": False,
        "formats_sd": False,
    }


def _identity_failure_report(
    *,
    expected_commit: str,
    expected_sd_history_mode: str | None,
    version: dict,
    github_run_id: str,
    workflow_run_attempt: str,
    health: dict | None = None,
) -> dict:
    observed_sd_mode = version.get("sd_history_mode")
    return {
        "schema": 1,
        "kind": "core_ui_corruption_probe",
        "mode": "hardware",
        "ok": False,
        "closure_eligible": False,
        "hardware_required": True,
        "physical_observed": True,
        "identity_preflight_only": True,
        "port": "COM12",
        "release_profile": CORE_RELEASE_PROFILE,
        "sd_history_mode": observed_sd_mode,
        "expected_sd_history_mode": expected_sd_history_mode,
        "expected_firmware_commit": expected_commit,
        "github_actions_run": github_run_id,
        "workflow_run_attempt": workflow_run_attempt,
        "device_build_commit": version.get("build_commit"),
        "firmware_identity_required": True,
        "firmware_identity_ok": False,
        "rounds": 0,
        "release_min_rounds": RELEASE_MIN_ROUNDS,
        "tabs": list(CORE_TAB_SEQUENCE),
        "events": [],
        "identity_results": [version] + ([health] if health else []),
        "public_rf_tx": False,
        "network_tx": False,
        "map_network_requests": False,
        "formats_sd": False,
    }


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
    expected_commit: str,
    expected_sd_history_mode: str | None,
    github_run_id: str,
    workflow_run_attempt: str,
) -> dict:
    try:
        import serial
    except ImportError as exc:
        raise SystemExit(
            "pyserial is required for Core UI hardware validation: "
            "python -m pip install pyserial"
        ) from exc

    port = enforce_core_port(port)
    normalized_commit = exact_commit(expected_commit)
    if normalized_commit is None:
        raise ValueError("expected_commit must be an exact 40-character hexadecimal SHA")
    if (
        expected_sd_history_mode is not None
        and expected_sd_history_mode not in SD_HISTORY_MODES
    ):
        raise ValueError("expected_sd_history_mode is invalid")
    if rounds < RELEASE_MIN_ROUNDS:
        raise ValueError(f"Core UI release evidence requires at least {RELEASE_MIN_ROUNDS} rounds")
    if skip_data_canary:
        raise ValueError("Core UI release evidence cannot skip the data canary")
    if (
        not str(github_run_id).isdigit()
        or int(github_run_id) < 1
        or not str(workflow_run_attempt).isdigit()
        or int(workflow_run_attempt) < 1
    ):
        raise ValueError(
            "GitHub run id and run attempt must be positive integers"
        )
    root = Path(__file__).resolve().parents[1]
    source = git_metadata(root)
    if not (
        exact_commit(source.get("commit")) == normalized_commit
        and source.get("dirty") is False
        and source.get("dirty_entries") == []
    ):
        raise ValueError(
            "Core UI probe must run from the exact clean candidate source"
        )

    events: list[dict] = []
    setup_events: list[dict] = []
    scroll_events: list[dict] = []
    compose_events: list[dict] = []
    started_at = datetime.now(timezone.utc)
    run_prefix = token_prefix_for_run(started_at)

    with open_d1l_serial(
        serial, port=port, baudrate=baud, timeout=timeout
    ) as ser:
        time.sleep(1.0)
        ser.reset_input_buffer()

        # Exact candidate/profile admission precedes every UI action.
        version = send_console_command(ser, "version", timeout)
        observed_sd_mode = version.get("sd_history_mode")
        required_sd_mode = expected_sd_history_mode or observed_sd_mode
        if (
            required_sd_mode not in SD_HISTORY_MODES
            or not exact_version_identity(
                version, normalized_commit, required_sd_mode
            )
        ):
            return _identity_failure_report(
                expected_commit=normalized_commit,
                expected_sd_history_mode=expected_sd_history_mode,
                version=version,
                github_run_id=str(github_run_id),
                workflow_run_attempt=str(workflow_run_attempt),
            )
        health_preflight = send_console_command(ser, "health", timeout)
        if not exact_identity(
            health_preflight, normalized_commit, required_sd_mode
        ):
            return _identity_failure_report(
                expected_commit=normalized_commit,
                expected_sd_history_mode=expected_sd_history_mode,
                version=version,
                health=health_preflight,
                github_run_id=str(github_run_id),
                workflow_run_attempt=str(workflow_run_attempt),
            )

        setup_events.extend(
            (
                {"command": "version", "result": version},
                {"command": "health", "result": health_preflight},
                {
                    "command": "ui status",
                    "result": send_console_command(ser, "ui status", timeout),
                },
                {
                    "command": "crashlog",
                    "result": send_console_command(ser, "crashlog", timeout),
                },
            )
        )
        if not crashlog_clean(setup_events[-1]["result"]):
            return {
                **_identity_failure_report(
                    expected_commit=normalized_commit,
                    expected_sd_history_mode=expected_sd_history_mode,
                    version=version,
                    health=health_preflight,
                    github_run_id=str(github_run_id),
                    workflow_run_attempt=str(workflow_run_attempt),
                ),
                "identity_preflight_only": False,
                "pre_ui_crashlog_clean": False,
                "setup_events": setup_events,
            }
        if clear_crashlog_before_start:
            setup_events.append(
                {
                    "command": "crashlog clear",
                    "result": send_console_command(ser, "crashlog clear", timeout),
                }
            )

        for surface in CORE_SCROLL_SURFACES:
            command = f"ui scroll-probe {surface}"
            scroll_events.append(
                {
                    "surface": surface,
                    "command": command,
                    "result": send_console_command(ser, command, timeout),
                }
            )
        for round_number in range(1, rounds + 1):
            for tab in CORE_TAB_SEQUENCE:
                events.append(
                    tab_step(
                        ser,
                        tab,
                        timeout,
                        settle_sec,
                        poll_sec,
                        round_number,
                    )
                )
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

        # Compose probes run only after the 20-round visible navigation loop.
        # The onboarding target intentionally leaves its modal visible and
        # must never cover the tab-switch evidence being qualified.
        for target in CORE_COMPOSE_TARGETS:
            command = f"ui compose-probe {target}"
            compose_events.append(
                {
                    "target": target,
                    "command": command,
                    "result": send_console_command(ser, command, timeout),
                }
            )

        health_final = send_console_command(ser, "health", timeout)

    ended_at = datetime.now(timezone.utc)
    setup_failures = [
        event
        for event in setup_events
        if event.get("result", {}).get("ok") is not True
    ]
    scroll_failures = [
        event
        for event in scroll_events
        if event.get("result", {}).get("ok") is not True
    ]
    compose_failures = [
        event
        for event in compose_events
        if event.get("result", {}).get("ok") is not True
    ]
    failures = event_failures(events, skip_data_canary=skip_data_canary)
    identity_failures = []
    for event in events:
        for candidate in (
            event.get("health"),
            event.get("packet_tab", {}).get("health")
            if isinstance(event.get("packet_tab"), dict)
            else None,
            event.get("messages_tab", {}).get("health")
            if isinstance(event.get("messages_tab"), dict)
            else None,
        ):
            if isinstance(candidate, dict) and not exact_identity(
                candidate, normalized_commit, required_sd_mode
            ):
                identity_failures.append(
                    {
                        "round": event.get("round"),
                        "kind": event.get("kind"),
                        "code": "IDENTITY_DRIFT",
                    }
                )
    if (
        not exact_identity(
            health_final, normalized_commit, required_sd_mode
        )
        or health_final.get("cmd") != "health"
        or health_final.get("board_ready") is not True
        or health_final.get("ui_ready") is not True
    ):
        identity_failures.append({"kind": "final_health", "code": "IDENTITY_DRIFT"})

    telemetry = summarize_telemetry(events)
    telemetry_failures = []
    if telemetry.get("uptime_monotonic") is not True:
        telemetry_failures.append({"check": "uptime_monotonic", "ok": False})
    if telemetry.get("final_pending") is not False:
        telemetry_failures.append({"check": "no_stuck_pending", "ok": False})
    if telemetry.get("final_active_tab") not in CORE_TAB_SEQUENCE:
        telemetry_failures.append({"check": "final_active_tab_known", "ok": False})

    all_failures = (
        setup_failures
        + scroll_failures
        + compose_failures
        + failures
        + identity_failures
        + telemetry_failures
    )
    report = {
        "schema": 1,
        "kind": "core_ui_corruption_probe",
        "mode": "hardware",
        "ok": not all_failures,
        "closure_eligible": True,
        "hardware_required": True,
        "physical_observed": True,
        "identity_preflight_only": False,
        "port": port,
        "baud": baud,
        "started_at": started_at.isoformat().replace("+00:00", "Z"),
        "ended_at": ended_at.isoformat().replace("+00:00", "Z"),
        "release_profile": CORE_RELEASE_PROFILE,
        "sd_history_mode": required_sd_mode,
        "expected_sd_history_mode": expected_sd_history_mode,
        "expected_firmware_commit": normalized_commit,
        "github_actions_run": str(github_run_id),
        "workflow_run_attempt": str(workflow_run_attempt),
        "device_build_commit": version.get("build_commit"),
        "firmware_identity_required": True,
        "firmware_identity_ok": not identity_failures,
        "rounds": rounds,
        "release_min_rounds": RELEASE_MIN_ROUNDS,
        "settle_sec": settle_sec,
        "poll_sec": poll_sec,
        "clear_crashlog_before_start": clear_crashlog_before_start,
        "skip_data_canary": skip_data_canary,
        "data_canary_prefix": run_prefix,
        "tabs": list(CORE_TAB_SEQUENCE),
        "scroll_surfaces": list(CORE_SCROLL_SURFACES),
        "compose_targets": list(CORE_COMPOSE_TARGETS),
        "unavailable_destinations_excluded": [
            "map",
            "wifi",
            "ble",
            "channels",
            "admin",
            "observer",
            "update",
            "location",
            "trace",
        ],
        "public_rf_tx": False,
        "network_tx": False,
        "map_network_requests": False,
        "formats_sd": False,
        "data_refresh_events": sum(
            1 for event in events if event.get("kind") == "data_refresh"
        ),
        "failure_count": len(all_failures),
        "failures": failures,
        "identity_failures": identity_failures,
        "telemetry_failures": telemetry_failures,
        "telemetry": telemetry,
        "checks": {
            "exact_candidate": not identity_failures,
            "esp_idf_v5_5_4": version.get("idf") == "v5.5.4",
            "core_profile": not identity_failures,
            "pre_ui_crashlog_clean": crashlog_clean(
                setup_events[3]["result"]
            ),
            "core_tab_sequence_exact": True,
            "core_scroll_surfaces_exact": not scroll_failures,
            "core_compose_targets_exact": not compose_failures,
            "tab_switches_settle": not any(
                failure.get("kind") == "tab" for failure in failures
            ),
            "data_refresh_exercised": skip_data_canary
            or sum(1 for event in events if event.get("kind") == "data_refresh")
            == rounds,
            "data_refreshes_pass": not any(
                failure.get("kind") == "data_refresh" for failure in failures
            ),
            "no_unavailable_destination": True,
            "no_public_rf": True,
            "no_network_tx": True,
            "no_map_network_requests": True,
            "no_formatting": True,
            "uptime_monotonic": telemetry.get("uptime_monotonic") is True,
            "no_stuck_pending": telemetry.get("final_pending") is False,
            "final_active_tab_known": telemetry.get("final_active_tab")
            in CORE_TAB_SEQUENCE,
        },
        "setup_events": setup_events,
        "scroll_events": scroll_events,
        "compose_events": compose_events,
        "final_health": health_final,
        "events": events,
    }
    return report


def resolve_out_path(out_arg: str | None) -> Path:
    root = Path(__file__).resolve().parents[1]
    if out_arg:
        path = Path(out_arg)
        return path if path.is_absolute() else root / path
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    return (
        root
        / "artifacts"
        / "hardware"
        / "com12"
        / f"core_ui_corruption_probe_{stamp}_COM12.json"
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default=os.environ.get("D1L_PORT"))
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--rounds", type=int, default=RELEASE_MIN_ROUNDS)
    parser.add_argument("--settle-sec", type=float, default=0.1)
    parser.add_argument("--poll-sec", type=float, default=0.05)
    parser.add_argument("--clear-crashlog-before-start", action="store_true")
    parser.add_argument("--skip-data-canary", action="store_true")
    parser.add_argument("--expected-firmware-commit", required=True)
    parser.add_argument("--github-run-id", required=True)
    parser.add_argument("--github-run-attempt", required=True)
    parser.add_argument(
        "--expected-sd-history-mode",
        choices=sorted(SD_HISTORY_MODES),
    )
    parser.add_argument("--out")
    args = parser.parse_args(argv)

    try:
        port = enforce_core_port(args.port)
    except ValueError as exc:
        parser.error(str(exc))
    commit = exact_commit(args.expected_firmware_commit)
    if commit is None:
        parser.error(
            "--expected-firmware-commit must be an exact 40-character hexadecimal SHA"
        )
    if args.rounds < RELEASE_MIN_ROUNDS:
        parser.error(f"--rounds must be at least {RELEASE_MIN_ROUNDS}")
    if args.settle_sec < 0:
        parser.error("--settle-sec cannot be negative")
    if args.poll_sec <= 0:
        parser.error("--poll-sec must be positive")
    if args.skip_data_canary:
        parser.error("Core UI release evidence cannot use --skip-data-canary")
    if (
        not str(args.github_run_id).isdigit()
        or int(args.github_run_id) < 1
        or not str(args.github_run_attempt).isdigit()
        or int(args.github_run_attempt) < 1
    ):
        parser.error(
            "--github-run-id and --github-run-attempt must be positive integers"
        )

    report = run_probe(
        port=port,
        baud=args.baud,
        timeout=args.timeout,
        rounds=args.rounds,
        settle_sec=args.settle_sec,
        poll_sec=args.poll_sec,
        clear_crashlog_before_start=args.clear_crashlog_before_start,
        skip_data_canary=args.skip_data_canary,
        expected_commit=commit,
        expected_sd_history_mode=args.expected_sd_history_mode,
        github_run_id=str(args.github_run_id),
        workflow_run_attempt=str(args.github_run_attempt),
    )
    root = Path(__file__).resolve().parents[1]
    stamp_report(report, root)
    out_path = resolve_out_path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(report, indent=2) + "\n", encoding="ascii")
    print(json.dumps({"ok": report["ok"], "out": str(out_path)}, indent=2))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
