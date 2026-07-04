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
    from smoke_d1l import send_console_command
    from artifact_metadata import stamp_report
except ModuleNotFoundError:
    from scripts.artifact_metadata import stamp_report
    from scripts.smoke_d1l import send_console_command


SCROLL_SURFACES = {
    "home": {"tab": "home", "label": "Home previews"},
    "public_messages": {"tab": "messages", "label": "Public messages"},
    "dm_thread": {"tab": "messages", "label": "DM thread"},
    "nodes": {"tab": "nodes", "label": "Nodes"},
    "packets": {"tab": "packets", "label": "Packets"},
    "settings": {"tab": "settings", "label": "Settings"},
    "storage": {"tab": "settings", "label": "Storage settings"},
    "wifi": {"tab": "settings", "label": "Wi-Fi settings"},
    "map": {"tab": "map", "label": "Map"},
}
SCREEN_ALIASES = {
    "messages": "public_messages",
    "public": "public_messages",
    "public_messages": "public_messages",
    "public-messages": "public_messages",
    "dm": "dm_thread",
    "dm_thread": "dm_thread",
    "dm-thread": "dm_thread",
    "wi_fi": "wifi",
    "wi-fi": "wifi",
}
DEFAULT_SCREENS = tuple(SCROLL_SURFACES)
VALID_SCREENS = tuple(SCROLL_SURFACES)


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


def dry_run_report(screens: list[str], dwell_sec: float, manual_touch: bool) -> dict:
    plan = surface_plan(screens)
    commands: list[str] = []
    for item in plan:
        commands.extend([f"ui tab {item['tab']}", f"ui scroll-probe {item['screen']}"])
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
    }


def crashlog_has_entries(row: dict) -> bool:
    entries = row.get("entries")
    return isinstance(entries, list) and len(entries) > 0


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
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("pyserial is required for hardware scroll probe: python -m pip install pyserial") from exc

    setup_events: list[dict] = []
    events: list[dict] = []
    started_at = datetime.now(timezone.utc)

    with serial.Serial(port=port, baudrate=baud, timeout=timeout) as ser:
        time.sleep(1.0)
        ser.reset_input_buffer()

        if clear_crashlog_before_start:
            setup_events.append(
                {"cmd": "crashlog clear", "result": send_console_command(ser, "crashlog clear", timeout)}
            )

        for surface in surface_plan(screens):
            screen = surface["screen"]
            tab = surface["tab"]
            label = surface["label"]
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
            "crashlog_entries": crashlog_has_entries(event["crashlog"]),
        }
        for event in events
        if not event["request"].get("ok")
        or not event["tab_active"]
        or not event["probe"].get("ok")
        or event["probe"].get("surface") != event["screen"]
        or event["probe"].get("tab") != event["tab"]
        or event["probe"].get("target_found") is not True
        or event["probe"].get("scrollable") is not True
        or event["probe"].get("moved") is not True
        or event["status"].get("active_tab") != event["tab"]
        or not event["health"].get("ok")
        or crashlog_has_entries(event["crashlog"])
    ]
    setup_failures = [
        event for event in setup_events if not event.get("result", {}).get("ok")
    ]
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
        "ok": not failures and not setup_failures,
        "failure_count": len(failures) + len(setup_failures),
        "failures": failures,
        "setup_events": setup_events,
        "probe_results": {event["screen"]: event["probe"] for event in events},
        "events": events,
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
