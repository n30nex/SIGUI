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


def test_meshcore_service_decodes_verified_adverts():
    source = read("main/mesh/meshcore_service.c")
    cmake = read("main/CMakeLists.txt")
    header = read("main/mesh/meshcore_service.h")
    assert "D1L_MESHCORE_PAYLOAD_ADVERT 0x04U" in source
    assert "D1L_MESHCORE_ADVERT_MIN_PAYLOAD" in source
    assert "ed25519_verify" in source
    assert 'append_packet_log("rx", "advert"' in source
    assert "parse_rx_advert_packet(payload, size, rssi, snr)" in source
    assert "../third_party/MeshCore/lib/ed25519/verify.c" in cmake
    assert "uint32_t rx_adverts;" in header


def test_meshcore_service_generates_identity_and_signed_adverts():
    source = read("main/mesh/meshcore_service.c")
    cmake = read("main/CMakeLists.txt")
    assert "ed25519_create_keypair" in source
    assert "ed25519_sign" in source
    assert "esp_fill_random" in source
    assert "build_advert_packet" in source
    assert 'append_packet_log("tx", "advert"' in source
    assert "../third_party/MeshCore/lib/ed25519/keypair.c" in cmake
    assert "../third_party/MeshCore/lib/ed25519/sign.c" in cmake


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
    assert 'signed advert TX/RX enabled' in console
    assert '\\"rx_adverts\\":%lu' in console
    assert '\\"note\\":\\"%s\\"' in console
