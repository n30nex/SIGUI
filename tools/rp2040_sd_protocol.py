#!/usr/bin/env python3
"""Reference line-protocol simulator for the D1L RP2040 SD bridge."""

from __future__ import annotations

import argparse
import base64
import re
import zlib
from dataclasses import dataclass, field, replace


STATUS_REQUEST = "DESKOS_SD_STATUS"
STATUS_REPLY = "DESKOS_SD_STATUS"
FORMAT_REQUEST = "DESKOS_SD_FORMAT"
FORMAT_REPLY = "DESKOS_SD_FORMAT"
FORMAT_CONFIRMATION = "FORMAT-DESKOS-SD"
FILE_REQUEST = "DESKOS_SD_FILE"
FILE_REPLY = "DESKOS_SD_FILE"
FILE_PROTOCOL_VERSION = 1
FILE_LINE_MAX = 512
MAX_FILE_PATH_CHARS = 96
MAX_FILE_CHUNK_BYTES = 192
FILE_CANARY_TMP_PATH = "canary/filecanary.tmp"
FILE_CANARY_FINAL_PATH = "canary/filecanary.bin"
FILE_CANARY_PAYLOAD = b"DeskOS SD file canary v1"
FILE_CANARY_START_ID = 1
EXPORT_CANARY_START_ID = 20
EXPORT_CANARY_DEFAULT_TOKEN = "export1"
DIAGNOSTIC_EXPORT_START_ID = 40
DIAGNOSTIC_EXPORT_DEFAULT_TOKEN = "diag1"
STATUS_FIELDS = (
    "state",
    "present",
    "mounted",
    "deskos",
    "fs",
    "format_required",
    "format_supported",
    "capacity_kb",
    "free_kb",
    "note",
)
FILE_CAPABILITY_FIELDS = (
    "file_ops",
    "file_line_max",
    "file_chunk_max",
    "path_max",
    "atomic_rename",
)
FILE_REPLY_COMMON_FIELDS = ("v", "id", "ok", "op", "note")
FILE_ERROR_CODES = {
    "bad_request",
    "bad_value",
    "unsupported_op",
    "line_too_long",
    "not_ready",
    "no_card",
    "bad_path",
    "not_found",
    "is_dir",
    "exists",
    "range",
    "too_large",
    "decode_failed",
    "crc_mismatch",
    "open_failed",
    "read_failed",
    "write_failed",
    "flush_failed",
    "rename_failed",
    "delete_failed",
    "io_error",
    "timeout",
}
_VALID_PATH_RE = re.compile(r"^[A-Za-z0-9._/-]+$")


@dataclass(frozen=True)
class SdScenario:
    state: str
    present: bool
    mounted: bool
    deskos: bool
    fs: str
    format_required: bool
    format_supported: bool
    capacity_kb: int
    free_kb: int
    note: str


@dataclass
class SdFileSystem:
    files: dict[str, bytes] = field(default_factory=dict)
    dirs: set[str] = field(default_factory=lambda: {"."})


SCENARIOS: dict[str, SdScenario] = {
    "no-card": SdScenario(
        state="no_card",
        present=False,
        mounted=False,
        deskos=False,
        fs="none",
        format_required=False,
        format_supported=False,
        capacity_kb=0,
        free_kb=0,
        note="no_card",
    ),
    "ready": SdScenario(
        state="ready",
        present=True,
        mounted=True,
        deskos=True,
        fs="fat32",
        format_required=False,
        format_supported=True,
        capacity_kb=31166976,
        free_kb=31100000,
        note="ready",
    ),
    "format-required": SdScenario(
        state="setup_required",
        present=True,
        mounted=False,
        deskos=False,
        fs="unknown",
        format_required=True,
        format_supported=True,
        capacity_kb=31166976,
        free_kb=0,
        note="format_required",
    ),
    "root-missing": SdScenario(
        state="setup_required",
        present=True,
        mounted=True,
        deskos=False,
        fs="fat32",
        format_required=False,
        format_supported=True,
        capacity_kb=31166976,
        free_kb=31090000,
        note="deskos_root_missing",
    ),
}


def bool_token(value: bool) -> str:
    return "1" if value else "0"


def b64url(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).decode("ascii").rstrip("=")


def b64url_decode(value: str) -> bytes:
    if not value:
        return b""
    padding = "=" * ((4 - len(value) % 4) % 4)
    return base64.urlsafe_b64decode(value + padding)


def crc32_hex(data: bytes) -> str:
    return f"{zlib.crc32(data) & 0xFFFFFFFF:08X}"


def encode_path(path: str) -> str:
    if not validate_relative_path(path):
        raise ValueError(f"invalid path: {path}")
    return b64url(path.encode("ascii"))


def decode_path(path64: str) -> str:
    try:
        path = b64url_decode(path64).decode("ascii")
    except (ValueError, UnicodeDecodeError) as exc:
        raise ValueError("decode_failed") from exc
    if not validate_relative_path(path):
        raise ValueError("bad_path")
    return path


def validate_relative_path(path: str) -> bool:
    if not path or len(path) > MAX_FILE_PATH_CHARS:
        return False
    if path.startswith("/") or path.endswith("/"):
        return False
    if ".." in path or "//" in path:
        return False
    if not _VALID_PATH_RE.fullmatch(path):
        return False
    return all(segment not in {"", ".", ".."} for segment in path.split("/"))


def status_line(scenario: SdScenario, prefix: str = STATUS_REPLY) -> str:
    return (
        f"{prefix} state={scenario.state}"
        f" present={bool_token(scenario.present)}"
        f" mounted={bool_token(scenario.mounted)}"
        f" deskos={bool_token(scenario.deskos)}"
        f" fs={scenario.fs}"
        f" format_required={bool_token(scenario.format_required)}"
        f" format_supported={bool_token(scenario.format_supported)}"
        f" capacity_kb={scenario.capacity_kb}"
        f" free_kb={scenario.free_kb}"
        f" note={scenario.note}"
        f" file_ops={bool_token(file_ready(scenario))}"
        f" file_line_max={FILE_LINE_MAX}"
        f" file_chunk_max={MAX_FILE_CHUNK_BYTES}"
        f" path_max={MAX_FILE_PATH_CHARS}"
        f" atomic_rename={bool_token(file_ready(scenario))}"
    )


def formatted_scenario(scenario: SdScenario) -> SdScenario:
    if not scenario.present:
        return replace(scenario, state="no_card", note="no_card")
    if not scenario.format_supported:
        return replace(scenario, state="unsupported", note="format_unsupported")
    return replace(
        scenario,
        state="ready",
        mounted=True,
        deskos=True,
        fs="fat32",
        format_required=False,
        free_kb=scenario.capacity_kb,
        note="format_complete",
    )


def parse_tokens(request: str) -> tuple[str, dict[str, str]]:
    parts = request.strip().split()
    if not parts:
        raise ValueError("empty request")
    tokens: dict[str, str] = {}
    for token in parts[1:]:
        if "=" not in token:
            raise ValueError(f"malformed token: {token}")
        key, value = token.split("=", 1)
        tokens[key] = value
    return parts[0], tokens


def parse_uint_token(tokens: dict[str, str], key: str) -> int:
    try:
        value = int(tokens[key], 10)
    except (KeyError, ValueError) as exc:
        raise ValueError("bad_value") from exc
    if value < 0 or value > 0xFFFFFFFF:
        raise ValueError("bad_value")
    return value


def parse_bool_token(tokens: dict[str, str], key: str) -> bool:
    value = tokens.get(key)
    if value == "1":
        return True
    if value == "0":
        return False
    raise ValueError("bad_value")


def file_ready(scenario: SdScenario) -> bool:
    return scenario.present and scenario.mounted and scenario.deskos and not scenario.format_required


def file_line(**tokens: object) -> str:
    merged = {"v": FILE_PROTOCOL_VERSION, **tokens}
    return " ".join([FILE_REPLY, *(f"{key}={value}" for key, value in merged.items())])


def file_request_line(request_id: int, op: str, **tokens: object) -> str:
    merged = {"v": FILE_PROTOCOL_VERSION, "id": request_id, "op": op, **tokens}
    return " ".join([FILE_REQUEST, *(f"{key}={value}" for key, value in merged.items())])


def file_error(request_id: int, op: str, err: str) -> str:
    assert err in FILE_ERROR_CODES
    return file_line(id=request_id, ok=0, op=op or "unknown", err=err, note=err)


def require_file_ready(scenario: SdScenario) -> str | None:
    if file_ready(scenario):
        return None
    return "no_card" if not scenario.present else "not_ready"


def command_identity(tokens: dict[str, str]) -> tuple[int, str]:
    request_id = parse_uint_token(tokens, "id")
    if request_id == 0 or request_id > 65535:
        raise ValueError("bad_value")
    op = tokens.get("op", "unknown")
    return request_id, op


def decode_request_path(tokens: dict[str, str], key: str = "path") -> str:
    try:
        return decode_path(tokens[key])
    except KeyError as exc:
        raise ValueError("bad_path") from exc


def decode_request_data(tokens: dict[str, str], expected_len: int) -> bytes:
    if expected_len > MAX_FILE_CHUNK_BYTES:
        raise ValueError("too_large")
    try:
        data = b64url_decode(tokens.get("data", ""))
    except ValueError as exc:
        raise ValueError("decode_failed") from exc
    if len(data) != expected_len:
        raise ValueError("bad_value")
    if tokens.get("crc", "").upper() != crc32_hex(data):
        raise ValueError("crc_mismatch")
    return data


def ensure_parent_dirs(path: str, fs: SdFileSystem) -> None:
    parts = path.split("/")[:-1]
    current = ""
    for part in parts:
        current = part if not current else f"{current}/{part}"
        fs.dirs.add(current)


def stat_file(request_id: int, path: str, fs: SdFileSystem) -> str:
    data = fs.files.get(path)
    if data is not None:
        return file_line(id=request_id, ok=1, op="stat", exists=1, kind="file", size=len(data), note="ok")
    if path in fs.dirs:
        return file_line(id=request_id, ok=1, op="stat", exists=1, kind="dir", size=0, note="ok")
    return file_line(id=request_id, ok=1, op="stat", exists=0, kind="none", size=0, note="ok")


def read_file(request_id: int, tokens: dict[str, str], fs: SdFileSystem) -> str:
    path = decode_request_path(tokens)
    offset = parse_uint_token(tokens, "off")
    requested_len = parse_uint_token(tokens, "len")
    if requested_len > MAX_FILE_CHUNK_BYTES:
        raise ValueError("too_large")
    data = fs.files.get(path)
    if data is None:
        raise ValueError("not_found")
    if offset > len(data):
        raise ValueError("range")
    chunk = data[offset : offset + requested_len]
    return file_line(
        id=request_id,
        ok=1,
        op="read",
        off=offset,
        len=len(chunk),
        eof=bool_token(offset + len(chunk) >= len(data)),
        data=b64url(chunk),
        crc=crc32_hex(chunk),
        note="ok",
    )


def write_file(request_id: int, tokens: dict[str, str], fs: SdFileSystem) -> str:
    path = decode_request_path(tokens)
    offset = parse_uint_token(tokens, "off")
    expected_len = parse_uint_token(tokens, "len")
    truncate = parse_bool_token(tokens, "trunc")
    data = decode_request_data(tokens, expected_len)
    if truncate and offset != 0:
        raise ValueError("range")
    current = b"" if truncate else fs.files.get(path, b"")
    if offset != len(current):
        raise ValueError("range")
    ensure_parent_dirs(path, fs)
    fs.files[path] = current + data
    return file_line(
        id=request_id,
        ok=1,
        op="write",
        off=offset,
        len=len(data),
        size=len(fs.files[path]),
        note="ok",
    )


def append_file(request_id: int, tokens: dict[str, str], fs: SdFileSystem) -> str:
    path = decode_request_path(tokens)
    expected_len = parse_uint_token(tokens, "len")
    if expected_len == 0:
        raise ValueError("bad_value")
    data = decode_request_data(tokens, expected_len)
    current = fs.files.get(path, b"")
    ensure_parent_dirs(path, fs)
    fs.files[path] = current + data
    return file_line(
        id=request_id,
        ok=1,
        op="append",
        off=len(current),
        len=len(data),
        size=len(fs.files[path]),
        note="ok",
    )


def delete_file(request_id: int, tokens: dict[str, str], fs: SdFileSystem) -> str:
    path = decode_request_path(tokens)
    if path not in fs.files:
        raise ValueError("not_found")
    del fs.files[path]
    return file_line(id=request_id, ok=1, op="delete", note="ok")


def rename_file(request_id: int, tokens: dict[str, str], fs: SdFileSystem) -> str:
    source = decode_request_path(tokens)
    target = decode_request_path(tokens, "to")
    replace_target = parse_bool_token(tokens, "replace")
    if source not in fs.files:
        raise ValueError("not_found")
    if target in fs.files and not replace_target:
        raise ValueError("exists")
    ensure_parent_dirs(target, fs)
    fs.files[target] = fs.files.pop(source)
    return file_line(id=request_id, ok=1, op="rename", note="ok")


def file_reply_for_request(request: str, scenario: SdScenario, fs: SdFileSystem) -> str:
    if len(request) > FILE_LINE_MAX:
        return file_error(0, "unknown", "line_too_long")
    try:
        prefix, tokens = parse_tokens(request)
        if prefix != FILE_REQUEST:
            raise ValueError("bad_request")
        if parse_uint_token(tokens, "v") != FILE_PROTOCOL_VERSION:
            raise ValueError("bad_value")
        request_id, op = command_identity(tokens)
        if op not in {"stat", "read", "write", "append", "delete", "rename"}:
            raise ValueError("unsupported_op")
        not_ready = require_file_ready(scenario)
        if not_ready:
            raise ValueError(not_ready)
        if op == "stat":
            return stat_file(request_id, decode_request_path(tokens), fs)
        if op == "read":
            return read_file(request_id, tokens, fs)
        if op == "write":
            return write_file(request_id, tokens, fs)
        if op == "append":
            return append_file(request_id, tokens, fs)
        if op == "delete":
            return delete_file(request_id, tokens, fs)
        if op == "rename":
            return rename_file(request_id, tokens, fs)
    except ValueError as exc:
        err = str(exc) if str(exc) in FILE_ERROR_CODES else "bad_request"
        request_id = 0
        op = "unknown"
        try:
            _, tokens = parse_tokens(request)
            request_id, op = command_identity(tokens)
        except ValueError:
            pass
        return file_error(request_id, op, err)
    raise AssertionError("unreachable")


def reply_for_request(
    request: str,
    scenario: SdScenario,
    fs: SdFileSystem | None = None,
) -> str:
    request = request.strip()
    fs = fs or SdFileSystem()
    if request == STATUS_REQUEST:
        return status_line(scenario)
    if request.startswith(FORMAT_REQUEST + " "):
        phrase = request[len(FORMAT_REQUEST) + 1 :].strip()
        if phrase != FORMAT_CONFIRMATION:
            refused = replace(scenario, state="confirmation_required", note="confirmation_required")
            return status_line(refused, FORMAT_REPLY)
        fs.files.clear()
        fs.dirs = {"."}
        return status_line(formatted_scenario(scenario), FORMAT_REPLY)
    if request.startswith(FILE_REQUEST):
        return file_reply_for_request(request, scenario, fs)
    raise ValueError(f"unsupported request: {request}")


def file_canary_requests(request_id_start: int = FILE_CANARY_START_ID) -> list[str]:
    tmp = encode_path(FILE_CANARY_TMP_PATH)
    final = encode_path(FILE_CANARY_FINAL_PATH)
    payload = FILE_CANARY_PAYLOAD
    payload_len = len(payload)
    payload64 = b64url(payload)
    payload_crc = crc32_hex(payload)
    request_id = request_id_start
    requests = [
        file_request_line(request_id, "delete", path=tmp),
        file_request_line(request_id + 1, "delete", path=final),
        file_request_line(
            request_id + 2,
            "write",
            path=tmp,
            off=0,
            len=payload_len,
            trunc=1,
            data=payload64,
            crc=payload_crc,
        ),
        file_request_line(request_id + 3, "read", path=tmp, off=0, len=payload_len),
        file_request_line(request_id + 4, "rename", path=tmp, to=final, replace=1),
        file_request_line(request_id + 5, "stat", path=final),
        file_request_line(request_id + 6, "read", path=final, off=0, len=payload_len),
        file_request_line(request_id + 7, "delete", path=final),
        file_request_line(request_id + 8, "stat", path=final),
    ]
    return requests


def file_canary_transcript(
    scenario: SdScenario,
    request_id_start: int = FILE_CANARY_START_ID,
) -> list[dict[str, str]]:
    fs = SdFileSystem()
    transcript = []
    for request in file_canary_requests(request_id_start):
        transcript.append({"request": request, "reply": reply_for_request(request, scenario, fs)})
    return transcript


def export_canary_paths(token: str) -> tuple[str, str]:
    return (
        f"exports/diagnostics/export-canary-{token}.tmp",
        f"exports/diagnostics/export-canary-{token}.json",
    )


def export_canary_payload(token: str) -> bytes:
    return (
        f'{{"schema":1,"kind":"diagnostic_export_canary","token":"{token}",'
        '"public_rf_tx":false,"formats_sd":false}\n'
    ).encode("ascii")


def export_canary_requests(token: str, request_id_start: int = EXPORT_CANARY_START_ID) -> list[str]:
    tmp_path, final_path = export_canary_paths(token)
    tmp = encode_path(tmp_path)
    final = encode_path(final_path)
    payload = export_canary_payload(token)
    payload64 = b64url(payload)
    payload_crc = crc32_hex(payload)
    request_id = request_id_start
    return [
        file_request_line(request_id, "delete", path=tmp),
        file_request_line(
            request_id + 1,
            "write",
            path=tmp,
            off=0,
            len=len(payload),
            trunc=1,
            data=payload64,
            crc=payload_crc,
        ),
        file_request_line(request_id + 2, "read", path=tmp, off=0, len=len(payload)),
        file_request_line(request_id + 3, "rename", path=tmp, to=final, replace=1),
        file_request_line(request_id + 4, "stat", path=final),
        file_request_line(request_id + 5, "read", path=final, off=0, len=len(payload)),
    ]


def export_canary_transcript(
    scenario: SdScenario,
    token: str = EXPORT_CANARY_DEFAULT_TOKEN,
    request_id_start: int = EXPORT_CANARY_START_ID,
) -> list[dict[str, str]]:
    fs = SdFileSystem()
    transcript = []
    for request in export_canary_requests(token, request_id_start):
        transcript.append({"request": request, "reply": reply_for_request(request, scenario, fs)})
    return transcript


def diagnostic_export_paths(token: str) -> tuple[str, str]:
    return (
        f"exports/diagnostics/diagnostic-export-{token}.tmp",
        f"exports/diagnostics/diagnostic-export-{token}.json",
    )


def diagnostic_export_payload(token: str) -> bytes:
    return (
        f'{{"schema":1,"kind":"diagnostic_export","token":"{token}",'
        '"public_rf_tx":false,"formats_sd":false,'
        '"storage":{"sd_state":"ready","data_backend":"mixed","export_backend":"sd_diagnostic_exports_ready"},'
        '"health":{"uptime_ms":123456,"heap_free":100000,"heap_min_free":99000,'
        '"heap_largest_free":50000,"psram_free":200000,"psram_min_free":199000,'
        '"psram_largest_free":150000,"current_task_stack_free_words":1200,'
        '"ui_task_stack_free_words":1300,"lvgl_used_pct":42,"reset_reason":"SW"},'
        '"limits":{"payload_max":4096,"file_chunk_max":192,"crashlog_capacity":8,"sampled":true},'
        '"map_tiles":{"exported":false,"reason":"pending"},'
        '"crashlog":{"count":1,"capacity":8,"total_written":3,"dropped_oldest":0,'
        '"entries":[{"seq":3,"uptime_ms":50,"reset_reason":"SW","reset_reason_code":3,'
        '"crash_like":false,"heap_free":100000,"heap_min_free":99000,"psram_free":200000}]}}\n'
    ).encode("ascii")


def chunk_ranges(total_len: int, chunk_len: int = MAX_FILE_CHUNK_BYTES) -> list[tuple[int, int]]:
    return [
        (offset, min(chunk_len, total_len - offset))
        for offset in range(0, total_len, chunk_len)
    ]


def diagnostic_export_requests(
    token: str,
    request_id_start: int = DIAGNOSTIC_EXPORT_START_ID,
) -> list[str]:
    tmp_path, final_path = diagnostic_export_paths(token)
    tmp = encode_path(tmp_path)
    final = encode_path(final_path)
    payload = diagnostic_export_payload(token)
    request_id = request_id_start
    requests = [file_request_line(request_id, "delete", path=tmp)]
    request_id += 1
    for offset, chunk_len in chunk_ranges(len(payload)):
        chunk = payload[offset : offset + chunk_len]
        requests.append(
            file_request_line(
                request_id,
                "write",
                path=tmp,
                off=offset,
                len=chunk_len,
                trunc=1 if offset == 0 else 0,
                data=b64url(chunk),
                crc=crc32_hex(chunk),
            )
        )
        request_id += 1
    for offset, chunk_len in chunk_ranges(len(payload)):
        requests.append(file_request_line(request_id, "read", path=tmp, off=offset, len=chunk_len))
        request_id += 1
    requests.append(file_request_line(request_id, "rename", path=tmp, to=final, replace=1))
    request_id += 1
    requests.append(file_request_line(request_id, "stat", path=final))
    request_id += 1
    for offset, chunk_len in chunk_ranges(len(payload)):
        requests.append(file_request_line(request_id, "read", path=final, off=offset, len=chunk_len))
        request_id += 1
    return requests


def diagnostic_export_transcript(
    scenario: SdScenario,
    token: str = DIAGNOSTIC_EXPORT_DEFAULT_TOKEN,
    request_id_start: int = DIAGNOSTIC_EXPORT_START_ID,
) -> list[dict[str, str]]:
    fs = SdFileSystem()
    transcript = []
    for request in diagnostic_export_requests(token, request_id_start):
        transcript.append({"request": request, "reply": reply_for_request(request, scenario, fs)})
    return transcript


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--scenario", choices=sorted(SCENARIOS), default="ready")
    parser.add_argument("--request", default=STATUS_REQUEST)
    parser.add_argument("--file-canary-transcript", action="store_true")
    parser.add_argument("--export-canary-transcript", action="store_true")
    parser.add_argument("--diagnostic-export-transcript", action="store_true")
    parser.add_argument("--request-id-start", type=int)
    parser.add_argument("--token")
    parser.add_argument("--list-scenarios", action="store_true")
    args = parser.parse_args()

    if args.list_scenarios:
        for name in sorted(SCENARIOS):
            print(name)
        return 0

    if args.file_canary_transcript:
        request_id_start = (
            FILE_CANARY_START_ID if args.request_id_start is None else args.request_id_start
        )
        for exchange in file_canary_transcript(SCENARIOS[args.scenario], request_id_start):
            print(f"> {exchange['request']}")
            print(f"< {exchange['reply']}")
        return 0

    if args.export_canary_transcript:
        request_id_start = (
            EXPORT_CANARY_START_ID if args.request_id_start is None else args.request_id_start
        )
        token = args.token or EXPORT_CANARY_DEFAULT_TOKEN
        for exchange in export_canary_transcript(SCENARIOS[args.scenario], token, request_id_start):
            print(f"> {exchange['request']}")
            print(f"< {exchange['reply']}")
        return 0

    if args.diagnostic_export_transcript:
        request_id_start = (
            DIAGNOSTIC_EXPORT_START_ID if args.request_id_start is None else args.request_id_start
        )
        token = args.token or DIAGNOSTIC_EXPORT_DEFAULT_TOKEN
        for exchange in diagnostic_export_transcript(SCENARIOS[args.scenario], token, request_id_start):
            print(f"> {exchange['request']}")
            print(f"< {exchange['reply']}")
        return 0

    print(reply_for_request(args.request, SCENARIOS[args.scenario], SdFileSystem()))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
