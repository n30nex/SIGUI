#!/usr/bin/env python3
"""Autonomous scroll probe for MeshCore DeskOS D1L screens."""

from __future__ import annotations

import argparse
import json
import os
import time
from datetime import datetime, timezone
from pathlib import Path

try:
    from smoke_d1l import open_d1l_serial, send_console_command
    from artifact_metadata import stamp_report
except ModuleNotFoundError:
    from scripts.artifact_metadata import stamp_report
    from scripts.smoke_d1l import open_d1l_serial, send_console_command


SCROLL_SURFACES = {
    "home": {"tab": "home", "label": "Home previews"},
    "public_messages": {"tab": "messages", "label": "Public messages"},
    "dm_thread": {"tab": "messages", "label": "DM thread"},
    "nodes": {"tab": "nodes", "label": "Nodes"},
    "packets": {"tab": "packets", "label": "Packets"},
    "settings": {"tab": "settings", "label": "Settings"},
    "storage": {"tab": "settings", "label": "Storage overview"},
    "storage_card": {"tab": "settings", "label": "SD card status"},
    "storage_data": {"tab": "settings", "label": "Data locations"},
    "wifi": {"tab": "settings", "label": "Wi-Fi settings"},
    "map": {"tab": "map", "label": "Actual Map view"},
    "map_options": {"tab": "map", "label": "Map options"},
    "map_location": {"tab": "map", "label": "Map location"},
    "map_cache": {"tab": "map", "label": "Cache status"},
}
SCREEN_ALIASES = {
    "messages": "public_messages",
    "public": "public_messages",
    "public_messages": "public_messages",
    "public-messages": "public_messages",
    "dm": "dm_thread",
    "dm_thread": "dm_thread",
    "dm-thread": "dm_thread",
    "sd": "storage",
    "sd_card": "storage_card",
    "sd-card": "storage_card",
    "storage-card": "storage_card",
    "storage-data": "storage_data",
    "wi_fi": "wifi",
    "wi-fi": "wifi",
    "map-options": "map_options",
    "map_options_page": "map_options",
    "map-options-page": "map_options",
    "map-location": "map_location",
    "map_location_page": "map_location",
    "map-location-page": "map_location",
    "map-cache": "map_cache",
}
DEFAULT_SCREENS = tuple(SCROLL_SURFACES)
VALID_SCREENS = tuple(SCROLL_SURFACES)
MAP_READ_ONLY_SURFACES = frozenset({
    "map",
    "map_options",
    "map_location",
    "map_cache",
})
SCROLL_MOVEMENT_OPTIONAL = {"home", "storage", "storage_card", *MAP_READ_ONLY_SURFACES}


def probe_safety(*, clear_crashlog_before_start: bool) -> dict[str, bool | int]:
    return {
        "read_only": not clear_crashlog_before_start,
        "save_actions": False,
        "clear_actions": clear_crashlog_before_start,
        "map_download": False,
        "network_tx": False,
        "map_network_requests": False,
        "background_download": False,
        "area_download": False,
        "visible_tile_limit": 9,
        "zoom_batch_limit": 1,
        "wifi_mutation": False,
        "rf_tx": False,
        "public_rf_tx": False,
        "dm_rf_tx": False,
        "storage_mutation": False,
        "formats_sd": False,
    }


def validate_setup_actions(screens: list[str], clear_crashlog_before_start: bool) -> None:
    if clear_crashlog_before_start and MAP_READ_ONLY_SURFACES.intersection(screens):
        raise ValueError(
            "--clear-crashlog-before-start is not allowed with network-suppressed Map probes"
        )


def utc_stamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def parse_screens(value: str) -> list[str]:
    raw_screens = [item.strip().lower() for item in value.split(",") if item.strip()]
    screens = [SCREEN_ALIASES.get(screen.replace(" ", "_"), screen.replace(" ", "_")) for screen in raw_screens]
    invalid = [raw for raw, screen in zip(raw_screens, screens) if screen not in SCROLL_SURFACES]
    if invalid:
        raise ValueError(f"invalid screen(s): {', '.join(invalid)}")
    return screens


def surface_plan(screens: list[str]) -> list[dict]:
    return [
        {"screen": screen, "tab": SCROLL_SURFACES[screen]["tab"], "label": SCROLL_SURFACES[screen]["label"]}
        for screen in screens
    ]


def map_network_request_count(row: dict | None) -> int | None:
    if not isinstance(row, dict) or row.get("ok") is not True:
        return None
    value = row.get("network_requests")
    if isinstance(value, bool):
        return None
    if isinstance(value, int) and value >= 0:
        return value
    if isinstance(value, str) and value.isdigit():
        return int(value)
    return None


def summarize_map_network_evidence(
    before: dict | None,
    after: dict | None,
    *,
    measured: bool,
) -> dict:
    before_count = map_network_request_count(before)
    after_count = map_network_request_count(after)
    samples_valid = measured and before_count is not None and after_count is not None
    return {
        "command": "map tiles status",
        "measured": measured,
        "before": before,
        "after": after,
        "before_count": before_count,
        "after_count": after_count,
        "delta": after_count - before_count if samples_valid else None,
        "samples_valid": samples_valid,
        "unchanged": samples_valid and before_count == after_count,
    }


def dry_run_report(screens: list[str], dwell_sec: float, manual_touch: bool) -> dict:
    plan = surface_plan(screens)
    commands: list[str] = []
    map_probe_requested = bool(MAP_READ_ONLY_SURFACES.intersection(screens))
    if map_probe_requested:
        commands.append("map tiles status")
    for item in plan:
        if item["screen"] in MAP_READ_ONLY_SURFACES:
            commands.append(f"ui scroll-probe {item['screen']}")
        else:
            commands.extend([f"ui tab {item['tab']}", f"ui scroll-probe {item['screen']}"])
    if map_probe_requested:
        commands.append("map tiles status")
    commands.extend(["ui status", "health", "crashlog"])
    return {
        "schema": 1,
        "mode": "dry-run",
        "ok": True,
        "hardware_required": False,
        "explicit_port_required": True,
        "manual_touch": manual_touch,
        "dwell_sec": dwell_sec,
        "screens": screens,
        "surface_plan": plan,
        "commands": commands,
        "map_network_evidence": {
            "command": "map tiles status",
            "measured": False,
            "declared_no_network": True,
            "before": None,
            "after": None,
        },
        **probe_safety(clear_crashlog_before_start=False),
    }


def crashlog_has_crash_like_entries(row: dict) -> bool:
    entries = row.get("entries")
    return isinstance(entries, list) and any(
        isinstance(entry, dict) and entry.get("crash_like") is True
        for entry in entries
    )


def event_failed(event: dict) -> bool:
    probe = event.get("probe") if isinstance(event.get("probe"), dict) else {}
    screen = str(event.get("screen") or "")
    movement_ok = screen in SCROLL_MOVEMENT_OPTIONAL or (
        probe.get("scrollable") is True and probe.get("moved") is True
    )
    probe_ok = probe.get("ok") is True or (screen in SCROLL_MOVEMENT_OPTIONAL and movement_ok)
    return (
        not event["request"].get("ok")
        or not event["tab_active"]
        or not probe_ok
        or probe.get("surface") != event["screen"]
        or probe.get("tab") != event["tab"]
        or probe.get("target_found") is not True
        or not movement_ok
        or event["status"].get("active_tab") != event["tab"]
        or not event["health"].get("ok")
        or crashlog_has_crash_like_entries(event["crashlog"])
    )


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


def run_scroll_probe(
    *,
    port: str,
    baud: int,
    timeout: float,
    screens: list[str],
    dwell_sec: float,
    manual_touch: bool,
    clear_crashlog_before_start: bool,
    poll_sec: float,
) -> dict:
    validate_setup_actions(screens, clear_crashlog_before_start)
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("pyserial is required for hardware scroll probe: python -m pip install pyserial") from exc

    setup_events: list[dict] = []
    events: list[dict] = []
    map_tiles_before: dict | None = None
    map_tiles_after: dict | None = None
    started_at = datetime.now(timezone.utc)
    map_indices = [
        index for index, surface in enumerate(surface_plan(screens))
        if surface["screen"] in MAP_READ_ONLY_SURFACES
    ]
    first_map_index = map_indices[0] if map_indices else None
    last_map_index = map_indices[-1] if map_indices else None

    with open_d1l_serial(serial, port=port, baudrate=baud, timeout=timeout) as ser:
        time.sleep(1.0)
        ser.reset_input_buffer()

        if clear_crashlog_before_start:
            setup_events.append(
                {"cmd": "crashlog clear", "result": send_console_command(ser, "crashlog clear", timeout)}
            )

        for surface_index, surface in enumerate(surface_plan(screens)):
            screen = surface["screen"]
            tab = surface["tab"]
            label = surface["label"]
            if surface_index == first_map_index:
                map_tiles_before = send_console_command(ser, "map tiles status", timeout)
            if screen in MAP_READ_ONLY_SURFACES:
                # The probe command arms firmware-side network suppression before
                # navigating to Map. Never open the live Map with `ui tab map`
                # from an evidence probe.
                probe = send_console_command(ser, f"ui scroll-probe {screen}", max(timeout, 15.0))
                request = probe
                time.sleep(dwell_sec)
                tab_active, statuses = wait_for_tab(ser, tab, max(timeout, 15.0), poll_sec)
            else:
                request = send_console_command(ser, f"ui tab {tab}", timeout)
                time.sleep(dwell_sec)
                tab_active, statuses = wait_for_tab(ser, tab, max(timeout, 15.0), poll_sec)
                probe = send_console_command(ser, f"ui scroll-probe {screen}", max(timeout, 15.0))
            if manual_touch:
                print(f"Manual check: scroll the {label} surface, then press Enter.")
                input()
            status = send_console_command(ser, "ui status", timeout)
            health = send_console_command(ser, "health", timeout)
            crashlog = send_console_command(ser, "crashlog", timeout)
            events.append(
                {
                    "screen": screen,
                    "tab": tab,
                    "label": label,
                    "request": request,
                    "tab_active": tab_active,
                    "statuses": statuses,
                    "probe": probe,
                    "status": status,
                    "health": health,
                    "crashlog": crashlog,
                    "manual_touch_confirmed": manual_touch,
                }
            )
            if surface_index == last_map_index:
                map_tiles_after = send_console_command(ser, "map tiles status", timeout)

    ended_at = datetime.now(timezone.utc)
    failures = [
        {
            "screen": event["screen"],
            "request_ok": event["request"].get("ok"),
            "tab_active": event["tab_active"],
            "probe_ok": event["probe"].get("ok"),
            "probe_surface": event["probe"].get("surface"),
            "probe_tab": event["probe"].get("tab"),
            "target_found": event["probe"].get("target_found"),
            "scrollable": event["probe"].get("scrollable"),
            "moved": event["probe"].get("moved"),
            "active_tab": event["status"].get("active_tab"),
            "expected_active_tab": event["tab"],
            "health_ok": event["health"].get("ok"),
            "crashlog_crash_like_entries": crashlog_has_crash_like_entries(event["crashlog"]),
            "movement_required": event["screen"] not in SCROLL_MOVEMENT_OPTIONAL,
        }
        for event in events
        if event_failed(event)
    ]
    map_network_evidence = summarize_map_network_evidence(
        map_tiles_before,
        map_tiles_after,
        measured=bool(map_indices),
    )
    map_network_failures = []
    if map_indices and map_network_evidence.get("unchanged") is not True:
        map_network_failures.append(
            {
                "kind": "map_network_counter",
                "samples_valid": map_network_evidence.get("samples_valid"),
                "before_count": map_network_evidence.get("before_count"),
                "after_count": map_network_evidence.get("after_count"),
                "delta": map_network_evidence.get("delta"),
            }
        )
    failures.extend(map_network_failures)
    setup_failures = [
        event for event in setup_events if not event.get("result", {}).get("ok")
    ]
    network_assertion = (
        False if not map_indices or map_network_evidence.get("unchanged") is True
        else True if map_network_evidence.get("samples_valid") is True
        else None
    )
    safety = probe_safety(clear_crashlog_before_start=clear_crashlog_before_start)
    safety["network_tx"] = network_assertion
    safety["map_network_requests"] = network_assertion
    return {
        "schema": 1,
        "mode": "hardware",
        "port": port,
        "baud": baud,
        "started_at": started_at.isoformat().replace("+00:00", "Z"),
        "ended_at": ended_at.isoformat().replace("+00:00", "Z"),
        "screens": screens,
        "surface_plan": surface_plan(screens),
        "dwell_sec": dwell_sec,
        "manual_touch": manual_touch,
        "clear_crashlog_before_start": clear_crashlog_before_start,
        "scroll_movement_optional": sorted(SCROLL_MOVEMENT_OPTIONAL),
        "ok": not failures and not setup_failures,
        "failure_count": len(failures) + len(setup_failures),
        "failures": failures,
        "setup_events": setup_events,
        "probe_results": {event["screen"]: event["probe"] for event in events},
        "events": events,
        "map_network_evidence": map_network_evidence,
        **safety,
    }


def resolve_out_path(out_arg: str | None, mode: str) -> Path:
    root = Path(__file__).resolve().parents[1]
    if out_arg:
        out_path = Path(out_arg)
        if not out_path.is_absolute():
            out_path = root / out_path
        return out_path
    return root / "artifacts" / "scroll-probe" / f"d1l-scroll-probe-{mode}-{utc_stamp()}.json"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default=os.environ.get("D1L_PORT"))
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--screens", default=",".join(DEFAULT_SCREENS))
    parser.add_argument("--dwell-sec", type=float, default=0.5)
    parser.add_argument("--poll-sec", type=float, default=0.05)
    parser.add_argument("--manual-touch", action="store_true")
    parser.add_argument("--clear-crashlog-before-start", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--out", default=None)
    args = parser.parse_args()

    if args.dwell_sec < 0:
        parser.error("--dwell-sec cannot be negative")
    if args.poll_sec <= 0:
        parser.error("--poll-sec must be positive")
    try:
        screens = parse_screens(args.screens)
    except ValueError as exc:
        parser.error(str(exc))
    if not screens:
        parser.error("--screens must include at least one screen")
    try:
        validate_setup_actions(screens, args.clear_crashlog_before_start)
    except ValueError as exc:
        parser.error(str(exc))

    if args.dry_run:
        report = dry_run_report(screens, args.dwell_sec, args.manual_touch)
        mode = "dry-run"
    else:
        if not args.port:
            parser.error("No D1L port supplied. Set D1L_PORT or pass --port.")
        report = run_scroll_probe(
            port=args.port,
            baud=args.baud,
            timeout=args.timeout,
            screens=screens,
            dwell_sec=args.dwell_sec,
            manual_touch=args.manual_touch,
            clear_crashlog_before_start=args.clear_crashlog_before_start,
            poll_sec=args.poll_sec,
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
