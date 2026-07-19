#!/usr/bin/env python3
"""Produce fail-closed Core 1.0 reboot and retained-state evidence on COM12.

The producer deliberately separates two phases:

* ``seed`` runs on the exact candidate, writes deterministic local-only NVS
  Public/DM/contact and settings canaries, and captures retained Core state.
* ``verify`` requires the seed plus the exact-candidate non-erasing reflash
  receipt, then executes five software reboots and three operator-controlled
  cold power cycles.

Every console command is retained as hash-bound raw serial bytes.  The final
matrix references immutable child receipts and is only accepted after the
validator recomputes identity, state, reboot, crashlog, and USB-port facts from
those raw fields.  This proves a same-exact-candidate non-erasing reinstall; it
does not claim cross-version migration.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import re
import sys
import time
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable, Iterable

try:
    from artifact_metadata import git_metadata
    from smoke_d1l import (
        exact_commit,
        expected_command_name,
        open_d1l_serial,
        parse_jsonl_line,
    )
    from verify_checksums import is_link_or_reparse, sha256_file
except ImportError:  # pragma: no cover - package import path used by pytest
    from scripts.artifact_metadata import git_metadata
    from scripts.smoke_d1l import (
        exact_commit,
        expected_command_name,
        open_d1l_serial,
        parse_jsonl_line,
    )
    from scripts.verify_checksums import is_link_or_reparse, sha256_file


ROOT = Path(__file__).resolve().parents[1]
D1L_CORE_PORT = "COM12"
D1L_BAUD = 115200
CORE_RELEASE_PROFILE = "core_1_0"
SD_HISTORY_MODE = "disabled"
EXPECTED_IDF_VERSION = "v5.5.4"
SOFTWARE_CYCLE_COUNT = 5
COLD_CYCLE_COUNT = 3
MINIMUM_POWER_OFF_SEC = 2.0
FORBIDDEN_PORTS = frozenset({"COM8", "COM11", "COM16", "COM29"})
STATE_BASE_COMMANDS = (
    "version",
    "health",
    "crashlog",
    "settings get",
)
STATE_TAIL_COMMANDS = (
    "messages unread",
    "contacts",
)
MAX_RAW_LINE_BYTES = 131072
MAX_PAGES = 32
CLAIM = "same_exact_candidate_non_erasing_reinstall"
CORE_CANARY_KEY_DOMAIN = "sigui-core-retained-canary:"
CORE_CANARY_MESSAGE_PREFIX = "core-retained-canary "

RESET_RE = re.compile(
    r"(?:^|\s)rst:0x(?P<code>[0-9a-f]+)\s*\((?P<reason>[^)]+)\)",
    re.IGNORECASE,
)
ROM_RE = re.compile(r"(?:ESP-ROM:|^ets\s)", re.IGNORECASE)
BOOT_RE = re.compile(r"(?:^|[,\s])boot:0x[0-9a-f]+", re.IGNORECASE)
CRASH_RE = re.compile(r"(?:WDT|PANIC|BROWNOUT)", re.IGNORECASE)
SOFTWARE_SYSTEM_RESET = (0x03, "RTC_SW_SYS_RST")
COLD_RESET_REASONS = frozenset(
    {
        "POWERON",
        "POWERON_RESET",
        "RTC_POWERON",
        "RTC_POWERON_RESET",
    }
)


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def canonical_json(value: object) -> bytes:
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=True
    ).encode("ascii")


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def positive_decimal(value: object) -> bool:
    text = str(value)
    return text.isdigit() and int(text) > 0


_ED25519_Q = 2**255 - 19
_ED25519_D = (
    -121665 * pow(121666, _ED25519_Q - 2, _ED25519_Q)
) % _ED25519_Q
_ED25519_BASE = (
    15112221349535400772501151409588531511454012693041857206046113283949847762202,
    46316835694926478169428394003475163141307993866256225615783033603165251855960,
)


def _ed25519_add(
    left: tuple[int, int],
    right: tuple[int, int],
) -> tuple[int, int]:
    x1, y1 = left
    x2, y2 = right
    product = _ED25519_D * x1 * x2 * y1 * y2 % _ED25519_Q
    x3 = (
        (x1 * y2 + x2 * y1)
        * pow(1 + product, _ED25519_Q - 2, _ED25519_Q)
    ) % _ED25519_Q
    y3 = (
        (y1 * y2 + x1 * x2)
        * pow(1 - product, _ED25519_Q - 2, _ED25519_Q)
    ) % _ED25519_Q
    return x3, y3


def ed25519_public_key_from_seed(seed: bytes) -> bytes:
    if not isinstance(seed, bytes) or len(seed) != 32:
        raise ValueError("Ed25519 seed must be exactly 32 bytes")
    expanded = bytearray(hashlib.sha512(seed).digest())
    expanded[0] &= 248
    expanded[31] &= 63
    expanded[31] |= 64
    scalar = int.from_bytes(expanded[:32], "little")
    point = (0, 1)
    addend = _ED25519_BASE
    while scalar:
        if scalar & 1:
            point = _ed25519_add(point, addend)
        addend = _ed25519_add(addend, addend)
        scalar >>= 1
    encoded = point[1] | ((point[0] & 1) << 255)
    return encoded.to_bytes(32, "little")


def candidate_canary(
    commit: str,
    run_id: str,
    run_attempt: str,
) -> dict[str, Any]:
    commit = exact_commit(commit) or ""
    if (
        not commit
        or not positive_decimal(run_id)
        or not positive_decimal(run_attempt)
    ):
        raise ValueError("candidate canary identity is invalid")
    identity = canonical_json(
        {
            "commit": commit,
            "github_actions_run": str(run_id),
            "workflow_run_attempt": str(run_attempt),
        }
    )
    token = f"core-{sha256_bytes(identity)[:24]}"
    seed = hashlib.sha256(
        f"{CORE_CANARY_KEY_DOMAIN}{token}".encode("ascii")
    ).digest()
    public_key = ed25519_public_key_from_seed(seed).hex()
    return {
        "settings_node_name": f"D1L-Core-{commit[:7]}",
        "token": token,
        "message_text": f"{CORE_CANARY_MESSAGE_PREFIX}{token}",
        "contact_fingerprint": public_key[:16].upper(),
        "contact_public_key": public_key,
    }


def normalize_port(value: object) -> str | None:
    if not isinstance(value, str) or not value.strip():
        return None
    normalized = value.strip().upper().replace("/", "\\")
    for prefix in ("\\\\.\\", "\\\\?\\"):
        if normalized.startswith(prefix):
            normalized = normalized[len(prefix) :]
            break
    return normalized


def enforce_core_port(value: object) -> str:
    port = normalize_port(value)
    if port != D1L_CORE_PORT:
        raise ValueError(
            f"Core reboot/persistence validation requires {D1L_CORE_PORT}; "
            f"got {port or '<missing>'}"
        )
    if port in FORBIDDEN_PORTS:
        raise ValueError(f"refusing forbidden port {port}")
    return port


def exact_source_git(
    root: Path,
    commit: str,
    *,
    metadata: dict[str, Any] | None = None,
) -> dict[str, Any]:
    source = dict(metadata if metadata is not None else git_metadata(root))
    if not (
        exact_commit(source.get("commit")) == commit
        and source.get("dirty") is False
        and source.get("dirty_entries") == []
    ):
        raise ValueError(
            "producer source must be the exact clean candidate commit"
        )
    return source


def _inside_existing(root: Path, path: Path, label: str) -> Path:
    try:
        resolved_root = root.resolve(strict=True)
        resolved = path.resolve(strict=True)
        resolved.relative_to(resolved_root)
    except (OSError, RuntimeError, ValueError) as exc:
        raise ValueError(f"{label} must stay inside {root}") from exc
    if is_link_or_reparse(path):
        raise ValueError(f"{label} cannot be a link/reparse point: {path}")
    return resolved


def _inside_output(root: Path, path: Path, label: str) -> Path:
    resolved_root = root.resolve(strict=True)
    resolved = path.resolve()
    try:
        resolved.relative_to(resolved_root)
    except ValueError as exc:
        raise ValueError(f"{label} must stay inside {root}") from exc
    cursor = resolved.parent
    while cursor != resolved_root:
        if cursor.exists() and is_link_or_reparse(cursor):
            raise ValueError(f"{label} parent cannot be a link/reparse point")
        cursor = cursor.parent
    return resolved


def load_json(path: Path, root: Path, label: str) -> dict[str, Any]:
    resolved = _inside_existing(root, path, label)
    if not resolved.is_file():
        raise ValueError(f"{label} is not a file: {path}")
    try:
        value = json.loads(resolved.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"{label} is invalid JSON: {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ValueError(f"{label} must be a JSON object: {path}")
    return value


def relative_file_row(path: Path, root: Path, label: str) -> dict[str, Any]:
    resolved = _inside_existing(root, path, label)
    return {
        "path": resolved.relative_to(root.resolve()).as_posix(),
        "size": resolved.stat().st_size,
        "sha256": sha256_file(resolved),
    }


def validate_file_row(
    row: object, root: Path, label: str
) -> tuple[Path | None, list[str]]:
    errors: list[str] = []
    if not isinstance(row, dict):
        return None, [f"{label}: missing file row"]
    raw_path = row.get("path")
    if not isinstance(raw_path, str) or not raw_path:
        return None, [f"{label}: missing path"]
    try:
        path = _inside_existing(root, root / raw_path, label)
    except ValueError as exc:
        return None, [str(exc)]
    expected_size = row.get("size")
    expected_digest = row.get("sha256")
    if type(expected_size) is not int or expected_size != path.stat().st_size:
        errors.append(f"{label}: size mismatch")
    if (
        not isinstance(expected_digest, str)
        or not re.fullmatch(r"[0-9a-f]{64}", expected_digest)
        or expected_digest != sha256_file(path)
    ):
        errors.append(f"{label}: sha256 mismatch")
    return path, errors


def write_json_once(path: Path, root: Path, value: dict[str, Any]) -> None:
    resolved = _inside_output(root, path, "evidence output")
    if resolved.exists():
        raise ValueError(f"refusing to overwrite evidence: {resolved}")
    resolved.parent.mkdir(parents=True, exist_ok=True)
    resolved.write_text(
        json.dumps(value, indent=2, sort_keys=True, ensure_ascii=True) + "\n",
        encoding="ascii",
    )


def _raw_line(raw: bytes, observed_at: str) -> dict[str, Any]:
    return {
        "observed_at": observed_at,
        "size": len(raw),
        "sha256": sha256_bytes(raw),
        "base64": base64.b64encode(raw).decode("ascii"),
    }


def decode_raw_line(row: object) -> tuple[bytes | None, list[str]]:
    errors: list[str] = []
    if not isinstance(row, dict):
        return None, ["raw line is not an object"]
    encoded = row.get("base64")
    try:
        raw = base64.b64decode(encoded, validate=True)
    except (TypeError, ValueError):
        return None, ["raw line base64 is invalid"]
    if type(row.get("size")) is not int or row.get("size") != len(raw):
        errors.append("raw line size mismatch")
    digest = row.get("sha256")
    if (
        not isinstance(digest, str)
        or digest != sha256_bytes(raw)
    ):
        errors.append("raw line sha256 mismatch")
    if not isinstance(row.get("observed_at"), str):
        errors.append("raw line observed_at is missing")
    return raw, errors


def read_raw_command(
    ser: Any,
    command: str,
    timeout: float,
    *,
    clock: Callable[[], float] = time.monotonic,
    now: Callable[[], str] = utc_now,
) -> dict[str, Any]:
    expected = expected_command_name(command)
    started_at = now()
    ser.write((command + "\n").encode("utf-8"))
    if hasattr(ser, "flush"):
        ser.flush()
    deadline = clock() + timeout
    lines: list[dict[str, Any]] = []
    result: dict[str, Any] | None = None
    original_timeout = getattr(ser, "timeout", None)
    try:
        while clock() < deadline:
            remaining = max(0.001, deadline - clock())
            if hasattr(ser, "timeout"):
                ser.timeout = min(0.25, remaining)
            raw = ser.readline(MAX_RAW_LINE_BYTES + 1)
            if not raw:
                continue
            lines.append(_raw_line(raw, now()))
            if len(raw) > MAX_RAW_LINE_BYTES:
                break
            try:
                text = raw.decode("utf-8", errors="strict")
            except UnicodeDecodeError:
                continue
            parsed = parse_jsonl_line(text)
            if parsed is not None and parsed.get("cmd") == expected:
                result = parsed
                break
    finally:
        if hasattr(ser, "timeout"):
            ser.timeout = original_timeout
    if result is None:
        result = {
            "schema": 1,
            "ok": False,
            "cmd": expected,
            "code": "TIMEOUT_OR_INVALID_RAW",
        }
    return {
        "command": command,
        "expected_cmd": expected,
        "started_at": started_at,
        "ended_at": now(),
        "raw_lines": lines,
        "result": result,
    }


def recompute_raw_command(row: object) -> tuple[dict[str, Any] | None, list[str]]:
    errors: list[str] = []
    if not isinstance(row, dict):
        return None, ["command receipt is not an object"]
    command = row.get("command")
    expected = row.get("expected_cmd")
    if not isinstance(command, str) or not command:
        errors.append("command receipt command is missing")
        return None, errors
    if expected != expected_command_name(command):
        errors.append(f"{command}: expected command mismatch")
    raw_lines = row.get("raw_lines")
    if not isinstance(raw_lines, list) or not raw_lines:
        errors.append(f"{command}: raw lines are missing")
        return None, errors
    matched: list[dict[str, Any]] = []
    for index, raw_row in enumerate(raw_lines):
        raw, raw_errors = decode_raw_line(raw_row)
        errors.extend(f"{command} raw[{index}]: {error}" for error in raw_errors)
        if raw is None or len(raw) > MAX_RAW_LINE_BYTES:
            if raw is not None:
                errors.append(f"{command} raw[{index}]: line exceeds limit")
            continue
        try:
            parsed = parse_jsonl_line(raw.decode("utf-8", errors="strict"))
        except UnicodeDecodeError:
            parsed = None
        if parsed is not None and parsed.get("cmd") == expected:
            matched.append(parsed)
    if len(matched) != 1:
        errors.append(f"{command}: expected exactly one matching raw result")
        return None, errors
    if row.get("result") != matched[0]:
        errors.append(f"{command}: parsed result does not match raw result")
    return matched[0], errors


def capture_boot_lines(
    ser: Any,
    timeout: float,
    *,
    clock: Callable[[], float] = time.monotonic,
    now: Callable[[], str] = utc_now,
) -> list[dict[str, Any]]:
    deadline = clock() + timeout
    lines: list[dict[str, Any]] = []
    original_timeout = getattr(ser, "timeout", None)
    try:
        while clock() < deadline:
            remaining = max(0.001, deadline - clock())
            if hasattr(ser, "timeout"):
                ser.timeout = min(0.25, remaining)
            raw = ser.readline(MAX_RAW_LINE_BYTES + 1)
            if not raw:
                continue
            lines.append(_raw_line(raw, now()))
            if len(raw) > MAX_RAW_LINE_BYTES:
                break
            try:
                parsed = parse_jsonl_line(raw.decode("utf-8", errors="strict"))
            except UnicodeDecodeError:
                parsed = None
            if parsed is not None and parsed.get("cmd") == "help":
                break
    finally:
        if hasattr(ser, "timeout"):
            ser.timeout = original_timeout
    return lines


def analyze_boot_lines(rows: object) -> tuple[dict[str, Any], list[str]]:
    errors: list[str] = []
    analysis = {
        "rom_count": 0,
        "reset_count": 0,
        "boot_count": 0,
        "help_count": 0,
        "crash_marker_count": 0,
        "reset_events": [],
    }
    if not isinstance(rows, list):
        return analysis, ["boot raw lines are missing"]
    for index, row in enumerate(rows):
        raw, raw_errors = decode_raw_line(row)
        errors.extend(f"boot raw[{index}]: {error}" for error in raw_errors)
        if raw is None or len(raw) > MAX_RAW_LINE_BYTES:
            continue
        text = raw.decode("utf-8", errors="replace")
        if ROM_RE.search(text):
            analysis["rom_count"] += 1
        match = RESET_RE.search(text)
        if match is not None:
            analysis["reset_count"] += 1
            analysis["reset_events"].append(
                {
                    "code": int(match.group("code"), 16),
                    "reason": match.group("reason").strip().upper(),
                }
            )
        if BOOT_RE.search(text):
            analysis["boot_count"] += 1
        if CRASH_RE.search(text):
            analysis["crash_marker_count"] += 1
        try:
            parsed = parse_jsonl_line(raw.decode("utf-8", errors="strict"))
        except UnicodeDecodeError:
            parsed = None
        if parsed is not None and parsed.get("cmd") == "help":
            analysis["help_count"] += 1
    return analysis, errors


def _command_ok(result: object, command: str) -> bool:
    return (
        isinstance(result, dict)
        and result.get("schema") == 1
        and result.get("ok") is True
        and result.get("cmd") == command
    )


def exact_version(
    result: object, commit: str
) -> bool:
    return (
        _command_ok(result, "version")
        and isinstance(result, dict)
        and exact_commit(result.get("build_commit")) == commit
        and result.get("idf") == EXPECTED_IDF_VERSION
        and result.get("release_profile") == CORE_RELEASE_PROFILE
        and result.get("sd_history_mode") == SD_HISTORY_MODE
    )


def exact_health(result: object) -> bool:
    return (
        _command_ok(result, "health")
        and isinstance(result, dict)
        and result.get("release_profile") == CORE_RELEASE_PROFILE
        and result.get("sd_history_mode") == SD_HISTORY_MODE
        and result.get("board_ready") is True
        and result.get("ui_ready") is True
        and result.get("nvs_ready") is True
        and type(result.get("boot_nonce")) is int
        and result.get("boot_nonce") > 0
        and type(result.get("uptime_ms")) is int
        and result.get("uptime_ms") >= 0
    )


def _capture_pages(
    ser: Any,
    command: str,
    timeout: float,
    *,
    clock: Callable[[], float],
    now: Callable[[], str],
) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    offset = 0
    for _ in range(MAX_PAGES):
        request = command if offset == 0 else f"{command} offset {offset}"
        row = read_raw_command(
            ser, request, timeout, clock=clock, now=now
        )
        rows.append(row)
        result = row.get("result")
        if not _command_ok(result, command):
            break
        if result.get("has_older") is not True:
            break
        next_offset = result.get("next_offset")
        if (
            type(next_offset) is not int
            or next_offset <= offset
        ):
            break
        offset = next_offset
    return rows


def capture_state(
    ser: Any,
    timeout: float,
    *,
    clock: Callable[[], float] = time.monotonic,
    now: Callable[[], str] = utc_now,
) -> dict[str, Any]:
    commands = [
        read_raw_command(ser, command, timeout, clock=clock, now=now)
        for command in STATE_BASE_COMMANDS
    ]
    commands.extend(
        _capture_pages(
            ser,
            command,
            timeout,
            clock=clock,
            now=now,
        )
        for command in ("messages public", "messages dm")
    )
    flattened: list[dict[str, Any]] = []
    for row in commands:
        if isinstance(row, list):
            flattened.extend(row)
        else:
            flattened.append(row)
    flattened.extend(
        read_raw_command(ser, command, timeout, clock=clock, now=now)
        for command in STATE_TAIL_COMMANDS
    )
    return {
        "captured_at": now(),
        "commands": flattened,
    }


def _clean_persistence(result: dict[str, Any]) -> bool:
    persistence = result.get("persistence")
    nvs = persistence.get("nvs") if isinstance(persistence, dict) else None
    return (
        isinstance(persistence, dict)
        and persistence.get("loaded") is True
        and persistence.get("dirty") is False
        and persistence.get("failures") == 0
        and isinstance(nvs, dict)
        and nvs.get("dirty") is False
        and nvs.get("failures") == 0
        and nvs.get("last_error") == "ESP_OK"
        and result.get("persisted") is True
    )


def _settings_projection(result: dict[str, Any]) -> dict[str, Any] | None:
    required_scalars = (
        "node_name",
        "role",
        "onboarding_complete",
        "wifi_enabled",
        "ble_companion_enabled",
        "observer_enabled",
        "high_contrast",
        "night_mode",
        "path_hash_bytes",
    )
    if not _command_ok(result, "settings get"):
        return None
    if not isinstance(result.get("node_name"), str) or not result["node_name"]:
        return None
    if any(key not in result for key in required_scalars):
        return None
    timezone_value = result.get("timezone")
    radio = result.get("radio")
    if not isinstance(timezone_value, dict) or not isinstance(radio, dict):
        return None
    return {
        key: result[key] for key in required_scalars
    } | {
        "timezone": timezone_value,
        "radio": radio,
    }


def _page_projection(
    command: str,
    pages: list[dict[str, Any]],
) -> tuple[dict[str, Any] | None, list[str]]:
    errors: list[str] = []
    if not pages:
        return None, [f"{command}: pages are missing"]
    entries: list[dict[str, Any]] = []
    offsets: list[int] = []
    first: dict[str, Any] | None = None
    stable_fields = (
        "count",
        "capacity",
        "total_written",
        "dropped_oldest",
        "total_matches",
        "retained_epoch",
        "content_revision",
    )
    for index, result in enumerate(pages):
        if not _command_ok(result, command):
            errors.append(f"{command}: page {index} failed")
            continue
        if not _clean_persistence(result):
            errors.append(f"{command}: page {index} persistence is not clean")
        offset = result.get("offset")
        page_entries = result.get("entries")
        if type(offset) is not int or offset < 0:
            errors.append(f"{command}: page {index} offset is invalid")
            continue
        if (
            not isinstance(page_entries, list)
            or not all(isinstance(entry, dict) for entry in page_entries)
            or result.get("page_count") != len(page_entries)
        ):
            errors.append(f"{command}: page {index} entries are invalid")
            continue
        if first is None:
            first = result
        elif any(result.get(key) != first.get(key) for key in stable_fields):
            errors.append(f"{command}: page metadata changed during capture")
        offsets.append(offset)
        entries.extend(page_entries)
    if first is None:
        return None, errors
    expected_offsets: list[int] = []
    cursor = 0
    for result in pages:
        expected_offsets.append(cursor)
        if result.get("has_older") is True:
            next_offset = result.get("next_offset")
            if type(next_offset) is not int or next_offset <= cursor:
                errors.append(f"{command}: next_offset is invalid")
                break
            cursor = next_offset
        else:
            break
    if offsets != expected_offsets[: len(offsets)]:
        errors.append(f"{command}: pagination is not consecutive")
    if pages[-1].get("has_older") is not False:
        errors.append(f"{command}: capture is truncated")
    if first.get("total_matches") != len(entries):
        errors.append(f"{command}: total_matches does not equal captured entries")
    sequences = [
        entry.get("seq")
        for entry in entries
        if type(entry.get("seq")) is int
    ]
    if len(sequences) != len(entries) or len(set(sequences)) != len(sequences):
        errors.append(f"{command}: entry sequence values are invalid or duplicated")
    entries.sort(key=lambda entry: entry["seq"])
    return {
        "count": first.get("count"),
        "capacity": first.get("capacity"),
        "total_written": first.get("total_written"),
        "dropped_oldest": first.get("dropped_oldest"),
        "retained_epoch": first.get("retained_epoch"),
        "content_revision": first.get("content_revision"),
        "entries": entries,
    }, errors


def _contact_projection(
    result: dict[str, Any],
) -> tuple[dict[str, Any] | None, list[str]]:
    errors: list[str] = []
    if not _command_ok(result, "contacts") or result.get("persisted") is not True:
        return None, ["contacts: command or persistence failed"]
    entries = result.get("entries")
    count = result.get("count")
    if (
        type(count) is not int
        or not isinstance(entries, list)
        or not all(isinstance(entry, dict) for entry in entries)
    ):
        return None, ["contacts: entries are invalid"]
    if count != len(entries):
        errors.append(
            "contacts: snapshot is truncated; all retained contacts are required"
        )
    durable_fields = (
        "seq",
        "fingerprint",
        "public_key",
        "alias",
        "heard_name",
        "type",
        "verification_source",
        "verified_at_ms",
        "signed_advert_timestamp",
        "canonical",
        "can_dm",
        "favorite",
        "muted",
    )
    projected: list[dict[str, Any]] = []
    for index, entry in enumerate(entries):
        if any(field not in entry for field in durable_fields):
            errors.append(f"contacts: entry {index} lacks durable fields")
            continue
        projected.append({field: entry[field] for field in durable_fields})
    projected.sort(key=lambda entry: entry["seq"])
    return {
        "count": count,
        "capacity": result.get("capacity"),
        "total_written": result.get("total_written"),
        "dropped_oldest": result.get("dropped_oldest"),
        "entries": projected,
    }, errors


def _read_state_projection(
    result: dict[str, Any],
) -> tuple[dict[str, Any] | None, list[str]]:
    fields = (
        "public_unread",
        "dm_unread",
        "muted_dm_unread",
        "dm_thread_count",
        "last_public_read_seq",
        "last_dm_read_seq",
        "newest_public_rx_seq",
        "newest_dm_rx_seq",
        "mark_read_count",
        "dm_threads",
    )
    if not _command_ok(result, "messages unread") or result.get("persisted") is not True:
        return None, ["messages unread: command or persistence failed"]
    if any(field not in result for field in fields):
        return None, ["messages unread: retained fields are incomplete"]
    if not isinstance(result.get("dm_threads"), list):
        return None, ["messages unread: dm_threads is invalid"]
    return {field: result[field] for field in fields}, []


def recompute_state_capture(
    capture: object,
    commit: str,
) -> tuple[dict[str, Any] | None, list[str], dict[str, Any]]:
    errors: list[str] = []
    raw_results: dict[str, Any] = {}
    if not isinstance(capture, dict) or not isinstance(capture.get("commands"), list):
        return None, ["state capture is missing commands"], raw_results
    results: list[tuple[str, dict[str, Any]]] = []
    for index, row in enumerate(capture["commands"]):
        result, row_errors = recompute_raw_command(row)
        errors.extend(f"state command[{index}]: {error}" for error in row_errors)
        if result is not None and isinstance(row, dict):
            results.append((str(row.get("command")), result))
    by_exact = {request: result for request, result in results}
    version = by_exact.get("version")
    health = by_exact.get("health")
    crashlog = by_exact.get("crashlog")
    settings = by_exact.get("settings get")
    unread = by_exact.get("messages unread")
    contacts = by_exact.get("contacts")
    if not exact_version(version, commit):
        errors.append("state: exact candidate version identity failed")
    if not exact_health(health):
        errors.append("state: exact candidate health identity/readiness failed")
    if not _command_ok(crashlog, "crashlog"):
        errors.append("state: crashlog command failed")
    settings_projection = (
        _settings_projection(settings) if isinstance(settings, dict) else None
    )
    if settings_projection is None:
        errors.append("state: settings projection failed")

    public_pages = [
        result
        for request, result in results
        if request == "messages public"
        or request.startswith("messages public offset ")
    ]
    dm_pages = [
        result
        for request, result in results
        if request == "messages dm"
        or request.startswith("messages dm offset ")
    ]
    public_projection, public_errors = _page_projection(
        "messages public", public_pages
    )
    direct_projection, direct_errors = _page_projection(
        "messages dm", dm_pages
    )
    errors.extend(public_errors)
    errors.extend(direct_errors)
    contacts_projection, contact_errors = (
        _contact_projection(contacts)
        if isinstance(contacts, dict)
        else (None, ["contacts: result is missing"])
    )
    unread_projection, unread_errors = (
        _read_state_projection(unread)
        if isinstance(unread, dict)
        else (None, ["messages unread: result is missing"])
    )
    errors.extend(contact_errors)
    errors.extend(unread_errors)

    raw_results = {
        "version": version,
        "health": health,
        "crashlog": crashlog,
    }
    if any(
        value is None
        for value in (
            settings_projection,
            public_projection,
            direct_projection,
            contacts_projection,
            unread_projection,
        )
    ):
        return None, errors, raw_results
    projection = {
        "settings": settings_projection,
        "public_messages": public_projection,
        "direct_messages": direct_projection,
        "read_state": unread_projection,
        "contacts": contacts_projection,
    }
    return projection, errors, raw_results


def projection_sha256(projection: dict[str, Any]) -> str:
    return sha256_bytes(canonical_json(projection))


def state_preserved(
    baseline: dict[str, Any],
    observed: dict[str, Any],
) -> tuple[bool, list[str]]:
    errors: list[str] = []
    if baseline.get("settings") != observed.get("settings"):
        errors.append("settings changed or were lost")
    if baseline.get("read_state") != observed.get("read_state"):
        errors.append("message read-state changed or was lost")
    for key in ("public_messages", "direct_messages"):
        before = baseline.get(key)
        after = observed.get(key)
        if not isinstance(before, dict) or not isinstance(after, dict):
            errors.append(f"{key} projection is missing")
            continue
        before_entries = {
            canonical_json(entry) for entry in before.get("entries", [])
        }
        after_entries = {
            canonical_json(entry) for entry in after.get("entries", [])
        }
        if not before_entries.issubset(after_entries):
            errors.append(f"{key} lost retained rows")
        for counter in ("total_written", "dropped_oldest", "content_revision"):
            before_value = before.get(counter)
            after_value = after.get(counter)
            if (
                type(before_value) is not int
                or type(after_value) is not int
                or after_value < before_value
            ):
                errors.append(f"{key} counter {counter} regressed")
    before_contacts = baseline.get("contacts")
    after_contacts = observed.get("contacts")
    if not isinstance(before_contacts, dict) or not isinstance(after_contacts, dict):
        errors.append("contacts projection is missing")
    else:
        before_rows = {
            canonical_json(entry) for entry in before_contacts.get("entries", [])
        }
        after_rows = {
            canonical_json(entry) for entry in after_contacts.get("entries", [])
        }
        if not before_rows.issubset(after_rows):
            errors.append("contacts lost retained rows")
    return not errors, errors


def recompute_local_canary_mutation(
    result: object,
    *,
    expected: dict[str, Any],
) -> tuple[dict[str, Any] | None, list[str]]:
    errors: list[str] = []
    if not _command_ok(result, "core retained-canary"):
        return None, ["local retained canary command failed"]
    assert isinstance(result, dict)
    required = {
        "token": expected.get("token"),
        "message_text": expected.get("message_text"),
        "fingerprint": expected.get("contact_fingerprint"),
        "public_key": expected.get("contact_public_key"),
        "persisted": True,
        "retention": "nvs",
        "backend_mode": "nvs_disabled",
        "synthetic_local": True,
        "retained_flush": "ESP_OK",
        "public_rf_tx": False,
        "dm_rf_tx": False,
        "sd_access": False,
        "rp2040_access": False,
        "formats_sd": False,
        "predecessor_evidence_used": False,
    }
    for key, expected_value in required.items():
        observed = result.get(key)
        if key == "fingerprint" and isinstance(observed, str):
            observed = observed.upper()
        if key == "public_key" and isinstance(observed, str):
            observed = observed.lower()
        if observed != expected_value:
            errors.append(f"local retained canary {key} mismatch")
    if result.get("contact_result") not in {"created", "updated", "promoted"}:
        errors.append("local retained canary contact result is not admissible")
    sequence_fields = ("public_seq", "dm_seq", "contact_seq")
    for key in sequence_fields:
        if type(result.get(key)) is not int or result[key] <= 0:
            errors.append(f"local retained canary {key} is invalid")
    if errors:
        return None, errors
    return {
        **expected,
        **{key: result[key] for key in sequence_fields},
    }, []


def canary_check(
    projection: dict[str, Any],
    *,
    canary: dict[str, Any],
) -> tuple[bool, list[str]]:
    errors: list[str] = []
    if (
        projection.get("settings", {}).get("node_name")
        != canary.get("settings_node_name")
    ):
        errors.append("settings canary is missing")
    public_rows = projection.get("public_messages", {}).get("entries", [])
    matching_public = [
        row
        for row in public_rows
        if (
            isinstance(row, dict)
            and row.get("seq") == canary.get("public_seq")
            and row.get("text") == canary.get("message_text")
            and row.get("direction") == "rx"
            and row.get("author") == "Core Canary"
            and row.get("delivered") is True
        )
    ]
    if len(matching_public) != 1:
        errors.append("exact retained Public canary is missing or duplicated")
    normalized_fingerprint = str(
        canary.get("contact_fingerprint") or ""
    ).upper()
    direct_rows = projection.get("direct_messages", {}).get("entries", [])
    matching_direct = [
        row
        for row in direct_rows
        if (
            isinstance(row, dict)
            and row.get("seq") == canary.get("dm_seq")
            and row.get("text") == canary.get("message_text")
            and str(row.get("fingerprint") or "").upper()
            == normalized_fingerprint
            and row.get("alias") == "Core Canary"
            and row.get("direction") == "rx"
            and row.get("delivered") is True
            and row.get("acked") is False
        )
    ]
    if len(matching_direct) != 1:
        errors.append("exact retained DM canary is missing or duplicated")
    matching_contacts = [
        row
        for row in projection.get("contacts", {}).get("entries", [])
        if (
            isinstance(row, dict)
            and row.get("seq") == canary.get("contact_seq")
            and str(row.get("fingerprint") or "").upper()
            == normalized_fingerprint
            and str(row.get("public_key") or "").lower()
            == canary.get("contact_public_key")
            and row.get("verification_source") == "uri_import"
            and row.get("canonical") is True
            and row.get("can_dm") is True
        )
    ]
    if len(matching_contacts) != 1:
        errors.append("exact canonical local contact canary is missing or duplicated")
    return not errors, errors


def _port_record(item: object) -> dict[str, Any]:
    def value(name: str) -> Any:
        if isinstance(item, dict):
            return item.get(name)
        return getattr(item, name, None)

    return {
        "device": normalize_port(value("device")),
        "description": value("description"),
        "hwid": value("hwid"),
        "serial_number": value("serial_number"),
        "vid": value("vid"),
        "pid": value("pid"),
        "location": value("location"),
        "manufacturer": value("manufacturer"),
        "product": value("product"),
    }


def port_snapshot(
    port_lister: Callable[[], Iterable[object]],
    *,
    now: Callable[[], str] = utc_now,
    clock: Callable[[], float] = time.monotonic,
) -> dict[str, Any]:
    matches = [
        _port_record(item)
        for item in port_lister()
        if normalize_port(
            item.get("device") if isinstance(item, dict) else getattr(item, "device", None)
        )
        == D1L_CORE_PORT
    ]
    return {
        "observed_at": now(),
        "monotonic_sec": round(clock(), 6),
        "port": D1L_CORE_PORT,
        "present": len(matches) == 1,
        "matches": matches,
    }


def port_identity(snapshot: object) -> str | None:
    if not isinstance(snapshot, dict) or snapshot.get("present") is not True:
        return None
    matches = snapshot.get("matches")
    if not isinstance(matches, list) or len(matches) != 1:
        return None
    match = matches[0]
    if not isinstance(match, dict):
        return None
    strong = (
        match.get("serial_number"),
        match.get("hwid"),
        match.get("vid"),
        match.get("pid"),
        match.get("location"),
    )
    if not any(value not in (None, "") for value in strong):
        return None
    return sha256_bytes(canonical_json(match))


def wait_for_port_state(
    port_lister: Callable[[], Iterable[object]],
    *,
    present: bool,
    timeout: float,
    poll_sec: float,
    clock: Callable[[], float] = time.monotonic,
    now: Callable[[], str] = utc_now,
    sleep: Callable[[float], None] = time.sleep,
) -> tuple[bool, list[dict[str, Any]]]:
    deadline = clock() + timeout
    samples: list[dict[str, Any]] = []
    while clock() <= deadline:
        sample = port_snapshot(port_lister, now=now, clock=clock)
        samples.append(sample)
        if sample.get("present") is present:
            return True, samples
        sleep(poll_sec)
    return False, samples


def prove_power_off_window(
    port_lister: Callable[[], Iterable[object]],
    *,
    minimum_sec: float,
    poll_sec: float,
    clock: Callable[[], float] = time.monotonic,
    now: Callable[[], str] = utc_now,
    sleep: Callable[[float], None] = time.sleep,
) -> tuple[bool, list[dict[str, Any]], float]:
    started = clock()
    samples: list[dict[str, Any]] = []
    while clock() - started < minimum_sec:
        sample = port_snapshot(port_lister, now=now, clock=clock)
        samples.append(sample)
        if sample.get("present") is not False:
            return False, samples, max(0.0, clock() - started)
        sleep(min(poll_sec, max(0.0, minimum_sec - (clock() - started))))
    final = port_snapshot(port_lister, now=now, clock=clock)
    samples.append(final)
    duration = max(0.0, clock() - started)
    return final.get("present") is False, samples, duration


def _crash_transition(
    before: object, after: object, allowed_reasons: set[str] | frozenset[str]
) -> tuple[bool, dict[str, Any]]:
    detail = {
        "total_delta": None,
        "new_entries": [],
    }
    if not (
        isinstance(before, dict)
        and isinstance(after, dict)
        and _command_ok(before, "crashlog")
        and _command_ok(after, "crashlog")
        and type(before.get("total_written")) is int
        and type(after.get("total_written")) is int
        and isinstance(before.get("entries"), list)
        and isinstance(after.get("entries"), list)
    ):
        return False, detail
    detail["total_delta"] = after["total_written"] - before["total_written"]
    max_before = max(
        (
            entry.get("seq")
            for entry in before["entries"]
            if isinstance(entry, dict) and type(entry.get("seq")) is int
        ),
        default=0,
    )
    new_entries = [
        entry
        for entry in after["entries"]
        if (
            isinstance(entry, dict)
            and type(entry.get("seq")) is int
            and entry["seq"] > max_before
        )
    ]
    detail["new_entries"] = new_entries
    ok = (
        detail["total_delta"] == 1
        and len(new_entries) == 1
        and new_entries[0].get("crash_like") is False
        and str(new_entries[0].get("reset_reason") or "").upper()
        in allowed_reasons
    )
    return ok, detail


def _reboot_ack_ok(result: object) -> bool:
    return (
        _command_ok(result, "reboot")
        and isinstance(result, dict)
        and result.get("rebooting") is True
        and result.get("reset_scope") == "system"
        and result.get("storage_manager_quiesced") is True
        and result.get("retained_worker_quiesced") is True
        and result.get("rp2040_bridge_quiesced") is True
        and result.get("connectivity_prepare") == "ESP_OK"
        and result.get("retained_flush") == "ESP_OK"
        and result.get("route_flush") == "ESP_OK"
    )


def _transition_checks(
    pre_raw: dict[str, Any],
    post_raw: dict[str, Any],
    *,
    cycle_type: str,
) -> tuple[bool, dict[str, Any], list[str]]:
    errors: list[str] = []
    pre_health = pre_raw.get("health")
    post_health = post_raw.get("health")
    if not isinstance(pre_health, dict) or not isinstance(post_health, dict):
        return False, {}, ["health transition is missing"]
    nonce_changed = (
        type(pre_health.get("boot_nonce")) is int
        and type(post_health.get("boot_nonce")) is int
        and pre_health.get("boot_nonce") != post_health.get("boot_nonce")
    )
    post_uptime = post_health.get("uptime_ms")
    uptime_reset_window = (
        type(post_uptime) is int and 0 <= post_uptime <= 120000
    )
    if not nonce_changed:
        errors.append("boot nonce did not change")
    if not uptime_reset_window:
        errors.append("post-boot uptime is outside the bounded reset window")
    expected_reasons = (
        frozenset({"SW"}) if cycle_type == "software" else COLD_RESET_REASONS
    )
    reset_reason = str(post_health.get("reset_reason") or "").upper()
    if reset_reason not in expected_reasons:
        errors.append(f"unexpected {cycle_type} reset reason: {reset_reason}")
    crash_ok, crash_detail = _crash_transition(
        pre_raw.get("crashlog"),
        post_raw.get("crashlog"),
        expected_reasons,
    )
    if not crash_ok:
        errors.append("crashlog transition is not exactly one non-crash boot")
    return not errors, {
        "pre_boot_nonce": pre_health.get("boot_nonce"),
        "post_boot_nonce": post_health.get("boot_nonce"),
        "pre_uptime_ms": pre_health.get("uptime_ms"),
        "post_uptime_ms": post_uptime,
        "post_reset_reason": reset_reason,
        "nonce_changed": nonce_changed,
        "uptime_reset_window": uptime_reset_window,
        "crashlog": crash_detail,
    }, errors


def validate_seed_receipt(
    receipt: object,
    *,
    commit: str,
    run_id: str,
    run_attempt: str,
) -> tuple[dict[str, Any] | None, list[str]]:
    errors: list[str] = []
    if not isinstance(receipt, dict):
        return None, ["seed receipt is not an object"]
    if (
        exact_commit(commit) is None
        or not positive_decimal(run_id)
        or not positive_decimal(run_attempt)
    ):
        return None, ["seed expected candidate identity is invalid"]
    required = {
        "schema": 1,
        "kind": "core_retained_state_seed",
        "mode": "hardware",
        "ok": True,
        "closure_eligible": False,
        "hardware_required": True,
        "physical_observed": True,
        "port": D1L_CORE_PORT,
        "commit": commit,
        "github_actions_run": run_id,
        "workflow_run_attempt": run_attempt,
        "release_profile": CORE_RELEASE_PROFILE,
        "sd_history_mode": SD_HISTORY_MODE,
        "public_rf_tx": False,
        "dm_rf_tx": False,
        "sd_access": False,
        "rp2040_access": False,
        "formats_sd": False,
        "predecessor_evidence_used": False,
    }
    for key, expected in required.items():
        if receipt.get(key) != expected:
            errors.append(f"seed: {key} mismatch")
    source = receipt.get("git")
    if not (
        isinstance(source, dict)
        and exact_commit(source.get("commit")) == commit
        and source.get("dirty") is False
        and source.get("dirty_entries") == []
    ):
        errors.append("seed: source git is not the exact clean candidate")
    initial_projection, initial_errors, _ = recompute_state_capture(
        receipt.get("initial_state_capture"), commit
    )
    errors.extend(f"seed initial: {error}" for error in initial_errors)
    if initial_projection is None:
        errors.append("seed: initial exact-candidate state is missing")
    local_result, local_raw_errors = recompute_raw_command(
        receipt.get("local_retained_canary_mutation")
    )
    errors.extend(
        f"seed local mutation: {error}" for error in local_raw_errors
    )
    expected_canary = candidate_canary(commit, run_id, run_attempt)
    expected_local_command = (
        f"core retained-canary {expected_canary['token']}"
    )
    local_receipt = receipt.get("local_retained_canary_mutation")
    if not (
        isinstance(local_receipt, dict)
        and local_receipt.get("command") == expected_local_command
    ):
        errors.append("seed local mutation: exact command mismatch")
    derived_canary, local_errors = recompute_local_canary_mutation(
        local_result,
        expected=expected_canary,
    )
    errors.extend(f"seed local mutation: {error}" for error in local_errors)
    settings_result, settings_errors = recompute_raw_command(
        receipt.get("settings_canary_mutation")
    )
    errors.extend(f"seed settings mutation: {error}" for error in settings_errors)
    settings_receipt = receipt.get("settings_canary_mutation")
    if not (
        isinstance(settings_receipt, dict)
        and settings_receipt.get("command")
        == f"settings set name {expected_canary['settings_node_name']}"
    ):
        errors.append("seed settings mutation: exact command mismatch")
    projection, capture_errors, _ = recompute_state_capture(
        receipt.get("state_capture"), commit
    )
    errors.extend(f"seed: {error}" for error in capture_errors)
    canary = receipt.get("canary")
    if not isinstance(canary, dict):
        errors.append("seed: canary descriptor is missing")
    elif derived_canary is None:
        errors.append("seed: local canary descriptor cannot be derived from raw")
    else:
        if canary != derived_canary:
            errors.append("seed: canary descriptor differs from raw mutation")
        if not (
            _command_ok(settings_result, "settings set name")
            and settings_result.get("persisted") is True
            and settings_result.get("node_name")
            == expected_canary["settings_node_name"]
        ):
            errors.append("seed: settings canary mutation is not proven")
        if projection is not None:
            canary_ok, canary_errors = canary_check(
                projection,
                canary=derived_canary,
            )
            if not canary_ok:
                errors.extend(f"seed: {error}" for error in canary_errors)
    if projection is not None and receipt.get("projection_sha256") != projection_sha256(
        projection
    ):
        errors.append("seed: projection digest mismatch")
    return projection, errors


def _snapshot_has_canaries(
    snapshot: dict[str, Any],
    canary: dict[str, Any],
    commit: str,
) -> bool:
    results = snapshot.get("results")
    if not isinstance(results, list):
        return False
    rows = [row for row in results if isinstance(row, dict)]

    def first(command: str) -> dict[str, Any] | None:
        return next((row for row in rows if row.get("cmd") == command), None)

    if not exact_version(first("version"), commit):
        return False
    if not exact_health(first("health")):
        return False
    projected: dict[str, Any] = {
        "settings": first("settings get") or {},
        "public_messages": {
            "entries": [
                entry
                for row in rows
                if row.get("cmd") == "messages public"
                and isinstance(row.get("entries"), list)
                for entry in row["entries"]
                if isinstance(entry, dict)
            ],
        },
        "direct_messages": {
            "entries": [
                entry
                for row in rows
                if row.get("cmd") == "messages dm"
                and isinstance(row.get("entries"), list)
                for entry in row["entries"]
                if isinstance(entry, dict)
            ],
        },
        "contacts": {
            "entries": (
                first("contacts").get("entries", [])
                if isinstance(first("contacts"), dict)
                else []
            ),
        },
    }
    canary_ok, _ = canary_check(projected, canary=canary)
    return canary_ok


def validate_closing_flash_receipt(
    receipt: object,
    *,
    root: Path,
    commit: str,
    run_id: str,
    run_attempt: str,
    canary: dict[str, Any],
) -> list[str]:
    errors: list[str] = []
    if not isinstance(receipt, dict):
        return ["flash receipt is not an object"]
    if (
        exact_commit(commit) is None
        or not positive_decimal(run_id)
        or not positive_decimal(run_attempt)
    ):
        return ["flash expected candidate identity is invalid"]
    required = {
        "schema": 1,
        "kind": "esp32_flash",
        "mode": "hardware",
        "scope": "core-retained-reflash-only",
        "flash_phase": "retained-reflash",
        "ok": True,
        "closure_eligible": True,
        "hardware_required": True,
        "physical_observed": True,
        "port": D1L_CORE_PORT,
        "commit": commit,
        "github_actions_run": run_id,
        "workflow_run_attempt": run_attempt,
        "release_profile": CORE_RELEASE_PROFILE,
        "sd_history_mode": SD_HISTORY_MODE,
        "firmware_identity_ok": True,
        "runner_source_identity_ok": True,
        "retained_state_preserved": True,
        "erase_flash": False,
        "public_rf_tx": False,
        "dm_rf_tx": False,
        "sd_access": False,
        "rp2040_access": False,
        "formats_sd": False,
        "legacy_suite_ran": False,
    }
    for key, expected in required.items():
        if receipt.get(key) != expected:
            errors.append(f"flash: {key} mismatch")
    source = receipt.get("git")
    if not (
        isinstance(source, dict)
        and exact_commit(source.get("commit")) == commit
        and source.get("dirty") is False
        and source.get("dirty_entries") == []
    ):
        errors.append("flash: source git is not the exact clean candidate")
    command = receipt.get("command")
    if not (
        isinstance(command, list)
        and "write-flash" in command
        and not any("erase" in str(token).lower() for token in command)
        and D1L_CORE_PORT in [normalize_port(token) for token in command]
    ):
        errors.append("flash: command is not exact non-erasing COM12 write-flash")
    for label, field in (
        ("flash raw log", "raw_flash_log"),
        ("pre-flash retained snapshot", "retained_state_before"),
        ("post-flash retained snapshot", "retained_state_after"),
    ):
        path, row_errors = validate_file_row(receipt.get(field), root, label)
        errors.extend(row_errors)
        if path is not None and field != "raw_flash_log":
            try:
                snapshot = load_json(path, root, label)
            except ValueError as exc:
                errors.append(str(exc))
                continue
            expected_phase = "pre_flash" if field == "retained_state_before" else "post_flash"
            projection = snapshot.get("projection")
            if not (
                snapshot.get("schema") == 1
                and snapshot.get("kind") == "core_retained_state_snapshot"
                and snapshot.get("mode") == "hardware"
                and snapshot.get("phase") == expected_phase
                and snapshot.get("port") == D1L_CORE_PORT
                and exact_commit(snapshot.get("expected_firmware_commit")) == commit
                and isinstance(projection, dict)
                and snapshot.get("projection_sha256") == projection_sha256(projection)
                and _snapshot_has_canaries(snapshot, canary, commit)
            ):
                errors.append(f"flash: {expected_phase} snapshot failed canary/identity checks")
    return errors


def _cycle_base(
    *,
    matrix_id: str,
    cycle_type: str,
    ordinal: int,
    commit: str,
    run_id: str,
    run_attempt: str,
    seed_sha256: str,
    flash_sha256: str,
    previous_sha256: str,
) -> dict[str, Any]:
    return {
        "schema": 1,
        "kind": "core_reboot_persistence_cycle",
        "mode": "hardware",
        "matrix_id": matrix_id,
        "cycle_id": f"{matrix_id}:{cycle_type}:{ordinal}",
        "cycle_type": cycle_type,
        "ordinal": ordinal,
        "port": D1L_CORE_PORT,
        "baud": D1L_BAUD,
        "commit": commit,
        "github_actions_run": run_id,
        "workflow_run_attempt": run_attempt,
        "release_profile": CORE_RELEASE_PROFILE,
        "sd_history_mode": SD_HISTORY_MODE,
        "seed_receipt_sha256": seed_sha256,
        "closing_flash_receipt_sha256": flash_sha256,
        "previous_receipt_sha256": previous_sha256,
        "started_at": utc_now(),
        "ended_at": None,
        "pre": None,
        "post": None,
        "action": None,
        "checks": {},
        "ok": False,
        "hardware_required": True,
        "physical_observed": True,
        "public_rf_tx": False,
        "formats_sd": False,
        "predecessor_evidence_used": False,
    }


def _open_serial(serial_module: Any, timeout: float) -> Any:
    return open_d1l_serial(
        serial_module,
        port=D1L_CORE_PORT,
        baudrate=D1L_BAUD,
        timeout=timeout,
    )


def _capture_and_recompute(
    ser: Any,
    *,
    timeout: float,
    commit: str,
    clock: Callable[[], float],
    now: Callable[[], str],
) -> tuple[dict[str, Any], dict[str, Any] | None, list[str], dict[str, Any]]:
    capture = capture_state(ser, timeout, clock=clock, now=now)
    projection, errors, raw = recompute_state_capture(capture, commit)
    return capture, projection, errors, raw


def run_software_cycle(
    *,
    serial_module: Any,
    port_lister: Callable[[], Iterable[object]],
    timeout: float,
    transition_timeout: float,
    commit: str,
    baseline: dict[str, Any],
    report: dict[str, Any],
    clock: Callable[[], float] = time.monotonic,
    now: Callable[[], str] = utc_now,
    sleep: Callable[[float], None] = time.sleep,
) -> dict[str, Any]:
    errors: list[str] = []
    before_port = port_snapshot(port_lister, now=now, clock=clock)
    with _open_serial(serial_module, timeout) as ser:
        if hasattr(ser, "reset_input_buffer"):
            ser.reset_input_buffer()
        pre, pre_projection, pre_errors, pre_raw = _capture_and_recompute(
            ser, timeout=timeout, commit=commit, clock=clock, now=now
        )
        report["pre"] = pre
        errors.extend(f"pre: {error}" for error in pre_errors)
        if pre_projection is not None:
            preserved, state_errors = state_preserved(baseline, pre_projection)
            if not preserved:
                errors.extend(f"pre: {error}" for error in state_errors)
        reboot = read_raw_command(
            ser, "reboot", max(timeout, 30.0), clock=clock, now=now
        )
        reboot_result, reboot_errors = recompute_raw_command(reboot)
        errors.extend(f"reboot: {error}" for error in reboot_errors)
        boot_lines = capture_boot_lines(
            ser, transition_timeout, clock=clock, now=now
        )
        if hasattr(ser, "reset_input_buffer"):
            ser.reset_input_buffer()
        sleep(0.1)
        post, post_projection, post_errors, post_raw = _capture_and_recompute(
            ser, timeout=timeout, commit=commit, clock=clock, now=now
        )
        report["post"] = post
        errors.extend(f"post: {error}" for error in post_errors)
    after_port = port_snapshot(port_lister, now=now, clock=clock)
    boot_analysis, boot_errors = analyze_boot_lines(boot_lines)
    errors.extend(boot_errors)
    raw_sw_reset = (
        boot_analysis.get("reset_count") == 1
        and boot_analysis.get("reset_events") == [
            {"code": SOFTWARE_SYSTEM_RESET[0], "reason": SOFTWARE_SYSTEM_RESET[1]}
        ]
        and boot_analysis.get("help_count") >= 1
        and boot_analysis.get("crash_marker_count") == 0
    )
    if not raw_sw_reset:
        errors.append("software reboot raw boot/reset evidence failed")
    if not _reboot_ack_ok(reboot_result):
        errors.append("software reboot acknowledgement failed")
    transition_ok, transition, transition_errors = _transition_checks(
        pre_raw, post_raw, cycle_type="software"
    )
    errors.extend(transition_errors)
    if pre_projection is not None and post_projection is not None:
        preserved, state_errors = state_preserved(pre_projection, post_projection)
        if not preserved:
            errors.extend(f"post: {error}" for error in state_errors)
        baseline_preserved, baseline_errors = state_preserved(
            baseline, post_projection
        )
        if not baseline_preserved:
            errors.extend(f"baseline: {error}" for error in baseline_errors)
    before_identity = port_identity(before_port)
    after_identity = port_identity(after_port)
    port_ok = (
        before_port.get("present") is True
        and after_port.get("present") is True
        and before_identity is not None
        and before_identity == after_identity
    )
    if not port_ok:
        errors.append("COM12 presence/identity changed across software reboot")
    report["action"] = {
        "kind": "software_reboot",
        "reboot_command": reboot,
        "boot_raw_lines": boot_lines,
        "boot_analysis": boot_analysis,
        "port_disappear_required": False,
        "port_before": before_port,
        "port_after": after_port,
    }
    report["checks"] = {
        "raw_reboot_ack": _reboot_ack_ok(reboot_result),
        "raw_sw_system_reset": raw_sw_reset,
        "transition": transition,
        "transition_ok": transition_ok,
        "port_identity_stable": port_ok,
        "retained_state_preserved": not any(
            error.startswith(("pre:", "post:", "baseline:")) for error in errors
        ),
        "errors": errors,
    }
    report["ok"] = not errors
    report["ended_at"] = now()
    return report


def run_cold_cycle(
    *,
    serial_module: Any,
    port_lister: Callable[[], Iterable[object]],
    prompt: Callable[[str], str],
    timeout: float,
    port_timeout: float,
    port_poll_sec: float,
    minimum_power_off_sec: float,
    commit: str,
    baseline: dict[str, Any],
    report: dict[str, Any],
    clock: Callable[[], float] = time.monotonic,
    now: Callable[[], str] = utc_now,
    sleep: Callable[[float], None] = time.sleep,
) -> dict[str, Any]:
    errors: list[str] = []
    before_port = port_snapshot(port_lister, now=now, clock=clock)
    with _open_serial(serial_module, timeout) as ser:
        if hasattr(ser, "reset_input_buffer"):
            ser.reset_input_buffer()
        pre, pre_projection, pre_errors, pre_raw = _capture_and_recompute(
            ser, timeout=timeout, commit=commit, clock=clock, now=now
        )
    report["pre"] = pre
    errors.extend(f"pre: {error}" for error in pre_errors)
    if pre_projection is not None:
        preserved, state_errors = state_preserved(baseline, pre_projection)
        if not preserved:
            errors.extend(f"pre: {error}" for error in state_errors)

    prompt(
        f"Cold cycle {report['ordinal']}/{COLD_CYCLE_COUNT}: press Enter to arm, "
        f"then remove all power from the D1L until {D1L_CORE_PORT} disappears."
    )
    disappeared, disappear_samples = wait_for_port_state(
        port_lister,
        present=False,
        timeout=port_timeout,
        poll_sec=port_poll_sec,
        clock=clock,
        now=now,
        sleep=sleep,
    )
    off_ok = False
    off_samples: list[dict[str, Any]] = []
    off_duration = 0.0
    if disappeared:
        off_ok, off_samples, off_duration = prove_power_off_window(
            port_lister,
            minimum_sec=minimum_power_off_sec,
            poll_sec=port_poll_sec,
            clock=clock,
            now=now,
            sleep=sleep,
        )
    if not disappeared:
        errors.append("COM12 did not disappear during cold power removal")
    if not off_ok or off_duration < minimum_power_off_sec:
        errors.append("cold power-off interval was not continuously observed")

    prompt(
        f"Cold cycle {report['ordinal']}/{COLD_CYCLE_COUNT}: press Enter, "
        f"then restore power and wait for {D1L_CORE_PORT}."
    )
    reappeared, reappear_samples = wait_for_port_state(
        port_lister,
        present=True,
        timeout=port_timeout,
        poll_sec=port_poll_sec,
        clock=clock,
        now=now,
        sleep=sleep,
    )
    after_port = reappear_samples[-1] if reappear_samples else port_snapshot(
        port_lister, now=now, clock=clock
    )
    if not reappeared:
        errors.append("COM12 did not reappear after cold power restore")

    post: dict[str, Any] = {"captured_at": now(), "commands": []}
    post_projection: dict[str, Any] | None = None
    post_raw: dict[str, Any] = {}
    if reappeared:
        with _open_serial(serial_module, timeout) as ser:
            sleep(1.0)
            post, post_projection, post_errors, post_raw = _capture_and_recompute(
                ser, timeout=timeout, commit=commit, clock=clock, now=now
            )
            errors.extend(f"post: {error}" for error in post_errors)
    report["post"] = post

    transition_ok, transition, transition_errors = _transition_checks(
        pre_raw, post_raw, cycle_type="cold"
    )
    errors.extend(transition_errors)
    if pre_projection is not None and post_projection is not None:
        preserved, state_errors = state_preserved(pre_projection, post_projection)
        if not preserved:
            errors.extend(f"post: {error}" for error in state_errors)
        baseline_preserved, baseline_errors = state_preserved(
            baseline, post_projection
        )
        if not baseline_preserved:
            errors.extend(f"baseline: {error}" for error in baseline_errors)
    before_identity = port_identity(before_port)
    after_identity = port_identity(after_port)
    port_ok = (
        before_port.get("present") is True
        and disappeared
        and off_ok
        and reappeared
        and before_identity is not None
        and before_identity == after_identity
    )
    if not port_ok:
        errors.append("cold-cycle COM12 disappearance/reappearance identity failed")
    report["action"] = {
        "kind": "operator_controlled_cold_power_cycle",
        "operator_interactive": True,
        "minimum_power_off_sec": minimum_power_off_sec,
        "observed_power_off_sec": round(off_duration, 6),
        "port_before": before_port,
        "disappear_samples": disappear_samples,
        "power_off_samples": off_samples,
        "reappear_samples": reappear_samples,
        "port_after": after_port,
    }
    report["checks"] = {
        "port_disappeared": disappeared,
        "power_off_window_observed": off_ok,
        "port_reappeared": reappeared,
        "port_identity_stable": port_ok,
        "transition": transition,
        "transition_ok": transition_ok,
        "retained_state_preserved": not any(
            error.startswith(("pre:", "post:", "baseline:")) for error in errors
        ),
        "errors": errors,
    }
    report["ok"] = not errors
    report["ended_at"] = now()
    return report


def _port_action_recomputed(
    action: object,
    *,
    cycle_type: str,
) -> tuple[bool, list[str]]:
    errors: list[str] = []
    if not isinstance(action, dict):
        return False, ["cycle action is missing"]
    before = action.get("port_before")
    after = action.get("port_after")
    before_identity = port_identity(before)
    after_identity = port_identity(after)
    stable = (
        before_identity is not None
        and after_identity is not None
        and before_identity == after_identity
    )
    if cycle_type == "software":
        if not (
            action.get("kind") == "software_reboot"
            and action.get("port_disappear_required") is False
            and isinstance(before, dict)
            and before.get("present") is True
            and isinstance(after, dict)
            and after.get("present") is True
            and stable
        ):
            errors.append("software cycle port evidence failed")
        return not errors, errors
    disappear = action.get("disappear_samples")
    off = action.get("power_off_samples")
    reappear = action.get("reappear_samples")
    minimum = action.get("minimum_power_off_sec")
    observed = action.get("observed_power_off_sec")
    off_times = [
        sample.get("monotonic_sec")
        for sample in off
        if isinstance(sample, dict)
    ] if isinstance(off, list) else []
    off_times_valid = (
        len(off_times) >= 2
        and all(type(value) in (int, float) for value in off_times)
        and all(
            later >= earlier
            for earlier, later in zip(off_times, off_times[1:])
        )
    )
    recomputed_off_duration = (
        float(off_times[-1]) - float(off_times[0])
        if off_times_valid
        else -1.0
    )
    if not (
        action.get("kind") == "operator_controlled_cold_power_cycle"
        and action.get("operator_interactive") is True
        and isinstance(disappear, list)
        and disappear
        and any(
            isinstance(sample, dict) and sample.get("present") is False
            for sample in disappear
        )
        and isinstance(off, list)
        and len(off) >= 2
        and all(
            isinstance(sample, dict) and sample.get("present") is False
            for sample in off
        )
        and type(minimum) in (int, float)
        and minimum >= MINIMUM_POWER_OFF_SEC
        and type(observed) in (int, float)
        and observed >= minimum
        and off_times_valid
        and recomputed_off_duration >= minimum
        and abs(float(observed) - recomputed_off_duration) <= 0.01
        and isinstance(reappear, list)
        and reappear
        and any(
            isinstance(sample, dict) and sample.get("present") is True
            for sample in reappear
        )
        and stable
    ):
        errors.append("cold cycle raw port transition evidence failed")
    return not errors, errors


def recompute_cycle(
    receipt: object,
    *,
    commit: str,
    run_id: str,
    run_attempt: str,
    matrix_id: str,
    baseline: dict[str, Any],
) -> tuple[bool, list[str]]:
    errors: list[str] = []
    if not isinstance(receipt, dict):
        return False, ["cycle receipt is not an object"]
    cycle_type = receipt.get("cycle_type")
    if cycle_type not in {"software", "cold"}:
        return False, ["cycle type is invalid"]
    required = {
        "schema": 1,
        "kind": "core_reboot_persistence_cycle",
        "mode": "hardware",
        "matrix_id": matrix_id,
        "port": D1L_CORE_PORT,
        "baud": D1L_BAUD,
        "commit": commit,
        "github_actions_run": run_id,
        "workflow_run_attempt": run_attempt,
        "release_profile": CORE_RELEASE_PROFILE,
        "sd_history_mode": SD_HISTORY_MODE,
        "hardware_required": True,
        "physical_observed": True,
        "public_rf_tx": False,
        "formats_sd": False,
        "predecessor_evidence_used": False,
    }
    for key, expected in required.items():
        if receipt.get(key) != expected:
            errors.append(f"cycle: {key} mismatch")
    pre_projection, pre_errors, pre_raw = recompute_state_capture(
        receipt.get("pre"), commit
    )
    post_projection, post_errors, post_raw = recompute_state_capture(
        receipt.get("post"), commit
    )
    errors.extend(f"pre: {error}" for error in pre_errors)
    errors.extend(f"post: {error}" for error in post_errors)
    if pre_projection is not None:
        preserved, state_errors = state_preserved(baseline, pre_projection)
        if not preserved:
            errors.extend(f"pre baseline: {error}" for error in state_errors)
    if pre_projection is not None and post_projection is not None:
        preserved, state_errors = state_preserved(pre_projection, post_projection)
        if not preserved:
            errors.extend(f"post: {error}" for error in state_errors)
        preserved, state_errors = state_preserved(baseline, post_projection)
        if not preserved:
            errors.extend(f"post baseline: {error}" for error in state_errors)
    transition_ok, _, transition_errors = _transition_checks(
        pre_raw, post_raw, cycle_type=cycle_type
    )
    errors.extend(transition_errors)
    action = receipt.get("action")
    port_ok, port_errors = _port_action_recomputed(
        action, cycle_type=cycle_type
    )
    errors.extend(port_errors)
    if cycle_type == "software":
        reboot_result, reboot_errors = recompute_raw_command(
            action.get("reboot_command") if isinstance(action, dict) else None
        )
        errors.extend(reboot_errors)
        if not _reboot_ack_ok(reboot_result):
            errors.append("software reboot raw acknowledgement failed")
        boot_analysis, boot_errors = analyze_boot_lines(
            action.get("boot_raw_lines") if isinstance(action, dict) else None
        )
        errors.extend(boot_errors)
        if not (
            boot_analysis.get("reset_count") == 1
            and boot_analysis.get("reset_events")
            == [
                {
                    "code": SOFTWARE_SYSTEM_RESET[0],
                    "reason": SOFTWARE_SYSTEM_RESET[1],
                }
            ]
            and boot_analysis.get("help_count") >= 1
            and boot_analysis.get("crash_marker_count") == 0
        ):
            errors.append("software reboot raw boot/reset evidence failed")
    return not errors and transition_ok and port_ok, errors


def validate_core_reboot_persistence_receipt(
    matrix_path: Path,
    *,
    root: Path,
    expected_commit: str,
    expected_run_id: str,
    expected_run_attempt: str,
) -> tuple[bool, list[str], dict[str, Any]]:
    errors: list[str] = []
    matrix = load_json(matrix_path, root, "Core reboot persistence matrix")
    commit = exact_commit(expected_commit)
    run_id = str(expected_run_id)
    run_attempt = str(expected_run_attempt)
    if (
        commit is None
        or not positive_decimal(run_id)
        or not positive_decimal(run_attempt)
    ):
        return False, ["validator expected identity is invalid"], matrix
    required = {
        "schema": 1,
        "kind": "core_reboot_persistence_matrix",
        "mode": "hardware",
        "ok": True,
        "closure_eligible": True,
        "hardware_required": True,
        "physical_observed": True,
        "port": D1L_CORE_PORT,
        "commit": commit,
        "github_actions_run": run_id,
        "workflow_run_attempt": run_attempt,
        "release_profile": CORE_RELEASE_PROFILE,
        "sd_history_mode": SD_HISTORY_MODE,
        "claim": CLAIM,
        "cross_version_migration_proven": False,
        "predecessor_evidence_used": False,
        "software_cycle_count": SOFTWARE_CYCLE_COUNT,
        "cold_cycle_count": COLD_CYCLE_COUNT,
        "public_rf_tx": False,
        "formats_sd": False,
    }
    for key, expected in required.items():
        if matrix.get(key) != expected:
            errors.append(f"matrix: {key} mismatch")
    matrix_id = matrix.get("matrix_id")
    if not isinstance(matrix_id, str) or not re.fullmatch(
        r"[0-9a-f]{32}", matrix_id
    ):
        errors.append("matrix: matrix_id is invalid")
        matrix_id = ""
    source = matrix.get("git")
    if not (
        isinstance(source, dict)
        and exact_commit(source.get("commit")) == commit
        and source.get("dirty") is False
        and source.get("dirty_entries") == []
    ):
        errors.append("matrix: source git is not the exact clean candidate")

    seed_path, seed_row_errors = validate_file_row(
        matrix.get("seed_receipt"), root, "seed receipt"
    )
    flash_path, flash_row_errors = validate_file_row(
        matrix.get("closing_flash_receipt"), root, "closing flash receipt"
    )
    errors.extend(seed_row_errors)
    errors.extend(flash_row_errors)
    baseline: dict[str, Any] | None = None
    seed: dict[str, Any] = {}
    if seed_path is not None:
        seed = load_json(seed_path, root, "seed receipt")
        baseline, seed_errors = validate_seed_receipt(
            seed,
            commit=commit,
            run_id=run_id,
            run_attempt=run_attempt,
        )
        errors.extend(seed_errors)
    if flash_path is not None:
        flash = load_json(flash_path, root, "closing flash receipt")
        errors.extend(
            validate_closing_flash_receipt(
                flash,
                root=root,
                commit=commit,
                run_id=run_id,
                run_attempt=run_attempt,
                canary=seed.get("canary", {}),
            )
        )

    live_projection, live_errors, _ = recompute_state_capture(
        matrix.get("post_reinstall_live_capture"), commit
    )
    errors.extend(f"post-reinstall live: {error}" for error in live_errors)
    if baseline is not None and live_projection is not None:
        live_ok, live_state_errors = state_preserved(
            baseline, live_projection
        )
        if not live_ok:
            errors.extend(
                f"post-reinstall live: {error}"
                for error in live_state_errors
            )
        if matrix.get("post_reinstall_projection_sha256") != projection_sha256(
            live_projection
        ):
            errors.append("matrix: post-reinstall projection digest mismatch")

    cycles = matrix.get("cycle_receipts")
    if not isinstance(cycles, list) or len(cycles) != (
        SOFTWARE_CYCLE_COUNT + COLD_CYCLE_COUNT
    ):
        errors.append("matrix: cycle receipt count is not exactly eight")
        cycles = []
    seen_paths: set[str] = set()
    seen_hashes: set[str] = set()
    seen_ids: set[str] = set()
    expected_previous = (
        matrix.get("closing_flash_receipt", {}).get("sha256")
        if isinstance(matrix.get("closing_flash_receipt"), dict)
        else None
    )
    types: list[str] = []
    ordinals: dict[str, list[int]] = {"software": [], "cold": []}
    for index, row in enumerate(cycles):
        path, row_errors = validate_file_row(
            row, root, f"cycle receipt {index}"
        )
        errors.extend(row_errors)
        if not isinstance(row, dict):
            continue
        raw_path = row.get("path")
        digest = row.get("sha256")
        if raw_path in seen_paths:
            errors.append("matrix: duplicate cycle path")
        if digest in seen_hashes:
            errors.append("matrix: duplicate cycle content/hash")
        seen_paths.add(str(raw_path))
        seen_hashes.add(str(digest))
        if path is None or baseline is None:
            continue
        cycle = load_json(path, root, f"cycle receipt {index}")
        cycle_id = cycle.get("cycle_id")
        if cycle_id in seen_ids:
            errors.append("matrix: duplicate cycle_id")
        seen_ids.add(str(cycle_id))
        if cycle.get("previous_receipt_sha256") != expected_previous:
            errors.append(f"matrix: cycle {index} hash chain mismatch")
        if cycle.get("seed_receipt_sha256") != (
            matrix.get("seed_receipt", {}).get("sha256")
            if isinstance(matrix.get("seed_receipt"), dict)
            else None
        ):
            errors.append(f"matrix: cycle {index} seed hash mismatch")
        if cycle.get("closing_flash_receipt_sha256") != (
            matrix.get("closing_flash_receipt", {}).get("sha256")
            if isinstance(matrix.get("closing_flash_receipt"), dict)
            else None
        ):
            errors.append(f"matrix: cycle {index} flash hash mismatch")
        expected_previous = digest
        cycle_type = cycle.get("cycle_type")
        ordinal = cycle.get("ordinal")
        if cycle_type in ordinals and type(ordinal) is int:
            types.append(cycle_type)
            ordinals[cycle_type].append(ordinal)
        cycle_ok, cycle_errors = recompute_cycle(
            cycle,
            commit=commit,
            run_id=run_id,
            run_attempt=run_attempt,
            matrix_id=matrix_id,
            baseline=baseline,
        )
        if not cycle_ok:
            errors.extend(
                f"cycle {cycle_type} {ordinal}: {error}"
                for error in cycle_errors
            )
    if types != (
        ["software"] * SOFTWARE_CYCLE_COUNT
        + ["cold"] * COLD_CYCLE_COUNT
    ):
        errors.append("matrix: cycle type order is invalid")
    if ordinals["software"] != list(range(1, SOFTWARE_CYCLE_COUNT + 1)):
        errors.append("matrix: software ordinals are invalid")
    if ordinals["cold"] != list(range(1, COLD_CYCLE_COUNT + 1)):
        errors.append("matrix: cold ordinals are invalid")
    if matrix.get("hash_chain_tail") != expected_previous:
        errors.append("matrix: hash chain tail mismatch")
    if matrix.get("all_child_receipts_unique") is not True:
        errors.append("matrix: uniqueness declaration is false")
    return not errors, errors, matrix


def seed_retained_state(
    *,
    root: Path,
    out: Path,
    serial_module: Any,
    commit: str,
    run_id: str,
    run_attempt: str,
    timeout: float,
    source_git: dict[str, Any],
    now: Callable[[], str] = utc_now,
    clock: Callable[[], float] = time.monotonic,
) -> dict[str, Any]:
    expected_canary = candidate_canary(commit, run_id, run_attempt)
    local_mutation: dict[str, Any] | None = None
    settings_mutation: dict[str, Any] | None = None
    local_result: dict[str, Any] | None = None
    settings_result: dict[str, Any] | None = None
    derived_canary: dict[str, Any] | None = None
    local_errors: list[str] = []
    settings_errors: list[str] = []
    with _open_serial(serial_module, timeout) as ser:
        if hasattr(ser, "reset_input_buffer"):
            ser.reset_input_buffer()
        initial = capture_state(ser, timeout, clock=clock, now=now)
        initial_projection, initial_errors, _ = recompute_state_capture(
            initial, commit
        )
        if initial_projection is not None and not initial_errors:
            local_mutation = read_raw_command(
                ser,
                f"core retained-canary {expected_canary['token']}",
                timeout,
                clock=clock,
                now=now,
            )
            local_result, local_raw_errors = recompute_raw_command(
                local_mutation
            )
            local_errors.extend(local_raw_errors)
            local_canary, local_canary_errors = (
                recompute_local_canary_mutation(
                    local_result,
                    expected=expected_canary,
                )
            )
            local_errors.extend(local_canary_errors)
            if not local_errors:
                derived_canary = local_canary
                settings_mutation = read_raw_command(
                    ser,
                    "settings set name "
                    f"{expected_canary['settings_node_name']}",
                    timeout,
                    clock=clock,
                    now=now,
                )
                settings_result, settings_errors = recompute_raw_command(
                    settings_mutation
                )
            else:
                settings_errors.append(
                    "settings canary mutation skipped because the "
                    "candidate-local retained mutation failed"
                )
            final = capture_state(ser, timeout, clock=clock, now=now)
        else:
            final = initial
            local_errors.append(
                "candidate-local mutations skipped because initial exact "
                "candidate state failed"
            )
            settings_errors.append(
                "settings canary mutation skipped because initial exact "
                "candidate state failed"
            )
    projection, capture_errors, _ = recompute_state_capture(final, commit)
    errors = [
        *(f"initial: {error}" for error in initial_errors),
        *(f"local mutation: {error}" for error in local_errors),
        *(f"settings mutation: {error}" for error in settings_errors),
        *(f"final: {error}" for error in capture_errors),
    ]
    if initial_projection is None:
        errors.append("initial exact-candidate state capture failed")
    if not (
        _command_ok(settings_result, "settings set name")
        and settings_result.get("persisted") is True
        and settings_result.get("node_name")
        == expected_canary["settings_node_name"]
    ):
        errors.append("settings canary mutation failed")
    canary = derived_canary or expected_canary
    canary_errors: list[str] = []
    if projection is not None and derived_canary is not None:
        canary_ok, canary_errors = canary_check(
            projection,
            canary=derived_canary,
        )
        if not canary_ok:
            errors.extend(canary_errors)
    else:
        errors.append(
            "final retained projection or raw-derived canary is unavailable"
        )
    report = {
        "schema": 1,
        "kind": "core_retained_state_seed",
        "mode": "hardware",
        "ok": not errors,
        "closure_eligible": False,
        "hardware_required": True,
        "physical_observed": True,
        "port": D1L_CORE_PORT,
        "baud": D1L_BAUD,
        "commit": commit,
        "github_actions_run": run_id,
        "workflow_run_attempt": run_attempt,
        "release_profile": CORE_RELEASE_PROFILE,
        "sd_history_mode": SD_HISTORY_MODE,
        "git": source_git,
        "captured_at": now(),
        "canary": canary,
        "initial_state_capture": initial,
        "local_retained_canary_mutation": local_mutation,
        "settings_canary_mutation": settings_mutation,
        "state_capture": final,
        "projection_sha256": (
            projection_sha256(projection) if projection is not None else None
        ),
        "checks": {
            "exact_candidate": not initial_errors and not capture_errors,
            "candidate_local_retained_mutation_proven": (
                derived_canary is not None and not local_errors
            ),
            "settings_canary_persisted": not any(
                error.startswith("settings canary") for error in errors
            ),
            "retained_public_and_dm_canaries_present": (
                projection is not None
                and derived_canary is not None
                and not canary_errors
            ),
            "canonical_contact_canary_present": (
                projection is not None
                and derived_canary is not None
                and not canary_errors
            ),
            "errors": errors,
        },
        "public_rf_tx": False,
        "dm_rf_tx": False,
        "sd_access": False,
        "rp2040_access": False,
        "formats_sd": False,
        "predecessor_evidence_used": False,
    }
    write_json_once(out, root, report)
    return report


def verify_reboot_matrix(
    *,
    root: Path,
    out: Path,
    seed_path: Path,
    flash_path: Path,
    serial_module: Any,
    port_lister: Callable[[], Iterable[object]],
    prompt: Callable[[str], str],
    commit: str,
    run_id: str,
    run_attempt: str,
    timeout: float,
    transition_timeout: float,
    port_timeout: float,
    port_poll_sec: float,
    minimum_power_off_sec: float,
    source_git: dict[str, Any],
    now: Callable[[], str] = utc_now,
    clock: Callable[[], float] = time.monotonic,
    sleep: Callable[[float], None] = time.sleep,
) -> dict[str, Any]:
    if (
        exact_commit(commit) is None
        or not positive_decimal(run_id)
        or not positive_decimal(run_attempt)
    ):
        raise ValueError("reboot matrix candidate identity is invalid")
    if out.exists():
        raise ValueError(f"refusing to overwrite evidence: {out}")
    child_dir = _inside_output(
        root, out.with_suffix("").with_name(out.stem + "_cycles"), "cycle directory"
    )
    if child_dir.exists():
        raise ValueError(f"refusing to reuse cycle evidence directory: {child_dir}")

    seed = load_json(seed_path, root, "seed receipt")
    baseline, seed_errors = validate_seed_receipt(
        seed,
        commit=commit,
        run_id=run_id,
        run_attempt=run_attempt,
    )
    if seed_errors or baseline is None:
        raise ValueError("seed receipt failed validation: " + "; ".join(seed_errors))
    flash = load_json(flash_path, root, "closing flash receipt")
    flash_errors = validate_closing_flash_receipt(
        flash,
        root=root,
        commit=commit,
        run_id=run_id,
        run_attempt=run_attempt,
        canary=seed.get("canary", {}),
    )
    if flash_errors:
        raise ValueError(
            "closing flash receipt failed validation: " + "; ".join(flash_errors)
        )
    seed_row = relative_file_row(seed_path, root, "seed receipt")
    flash_row = relative_file_row(flash_path, root, "closing flash receipt")

    # Verify the live post-reinstall state before any reboot.
    with _open_serial(serial_module, timeout) as ser:
        if hasattr(ser, "reset_input_buffer"):
            ser.reset_input_buffer()
        live_capture = capture_state(ser, timeout, clock=clock, now=now)
    live_projection, live_errors, _ = recompute_state_capture(
        live_capture, commit
    )
    if live_projection is None:
        raise ValueError(
            "post-reinstall live state capture failed: " + "; ".join(live_errors)
        )
    live_preserved, live_state_errors = state_preserved(
        baseline, live_projection
    )
    if live_errors or not live_preserved:
        raise ValueError(
            "post-reinstall live state does not preserve the seed: "
            + "; ".join([*live_errors, *live_state_errors])
        )

    matrix_id = uuid.uuid4().hex
    child_dir.mkdir(parents=True, exist_ok=False)
    previous_sha = flash_row["sha256"]
    cycle_rows: list[dict[str, Any]] = []
    all_cycles_ok = True
    for ordinal in range(1, SOFTWARE_CYCLE_COUNT + 1):
        cycle = _cycle_base(
            matrix_id=matrix_id,
            cycle_type="software",
            ordinal=ordinal,
            commit=commit,
            run_id=run_id,
            run_attempt=run_attempt,
            seed_sha256=seed_row["sha256"],
            flash_sha256=flash_row["sha256"],
            previous_sha256=previous_sha,
        )
        cycle = run_software_cycle(
            serial_module=serial_module,
            port_lister=port_lister,
            timeout=timeout,
            transition_timeout=transition_timeout,
            commit=commit,
            baseline=baseline,
            report=cycle,
            clock=clock,
            now=now,
            sleep=sleep,
        )
        child = child_dir / f"software_{ordinal}.json"
        write_json_once(child, root, cycle)
        row = relative_file_row(child, root, f"software cycle {ordinal}")
        cycle_rows.append(row)
        previous_sha = row["sha256"]
        all_cycles_ok = all_cycles_ok and cycle.get("ok") is True
        if not cycle.get("ok"):
            break

    if all_cycles_ok:
        for ordinal in range(1, COLD_CYCLE_COUNT + 1):
            cycle = _cycle_base(
                matrix_id=matrix_id,
                cycle_type="cold",
                ordinal=ordinal,
                commit=commit,
                run_id=run_id,
                run_attempt=run_attempt,
                seed_sha256=seed_row["sha256"],
                flash_sha256=flash_row["sha256"],
                previous_sha256=previous_sha,
            )
            cycle = run_cold_cycle(
                serial_module=serial_module,
                port_lister=port_lister,
                prompt=prompt,
                timeout=timeout,
                port_timeout=port_timeout,
                port_poll_sec=port_poll_sec,
                minimum_power_off_sec=minimum_power_off_sec,
                commit=commit,
                baseline=baseline,
                report=cycle,
                clock=clock,
                now=now,
                sleep=sleep,
            )
            child = child_dir / f"cold_{ordinal}.json"
            write_json_once(child, root, cycle)
            row = relative_file_row(child, root, f"cold cycle {ordinal}")
            cycle_rows.append(row)
            previous_sha = row["sha256"]
            all_cycles_ok = all_cycles_ok and cycle.get("ok") is True
            if not cycle.get("ok"):
                break

    complete = len(cycle_rows) == SOFTWARE_CYCLE_COUNT + COLD_CYCLE_COUNT
    report = {
        "schema": 1,
        "kind": "core_reboot_persistence_matrix",
        "mode": "hardware",
        "ok": bool(all_cycles_ok and complete),
        "closure_eligible": bool(all_cycles_ok and complete),
        "hardware_required": True,
        "physical_observed": True,
        "matrix_id": matrix_id,
        "port": D1L_CORE_PORT,
        "baud": D1L_BAUD,
        "commit": commit,
        "github_actions_run": run_id,
        "workflow_run_attempt": run_attempt,
        "release_profile": CORE_RELEASE_PROFILE,
        "sd_history_mode": SD_HISTORY_MODE,
        "claim": CLAIM,
        "cross_version_migration_proven": False,
        "predecessor_evidence_used": False,
        "git": source_git,
        "seed_receipt": seed_row,
        "closing_flash_receipt": flash_row,
        "post_reinstall_live_capture": live_capture,
        "post_reinstall_projection_sha256": projection_sha256(live_projection),
        "software_cycle_count": SOFTWARE_CYCLE_COUNT,
        "cold_cycle_count": COLD_CYCLE_COUNT,
        "cycle_receipts": cycle_rows,
        "all_child_receipts_unique": len(
            {row["sha256"] for row in cycle_rows}
        )
        == len(cycle_rows),
        "hash_chain_tail": previous_sha,
        "started_at": seed.get("captured_at"),
        "ended_at": now(),
        "public_rf_tx": False,
        "formats_sd": False,
    }
    write_json_once(out, root, report)
    if report["ok"]:
        validated, validation_errors, _ = validate_core_reboot_persistence_receipt(
            out,
            root=root,
            expected_commit=commit,
            expected_run_id=run_id,
            expected_run_attempt=run_attempt,
        )
        if not validated:
            raise ValueError(
                "written reboot matrix failed raw validation: "
                + "; ".join(validation_errors)
            )
    return report


def _resolve(root: Path, value: str) -> Path:
    path = Path(value)
    return path.resolve() if path.is_absolute() else (root / path).resolve()


def _serial_runtime() -> tuple[Any, Callable[[], Iterable[object]]]:
    try:
        import serial
        import serial.tools.list_ports
    except ImportError as exc:
        raise RuntimeError(
            "pyserial is required for Core reboot/persistence evidence"
        ) from exc
    return serial, serial.tools.list_ports.comports


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=".")
    parser.add_argument("--port", default=D1L_CORE_PORT)
    parser.add_argument("--baud", type=int, default=D1L_BAUD)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--github-run-id", required=True)
    parser.add_argument("--github-run-attempt", required=True)
    parser.add_argument("--timeout", type=float, default=8.0)
    subparsers = parser.add_subparsers(dest="operation", required=True)

    seed = subparsers.add_parser("seed")
    seed.add_argument("--out", required=True)

    verify = subparsers.add_parser("verify")
    verify.add_argument("--seed-receipt", required=True)
    verify.add_argument("--closing-flash-receipt", required=True)
    verify.add_argument("--transition-timeout", type=float, default=20.0)
    verify.add_argument("--port-timeout", type=float, default=120.0)
    verify.add_argument("--port-poll-sec", type=float, default=0.25)
    verify.add_argument(
        "--minimum-power-off-sec",
        type=float,
        default=MINIMUM_POWER_OFF_SEC,
    )
    verify.add_argument("--out", required=True)
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        port = enforce_core_port(args.port)
        if port != D1L_CORE_PORT or args.baud != D1L_BAUD:
            raise ValueError(
                f"only {D1L_CORE_PORT} at {D1L_BAUD} baud is permitted"
            )
        commit = exact_commit(args.commit)
        if commit is None:
            raise ValueError("--commit must be an exact 40-character SHA")
        run_id = str(args.github_run_id)
        run_attempt = str(args.github_run_attempt)
        if (
            not positive_decimal(run_id)
            or not positive_decimal(run_attempt)
        ):
            raise ValueError(
                "--github-run-id and "
                "--github-run-attempt must be a positive integer"
            )
        if args.timeout <= 0:
            raise ValueError("--timeout must be positive")
        root = Path(args.root).resolve(strict=True)
        source_git = exact_source_git(root, commit)
        serial_module, port_lister = _serial_runtime()
        if args.operation == "seed":
            report = seed_retained_state(
                root=root,
                out=_resolve(root, args.out),
                serial_module=serial_module,
                commit=commit,
                run_id=run_id,
                run_attempt=run_attempt,
                timeout=args.timeout,
                source_git=source_git,
            )
        else:
            if (
                args.transition_timeout <= 0
                or args.port_timeout <= 0
                or args.port_poll_sec <= 0
                or args.minimum_power_off_sec < MINIMUM_POWER_OFF_SEC
            ):
                raise ValueError(
                    "transition/port timeouts must be positive and cold power-off "
                    f"must be at least {MINIMUM_POWER_OFF_SEC:.1f}s"
                )
            report = verify_reboot_matrix(
                root=root,
                out=_resolve(root, args.out),
                seed_path=_resolve(root, args.seed_receipt),
                flash_path=_resolve(root, args.closing_flash_receipt),
                serial_module=serial_module,
                port_lister=port_lister,
                prompt=input,
                commit=commit,
                run_id=run_id,
                run_attempt=run_attempt,
                timeout=args.timeout,
                transition_timeout=args.transition_timeout,
                port_timeout=args.port_timeout,
                port_poll_sec=args.port_poll_sec,
                minimum_power_off_sec=args.minimum_power_off_sec,
                source_git=source_git,
            )
    except (OSError, RuntimeError, ValueError) as exc:
        print(str(exc), file=sys.stderr)
        return 2
    print(
        json.dumps(
            {
                "ok": report.get("ok") is True,
                "kind": report.get("kind"),
                "out": str(args.out),
            },
            indent=2,
        )
    )
    return 0 if report.get("ok") is True else 1


if __name__ == "__main__":
    raise SystemExit(main())
