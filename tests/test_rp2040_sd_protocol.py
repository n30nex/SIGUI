from pathlib import Path

from tools.rp2040_sd_protocol import (
    FILE_CAPABILITY_FIELDS,
    FILE_CANARY_FINAL_PATH,
    FILE_CANARY_PAYLOAD,
    FILE_CANARY_TMP_PATH,
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
    EXPORT_CANARY_DEFAULT_TOKEN,
    EXPORT_CANARY_START_ID,
    export_canary_paths,
    export_canary_payload,
    export_canary_transcript,
    file_canary_transcript,
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


def test_filecanary_transcript_matches_storage_filecanary_contract():
    transcript = file_canary_transcript(SCENARIOS["ready"])
    requests = [exchange["request"] for exchange in transcript]
    replies = [parse_tokens(exchange["reply"]) for exchange in transcript]
    tmp_path = encode_path(FILE_CANARY_TMP_PATH)
    final_path = encode_path(FILE_CANARY_FINAL_PATH)
    payload64 = b64url(FILE_CANARY_PAYLOAD)
    payload_crc = crc32_hex(FILE_CANARY_PAYLOAD)

    assert len(transcript) == 9
    assert requests[0] == file_request(1, "delete", path=tmp_path)
    assert replies[0]["ok"] == "0"
    assert replies[0]["err"] == "not_found"
    assert requests[1] == file_request(2, "delete", path=final_path)
    assert replies[1]["err"] == "not_found"
    assert requests[2] == file_request(
        3,
        "write",
        path=tmp_path,
        off=0,
        len=len(FILE_CANARY_PAYLOAD),
        trunc=1,
        data=payload64,
        crc=payload_crc,
    )
    assert replies[2]["ok"] == "1"
    assert replies[2]["size"] == str(len(FILE_CANARY_PAYLOAD))
    assert requests[3] == file_request(4, "read", path=tmp_path, off=0, len=24)
    assert replies[3]["data"] == payload64
    assert replies[3]["crc"] == payload_crc
    assert requests[4] == file_request(5, "rename", path=tmp_path, to=final_path, replace=1)
    assert replies[4]["ok"] == "1"
    assert requests[5] == file_request(6, "stat", path=final_path)
    assert replies[5]["exists"] == "1"
    assert replies[5]["kind"] == "file"
    assert requests[6] == file_request(7, "read", path=final_path, off=0, len=24)
    assert replies[6]["data"] == payload64
    assert requests[7] == file_request(8, "delete", path=final_path)
    assert replies[7]["ok"] == "1"
    assert requests[8] == file_request(9, "stat", path=final_path)
    assert replies[8]["exists"] == "0"


def test_filecanary_transcript_fails_safely_without_ready_card():
    for scenario_name, expected_error in {
        "no-card": "no_card",
        "format-required": "not_ready",
        "root-missing": "not_ready",
    }.items():
        transcript = file_canary_transcript(SCENARIOS[scenario_name])
        replies = [parse_tokens(exchange["reply"]) for exchange in transcript]
        assert {reply["ok"] for reply in replies} == {"0"}
        assert {reply["err"] for reply in replies} == {expected_error}


def test_export_canary_transcript_commits_diagnostic_export_without_delete():
    token = EXPORT_CANARY_DEFAULT_TOKEN
    transcript = export_canary_transcript(SCENARIOS["ready"], token)
    requests = [exchange["request"] for exchange in transcript]
    replies = [parse_tokens(exchange["reply"]) for exchange in transcript]
    tmp_path, final_path = export_canary_paths(token)
    tmp = encode_path(tmp_path)
    final = encode_path(final_path)
    payload = export_canary_payload(token)
    payload64 = b64url(payload)
    payload_crc = crc32_hex(payload)

    assert len(transcript) == 6
    assert requests[0] == file_request(EXPORT_CANARY_START_ID, "delete", path=tmp)
    assert replies[0]["ok"] == "0"
    assert replies[0]["err"] == "not_found"
    assert requests[1] == file_request(
        EXPORT_CANARY_START_ID + 1,
        "write",
        path=tmp,
        off=0,
        len=len(payload),
        trunc=1,
        data=payload64,
        crc=payload_crc,
    )
    assert replies[1]["ok"] == "1"
    assert requests[2] == file_request(
        EXPORT_CANARY_START_ID + 2,
        "read",
        path=tmp,
        off=0,
        len=len(payload),
    )
    assert replies[2]["data"] == payload64
    assert requests[3] == file_request(
        EXPORT_CANARY_START_ID + 3,
        "rename",
        path=tmp,
        to=final,
        replace=1,
    )
    assert replies[3]["ok"] == "1"
    assert requests[4] == file_request(EXPORT_CANARY_START_ID + 4, "stat", path=final)
    assert replies[4]["exists"] == "1"
    assert replies[4]["kind"] == "file"
    assert requests[5] == file_request(
        EXPORT_CANARY_START_ID + 5,
        "read",
        path=final,
        off=0,
        len=len(payload),
    )
    assert replies[5]["data"] == payload64
    assert not any(" op=delete " in request and final in request for request in requests[1:])
    assert b'"public_rf_tx":false' in payload
    assert b'"formats_sd":false' in payload


def test_export_canary_transcript_fails_safely_without_ready_card():
    for scenario_name, expected_error in {
        "no-card": "no_card",
        "format-required": "not_ready",
        "root-missing": "not_ready",
    }.items():
        transcript = export_canary_transcript(SCENARIOS[scenario_name], "ex1")
        replies = [parse_tokens(exchange["reply"]) for exchange in transcript]
        assert {reply["ok"] for reply in replies} == {"0"}
        assert {reply["err"] for reply in replies} == {expected_error}


def test_retained_history_blob_paths_support_atomic_commit_cycle():
    retained_paths = (
        "stores/messages/public/public",
        "stores/messages/dm/threads",
        "stores/routes/routes",
        "stores/packet_log/ring",
    )
    payload = b"retained-history-blob-v1"

    for index, base_path in enumerate(retained_paths, start=40):
        fs = SdFileSystem()
        tmp = encode_path(f"{base_path}.tmp")
        final = encode_path(f"{base_path}.bin")
        write = file_tokens(
            reply_for_request(
                file_request(
                    index,
                    "write",
                    path=tmp,
                    off=0,
                    len=len(payload),
                    trunc=1,
                    data=b64url(payload),
                    crc=crc32_hex(payload),
                ),
                SCENARIOS["ready"],
                fs,
            )
        )
        rename = file_tokens(
            reply_for_request(
                file_request(index + 100, "rename", path=tmp, to=final, replace=1),
                SCENARIOS["ready"],
                fs,
            )
        )
        stat = file_tokens(
            reply_for_request(
                file_request(index + 200, "stat", path=final),
                SCENARIOS["ready"],
                fs,
            )
        )
        read = file_tokens(
            reply_for_request(
                file_request(index + 300, "read", path=final, off=0, len=len(payload)),
                SCENARIOS["ready"],
                fs,
            )
        )

        assert validate_relative_path(f"{base_path}.tmp")
        assert validate_relative_path(f"{base_path}.bin")
        assert write["ok"] == "1"
        assert rename["ok"] == "1"
        assert stat["exists"] == "1"
        assert stat["size"] == str(len(payload))
        assert read["data"] == b64url(payload)
        assert read["crc"] == crc32_hex(payload)


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
