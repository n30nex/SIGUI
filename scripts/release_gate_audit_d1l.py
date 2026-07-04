#!/usr/bin/env python3
"""Evidence-based public release gate audit for MeshCore DeskOS D1L."""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

try:
    from verify_checksums import verify_sha256_manifest
except ImportError:  # pragma: no cover - package import path used by pytest
    from scripts.verify_checksums import verify_sha256_manifest


RELEASE_UI_CORRUPTION_MIN_ROUNDS = 20
REQUIRED_UI_TELEMETRY_FIELDS = {
    "heap_free",
    "heap_min_free",
    "heap_largest_free",
    "lvgl_free_bytes",
    "lvgl_largest_free_bytes",
    "lvgl_used_pct",
    "ui_task_stack_free_words",
    "reset_reason",
}
REQUIRED_SCROLL_SURFACES = {
    "home": "home",
    "public_messages": "messages",
    "dm_thread": "messages",
    "nodes": "nodes",
    "packets": "packets",
    "settings": "settings",
    "storage": "settings",
    "wifi": "settings",
    "map": "map",
}
REQUIRED_SCROLL_SCREENS = set(REQUIRED_SCROLL_SURFACES)
REQUIRED_COMPOSE_CAPTURE_TARGETS = {"public", "public-long", "dm", "dm-long"}
REQUIRED_NOTICE_FILES = {
    "notices/LICENSE",
    "notices/THIRD_PARTY_NOTICES.md",
    "notices/ATTRIBUTIONS.md",
    "notices/SOURCE_AUDIT_AND_ATTRIBUTION.md",
}
FULL_SOAK_SECONDS = 12 * 60 * 60
REQUIRED_ROUTE_PROBE_CHECKS = {
    "trace_reports_active_probe",
    "probe_queued",
    "probe_is_dm_rf",
    "probe_not_public_rf",
    "token_generated",
    "packets_search_has_token",
    "messages_dm_has_token",
    "routes_trace_has_probe",
    "health_ready",
}
SAFE_SD_NEXT_ACTION = "confirm_fat32_card_or_inspect_rp2040_sd_mount_path"
OBSOLETE_SD_NEXT_ACTION_TOKENS = ("format", "mkfs", "erase", "wipe")
SD_BOOT_PREPARE_SCENARIOS = (
    "no-card",
    "correct-structure",
    "missing-structure",
    "unformatted",
    "existing-data",
    "rp2040-unavailable",
)
SD_BOOT_PREPARE_EXPECTED_CLASSIFICATIONS = {
    "no-card": {"no_card_fallback"},
    "correct-structure": {"ready_sd_file_gate"},
    "missing-structure": {"ready_sd_file_gate"},
    "unformatted": {"computer_fat32_required", "firmware_mount_error_fallback"},
    "existing-data": {
        "existing_data_preserved_ready",
        "existing_data_firmware_mount_fallback",
        "existing_data_not_wiped",
    },
    "rp2040-unavailable": {"bridge_unavailable_fallback"},
}
RAW_DIAG_PROBE_PREFIXES = ("hd", "hs", "ld", "ls", "bb", "bi", "bs", "bcm", "bsc")
RAW_DIAG_PROBE_SUFFIXES = (
    "_p",
    "_e",
    "_d",
    "_c0r",
    "_c8r",
    "_c0",
    "_c8",
    "_miso_pull",
    "_miso_spi",
    "_miso_idle",
)
UNSAFE_SD_COMMAND_TOKENS = ("setup confirm", "storage format", "format confirm", "mkfs", "erase sd", "wipe sd")
SD_FILE_LIMITS = {
    "file_line_max": 512,
    "file_chunk_max": 192,
    "path_max": 96,
}
TOP_LEVEL_COMMIT_FIELDS = (
    "commit",
    "commit_sha",
    "commitSha",
    "head_sha",
    "headSha",
    "head_sha256",
    "git_commit",
    "gitCommit",
    "git_sha",
    "gitSha",
    "source_commit",
    "sourceCommit",
    "source_head_sha",
    "sourceHeadSha",
    "firmware_commit",
    "firmwareCommit",
    "firmware_head_sha",
    "firmwareHeadSha",
    "firmware_git_sha",
    "firmwareGitSha",
    "build_commit",
    "buildCommit",
    "build_head_sha",
    "buildHeadSha",
    "workflow_sha",
    "workflowSha",
    "github_sha",
    "githubSha",
)
NESTED_COMMIT_FIELDS = (
    "commit",
    "commit_sha",
    "commitSha",
    "head_sha",
    "headSha",
    "head_sha256",
    "git_commit",
    "gitCommit",
    "git_sha",
    "gitSha",
    "source_commit",
    "sourceCommit",
    "source_head_sha",
    "sourceHeadSha",
    "firmware_commit",
    "firmwareCommit",
    "firmware_head_sha",
    "firmwareHeadSha",
    "firmware_git_sha",
    "firmwareGitSha",
    "build_commit",
    "buildCommit",
    "build_head_sha",
    "buildHeadSha",
)
COMMIT_METADATA_CONTAINERS = ("git", "artifact", "firmware", "build", "source", "workflow", "github", "metadata")
GENERIC_SHA_COMMIT_CONTAINERS = {"git", "workflow", "github"}


def default_d1l_port() -> str:
    return "COM" + "12"


def default_meshbot_port() -> str:
    return "COM" + "11"


def default_rp2040_port() -> str:
    return "COM" + "16"


def default_hardware_dir() -> str:
    return str(Path("artifacts") / "hardware" / default_d1l_port().lower())


def default_rp2040_hardware_dir() -> str:
    return str(Path("artifacts") / "hardware" / default_rp2040_port().lower())


@dataclass
class GateResult:
    gate_id: str
    severity: str
    ok: bool
    title: str
    evidence: list[str]
    message: str
    details: dict[str, Any] | None = None

    def to_dict(self) -> dict:
        payload = {
            "id": self.gate_id,
            "severity": self.severity,
            "ok": self.ok,
            "title": self.title,
            "evidence": self.evidence,
            "message": self.message,
        }
        if self.details:
            payload["details"] = self.details
        return payload


def read_json(path: Path | None) -> dict:
    if not path or not path.is_file():
        return {}
    with path.open("r", encoding="utf-8") as fh:
        return json.load(fh)


def rel(path: Path, root: Path) -> str:
    try:
        return str(path.resolve().relative_to(root.resolve())).replace("\\", "/")
    except ValueError:
        return str(path)


def newest_file(root: Path, *patterns: str) -> Path | None:
    files: list[Path] = []
    for pattern in patterns:
        files.extend(path for path in root.glob(pattern) if path.is_file())
    if not files:
        return None
    return max(files, key=lambda path: path.stat().st_mtime)


def commit_file(root: Path, commit: str | None, *patterns: str) -> Path | None:
    if not commit:
        return newest_file(root, *patterns)
    short = commit[:7]
    commit_patterns = [pattern.replace("*", f"*{short}*", 1) for pattern in patterns]
    return newest_file(root, *commit_patterns)


def artifact_commit_values(data: dict) -> list[str]:
    if not isinstance(data, dict):
        return []
    values: list[Any] = [
        data.get(field)
        for field in TOP_LEVEL_COMMIT_FIELDS
    ]
    for container_name in COMMIT_METADATA_CONTAINERS:
        container = data.get(container_name)
        if isinstance(container, dict):
            values.extend(container.get(field) for field in NESTED_COMMIT_FIELDS)
            if container_name in GENERIC_SHA_COMMIT_CONTAINERS:
                values.append(container.get("sha"))
    return [value.strip() for value in values if isinstance(value, str) and value.strip()]


def commit_value_matches(value: str, commit: str) -> bool:
    full = commit.lower()
    short = full[:7]
    lowered = value.lower()
    return lowered == full or lowered.startswith(short)


def artifact_commit_matches(path: Path, data: dict, commit: str | None) -> bool:
    if not commit:
        return True
    metadata_values = artifact_commit_values(data)
    if metadata_values:
        return any(commit_value_matches(value, commit) for value in metadata_values)

    name = path.name.lower()
    full = commit.lower()
    short = full[:7]
    return short in name or full in name


def newest_commit_json(root: Path, commit: str | None, *patterns: str) -> Path | None:
    files: list[Path] = []
    for pattern in patterns:
        files.extend(path for path in root.glob(pattern) if path.is_file())
    for candidate in sorted(set(files), key=lambda path: path.stat().st_mtime, reverse=True):
        if artifact_commit_matches(candidate, read_json(candidate), commit):
            return candidate
    return None


def unique_dirs(paths: list[Path]) -> list[Path]:
    seen: set[str] = set()
    unique: list[Path] = []
    for path in paths:
        try:
            key = str(path.resolve()).lower()
        except OSError:
            key = str(path).lower()
        if key in seen:
            continue
        seen.add(key)
        unique.append(path)
    return unique


def sd_artifact_roots(
    root: Path,
    github_run_dir: Path | None,
    hardware_dir: Path,
    *artifact_dirs: str,
) -> list[Path]:
    roots = [hardware_dir]
    for artifact_dir in artifact_dirs:
        roots.append(root / "artifacts" / artifact_dir)
        if github_run_dir:
            roots.append(github_run_dir / "d1l-host-artifacts" / artifact_dir)
    return unique_dirs(roots)


def newest_commit_json_from_roots(roots: list[Path], commit: str | None, *patterns: str) -> Path | None:
    files: list[Path] = []
    for root in roots:
        for pattern in patterns:
            files.extend(path for path in root.glob(pattern) if path.is_file())
    for candidate in sorted(set(files), key=lambda path: path.stat().st_mtime, reverse=True):
        if artifact_commit_matches(candidate, read_json(candidate), commit):
            return candidate
    return None


def find_release_package(github_run_dir: Path | None) -> Path | None:
    if not github_run_dir:
        return None
    package_root = github_run_dir / "d1l-release-package"
    if not package_root.is_dir():
        return None
    candidates = [path for path in package_root.iterdir() if path.is_dir() and path.name.startswith("d1l-release-")]
    if not candidates:
        return None
    return max(candidates, key=lambda path: path.stat().st_mtime)


def checksum_gate(github_run_dir: Path | None, root: Path) -> GateResult:
    manifests: list[Path] = []
    labels = [
        "d1l-firmware-artifacts/SHA256SUMS.txt",
        "rp2040-sd-bridge-firmware/SHA256SUMS.txt",
        "rp2040-seeed-official-sd-smoke-firmware/SHA256SUMS.txt",
    ]
    package = None
    if github_run_dir:
        manifests.extend(github_run_dir / label for label in labels)
        package = find_release_package(github_run_dir)
        if package:
            manifests.append(package / "SHA256SUMS.txt")
    present = [path for path in manifests if path.is_file()]
    missing = [path for path in manifests if not path.is_file()]
    failed = [path for path in present if not verify_sha256_manifest(path)]
    expected_count = len(labels) + (1 if package else 0)
    ok = bool(present) and not missing and not failed and len(present) == expected_count
    return GateResult(
        "ci_artifacts_checksums",
        "P0",
        ok,
        "GitHub Actions artifacts and checksums",
        [rel(path, root) for path in present],
        "All required downloaded Actions checksum manifests verify."
        if ok else "Downloaded Actions artifacts are missing or have a checksum failure.",
        {
            "missing": [rel(path, root) for path in missing],
            "failed": [rel(path, root) for path in failed],
        },
    )


def notices_gate(github_run_dir: Path | None, root: Path) -> GateResult:
    package = find_release_package(github_run_dir) if github_run_dir else None
    manifest = read_json(package / "manifest.json" if package else None)
    notice_entries = manifest.get("notice_files") if isinstance(manifest.get("notice_files"), list) else []
    listed = {entry.get("path") for entry in notice_entries if isinstance(entry, dict)}
    existing = {path for path in REQUIRED_NOTICE_FILES if package and (package / path).is_file()}
    missing = sorted(REQUIRED_NOTICE_FILES - listed)
    missing_files = sorted(REQUIRED_NOTICE_FILES - existing)
    ok = package is not None and not missing and not missing_files
    evidence = [rel(package / path, root) for path in sorted(existing)] if package else []
    if package and (package / "manifest.json").is_file():
        evidence.append(rel(package / "manifest.json", root))
    return GateResult(
        "release_notices_included",
        "P0",
        ok,
        "Third-party notices and attributions packaged",
        evidence,
        "Release package includes all required notice and attribution documents."
        if ok else "Release package is missing one or more required notice/attribution files.",
        {"missing_manifest_entries": missing, "missing_files": missing_files},
    )


def simple_json_ok_gate(
    gate_id: str,
    title: str,
    path: Path | None,
    root: Path,
    predicate,
    success: str,
    failure: str,
    severity: str = "P0",
) -> GateResult:
    data = read_json(path)
    ok = bool(path and data and predicate(data))
    return GateResult(
        gate_id,
        severity,
        ok,
        title,
        [rel(path, root)] if path else [],
        success if ok else failure,
        {"path_found": bool(path), "artifact_ok": data.get("ok") if data else None},
    )


def all_checks_true(data: dict) -> bool:
    checks = data.get("checks")
    return isinstance(checks, dict) and bool(checks) and all(value is True for value in checks.values())


def ui_corruption_probe_ok(data: dict, expected_port: str) -> bool:
    telemetry = data.get("telemetry") if isinstance(data.get("telemetry"), dict) else {}
    telemetry_fields = set(telemetry.get("telemetry_fields") or data.get("telemetry_fields") or [])
    checks = data.get("checks") if isinstance(data.get("checks"), dict) else {}
    return (
        data.get("ok") is True
        and data.get("port") == expected_port
        and data.get("public_rf_tx") is False
        and data.get("formats_sd") is False
        and data.get("skip_data_canary") is not True
        and int(data.get("rounds") or 0) >= RELEASE_UI_CORRUPTION_MIN_ROUNDS
        and int(data.get("data_refresh_events") or 0) >= int(data.get("rounds") or 0)
        and int(data.get("failure_count") or 0) == 0
        and int(telemetry.get("health_sample_count") or 0) > 0
        and telemetry.get("uptime_monotonic") is True
        and REQUIRED_UI_TELEMETRY_FIELDS.issubset(telemetry_fields)
        and checks.get("tab_switches_settle") is True
        and checks.get("data_refresh_exercised") is True
        and checks.get("data_refreshes_pass") is True
        and checks.get("no_public_rf") is True
        and checks.get("no_formatting") is True
    )


def ui_pixel_capture_ok(data: dict, expected_port: str) -> bool:
    return (
        data.get("ok") is True
        and data.get("kind") == "ui_pixel_capture"
        and data.get("mode") == "hardware"
        and data.get("port") == expected_port
        and data.get("public_rf_tx") is False
        and data.get("formats_sd") is False
        and data.get("width") == 480
        and data.get("height") == 480
        and data.get("bytes_per_pixel") == 2
        and data.get("pixel_format") == "rgb565-le"
        and data.get("total_bytes") == 480 * 480 * 2
        and data.get("captured_bytes") == 480 * 480 * 2
        and int(data.get("chunk_count") or 0) > 0
        and bool(data.get("crc32"))
        and bool(data.get("png_path"))
        and bool(data.get("raw_path"))
    )


def compose_capture_probe_ok(probe: dict, expected_target: str) -> bool:
    if not isinstance(probe, dict):
        return False
    keyboard = probe.get("keyboard") if isinstance(probe.get("keyboard"), dict) else {}
    textarea = probe.get("textarea") if isinstance(probe.get("textarea"), dict) else {}
    sheet = probe.get("sheet") if isinstance(probe.get("sheet"), dict) else {}
    try:
        keyboard_inside_sheet = (
            int(keyboard.get("x")) >= 0
            and int(keyboard.get("y")) >= 0
            and int(keyboard.get("x")) + int(keyboard.get("w")) <= int(sheet.get("w"))
            and int(keyboard.get("y")) + int(keyboard.get("h")) <= int(sheet.get("h"))
        )
        textarea_above_keyboard = (
            int(textarea.get("x")) >= 0
            and int(textarea.get("y")) >= 0
            and int(textarea.get("x")) + int(textarea.get("w")) <= int(sheet.get("w"))
            and int(textarea.get("y")) + int(textarea.get("h")) <= int(keyboard.get("y"))
        )
        keyboard_size_ok = int(keyboard.get("w")) >= 440 and int(keyboard.get("h")) >= 250
    except (TypeError, ValueError):
        return False
    return (
        probe.get("ok") is True
        and probe.get("cmd") == "ui compose-probe"
        and probe.get("target") == expected_target.replace("-", "_")
        and probe.get("target_supported") is True
        and probe.get("sheet_visible") is True
        and probe.get("textarea_visible") is True
        and probe.get("keyboard_visible") is True
        and probe.get("onboarding_visible") is False
        and probe.get("dock_hidden") is True
        and probe.get("dm_mode") is (expected_target.startswith("dm"))
        and probe.get("public_rf_tx") is False
        and probe.get("formats_sd") is False
        and keyboard_inside_sheet
        and textarea_above_keyboard
        and keyboard_size_ok
    )


def compose_keyboard_capture_ok(data: dict, expected_port: str) -> bool:
    captures = data.get("captures") if isinstance(data.get("captures"), list) else []
    captures_by_target = {
        item.get("target"): item
        for item in captures
        if isinstance(item, dict) and isinstance(item.get("target"), str)
    }
    if not REQUIRED_COMPOSE_CAPTURE_TARGETS.issubset(set(captures_by_target)):
        return False
    return (
        data.get("ok") is True
        and data.get("kind") == "ui_compose_keyboard_capture"
        and data.get("mode") == "hardware"
        and data.get("port") == expected_port
        and data.get("public_rf_tx") is False
        and data.get("formats_sd") is False
        and all(
            capture.get("ok") is True
            and capture.get("public_rf_tx") is False
            and capture.get("formats_sd") is False
            and bool(capture.get("png_path"))
            and bool(capture.get("raw_path"))
            and isinstance(capture.get("capture"), dict)
            and capture["capture"].get("ok") is True
            and capture["capture"].get("kind") == "ui_pixel_capture"
            and capture["capture"].get("port") == expected_port
            and capture["capture"].get("width") == 480
            and capture["capture"].get("height") == 480
            and capture["capture"].get("onboarding_visible") is False
            and capture.get("target_visible") is True
            and compose_capture_probe_ok(capture.get("compose_probe"), target)
            for target, capture in captures_by_target.items()
            if target in REQUIRED_COMPOSE_CAPTURE_TARGETS
        )
    )


def scroll_probe_results_by_screen(data: dict) -> dict:
    probes: dict = {}
    probe_results = data.get("probe_results")
    if isinstance(probe_results, dict):
        for screen, probe in probe_results.items():
            if isinstance(screen, str) and isinstance(probe, dict):
                probes[screen] = probe
    events = data.get("events")
    if isinstance(events, list):
        for event in events:
            if not isinstance(event, dict):
                continue
            screen = event.get("screen")
            probe = event.get("probe")
            if isinstance(screen, str) and isinstance(probe, dict):
                probes[screen] = probe
    return probes


def scroll_probe_ok(data: dict, expected_port: str) -> bool:
    screens = set(data.get("screens") or [])
    plan = data.get("surface_plan") if isinstance(data.get("surface_plan"), list) else []
    tabs_by_screen = {
        item.get("screen"): item.get("tab")
        for item in plan
        if isinstance(item, dict) and isinstance(item.get("screen"), str)
    }
    probes = scroll_probe_results_by_screen(data)
    return (
        data.get("ok") is True
        and data.get("port") == expected_port
        and int(data.get("failure_count") or 0) == 0
        and REQUIRED_SCROLL_SCREENS.issubset(screens)
        and all(tabs_by_screen.get(screen) == tab for screen, tab in REQUIRED_SCROLL_SURFACES.items())
        and all(
            isinstance(probes.get(screen), dict)
            and probes[screen].get("ok") is True
            and probes[screen].get("surface") == screen
            and probes[screen].get("tab") == tab
            and probes[screen].get("target_found") is True
            and probes[screen].get("scrollable") is True
            and probes[screen].get("moved") is True
            for screen, tab in REQUIRED_SCROLL_SURFACES.items()
        )
    )


def dm_probe_ok(data: dict, expected_port: str, expected_bot_port: str) -> bool:
    return (
        data.get("ok") is True
        and data.get("port") == expected_port
        and data.get("meshbot_expected_port") == expected_bot_port
        and data.get("public_rf_transmit") is False
        and all_checks_true(data)
    )


def route_probe_ok(data: dict, expected_port: str) -> bool:
    checks = data.get("checks") if isinstance(data.get("checks"), dict) else {}
    return (
        data.get("ok") is True
        and data.get("mode") == "hardware-route-probe"
        and data.get("port") == expected_port
        and data.get("dm_rf_tx") is True
        and data.get("public_rf_tx") is False
        and data.get("formats_sd") is False
        and all(checks.get(name) is True for name in REQUIRED_ROUTE_PROBE_CHECKS)
    )


def route_probe_gate(hardware_dir: Path, root: Path, commit: str | None, expected_port: str) -> GateResult:
    probe = newest_commit_json(hardware_dir, commit, "routes_probe_*.json")
    data = read_json(probe)
    checks = data.get("checks") if isinstance(data.get("checks"), dict) else {}
    missing_or_failed = sorted(name for name in REQUIRED_ROUTE_PROBE_CHECKS if checks.get(name) is not True)
    ok = bool(probe and data and route_probe_ok(data, expected_port))
    return GateResult(
        "supplemental_dm_route_probe",
        "P1",
        ok,
        "Supplemental DM-only route active probe",
        [rel(probe, root)] if probe else [],
        "Supplemental DM-only active route probe evidence is present; full RF acceptance still requires inbound DM, ACK/PATH, and direct-route proof."
        if ok else "No passing supplemental DM-only route probe artifact was found.",
        {
            "path_found": bool(probe),
            "artifact_ok": data.get("ok") if data else None,
            "mode": data.get("mode") if data else None,
            "dm_rf_tx": data.get("dm_rf_tx") if data else None,
            "public_rf_tx": data.get("public_rf_tx") if data else None,
            "formats_sd": data.get("formats_sd") if data else None,
            "missing_or_failed_checks": missing_or_failed,
            "scope": "supplementary_dm_only_not_full_rf_acceptance",
        },
    )


def iter_nested_dicts(value: Any):
    if isinstance(value, dict):
        yield value
        for nested in value.values():
            yield from iter_nested_dicts(nested)
    elif isinstance(value, list):
        for nested in value:
            yield from iter_nested_dicts(nested)
    elif isinstance(value, str):
        text = value.strip()
        if text.startswith("{") and text.endswith("}"):
            try:
                parsed = json.loads(text)
            except json.JSONDecodeError:
                return
            yield from iter_nested_dicts(parsed)


def first_official_seeed_smoke_payload(data: dict) -> dict:
    for candidate in iter_nested_dicts(data):
        if candidate.get("test") == "seeed_official_sd_smoke":
            return candidate
    return {}


def int_value(value: Any) -> int | None:
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def first_number(value: Any) -> float | None:
    if isinstance(value, (int, float)):
        return float(value)
    if isinstance(value, str):
        match = re.search(r"-?\d+(?:\.\d+)?", value)
        if match:
            try:
                return float(match.group(0))
            except ValueError:
                return None
    return None


def bool_field(data: dict, *names: str) -> bool:
    containers = [data]
    for key in ("checks", "measurements", "evidence"):
        nested = data.get(key)
        if isinstance(nested, dict):
            containers.append(nested)
    return any(container.get(name) is True for container in containers for name in names)


def first_numeric_field(data: dict, *names: str) -> float | None:
    containers = [data]
    for key in ("checks", "measurements", "evidence"):
        nested = data.get(key)
        if isinstance(nested, dict):
            containers.append(nested)
    for container in containers:
        for name in names:
            number = first_number(container.get(name))
            if number is not None:
                return number
    return None


def path_for_details(path: Path | None, root: Path) -> str | None:
    return rel(path, root) if path else None


def exact_port_ok(data: dict, expected_port: str) -> bool:
    return data.get("port") == expected_port


def optional_port_ok(data: dict, expected_port: str) -> bool:
    port = data.get("port")
    return port in (None, expected_port)


def nested_flag_true(data: dict, *flags: str) -> bool:
    for candidate in iter_nested_dicts(data):
        for flag in flags:
            if candidate.get(flag) is True:
                return True
    return False


def report_is_no_rf_no_format(data: dict) -> bool:
    return (
        data.get("public_rf_tx") is False
        and data.get("formats_sd") is False
        and not nested_flag_true(data, "public_rf_tx", "formats_sd", "format_performed", "format_confirmed")
    )


def unsafe_sd_commands(data: dict) -> list[str]:
    commands = data.get("commands")
    if not isinstance(commands, list):
        return []
    unsafe: list[str] = []
    for command in commands:
        if not isinstance(command, str):
            continue
        lowered = command.lower()
        if any(token in lowered for token in UNSAFE_SD_COMMAND_TOKENS):
            unsafe.append(command)
    return unsafe


def storage_file_gate_ready_status(storage_status: dict) -> bool:
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
        and all(int_value(sd.get(key)) is not None and int_value(sd.get(key)) >= minimum for key, minimum in SD_FILE_LIMITS.items())
    )


def storage_setup_no_format_policy(storage_setup: dict | None) -> bool:
    if not isinstance(storage_setup, dict):
        return False
    return (
        storage_setup.get("policy") == "no_device_format"
        and storage_setup.get("will_format") is False
        and storage_setup.get("format_performed") is False
    )


def sd_file_canary_artifact_ok(data: dict, expected_port: str) -> bool:
    return (
        data.get("mode") == "hardware"
        and exact_port_ok(data, expected_port)
        and report_is_no_rf_no_format(data)
        and not unsafe_sd_commands(data)
        and data.get("ok") is True
        and data.get("canary_passed") is True
        and data.get("canary_unavailable_ok") is not True
        and data.get("allow_unavailable") is not True
        and data.get("storage_file_gate_ready_before") is True
        and data.get("storage_file_gate_ready_after") is True
    )


def sd_retained_canary_artifact_ok(data: dict, expected_port: str) -> bool:
    return (
        data.get("mode") == "hardware"
        and exact_port_ok(data, expected_port)
        and report_is_no_rf_no_format(data)
        and not unsafe_sd_commands(data)
        and data.get("ok") is True
        and data.get("allow_unavailable") is not True
        and data.get("retained_canary_passed") is True
        and data.get("filecanary_passed") is True
        and data.get("pre_reboot_readbacks_ok") is True
        and data.get("post_reboot_readbacks_ok") is True
        and "reboot" in (data.get("commands") or [])
    )


def sd_reboot_remount_artifact_ok(data: dict, expected_port: str) -> bool:
    return (
        data.get("mode") == "hardware"
        and exact_port_ok(data, expected_port)
        and report_is_no_rf_no_format(data)
        and not unsafe_sd_commands(data)
        and data.get("ok") is True
        and data.get("pre_remount_ready") is True
        and data.get("post_remount_ready") is True
        and data.get("retained_canary_passed") is True
        and data.get("pre_reboot_readbacks_ok") is True
        and data.get("post_reboot_readbacks_ok") is True
        and data.get("pre_map_tile_canary_passed") is True
        and data.get("post_map_tile_canary_passed") is True
        and "reboot" in (data.get("commands") or [])
    )


def sd_map_tile_canary_artifact_ok(data: dict, expected_port: str) -> bool:
    return (
        data.get("mode") == "hardware"
        and exact_port_ok(data, expected_port)
        and report_is_no_rf_no_format(data)
        and not unsafe_sd_commands(data)
        and data.get("ok") is True
        and data.get("canary_passed") is True
        and data.get("canary_unavailable_ok") is not True
        and data.get("allow_unavailable") is not True
        and data.get("storage_file_gate_ready_before") is True
        and data.get("storage_file_gate_ready_after") is True
        and data.get("map_tile_backend_ready_before") is True
        and data.get("map_tile_backend_ready_after") is True
    )


def raw_diag_line(data: dict) -> str:
    for key in ("raw_line", "diag"):
        value = data.get(key)
        if isinstance(value, str) and "DESKOS_SD_DIAG" in value:
            return value
    lines = data.get("lines")
    if isinstance(lines, list):
        for line in lines:
            if isinstance(line, str) and "DESKOS_SD_DIAG" in line:
                return line
    storage_diag = data.get("storage_diag")
    if isinstance(storage_diag, dict):
        value = storage_diag.get("raw_line")
        if isinstance(value, str) and "DESKOS_SD_DIAG" in value:
            return value
    return ""


def raw_diag_fields(data: dict) -> dict[str, str]:
    fields = data.get("fields")
    if isinstance(fields, dict):
        return {str(key): str(value) for key, value in fields.items()}
    line = raw_diag_line(data)
    parsed: dict[str, str] = {}
    for token in line.split():
        if "=" not in token:
            continue
        key, value = token.split("=", 1)
        parsed[key] = value
    return parsed


def raw_diag_complete(data: dict, expected_port: str) -> bool:
    fields = raw_diag_fields(data)
    required_fields = {"pins", "hz", "pin_sck", "pin_mosi", "pin_miso", "pin_cs", "selected_power", "selected_mode"}
    required_fields.update(f"{prefix}{suffix}" for prefix in RAW_DIAG_PROBE_PREFIXES for suffix in RAW_DIAG_PROBE_SUFFIXES)
    return (
        data.get("ok") is True
        and optional_port_ok(data, expected_port)
        and report_is_no_rf_no_format(data)
        and data.get("response_truncated") is not True
        and raw_diag_line(data).startswith("DESKOS_SD_DIAG")
        and required_fields.issubset(fields)
    )


def sd_boot_prepare_artifact_ok(data: dict, scenario: str, expected_port: str) -> bool:
    classification = data.get("classification")
    storage_after = data.get("storage_after") if isinstance(data.get("storage_after"), dict) else {}
    file_gate_ok = data.get("storage_file_gate_ready") is True or storage_file_gate_ready_status(storage_after)
    format_policy_ok = (
        scenario not in {"unformatted", "existing-data"}
        or data.get("format_allowed") is False
        or storage_setup_no_format_policy(data.get("storage_setup") if isinstance(data.get("storage_setup"), dict) else None)
    )
    if scenario in {"correct-structure", "missing-structure"}:
        format_policy_ok = format_policy_ok and file_gate_ok and data.get("retained_store_gate_ready") is True
        format_policy_ok = format_policy_ok and data.get("filecanary_passed") is True
    if scenario == "unformatted":
        format_policy_ok = format_policy_ok and storage_setup_no_format_policy(
            data.get("storage_setup") if isinstance(data.get("storage_setup"), dict) else None
        )
    return (
        data.get("mode") == "hardware"
        and exact_port_ok(data, expected_port)
        and data.get("scenario") == scenario
        and data.get("ok") is True
        and report_is_no_rf_no_format(data)
        and data.get("commands_safe") is True
        and not unsafe_sd_commands(data)
        and classification in SD_BOOT_PREPARE_EXPECTED_CLASSIFICATIONS[scenario]
        and format_policy_ok
    )


def sd_power_rail_evidence_ok(data: dict, expected_port: str) -> bool:
    vcc = first_numeric_field(
        data,
        "vcc_at_socket_volts",
        "sd_vcc_volts",
        "vcc_after_gpio18_high_volts",
        "sd_power_rail_volts",
    )
    vcc_ok = vcc is not None and 3.0 <= vcc <= 3.6
    return (
        data.get("ok") is True
        and optional_port_ok(data, expected_port)
        and report_is_no_rf_no_format(data)
        and (data.get("mode") in (None, "hardware", "hardware-electrical", "electrical"))
        and (data.get("sd_power_rail_measured") is True or bool_field(data, "sd_power_rail_measured", "power_rail_measured"))
        and vcc_ok
        and bool_field(data, "gpio18_high", "gpio18_asserted")
        and bool_field(data, "gnd_continuity", "ground_continuity_ok")
        and bool_field(data, "cs_toggles_at_socket", "cs_toggle_seen")
        and bool_field(data, "sck_clocks_at_socket", "sck_clocks_seen")
        and bool_field(data, "mosi_commands_at_socket", "mosi_commands_seen")
        and bool_field(data, "miso_response_or_idle_high", "miso_idle_high", "miso_response_seen")
        and bool(data.get("sku") or data.get("board_revision") or data.get("board"))
    )


def card_capacity_gb(card: dict) -> float | None:
    for key in ("capacity_gb", "card_gb", "size_gb", "max_card_gb"):
        value = first_number(card.get(key))
        if value is not None:
            return value
    return None


def card_is_fat32(card: dict) -> bool:
    fat_type = int_value(card.get("fat_type"))
    if fat_type == 32:
        return True
    for key in ("fs", "format", "filesystem"):
        value = card.get(key)
        if isinstance(value, str) and value.lower() == "fat32":
            return True
    return False


def card_matrix_entries(data: dict) -> list[dict]:
    for key in ("cards", "matrix", "results"):
        value = data.get(key)
        if isinstance(value, list):
            return [item for item in value if isinstance(item, dict)]
    return []


def sd_matrix_artifact_ok(data: dict, expected_port: str) -> bool:
    cards = card_matrix_entries(data)
    capacities = [card_capacity_gb(card) for card in cards]
    return (
        data.get("ok") is True
        and optional_port_ok(data, expected_port)
        and report_is_no_rf_no_format(data)
        and not unsafe_sd_commands(data)
        and len(cards) >= 3
        and all(capacity is not None and capacity <= 32 for capacity in capacities)
        and any(capacity == 32 for capacity in capacities)
        and all(card_is_fat32(card) for card in cards)
        and all(card.get("ok") is True or card.get("official_seeed_smoke_passed") is True for card in cards)
        and all(card.get("formats_sd") is not True and card.get("public_rf_tx") is not True for card in cards)
    )


def official_seeed_sd_smoke_ok(data: dict, expected_port: str) -> bool:
    payload = first_official_seeed_smoke_payload(data)
    required_steps = ("mount", "root_open", "mkdir", "write", "read", "rename", "stat", "delete")
    max_card_gb = int_value(payload.get("max_card_gb"))
    fat_type = int_value(payload.get("fat_type"))
    port = data.get("port")
    port_ok = port in (None, expected_port)
    return (
        port_ok
        and payload.get("ok") is True
        and all(payload.get(step) is True for step in required_steps)
        and payload.get("public_rf_tx") is False
        and payload.get("formats_sd") is False
        and payload.get("format") == "non_destructive"
        and max_card_gb is not None
        and max_card_gb <= 32
        and fat_type == 32
    )


def official_seeed_sd_smoke_gate(
    artifact_roots: list[Path],
    root: Path,
    commit: str | None,
    expected_port: str,
) -> GateResult:
    smoke = newest_commit_json_from_roots(
        artifact_roots,
        commit,
        "seeed_official_sd_smoke_*.json",
        "sd_official_seeed_smoke_*.json",
        "rp2040_seeed_official_sd_smoke_*.json",
    )
    data = read_json(smoke)
    payload = first_official_seeed_smoke_payload(data)
    ok = bool(smoke and data and official_seeed_sd_smoke_ok(data, expected_port))
    required_steps = ("mount", "root_open", "mkdir", "write", "read", "rename", "stat", "delete")
    raw_fields = (
        "diag_ran",
        "raw_present",
        "raw_cmd8_echo_ok",
        "raw_acmd41_ready",
        "raw_err",
        "raw_data",
        "raw_miso_pullup",
        "raw_miso_idle",
        "raw_idle_ff",
        "raw_cmd0_ready",
        "raw_cmd0",
        "raw_cmd8_ready",
        "raw_cmd8",
        "raw_r70",
        "raw_r71",
        "raw_r72",
        "raw_r73",
        "raw_cmd55",
        "raw_acmd41",
        "raw_acmd41_attempts",
        "raw_ocr0",
        "raw_ocr1",
        "raw_ocr2",
        "raw_ocr3",
    )
    return GateResult(
        "sd_official_seeed_smoke_passed",
        "P0",
        ok,
        "Official Seeed RP2040 SD smoke",
        [rel(smoke, root)] if smoke else [],
        "Official Seeed SD smoke proves mount, root open, write/read/rename/stat/delete, FAT32, <=32GB, and no formatting."
        if ok else "No passing current-commit official Seeed SD smoke artifact was found.",
        {
            "path_found": bool(smoke),
            "artifact_ok": data.get("ok") if data else None,
            "inner_test": payload.get("test") if payload else None,
            "inner_ok": payload.get("ok") if payload else None,
            "port": data.get("port") if data else None,
            "required_steps": {step: payload.get(step) for step in required_steps} if payload else {},
            "fat_type": payload.get("fat_type") if payload else None,
            "max_card_gb": payload.get("max_card_gb") if payload else None,
            "fat32": payload.get("fat32") if payload else None,
            "needs_fat32": payload.get("needs_fat32") if payload else None,
            "will_format": payload.get("will_format") if payload else None,
            "format_performed": payload.get("format_performed") if payload else None,
            "detect_used_for_ok": payload.get("detect_used_for_ok") if payload else None,
            "power_measured": payload.get("power_measured") if payload else None,
            "power_state": payload.get("power_state") if payload else None,
            "detect": payload.get("detect") if payload else None,
            "detect_pullup": payload.get("detect_pullup") if payload else None,
            "detect_pulldown": payload.get("detect_pulldown") if payload else None,
            "raw_diagnostics": {field: payload.get(field) for field in raw_fields} if payload else {},
            "formats_sd": payload.get("formats_sd") if payload else data.get("formats_sd") if data else None,
            "public_rf_tx": payload.get("public_rf_tx") if payload else data.get("public_rf_tx") if data else None,
        },
    )


def sd_next_action(preflight: dict, classification: dict) -> str:
    value = classification.get("next_action")
    if value is None:
        value = preflight.get("next_action")
    return str(value or "")


def sd_next_action_is_obsolete(next_action: str) -> bool:
    lowered = next_action.lower()
    return any(token in lowered for token in OBSOLETE_SD_NEXT_ACTION_TOKENS)


def sanitized_sd_classification(preflight: dict, classification: dict) -> tuple[dict, bool]:
    sanitized = dict(classification)
    raw_next_action = sd_next_action(preflight, classification)
    obsolete = sd_next_action_is_obsolete(raw_next_action)
    if obsolete:
        sanitized["next_action"] = SAFE_SD_NEXT_ACTION
        sanitized["policy"] = "no_device_format"
        sanitized["obsolete_format_action_blocked"] = True
    return sanitized, obsolete


def sd_gate(preflight_path: Path | None, root: Path) -> GateResult:
    preflight = read_json(preflight_path)
    classification = preflight.get("classification") if isinstance(preflight.get("classification"), dict) else {}
    classification, obsolete_format_action = sanitized_sd_classification(preflight, classification)
    ready = preflight.get("ready_for_sd_acceptance") is True and classification.get("storage_file_gate_ready") is True
    no_device_format_policy_ok = preflight.get("formats_sd") is False and not obsolete_format_action
    ok = bool(preflight_path and preflight.get("ok") is True and ready and no_device_format_policy_ok)
    details = {
        "ready_for_sd_acceptance": preflight.get("ready_for_sd_acceptance"),
        "classification": classification,
        "candidate_volume_count": len(preflight.get("candidate_volumes") or []),
        "artifact_sha256": (preflight.get("artifact") or {}).get("sha256"),
        "formats_sd": preflight.get("formats_sd"),
        "public_rf_tx": preflight.get("public_rf_tx"),
        "copies_uf2": preflight.get("copies_uf2"),
        "no_device_format_policy_ok": no_device_format_policy_ok,
        "obsolete_format_action_blocked": obsolete_format_action,
    }
    if obsolete_format_action:
        message = (
            "SD matrix is not release-ready; stale format-capable SD evidence is obsolete. "
            "Refresh under the no-device-format policy with a FAT32 card prepared on a computer."
        )
    elif ok:
        message = "SD preflight is ready for acceptance matrix; run and attach the full matrix artifacts."
    else:
        message = "SD matrix is not release-ready; RP2040/SD file gate or follow-up matrix evidence is missing."
    return GateResult(
        "sd_acceptance_matrix",
        "P0",
        ok,
        "SD auto-prepare, retained stores, exports, map tiles, and reboot/remount",
        [rel(preflight_path, root)] if preflight_path else [],
        message,
        details,
    )


def sd_filecanary_gate(artifact_roots: list[Path], root: Path, commit: str | None, expected_port: str) -> GateResult:
    canary = newest_commit_json_from_roots(
        artifact_roots,
        commit,
        "d1l-sd-file-canary-*.json",
        "sd_file_canary_*.json",
        "sd_filecanary_*.json",
        "filecanary_*.json",
    )
    data = read_json(canary)
    ok = bool(canary and data and sd_file_canary_artifact_ok(data, expected_port))
    return GateResult(
        "sd_filecanary_independent",
        "P0",
        ok,
        "Independent RP2040 SD file-operation canary",
        [rel(canary, root)] if canary else [],
        "Raw RP2040 file-operation canary passes without requiring retained-store migration."
        if ok else "No passing current-commit hardware SD file-operation canary artifact was found.",
        {
            "path_found": bool(canary),
            "artifact_ok": data.get("ok") if data else None,
            "mode": data.get("mode") if data else None,
            "port": data.get("port") if data else None,
            "canary_passed": data.get("canary_passed") if data else None,
            "storage_file_gate_ready_before": data.get("storage_file_gate_ready_before") if data else None,
            "storage_file_gate_ready_after": data.get("storage_file_gate_ready_after") if data else None,
            "retained_history_sd_ready_before": data.get("retained_history_sd_ready_before") if data else None,
            "retained_history_sd_ready_after": data.get("retained_history_sd_ready_after") if data else None,
        },
    )


def sd_retained_canary_gate(artifact_roots: list[Path], root: Path, commit: str | None, expected_port: str) -> GateResult:
    retained = newest_commit_json_from_roots(
        artifact_roots,
        commit,
        "d1l-sd-retained-history-*.json",
        "sd_retained_history_*.json",
        "sd_retained_canary_*.json",
        "retained_history_*.json",
    )
    data = read_json(retained)
    ok = bool(retained and data and sd_retained_canary_artifact_ok(data, expected_port))
    return GateResult(
        "sd_retained_canary_passed",
        "P0",
        ok,
        "SD retained-history canary",
        [rel(retained, root)] if retained else [],
        "Retained Public/DM/routes/packet canary survives reboot and readback without Public RF or formatting."
        if ok else "No passing current-commit hardware retained-history SD canary artifact was found.",
        {
            "path_found": bool(retained),
            "artifact_ok": data.get("ok") if data else None,
            "mode": data.get("mode") if data else None,
            "port": data.get("port") if data else None,
            "retained_canary_passed": data.get("retained_canary_passed") if data else None,
            "filecanary_passed": data.get("filecanary_passed") if data else None,
            "pre_reboot_readbacks_ok": data.get("pre_reboot_readbacks_ok") if data else None,
            "post_reboot_readbacks_ok": data.get("post_reboot_readbacks_ok") if data else None,
        },
    )


def sd_reboot_remount_gate(artifact_roots: list[Path], root: Path, commit: str | None, expected_port: str) -> GateResult:
    remount = newest_commit_json_from_roots(
        artifact_roots,
        commit,
        "d1l-sd-reboot-remount-*.json",
        "sd_reboot_remount_*.json",
        "reboot_remount_*.json",
    )
    data = read_json(remount)
    ok = bool(remount and data and sd_reboot_remount_artifact_ok(data, expected_port))
    return GateResult(
        "sd_reboot_remount_passed",
        "P0",
        ok,
        "SD reboot/remount convergence",
        [rel(remount, root)] if remount else [],
        "SD retained data and map-tile canary survive reboot/remount without Public RF or formatting."
        if ok else "No passing current-commit hardware SD reboot/remount artifact was found.",
        {
            "path_found": bool(remount),
            "artifact_ok": data.get("ok") if data else None,
            "mode": data.get("mode") if data else None,
            "port": data.get("port") if data else None,
            "pre_remount_ready": data.get("pre_remount_ready") if data else None,
            "post_remount_ready": data.get("post_remount_ready") if data else None,
            "pre_map_tile_canary_passed": data.get("pre_map_tile_canary_passed") if data else None,
            "post_map_tile_canary_passed": data.get("post_map_tile_canary_passed") if data else None,
        },
    )


def sd_map_tile_canary_gate(artifact_roots: list[Path], root: Path, commit: str | None, expected_port: str) -> GateResult:
    canary = newest_commit_json_from_roots(
        artifact_roots,
        commit,
        "d1l-sd-map-tile-canary-*.json",
        "sd_map_tile_canary_*.json",
        "map_tile_canary_*.json",
    )
    data = read_json(canary)
    ok = bool(canary and data and sd_map_tile_canary_artifact_ok(data, expected_port))
    return GateResult(
        "sd_map_tile_canary_passed",
        "P0",
        ok,
        "SD map-tile cache canary",
        [rel(canary, root)] if canary else [],
        "Map-tile cache canary writes through temp/read/atomic-rename/final-read on SD."
        if ok else "No passing current-commit hardware SD map-tile canary artifact was found.",
        {
            "path_found": bool(canary),
            "artifact_ok": data.get("ok") if data else None,
            "mode": data.get("mode") if data else None,
            "port": data.get("port") if data else None,
            "canary_passed": data.get("canary_passed") if data else None,
            "map_tile_backend_ready_before": data.get("map_tile_backend_ready_before") if data else None,
            "map_tile_backend_ready_after": data.get("map_tile_backend_ready_after") if data else None,
        },
    )


def sd_raw_diag_gate(artifact_roots: list[Path], root: Path, commit: str | None, expected_port: str) -> GateResult:
    diag = newest_commit_json_from_roots(
        artifact_roots,
        commit,
        "storage_diag_raw_*.json",
        "sd_diag_raw_*.json",
        "rp2040_raw_diag_*.json",
        "rp2040_direct_diag_*.json",
        "rp2040_direct_diag_poll_*.json",
        "rp2040_direct_preflight_*.json",
    )
    data = read_json(diag)
    fields = raw_diag_fields(data)
    missing_prefixes = [
        prefix
        for prefix in RAW_DIAG_PROBE_PREFIXES
        if not all(f"{prefix}{suffix}" in fields for suffix in RAW_DIAG_PROBE_SUFFIXES)
    ]
    ok = bool(diag and data and raw_diag_complete(data, expected_port))
    return GateResult(
        "sd_raw_diag_complete",
        "P0",
        ok,
        "Raw RP2040 SD diagnostic matrix",
        [rel(diag, root)] if diag else [],
        "Raw SD diagnostic artifact includes normal, power, bitbang, inverted-CS, swapped-pin, command, and MISO fields."
        if ok else "No complete current-commit raw SD diagnostic artifact was found.",
        {
            "path_found": bool(diag),
            "artifact_ok": data.get("ok") if data else None,
            "port": data.get("port") if data else None,
            "response_truncated": data.get("response_truncated") if data else None,
            "field_count": len(fields),
            "missing_probe_prefixes": missing_prefixes,
        },
    )


def newest_boot_prepare_artifact(
    artifact_roots: list[Path],
    commit: str | None,
    scenario: str,
) -> Path | None:
    underscored = scenario.replace("-", "_")
    return newest_commit_json_from_roots(
        artifact_roots,
        commit,
        f"d1l-sd-boot-prepare-{scenario}-*.json",
        f"d1l-sd-boot-prepare-{underscored}-*.json",
        f"sd_boot_prepare_{scenario}_*.json",
        f"sd_boot_prepare_{underscored}_*.json",
        f"*boot*prepare*{scenario}*.json",
        f"*boot*prepare*{underscored}*.json",
    )


def sd_boot_prepare_gate(artifact_roots: list[Path], root: Path, commit: str | None, expected_port: str) -> GateResult:
    missing: list[str] = []
    failed: list[str] = []
    evidence: list[str] = []
    details: dict[str, Any] = {}
    for scenario in SD_BOOT_PREPARE_SCENARIOS:
        path = newest_boot_prepare_artifact(artifact_roots, commit, scenario)
        data = read_json(path)
        if path:
            evidence.append(rel(path, root))
        ok = bool(path and data and sd_boot_prepare_artifact_ok(data, scenario, expected_port))
        if not path:
            missing.append(scenario)
        elif not ok:
            failed.append(scenario)
        details[scenario] = {
            "path": path_for_details(path, root),
            "ok": ok,
            "artifact_ok": data.get("ok") if data else None,
            "mode": data.get("mode") if data else None,
            "port": data.get("port") if data else None,
            "classification": data.get("classification") if data else None,
            "formats_sd": data.get("formats_sd") if data else None,
            "public_rf_tx": data.get("public_rf_tx") if data else None,
        }
    ok = not missing and not failed
    details["missing_scenarios"] = missing
    details["failed_scenarios"] = failed
    return GateResult(
        "sd_boot_retry_manager",
        "P0",
        ok,
        "SD boot/retry manager scenario matrix",
        evidence,
        "All SD boot/retry manager scenarios pass on current hardware evidence."
        if ok else "SD boot/retry manager scenario evidence is missing or incomplete.",
        details,
    )


def sd_fat32_policy_gate(unformatted_path: Path | None, root: Path, expected_port: str) -> GateResult:
    data = read_json(unformatted_path)
    storage_after = data.get("storage_after") if isinstance(data.get("storage_after"), dict) else {}
    storage_setup = data.get("storage_setup") if isinstance(data.get("storage_setup"), dict) else {}
    sd = storage_after.get("sd") if isinstance(storage_after.get("sd"), dict) else {}
    ok = bool(
        unformatted_path
        and data
        and data.get("mode") == "hardware"
        and exact_port_ok(data, expected_port)
        and data.get("scenario") == "unformatted"
        and report_is_no_rf_no_format(data)
        and data.get("classification") in SD_BOOT_PREPARE_EXPECTED_CLASSIFICATIONS["unformatted"]
        and storage_setup_no_format_policy(storage_setup)
        and (sd.get("needs_fat32") is True or storage_setup.get("needs_fat32") is True)
        and data.get("storage_file_gate_ready") is not True
    )
    return GateResult(
        "sd_fat32_only_enforced",
        "P0",
        ok,
        "FAT32-only SD policy enforcement",
        [rel(unformatted_path, root)] if unformatted_path else [],
        "Unformatted/non-FAT32 evidence keeps NVS fallback active and instructs computer-side FAT32 preparation."
        if ok else "No current hardware evidence proves non-FAT32 cards are rejected without enabling SD file ops.",
        {
            "path_found": bool(unformatted_path),
            "artifact_ok": data.get("ok") if data else None,
            "mode": data.get("mode") if data else None,
            "classification": data.get("classification") if data else None,
            "sd_state": sd.get("state") if sd else None,
            "needs_fat32": sd.get("needs_fat32") if sd else storage_setup.get("needs_fat32") if storage_setup else None,
            "storage_file_gate_ready": data.get("storage_file_gate_ready") if data else None,
        },
    )


def sd_no_format_policy_gate(preflight_path: Path | None, unformatted_path: Path | None, root: Path) -> GateResult:
    preflight = read_json(preflight_path)
    classification = preflight.get("classification") if isinstance(preflight.get("classification"), dict) else {}
    _, obsolete_format_action = sanitized_sd_classification(preflight, classification)
    unformatted = read_json(unformatted_path)
    storage_setup = unformatted.get("storage_setup") if isinstance(unformatted.get("storage_setup"), dict) else {}
    preflight_ok = bool(
        preflight_path
        and preflight
        and preflight.get("formats_sd") is False
        and preflight.get("copies_uf2") is not True
        and not obsolete_format_action
    )
    unformatted_ok = bool(
        unformatted_path
        and unformatted
        and report_is_no_rf_no_format(unformatted)
        and storage_setup_no_format_policy(storage_setup)
        and unformatted.get("format_command_sent") is False
        and unformatted.get("format_confirmed") is False
        and unformatted.get("format_allowed") is False
        and not unsafe_sd_commands(unformatted)
    )
    ok = preflight_ok and unformatted_ok
    evidence = []
    if preflight_path:
        evidence.append(rel(preflight_path, root))
    if unformatted_path:
        evidence.append(rel(unformatted_path, root))
    return GateResult(
        "sd_no_format_language",
        "P0",
        ok,
        "No-device-format SD evidence policy",
        evidence,
        "Preflight and setup evidence preserve no-device-format language and never request formatting."
        if ok else "Current SD evidence is missing no-device-format policy proof or contains stale format guidance.",
        {
            "preflight_path_found": bool(preflight_path),
            "preflight_formats_sd": preflight.get("formats_sd") if preflight else None,
            "preflight_copies_uf2": preflight.get("copies_uf2") if preflight else None,
            "obsolete_format_action_blocked": obsolete_format_action,
            "unformatted_path_found": bool(unformatted_path),
            "format_command_sent": unformatted.get("format_command_sent") if unformatted else None,
            "format_confirmed": unformatted.get("format_confirmed") if unformatted else None,
            "format_allowed": unformatted.get("format_allowed") if unformatted else None,
            "storage_setup_policy": storage_setup.get("policy") if storage_setup else None,
            "unsafe_commands": unsafe_sd_commands(unformatted) if unformatted else [],
        },
    )


def sd_power_rail_gate(artifact_roots: list[Path], root: Path, commit: str | None, expected_port: str) -> GateResult:
    power = newest_commit_json_from_roots(
        artifact_roots,
        commit,
        "sd_electrical_*.json",
        "sd_power_rail_*.json",
        "sd_slot_electrical_*.json",
        "sd_power_matrix_*.json",
    )
    data = read_json(power)
    ok = bool(power and data and sd_power_rail_evidence_ok(data, expected_port))
    return GateResult(
        "sd_power_rail_measured",
        "P0",
        ok,
        "SD slot power-rail and signal measurement",
        [rel(power, root)] if power else [],
        "Measured SD socket VCC/GND/SPI evidence is present for the release hardware."
        if ok else "No current hardware SD power-rail/electrical measurement artifact was found.",
        {
            "path_found": bool(power),
            "artifact_ok": data.get("ok") if data else None,
            "mode": data.get("mode") if data else None,
            "port": data.get("port") if data else None,
            "vcc_at_socket_volts": first_numeric_field(
                data,
                "vcc_at_socket_volts",
                "sd_vcc_volts",
                "vcc_after_gpio18_high_volts",
                "sd_power_rail_volts",
            ) if data else None,
            "sd_power_rail_measured": data.get("sd_power_rail_measured") if data else None,
        },
    )


def sd_32gb_matrix_gate(artifact_roots: list[Path], root: Path, commit: str | None, expected_port: str) -> GateResult:
    matrix = newest_commit_json_from_roots(
        artifact_roots,
        commit,
        "sd_32gb_matrix_*.json",
        "sd_fat32_matrix_*.json",
        "sd_card_matrix_*.json",
        "sd_matrix_*.json",
    )
    data = read_json(matrix)
    cards = card_matrix_entries(data)
    capacities = [card_capacity_gb(card) for card in cards]
    ok = bool(matrix and data and sd_matrix_artifact_ok(data, expected_port))
    return GateResult(
        "sd_32gb_max_matrix_passed",
        "P0",
        ok,
        "FAT32 <=32GB SD card matrix",
        [rel(matrix, root)] if matrix else [],
        "At least three FAT32 cards, including a 32GB card, pass non-formatting SD acceptance."
        if ok else "No passing current-commit FAT32 <=32GB SD matrix artifact was found.",
        {
            "path_found": bool(matrix),
            "artifact_ok": data.get("ok") if data else None,
            "port": data.get("port") if data else None,
            "card_count": len(cards),
            "capacities_gb": capacities,
            "all_fat32": all(card_is_fat32(card) for card in cards) if cards else False,
            "has_32gb_card": any(capacity == 32 for capacity in capacities),
        },
    )


def full_soak_ok(data: dict) -> bool:
    summary = data.get("summary") if isinstance(data.get("summary"), dict) else {}
    return (
        data.get("ok") is True
        and data.get("mode") == "hardware"
        and float(data.get("duration_sec") or 0) >= FULL_SOAK_SECONDS
        and summary.get("ok") is True
        and not summary.get("threshold_failures")
        and summary.get("board_ready_all") is True
        and summary.get("ui_ready_all") is True
        and summary.get("uptime_monotonic") is True
    )


def full_soak_gate(soak_root: Path, root: Path, commit: str | None) -> GateResult:
    candidates = [
        path for path in sorted(soak_root.glob("*.json"), key=lambda path: path.stat().st_mtime, reverse=True)
        if artifact_commit_matches(path, read_json(path), commit)
    ]
    for candidate in candidates:
        data = read_json(candidate)
        if full_soak_ok(data):
            return GateResult(
                "full_duration_idle_soak",
                "P0",
                True,
                "12-hour idle/listening soak",
                [rel(candidate, root)],
                "12-hour idle/listening soak artifact passes.",
                {"duration_sec": data.get("duration_sec")},
            )
    newest = candidates[0] if candidates else None
    newest_data = read_json(newest)
    return GateResult(
        "full_duration_idle_soak",
        "P0",
        False,
        "12-hour idle/listening soak",
        [rel(newest, root)] if newest else [],
        "No passing 12-hour idle/listening hardware soak artifact was found.",
        {"latest_duration_sec": newest_data.get("duration_sec") if newest_data else None},
    )


def manual_evidence_gate(hardware_dir: Path, root: Path, commit: str | None) -> GateResult:
    review = newest_commit_json(hardware_dir, commit, "manual_touch_review*.json", "*manual*ui*review*.json")
    photos = [path for pattern in ("photos/*", "screen_photos/*", "*screen_photo*") for path in hardware_dir.glob(pattern) if path.is_file()]
    data = read_json(review)
    ok = bool(review and data.get("ok") is True and photos)
    evidence = []
    if review:
        evidence.append(rel(review, root))
    evidence.extend(rel(path, root) for path in sorted(photos)[:10])
    return GateResult(
        "manual_physical_ui_review",
        "P0",
        ok,
        "Manual physical UI/touch review and screen photos",
        evidence,
        "Manual physical UI/touch review and screen photos are present."
        if ok else "Manual physical UI/touch review and physical screen photos are still missing.",
        {"photo_count": len(photos), "review_found": bool(review)},
    )


def full_rf_gate(hardware_dir: Path, root: Path, commit: str | None) -> GateResult:
    candidates = [
        newest_commit_json(hardware_dir, commit, "rf_full_acceptance*.json", "*ack_path*.json", "*direct_route*.json", "*inbound_dm*.json")
    ]
    evidence_paths = [path for path in candidates if path]
    passing = False
    for path in evidence_paths:
        data = read_json(path)
        checks = data.get("checks") if isinstance(data.get("checks"), dict) else {}
        passing = data.get("ok") is True and all(
            checks.get(name) is True
            for name in ("inbound_dm", "ack_path", "direct_route")
        )
        if passing:
            break
    return GateResult(
        "full_rf_dm_acceptance",
        "P0",
        passing,
        "Inbound DM, ACK/PATH, and direct-route RF proof",
        [rel(path, root) for path in evidence_paths],
        "Full RF/DM acceptance evidence is present."
        if passing else "Only partial RF/DM evidence is present; inbound DM, ACK/PATH, and direct-route proof remain open.",
        {"candidate_count": len(evidence_paths)},
    )


def docs_freshness_gate(root: Path, commit: str | None, run_id: str | None) -> GateResult:
    docs = [
        root / "README.md",
        root / "docs" / "ROADMAP.md",
        root / "docs" / "RELEASE_CHECKLIST.md",
        root / "docs" / "KNOWN_LIMITATIONS.md",
    ]
    texts = {path: path.read_text(encoding="utf-8") for path in docs if path.is_file()}
    del commit, run_id
    combined = "\n".join(texts.values())
    needle_values = [
        "release_gate_audit_d1l.py",
        "ready_for_public_release=false",
        "No release tag should be cut until",
    ]
    missing = []
    for needle in needle_values:
        if needle not in combined:
            missing.append(needle)
    ok = not missing and len(texts) == len(docs)
    return GateResult(
        "docs_current_evidence",
        "P1",
        ok,
        "Release docs include fail-closed audit gate",
        [rel(path, root) for path in texts],
        "Release docs include the fail-closed audit gate and no-release-tag policy."
        if ok else "Release docs do not yet include the fail-closed audit gate and no-release-tag policy.",
        {"missing_needles": missing},
    )


def build_audit(args: argparse.Namespace) -> dict:
    root = Path(args.root).resolve()
    if args.github_run_id and not args.github_run_dir:
        args.github_run_dir = str(root / "artifacts" / "github" / args.github_run_id)
    github_run_dir = Path(args.github_run_dir).resolve() if args.github_run_dir else None
    hardware_dir = Path(args.hardware_dir).resolve()
    rp2040_hardware_dir = Path(args.rp2040_hardware_dir).resolve()
    soak_dir = Path(args.soak_dir).resolve()
    gates: list[GateResult] = []
    smoke_roots = sd_artifact_roots(root, github_run_dir, hardware_dir, "smoke")
    ui_corruption_roots = sd_artifact_roots(root, github_run_dir, hardware_dir, "ui-corruption-probe")
    ui_capture_roots = sd_artifact_roots(root, github_run_dir, hardware_dir, "ui-capture")
    scroll_probe_roots = sd_artifact_roots(root, github_run_dir, hardware_dir, "scroll-probe")
    official_smoke_roots = unique_dirs(
        sd_artifact_roots(root, github_run_dir, rp2040_hardware_dir, "rp2040-official-sd-smoke")
        + [hardware_dir]
    )
    preflight_roots = sd_artifact_roots(root, github_run_dir, hardware_dir, "rp2040-preflight")
    boot_prepare_roots = sd_artifact_roots(root, github_run_dir, hardware_dir, "sd-boot-prepare")
    file_canary_roots = sd_artifact_roots(root, github_run_dir, hardware_dir, "sd-canary")
    retained_roots = sd_artifact_roots(root, github_run_dir, hardware_dir, "sd-retained-history")
    reboot_remount_roots = sd_artifact_roots(root, github_run_dir, hardware_dir, "sd-reboot-remount")
    map_tile_roots = sd_artifact_roots(root, github_run_dir, hardware_dir, "sd-map-tile-canary")
    raw_diag_roots = unique_dirs(
        sd_artifact_roots(root, github_run_dir, rp2040_hardware_dir, "rp2040-preflight")
        + sd_artifact_roots(root, github_run_dir, hardware_dir, "rp2040-preflight")
    )
    sd_matrix_roots = sd_artifact_roots(root, github_run_dir, hardware_dir, "sd-matrix")
    sd_power_roots = sd_artifact_roots(root, github_run_dir, hardware_dir, "sd-electrical", "sd-matrix")
    preflight_path = newest_commit_json_from_roots(
        preflight_roots,
        args.commit,
        "rp2040_preflight_*.json",
        "rp2040_sd_preflight_*.json",
        "d1l-rp2040-sd-bridge-preflight-*.json",
    )
    unformatted_boot_path = newest_boot_prepare_artifact(boot_prepare_roots, args.commit, "unformatted")

    gates.append(checksum_gate(github_run_dir, root))
    gates.append(notices_gate(github_run_dir, root))
    gates.append(
        simple_json_ok_gate(
            "com12_smoke",
            "D1L hardware smoke",
            newest_commit_json_from_roots(smoke_roots, args.commit, "smoke_*.json", "d1l-smoke-*.json"),
            root,
            lambda data: data.get("ok") is True and data.get("port") == args.d1l_port,
            "Current-commit D1L smoke artifact passes.",
            "No passing current-commit D1L smoke artifact was found.",
        )
    )
    gates.append(
        simple_json_ok_gate(
            "ui_corruption_probe",
            "Targeted D1L UI corruption probe",
            newest_commit_json_from_roots(
                ui_corruption_roots,
                args.commit,
                "ui_corruption_probe_*.json",
                "d1l-ui-corruption-probe-*.json",
            ),
            root,
            lambda data: ui_corruption_probe_ok(data, args.d1l_port),
            "Targeted D1L UI corruption probe artifact passes.",
            "No passing targeted D1L UI corruption probe artifact was found.",
        )
    )
    gates.append(
        simple_json_ok_gate(
            "ui_pixel_capture",
            "D1L hardware pixel capture",
            newest_commit_json_from_roots(
                ui_capture_roots,
                args.commit,
                "ui_pixel_capture_*.json",
                "d1l-ui-capture-*.json",
            ),
            root,
            lambda data: ui_pixel_capture_ok(data, args.d1l_port),
            "Current-commit D1L hardware pixel capture reconstructs a 480x480 RGB565 frame.",
            "No passing current-commit D1L hardware pixel capture artifact was found.",
        )
    )
    gates.append(
        simple_json_ok_gate(
            "ui_compose_keyboard_capture",
            "D1L compose keyboard hardware capture",
            newest_commit_json_from_roots(
                ui_capture_roots,
                args.commit,
                "ui_compose_keyboard_capture*.json",
                "d1l-compose-keyboard-capture-*.json",
            ),
            root,
            lambda data: compose_keyboard_capture_ok(data, args.d1l_port),
            "Current-commit D1L compose keyboard capture proves Public and DM keyboard states fit the 480x480 hardware frame.",
            "No passing current-commit D1L compose keyboard hardware capture artifact was found.",
        )
    )
    gates.append(
        simple_json_ok_gate(
            "ui_scroll_probe",
            "D1L scroll probe",
            newest_commit_json_from_roots(
                scroll_probe_roots,
                args.commit,
                "scroll_probe_*.json",
                "d1l-scroll-probe-*.json",
            ),
            root,
            lambda data: scroll_probe_ok(data, args.d1l_port),
            "D1L scroll probe artifact passes.",
            "No passing D1L scroll probe artifact was found.",
        )
    )
    gates.append(
        simple_json_ok_gate(
            "outbound_dm_com11",
            "D1L outbound DM proof against meshbot",
            newest_commit_json(hardware_dir, args.commit, "dm_probe_*.json"),
            root,
            lambda data: dm_probe_ok(data, args.d1l_port, args.meshbot_port),
            "Outbound D1L-to-meshbot DM proof passes.",
            "No passing outbound D1L-to-meshbot DM proof artifact was found.",
        )
    )
    gates.append(route_probe_gate(hardware_dir, root, args.commit, args.d1l_port))
    gates.append(official_seeed_sd_smoke_gate(official_smoke_roots, root, args.commit, args.rp2040_port))
    gates.append(sd_gate(preflight_path, root))
    gates.append(sd_raw_diag_gate(raw_diag_roots, root, args.commit, args.d1l_port))
    gates.append(sd_boot_prepare_gate(boot_prepare_roots, root, args.commit, args.d1l_port))
    gates.append(sd_filecanary_gate(file_canary_roots, root, args.commit, args.d1l_port))
    gates.append(sd_retained_canary_gate(retained_roots, root, args.commit, args.d1l_port))
    gates.append(sd_reboot_remount_gate(reboot_remount_roots, root, args.commit, args.d1l_port))
    gates.append(sd_map_tile_canary_gate(map_tile_roots, root, args.commit, args.d1l_port))
    gates.append(sd_fat32_policy_gate(unformatted_boot_path, root, args.d1l_port))
    gates.append(sd_no_format_policy_gate(preflight_path, unformatted_boot_path, root))
    gates.append(sd_power_rail_gate(sd_power_roots, root, args.commit, args.d1l_port))
    gates.append(sd_32gb_matrix_gate(sd_matrix_roots, root, args.commit, args.d1l_port))
    gates.append(full_soak_gate(soak_dir, root, args.commit))
    gates.append(manual_evidence_gate(hardware_dir, root, args.commit))
    gates.append(full_rf_gate(hardware_dir, root, args.commit))
    gates.append(docs_freshness_gate(root, args.commit, args.github_run_id))

    p0_failed = [gate for gate in gates if gate.severity == "P0" and not gate.ok]
    failed = [gate for gate in gates if not gate.ok]
    return {
        "schema": 1,
        "mode": "release-gate-audit",
        "created_at": datetime.now(timezone.utc).isoformat(),
        "root": str(root),
        "github_run_id": args.github_run_id,
        "github_run_dir": str(github_run_dir) if github_run_dir else None,
        "commit": args.commit,
        "hardware_dir": str(hardware_dir),
        "rp2040_port": args.rp2040_port,
        "rp2040_hardware_dir": str(rp2040_hardware_dir),
        "ready_for_public_release": not p0_failed,
        "p0_failed_count": len(p0_failed),
        "failed_count": len(failed),
        "gates": [gate.to_dict() for gate in gates],
    }


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=".")
    parser.add_argument("--github-run-id")
    parser.add_argument("--github-run-dir")
    parser.add_argument("--commit")
    parser.add_argument("--d1l-port", default=default_d1l_port())
    parser.add_argument("--rp2040-port", default=default_rp2040_port())
    parser.add_argument("--meshbot-port", default=default_meshbot_port())
    parser.add_argument("--hardware-dir", default=default_hardware_dir())
    parser.add_argument("--rp2040-hardware-dir", default=default_rp2040_hardware_dir())
    parser.add_argument("--soak-dir", default="artifacts/soak")
    parser.add_argument("--out")
    parser.add_argument("--fail-on-open-p0", action="store_true")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])
    if args.github_run_id and not args.github_run_dir:
        args.github_run_dir = str(Path(args.root) / "artifacts" / "github" / args.github_run_id)
    report = build_audit(args)
    text = json.dumps(report, indent=2, sort_keys=True)
    if args.out:
        out = Path(args.out)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(text + "\n", encoding="utf-8")
    print(text)
    if args.fail_on_open_p0 and not report["ready_for_public_release"]:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
