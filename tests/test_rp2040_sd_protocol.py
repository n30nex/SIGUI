from pathlib import Path

from tools.rp2040_sd_protocol import (
    FILE_CAPABILITY_FIELDS,
    FILE_ERROR_CODES,
    FILE_LINE_MAX,
    FILE_REPLY,
    FILE_REQUEST,
    FORMAT_CONFIRMATION,
    FORMAT_REQUEST,
    FORMAT_REPLY,
    MAX_FILE_CHUNK_BYTES,
    MAX_FILE_PATH_CHARS,
    SdFileSystem,
    STATUS_FIELDS,
    SCENARIOS,
    STATUS_REPLY,
    STATUS_REQUEST,
    b64url,
    crc32_hex,
    encode_path,
    reply_for_request,
    validate_relative_path,
)


ROOT = Path(__file__).resolve().parents[1]


def parse_tokens(line: str) -> dict[str, str]:
    parts = line.split()
    return {"prefix": parts[0], **dict(token.split("=", 1) for token in parts[1:])}


def test_status_protocol_lines_cover_boot_states():
    no_card = parse_tokens(reply_for_request(STATUS_REQUEST, SCENARIOS["no-card"]))
    ready = parse_tokens(reply_for_request(STATUS_REQUEST, SCENARIOS["ready"]))
    setup = parse_tokens(reply_for_request(STATUS_REQUEST, SCENARIOS["format-required"]))

    assert no_card["prefix"] == STATUS_REPLY
    assert no_card["state"] == "no_card"
    assert no_card["present"] == "0"
    assert ready["state"] == "ready"
    assert ready["present"] == "1"
    assert ready["mounted"] == "1"
    assert ready["deskos"] == "1"
    assert ready["format_required"] == "0"
    assert setup["state"] == "setup_required"
    assert setup["format_required"] == "1"
    assert setup["format_supported"] == "1"
    assert ready["file_ops"] == "1"
    assert ready["file_line_max"] == str(FILE_LINE_MAX)
    assert ready["file_chunk_max"] == str(MAX_FILE_CHUNK_BYTES)
    assert ready["path_max"] == str(MAX_FILE_PATH_CHARS)
    assert ready["atomic_rename"] == "1"
    assert no_card["file_ops"] == "0"


def test_format_protocol_requires_exact_confirmation():
    bad = parse_tokens(reply_for_request(f"{FORMAT_REQUEST} WRONG", SCENARIOS["format-required"]))
    good = parse_tokens(
        reply_for_request(f"{FORMAT_REQUEST} {FORMAT_CONFIRMATION}", SCENARIOS["format-required"])
    )

    assert bad["prefix"] == FORMAT_REPLY
    assert bad["state"] == "confirmation_required"
    assert bad["format_required"] == "1"
    assert good["prefix"] == FORMAT_REPLY
    assert good["state"] == "ready"
    assert good["mounted"] == "1"
    assert good["deskos"] == "1"
    assert good["format_required"] == "0"
    assert good["note"] == "format_complete"


def test_storage_edge_scenarios_and_constants_match_c_contract():
    root_missing = parse_tokens(reply_for_request(STATUS_REQUEST, SCENARIOS["root-missing"]))
    no_card_after_format = parse_tokens(
        reply_for_request(f"{FORMAT_REQUEST} {FORMAT_CONFIRMATION}", SCENARIOS["no-card"])
    )

    assert root_missing["present"] == "1"
    assert root_missing["mounted"] == "1"
    assert root_missing["deskos"] == "0"
    assert root_missing["format_required"] == "0"
    assert root_missing["format_supported"] == "1"
    assert no_card_after_format["state"] == "no_card"
    assert no_card_after_format["present"] == "0"

    for scenario in SCENARIOS.values():
        assert " " not in scenario.note

    c_header = (ROOT / "main/hal/rp2040_bridge.h").read_text(encoding="utf-8")
    c_source = (ROOT / "main/hal/rp2040_bridge.c").read_text(encoding="utf-8")
    rp2040_sketch = (
        ROOT / "firmware/rp2040_sd_bridge/deskos_sd_bridge/deskos_sd_bridge.ino"
    ).read_text(encoding="utf-8")
    assert STATUS_REQUEST in c_source
    assert FORMAT_REQUEST in c_source
    assert FORMAT_CONFIRMATION in c_header
    assert STATUS_REQUEST in rp2040_sketch
    assert FORMAT_REQUEST in rp2040_sketch
    assert FORMAT_CONFIRMATION in rp2040_sketch
    for field in STATUS_FIELDS:
        assert f"{field}=" in rp2040_sketch
    for field in FILE_CAPABILITY_FIELDS:
        assert f"{field}=" in rp2040_sketch


def file_tokens(line: str) -> dict[str, str]:
    parsed = parse_tokens(line)
    assert parsed["prefix"] == FILE_REPLY
    return parsed


def file_request(request_id: int, op: str, **tokens: object) -> str:
    merged = {"v": 1, "id": request_id, "op": op, **tokens}
    return " ".join([FILE_REQUEST, *(f"{key}={value}" for key, value in merged.items())])


def test_file_protocol_supports_bounded_write_read_rename_delete_cycle():
    fs = SdFileSystem()
    path = encode_path("logs/packet.bin")
    tmp = encode_path("logs/packet.tmp")
    final = encode_path("logs/packet.final")
    data = b"packet-log-canary"
    more = b"-append"

    write = file_tokens(
        reply_for_request(
            file_request(
                7,
                "write",
                path=tmp,
                off=0,
                len=len(data),
                trunc=1,
                data=b64url(data),
                crc=crc32_hex(data),
            ),
            SCENARIOS["ready"],
            fs,
        )
    )
    append = file_tokens(
        reply_for_request(
            file_request(
                8,
                "append",
                path=tmp,
                len=len(more),
                data=b64url(more),
                crc=crc32_hex(more),
            ),
            SCENARIOS["ready"],
            fs,
        )
    )
    rename = file_tokens(
        reply_for_request(
            file_request(9, "rename", path=tmp, to=final, replace=1),
            SCENARIOS["ready"],
            fs,
        )
    )
    stat = file_tokens(
        reply_for_request(file_request(10, "stat", path=final), SCENARIOS["ready"], fs)
    )
    read = file_tokens(
        reply_for_request(
            file_request(11, "read", path=final, off=0, len=MAX_FILE_CHUNK_BYTES),
            SCENARIOS["ready"],
            fs,
        )
    )
    delete = file_tokens(
        reply_for_request(file_request(12, "delete", path=final), SCENARIOS["ready"], fs)
    )
    deleted = file_tokens(
        reply_for_request(file_request(13, "stat", path=final), SCENARIOS["ready"], fs)
    )

    assert write["ok"] == "1"
    assert write["op"] == "write"
    assert write["size"] == str(len(data))
    assert append["ok"] == "1"
    assert append["off"] == str(len(data))
    assert rename["ok"] == "1"
    assert stat["exists"] == "1"
    assert stat["kind"] == "file"
    assert stat["size"] == str(len(data + more))
    assert read["data"] == b64url(data + more)
    assert read["crc"] == crc32_hex(data + more)
    assert delete["ok"] == "1"
    assert deleted["exists"] == "0"
    assert path != final


def test_file_protocol_rejects_bad_paths_crc_and_large_chunks():
    fs = SdFileSystem()
    safe_path = encode_path("logs/packet.bin")
    data = b"x"
    bad_crc = file_tokens(
        reply_for_request(
            file_request(
                21,
                "write",
                path=safe_path,
                off=0,
                len=len(data),
                trunc=1,
                data=b64url(data),
                crc="00000000",
            ),
            SCENARIOS["ready"],
            fs,
        )
    )
    too_large = file_tokens(
        reply_for_request(
            file_request(22, "read", path=safe_path, off=0, len=MAX_FILE_CHUNK_BYTES + 1),
            SCENARIOS["ready"],
            fs,
        )
    )
    not_ready = file_tokens(
        reply_for_request(file_request(23, "stat", path=safe_path), SCENARIOS["no-card"], fs)
    )

    assert bad_crc["ok"] == "0"
    assert bad_crc["err"] == "crc_mismatch"
    assert too_large["ok"] == "0"
    assert too_large["err"] == "too_large"
    assert not_ready["ok"] == "0"
    assert not_ready["err"] == "no_card"
    assert not validate_relative_path("../escape")
    assert not validate_relative_path("/absolute")
    assert not validate_relative_path("tiles//gap")
    assert not validate_relative_path("tiles/.")
    assert validate_relative_path("tiles/z12/x1/y2.bin")
    assert FILE_LINE_MAX == 512
    assert MAX_FILE_CHUNK_BYTES == 192
    assert "crc_mismatch" in FILE_ERROR_CODES


def test_unknown_request_is_rejected():
    try:
        reply_for_request("DESKOS_SD_ERASE", SCENARIOS["ready"])
    except ValueError as exc:
        assert "unsupported request" in str(exc)
    else:
        raise AssertionError("unknown request should raise ValueError")
