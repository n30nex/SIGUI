#!/usr/bin/env python3
"""Bounded, serial-only trace of one controlled D1L software reboot."""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Callable

try:
    from artifact_metadata import stamp_report
    from smoke_d1l import (
        HOST_CONSOLE_EVIDENCE_FIELDS,
        boot_transition_proven,
        exact_commit,
        firmware_identity_matches,
        open_d1l_serial,
        parse_jsonl_line,
        reboot_command_passed,
        timeout_for_reboot_command,
    )
except ImportError:  # pragma: no cover - package import path used by pytest
    from scripts.artifact_metadata import stamp_report
    from scripts.smoke_d1l import (
        HOST_CONSOLE_EVIDENCE_FIELDS,
        boot_transition_proven,
        exact_commit,
        firmware_identity_matches,
        open_d1l_serial,
        parse_jsonl_line,
        reboot_command_passed,
        timeout_for_reboot_command,
    )


ROOT = Path(__file__).resolve().parents[1]
# The configured RP2040 smoke/UF2 maintenance port is never an ESP32 console
# target. Keeping it out of this runner also prevents an accidental 1200-baud
# UF2 touch from opening the RP2040 boot volume.
FORBIDDEN_PORTS = {"COM" + number for number in ("8", "11", "16", "29")}
D1L_CONSOLE_BAUD = 115200
PRE_COMMANDS = ("version", "health", "crashlog")
POST_COMMANDS = ("version", "health", "crashlog")
SAFE_COMMANDS = frozenset((*PRE_COMMANDS, "reboot"))
SERIAL_LINE_LIMIT_BYTES = 16384
IGNORED_JSON_LIMIT = 8
BOOT_EVENT_LIMIT = 16
TRANSITION_PHASES = frozenset({"transition", "post"})
SW_RESET_REASONS = frozenset({"RTC_SW_CPU_RST", "SW_CPU_RESET"})

ROM_BANNER_RE = re.compile(r"(?:ESP-ROM:|^ets\s)", re.IGNORECASE)
RESET_BANNER_RE = re.compile(r"(?:^|\s)rst:0x[0-9a-f]+", re.IGNORECASE)
RESET_CAUSE_RE = re.compile(
    r"(?:^|\s)rst:0x(?P<code>[0-9a-f]+)\s*\((?P<reason>[^)]+)\)",
    re.IGNORECASE,
)
BOOT_LINE_RE = re.compile(r"(?:^|[,\s])boot:0x[0-9a-f]+", re.IGNORECASE)
CRASH_BANNER_RE = re.compile(r"(?:WDT|PANIC|BROWNOUT)", re.IGNORECASE)


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def normalize_port(port: str) -> str:
    return port.strip().upper()


def enforce_port_guard(port: str) -> str:
    normalized = normalize_port(port)
    if not re.fullmatch(r"COM[1-9][0-9]*", normalized):
        raise ValueError(f"An explicit Windows COM port is required: {port!r}")
    if normalized in FORBIDDEN_PORTS:
        raise ValueError(f"Refusing to use forbidden port: {normalized}")
    return normalized


def enforce_safe_command(command: str) -> None:
    if command not in SAFE_COMMANDS:
        raise ValueError(f"Refusing command outside reboot trace allowlist: {command}")


def _clean_device_result(result: dict) -> dict:
    return {
        key: value
        for key, value in result.items()
        if key not in HOST_CONSOLE_EVIDENCE_FIELDS
    }


class TraceRecorder:
    """Retain bounded raw lines while counting all observed boot markers."""

    def __init__(
        self,
        *,
        clock: Callable[[], float],
        now: Callable[[], str],
        max_raw_lines: int,
        max_raw_line_chars: int,
    ) -> None:
        self.clock = clock
        self.now = now
        self.started = clock()
        self.max_raw_lines = max_raw_lines
        self.max_raw_line_chars = max_raw_line_chars
        self.raw_lines: list[dict] = []
        self.raw_lines_dropped = 0
        self.raw_chars_dropped = 0
        self.raw_lines_dropped_by_phase: dict[str, int] = {}
        self.raw_chars_dropped_by_phase: dict[str, int] = {}
        self.boot_events: list[dict] = []
        self.boot_events_dropped = 0
        self.reset_events: list[dict] = []
        self.reset_events_dropped = 0
        self.marker_counts = {
            "rom_banner": 0,
            "reset_banner": 0,
            "boot_line": 0,
            "boot_help": 0,
        }
        self.transition_marker_counts = {
            "rom_banner": 0,
            "reset_banner": 0,
            "boot_line": 0,
            "boot_help": 0,
            "crash_marker": 0,
            "sw_reset_banner": 0,
            "wdt_reset_banner": 0,
            "non_sw_reset_banner": 0,
            "unparsed_reset_banner": 0,
        }

    def elapsed_ms(self) -> float:
        return round(max(0.0, self.clock() - self.started) * 1000.0, 3)

    def _event_base(self, phase: str) -> dict:
        return {
            "observed_at": self.now(),
            "elapsed_ms": self.elapsed_ms(),
            "phase": phase,
        }

    def observe(self, raw_bytes: bytes, phase: str) -> dict | None:
        text_with_endings = raw_bytes.decode("utf-8", errors="replace")
        parsed = parse_jsonl_line(text_with_endings)
        if parsed is not None:
            if parsed.get("cmd") == "help":
                self.marker_counts["boot_help"] += 1
                if phase in TRANSITION_PHASES:
                    self.transition_marker_counts["boot_help"] += 1
                event = {
                    **self._event_base(phase),
                    "cmd": "help",
                    "ok": parsed.get("ok"),
                }
                if len(self.boot_events) < BOOT_EVENT_LIMIT:
                    self.boot_events.append(event)
                else:
                    self.boot_events_dropped += 1
            return _clean_device_result(parsed)

        text = text_with_endings.rstrip("\r\n")
        if not text:
            return None

        tags: list[str] = []
        if ROM_BANNER_RE.search(text):
            tags.append("rom_banner")
            self.marker_counts["rom_banner"] += 1
            if phase in TRANSITION_PHASES:
                self.transition_marker_counts["rom_banner"] += 1
        if RESET_BANNER_RE.search(text):
            tags.append("reset_banner")
            self.marker_counts["reset_banner"] += 1
            if phase in TRANSITION_PHASES:
                self.transition_marker_counts["reset_banner"] += 1
                cause = RESET_CAUSE_RE.search(text)
                if cause is None:
                    self.transition_marker_counts["unparsed_reset_banner"] += 1
                    reset_event = {
                        **self._event_base(phase),
                        "code": None,
                        "reason": None,
                        "is_sw": False,
                        "is_wdt": False,
                    }
                else:
                    code = int(cause.group("code"), 16)
                    reason = cause.group("reason").strip().upper()
                    is_sw = code == 0xC and reason in SW_RESET_REASONS
                    is_wdt = "WDT" in reason
                    marker = "sw_reset_banner" if is_sw else "non_sw_reset_banner"
                    self.transition_marker_counts[marker] += 1
                    if is_wdt:
                        self.transition_marker_counts["wdt_reset_banner"] += 1
                    reset_event = {
                        **self._event_base(phase),
                        "code": code,
                        "reason": reason,
                        "is_sw": is_sw,
                        "is_wdt": is_wdt,
                    }
                if len(self.reset_events) < BOOT_EVENT_LIMIT:
                    self.reset_events.append(reset_event)
                else:
                    self.reset_events_dropped += 1
        if BOOT_LINE_RE.search(text):
            tags.append("boot_line")
            self.marker_counts["boot_line"] += 1
            if phase in TRANSITION_PHASES:
                self.transition_marker_counts["boot_line"] += 1
        if CRASH_BANNER_RE.search(text):
            tags.append("crash_marker")
            if phase in TRANSITION_PHASES:
                self.transition_marker_counts["crash_marker"] += 1

        truncated = len(text) > self.max_raw_line_chars
        bounded_text = text[: self.max_raw_line_chars]
        retained = len(self.raw_lines) < self.max_raw_lines
        if retained:
            self.raw_lines.append(
                {
                    **self._event_base(phase),
                    "tags": tags,
                    "text": bounded_text,
                    "truncated": truncated,
                }
            )
        else:
            self.raw_lines_dropped += 1
            self.raw_chars_dropped += len(text)
            self.raw_lines_dropped_by_phase[phase] = (
                self.raw_lines_dropped_by_phase.get(phase, 0) + 1
            )
            self.raw_chars_dropped_by_phase[phase] = (
                self.raw_chars_dropped_by_phase.get(phase, 0) + len(text)
            )
        if truncated and retained:
            dropped_chars = len(text) - len(bounded_text)
            self.raw_chars_dropped += dropped_chars
            self.raw_chars_dropped_by_phase[phase] = (
                self.raw_chars_dropped_by_phase.get(phase, 0) + dropped_chars
            )
        return None

    def summary(self) -> dict:
        transition_lines = [
            line for line in self.raw_lines if line.get("phase") in TRANSITION_PHASES
        ]
        reset_banners = [
            line for line in transition_lines if "reset_banner" in line.get("tags", ())
        ]
        rom_banners = [
            line for line in transition_lines if "rom_banner" in line.get("tags", ())
        ]
        crash_markers = [
            line for line in transition_lines if "crash_marker" in line.get("tags", ())
        ]
        return {
            "raw_lines": self.raw_lines,
            "raw_line_count": len(self.raw_lines),
            "raw_lines_dropped": self.raw_lines_dropped,
            "raw_chars_dropped": self.raw_chars_dropped,
            "raw_lines_dropped_by_phase": dict(self.raw_lines_dropped_by_phase),
            "raw_chars_dropped_by_phase": dict(self.raw_chars_dropped_by_phase),
            "boot_events": self.boot_events,
            "boot_events_dropped": self.boot_events_dropped,
            "reset_events": self.reset_events,
            "reset_events_dropped": self.reset_events_dropped,
            "marker_counts": dict(self.marker_counts),
            "transition_marker_counts": dict(self.transition_marker_counts),
            "reset_banners": reset_banners,
            "rom_banners": rom_banners,
            "crash_markers": crash_markers,
        }


def _set_serial_timeout(ser, timeout: float) -> None:
    if hasattr(ser, "timeout"):
        ser.timeout = max(0.001, timeout)


def _readline(ser) -> bytes:
    return ser.readline(SERIAL_LINE_LIMIT_BYTES)


def read_expected_json(
    ser,
    *,
    command: str,
    timeout: float,
    poll_sec: float,
    recorder: TraceRecorder,
    phase: str,
    clock: Callable[[], float],
) -> dict:
    deadline = clock() + timeout
    ignored: list[dict] = []
    ignored_count = 0
    original_timeout = getattr(ser, "timeout", None)
    try:
        while clock() < deadline:
            remaining = max(0.0, deadline - clock())
            _set_serial_timeout(ser, min(poll_sec, remaining))
            raw = _readline(ser)
            if not raw:
                continue
            parsed = recorder.observe(raw, phase)
            if parsed is not None and parsed.get("cmd") == command:
                if ignored_count:
                    parsed["ignored_json_count"] = ignored_count
                    parsed["ignored_json"] = ignored
                return parsed
            if parsed is not None:
                ignored_count += 1
                if len(ignored) < IGNORED_JSON_LIMIT:
                    ignored.append(
                        {"cmd": parsed.get("cmd"), "ok": parsed.get("ok")}
                    )
    finally:
        if hasattr(ser, "timeout"):
            ser.timeout = original_timeout
    return {
        "schema": 1,
        "ok": False,
        "cmd": command,
        "code": "TIMEOUT",
        "ignored_json_count": ignored_count,
        "ignored_json": ignored,
    }


def send_safe_command(
    ser,
    command: str,
    *,
    timeout: float,
    poll_sec: float,
    recorder: TraceRecorder,
    phase: str,
    clock: Callable[[], float],
) -> dict:
    enforce_safe_command(command)
    ser.write((command + "\n").encode("ascii"))
    if hasattr(ser, "flush"):
        ser.flush()
    command_timeout = timeout_for_reboot_command(command, timeout)
    return read_expected_json(
        ser,
        command=command,
        timeout=command_timeout,
        poll_sec=poll_sec,
        recorder=recorder,
        phase=phase,
        clock=clock,
    )


def capture_boot_transition(
    ser,
    *,
    timeout: float,
    poll_sec: float,
    quiet_sec: float,
    recorder: TraceRecorder,
    clock: Callable[[], float],
) -> bool:
    deadline = clock() + timeout
    boot_help_seen_at: float | None = None
    original_timeout = getattr(ser, "timeout", None)
    try:
        while clock() < deadline:
            now = clock()
            if boot_help_seen_at is not None and now - boot_help_seen_at >= quiet_sec:
                return True
            remaining = max(0.0, deadline - now)
            if boot_help_seen_at is not None:
                remaining = min(
                    remaining,
                    max(0.001, quiet_sec - (now - boot_help_seen_at)),
                )
            _set_serial_timeout(ser, min(poll_sec, remaining))
            raw = _readline(ser)
            if not raw:
                continue
            parsed = recorder.observe(raw, "transition")
            if parsed is not None and parsed.get("cmd") == "help":
                boot_help_seen_at = clock()
                if quiet_sec == 0.0:
                    return True
    finally:
        if hasattr(ser, "timeout"):
            ser.timeout = original_timeout
    return boot_help_seen_at is not None


def command_result_ok(result: dict | None, command: str) -> bool:
    return (
        isinstance(result, dict)
        and result.get("ok") is True
        and result.get("cmd") == command
    )


def crashlog_transition(before: dict | None, after: dict | None) -> dict:
    analysis = {
        "ok": False,
        "total_delta": None,
        "new_entries": [],
        "exactly_one_sw_non_crash": False,
    }
    if not isinstance(before, dict) or not isinstance(after, dict):
        return analysis
    before_total = before.get("total_written")
    after_total = after.get("total_written")
    before_entries = before.get("entries")
    after_entries = after.get("entries")
    if (
        not command_result_ok(before, "crashlog")
        or not command_result_ok(after, "crashlog")
        or type(before_total) is not int
        or type(after_total) is not int
        or not isinstance(before_entries, list)
        or not isinstance(after_entries, list)
    ):
        return analysis

    analysis["total_delta"] = after_total - before_total
    previous_max = max(
        (
            entry.get("seq")
            for entry in before_entries
            if isinstance(entry, dict) and type(entry.get("seq")) is int
        ),
        default=0,
    )
    new_entries = [
        entry
        for entry in after_entries
        if isinstance(entry, dict)
        and type(entry.get("seq")) is int
        and entry.get("seq") > previous_max
    ]
    analysis["new_entries"] = new_entries
    exact = (
        analysis["total_delta"] == 1
        and len(new_entries) == 1
        and new_entries[0].get("reset_reason") == "SW"
        and new_entries[0].get("crash_like") is False
    )
    analysis["exactly_one_sw_non_crash"] = exact
    analysis["ok"] = exact
    return analysis


def _version_commit(result: dict | None) -> str | None:
    return exact_commit(result.get("build_commit") if isinstance(result, dict) else None)


def classify_report(report: dict) -> None:
    pre = report.get("pre") if isinstance(report.get("pre"), dict) else {}
    post = report.get("post") if isinstance(report.get("post"), dict) else {}
    pre_version = pre.get("version")
    pre_health = pre.get("health")
    pre_crashlog = pre.get("crashlog")
    post_version = post.get("version")
    post_health = post.get("health")
    post_crashlog = post.get("crashlog")
    transition = report.get("transition") if isinstance(report.get("transition"), dict) else {}
    transition_marker_counts = transition.get("transition_marker_counts")
    if not isinstance(transition_marker_counts, dict):
        transition_marker_counts = {}

    pre_complete = all(
        command_result_ok(pre.get(command), command) for command in PRE_COMMANDS
    )
    post_complete = all(
        command_result_ok(post.get(command), command) for command in POST_COMMANDS
    )
    crash = crashlog_transition(pre_crashlog, post_crashlog)
    nonce_changed = boot_transition_proven(pre_health, post_health)
    post_reset_reason = (
        post_health.get("reset_reason") if isinstance(post_health, dict) else None
    )
    pre_commit = _version_commit(pre_version)
    post_commit = _version_commit(post_version)
    identity_stable = pre_commit is not None and pre_commit == post_commit
    expected_commit = report.get("expected_firmware_commit")
    identity_expected = (
        firmware_identity_matches(pre_version, expected_commit)
        and firmware_identity_matches(post_version, expected_commit)
        if expected_commit is not None
        else None
    )
    multiple_boot_banners = any(
        type(transition_marker_counts.get(key)) is int
        and transition_marker_counts.get(key) > 1
        for key in ("rom_banner", "reset_banner", "boot_help")
    )
    raw_crash_marker_seen = transition_marker_counts.get("crash_marker", 0) > 0
    raw_wdt_reset_seen = transition_marker_counts.get("wdt_reset_banner", 0) > 0
    raw_reset_reason_sw = (
        transition_marker_counts.get("reset_banner") == 1
        and transition_marker_counts.get("sw_reset_banner") == 1
        and transition_marker_counts.get("non_sw_reset_banner") == 0
        and transition_marker_counts.get("unparsed_reset_banner") == 0
    )
    dropped_lines_by_phase = transition.get("raw_lines_dropped_by_phase")
    if not isinstance(dropped_lines_by_phase, dict):
        dropped_lines_by_phase = {}
    dropped_chars_by_phase = transition.get("raw_chars_dropped_by_phase")
    if not isinstance(dropped_chars_by_phase, dict):
        dropped_chars_by_phase = {}
    transition_lines_dropped = sum(
        value
        for phase, value in dropped_lines_by_phase.items()
        if phase in TRANSITION_PHASES and type(value) is int and value > 0
    )
    transition_chars_dropped = sum(
        value
        for phase, value in dropped_chars_by_phase.items()
        if phase in TRANSITION_PHASES and type(value) is int and value > 0
    )
    raw_transition_complete = (
        transition_lines_dropped == 0
        and transition_chars_dropped == 0
        and transition.get("reset_events_dropped", 0) == 0
    )

    checks = {
        "pre_complete": pre_complete,
        "reboot_attempted": report.get("reboot_attempted") is True,
        "reboot_ack_ok": reboot_command_passed(report.get("reboot")),
        "boot_help_seen": transition_marker_counts.get("boot_help", 0) >= 1,
        "post_complete": post_complete,
        "boot_nonce_changed": nonce_changed,
        "post_reset_reason": post_reset_reason,
        "post_reset_reason_sw": post_reset_reason == "SW",
        "crashlog_transition": crash,
        "firmware_identity_stable": identity_stable,
        "firmware_identity_expected": identity_expected,
        "multiple_boot_banners": multiple_boot_banners,
        "raw_crash_marker_seen": raw_crash_marker_seen,
        "raw_wdt_reset_seen": raw_wdt_reset_seen,
        "raw_reset_reason_sw": raw_reset_reason_sw,
        "raw_transition_complete": raw_transition_complete,
        "transition_lines_dropped": transition_lines_dropped,
        "transition_chars_dropped": transition_chars_dropped,
    }
    report["checks"] = checks

    if not pre_complete:
        classification = "preflight_failed"
    elif report.get("reboot_attempted") is not True:
        classification = "reboot_not_attempted"
    elif not checks["reboot_ack_ok"]:
        classification = "reboot_ack_failed"
    elif post_reset_reason == "WDT" or raw_wdt_reset_seen:
        classification = "wdt_reset"
    elif not checks["boot_help_seen"] or not post_complete:
        classification = "incomplete_transition"
    elif multiple_boot_banners:
        classification = "multiple_boot_banners"
    elif post_reset_reason != "SW":
        classification = "non_sw_reset"
    elif not raw_transition_complete:
        classification = "raw_transition_incomplete"
    elif not raw_reset_reason_sw:
        classification = "raw_reset_banner_mismatch"
    elif raw_crash_marker_seen:
        classification = "raw_reset_banner_mismatch"
    elif not nonce_changed:
        classification = "boot_nonce_not_changed"
    elif not crash.get("ok"):
        classification = "crashlog_transition_invalid"
    elif not identity_stable or identity_expected is not True:
        classification = "firmware_identity_mismatch"
    else:
        classification = "single_sw_transition"

    report["classification"] = classification
    report["ok"] = classification == "single_sw_transition"


def base_report(args: argparse.Namespace, *, now: Callable[[], str]) -> dict:
    return {
        "schema": 1,
        "kind": "d1l_reboot_transition_trace",
        "mode": "dry-run" if args.dry_run else "hardware",
        "port": normalize_port(args.port),
        "baud": args.baud,
        "command_timeout_sec": args.command_timeout,
        "transition_timeout_sec": args.transition_timeout,
        "post_boot_quiet_sec": args.post_boot_quiet_sec,
        "expected_firmware_commit": args.expected_firmware_commit,
        "commands": [*PRE_COMMANDS, "reboot", *POST_COMMANDS],
        "public_rf_tx": False,
        "formats_sd": False,
        "format_commands_allowed": False,
        "manual_user_required": False,
        "hardware_required": not args.dry_run,
        "serial_handle_reused_across_reboot": not args.dry_run,
        "raw_capture_limits": {
            "serial_line_bytes": SERIAL_LINE_LIMIT_BYTES,
            "retained_lines": args.max_raw_lines,
            "retained_chars_per_line": args.max_raw_line_chars,
        },
        "pre": {},
        "reboot": None,
        "reboot_attempted": False,
        "transition": {},
        "post": {},
        "started_at": now(),
        "ended_at": None,
        "classification": "not_run",
        "ok": False,
    }


def _validate_args(args: argparse.Namespace) -> str:
    port = enforce_port_guard(args.port)
    if args.baud != D1L_CONSOLE_BAUD:
        raise ValueError(
            f"--baud must be {D1L_CONSOLE_BAUD}; refusing reset-prone or non-D1L baud"
        )
    for name in (
        "command_timeout",
        "transition_timeout",
        "read_poll_sec",
    ):
        if getattr(args, name) <= 0.0:
            raise ValueError(f"--{name.replace('_', '-')} must be positive")
    if args.open_settle_sec < 0.0 or args.post_boot_quiet_sec < 0.0:
        raise ValueError("settle and quiet durations must be non-negative")
    if not 1 <= args.max_raw_lines <= 1024:
        raise ValueError("--max-raw-lines must be between 1 and 1024")
    if not 64 <= args.max_raw_line_chars <= 4096:
        raise ValueError("--max-raw-line-chars must be between 64 and 4096")
    expected_commit = exact_commit(args.expected_firmware_commit)
    if not args.dry_run and expected_commit is None:
        raise ValueError(
            "--expected-firmware-commit is required for hardware evidence and must "
            "be an exact 40-character SHA"
        )
    if args.expected_firmware_commit is not None and expected_commit is None:
        raise ValueError("--expected-firmware-commit must be an exact 40-character SHA")
    return port


def run_trace(
    args: argparse.Namespace,
    *,
    serial_module=None,
    clock: Callable[[], float] = time.monotonic,
    now: Callable[[], str] = utc_now,
    sleep: Callable[[float], None] = time.sleep,
) -> dict:
    port = _validate_args(args)
    report = base_report(args, now=now)
    report["port"] = port
    if args.dry_run:
        report["classification"] = "dry_run"
        report["ok"] = True
        report["ended_at"] = now()
        return report

    if serial_module is None:
        try:
            import serial as serial_module
        except ImportError as exc:  # pragma: no cover - dependency dependent
            report["classification"] = "serial_dependency_missing"
            report["error"] = f"pyserial is required: {exc}"
            report["ended_at"] = now()
            return report

    recorder = TraceRecorder(
        clock=clock,
        now=now,
        max_raw_lines=args.max_raw_lines,
        max_raw_line_chars=args.max_raw_line_chars,
    )
    try:
        with open_d1l_serial(
            serial_module,
            port=port,
            baudrate=args.baud,
            timeout=args.read_poll_sec,
        ) as ser:
            if args.open_settle_sec:
                sleep(args.open_settle_sec)
            if hasattr(ser, "reset_input_buffer"):
                ser.reset_input_buffer()

            for command in PRE_COMMANDS:
                report["pre"][command] = send_safe_command(
                    ser,
                    command,
                    timeout=args.command_timeout,
                    poll_sec=args.read_poll_sec,
                    recorder=recorder,
                    phase="pre",
                    clock=clock,
                )

            pre_complete = all(
                command_result_ok(report["pre"].get(command), command)
                for command in PRE_COMMANDS
            )
            pre_identity_ok = (
                firmware_identity_matches(
                    report["pre"].get("version"), args.expected_firmware_commit
                )
                if args.expected_firmware_commit is not None
                else True
            )
            if pre_complete and pre_identity_ok:
                report["reboot_attempted"] = True
                report["reboot"] = send_safe_command(
                    ser,
                    "reboot",
                    timeout=args.command_timeout,
                    poll_sec=args.read_poll_sec,
                    recorder=recorder,
                    phase="reboot_ack",
                    clock=clock,
                )
                report["reboot_ack_elapsed_ms"] = recorder.elapsed_ms()
                if (
                    isinstance(report["reboot"], dict)
                    and report["reboot"].get("rebooting") is True
                ):
                    boot_ready = capture_boot_transition(
                        ser,
                        timeout=args.transition_timeout,
                        poll_sec=args.read_poll_sec,
                        quiet_sec=args.post_boot_quiet_sec,
                        recorder=recorder,
                        clock=clock,
                    )
                    report["boot_console_ready"] = boot_ready
                    if boot_ready:
                        for command in POST_COMMANDS:
                            report["post"][command] = send_safe_command(
                                ser,
                                command,
                                timeout=args.command_timeout,
                                poll_sec=args.read_poll_sec,
                                recorder=recorder,
                                phase="post",
                                clock=clock,
                            )
            elif not pre_identity_ok:
                report["preflight_error"] = "firmware_identity_mismatch"
    except Exception as exc:  # pragma: no cover - hardware exception details vary
        report["serial_error"] = str(exc)

    report["transition"] = recorder.summary()
    classify_report(report)
    if report.get("serial_error"):
        report["classification"] = "serial_error"
        report["ok"] = False
    report["ended_at"] = now()
    return report


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True, help="Explicit D1L Windows COM port")
    parser.add_argument("--out", required=True, help="JSON receipt path")
    parser.add_argument("--baud", type=int, default=D1L_CONSOLE_BAUD)
    parser.add_argument("--command-timeout", type=float, default=30.0)
    parser.add_argument("--transition-timeout", type=float, default=15.0)
    parser.add_argument("--read-poll-sec", type=float, default=0.1)
    parser.add_argument("--open-settle-sec", type=float, default=0.25)
    parser.add_argument("--post-boot-quiet-sec", type=float, default=0.5)
    parser.add_argument("--max-raw-lines", type=int, default=128)
    parser.add_argument("--max-raw-line-chars", type=int, default=1024)
    parser.add_argument("--expected-firmware-commit")
    parser.add_argument("--dry-run", action="store_true")
    return parser.parse_args(argv)


def _write_receipt(path: Path, report: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="ascii",
    )


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])
    try:
        report = run_trace(args)
    except ValueError as exc:
        report = base_report(args, now=utc_now)
        report["classification"] = "input_rejected"
        report["error"] = str(exc)
        report["ended_at"] = utc_now()
    stamp_report(report, ROOT)
    out = Path(args.out)
    if not out.is_absolute():
        out = ROOT / out
    _write_receipt(out, report)
    print(
        json.dumps(
            {
                "schema": 1,
                "kind": report.get("kind"),
                "ok": report.get("ok") is True,
                "classification": report.get("classification"),
                "out": str(out),
            },
            separators=(",", ":"),
        )
    )
    return 0 if report.get("ok") else 1


if __name__ == "__main__":
    raise SystemExit(main())
