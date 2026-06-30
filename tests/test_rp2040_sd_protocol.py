from pathlib import Path

from tools.rp2040_sd_protocol import (
    FORMAT_CONFIRMATION,
    FORMAT_REQUEST,
    FORMAT_REPLY,
    STATUS_FIELDS,
    SCENARIOS,
    STATUS_REPLY,
    STATUS_REQUEST,
    reply_for_request,
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


def test_unknown_request_is_rejected():
    try:
        reply_for_request("DESKOS_SD_ERASE", SCENARIOS["ready"])
    except ValueError as exc:
        assert "unsupported request" in str(exc)
    else:
        raise AssertionError("unknown request should raise ValueError")
