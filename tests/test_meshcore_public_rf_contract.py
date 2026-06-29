from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_meshcore_service_builds_public_group_text_packets():
    source = read("main/mesh/meshcore_service.c")
    assert "D1L_MESHCORE_HEADER_GROUP_TEXT_FLOOD" in source
    assert "D1L_MESHCORE_PAYLOAD_GROUP_TEXT 0x05U" in source
    assert "0x8b, 0x33, 0x87, 0xe9" in source
    assert "mbedtls_aes_crypt_ecb" in source
    assert "mbedtls_md_hmac" in source
    assert "Radio.Send(raw, raw_len)" in source


def test_meshcore_service_uses_meshcore_narrow_radio_profile():
    source = read("main/mesh/meshcore_service.c")
    assert "D1L_MESHCORE_BW_INDEX_62K5" in source
    assert "case 625:" in source
    assert "Radio.SetPublicNetwork(false)" in source
    assert "D1L_MESHCORE_PREAMBLE_LOW_SF 32U" in source
    assert "LORA_BW_062" in source
    assert "SX126xSetModulationParams(&SX126x.ModulationParams)" in source


def test_console_reports_phase2_public_rf_evidence():
    console = read("main/comms/usb_console.c")
    assert 'phase2_public_rf' in console
    assert 'Public group text TX/RX enabled' in console
    assert '\\"note\\":\\"%s\\"' in console
