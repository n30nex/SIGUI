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
    assert "meshcore_service_send_raw(raw, raw_len" in source
    assert "Radio.Send(cmd->raw, cmd->raw_len)" in source


def test_meshcore_service_decodes_verified_adverts():
    source = read("main/mesh/meshcore_service.c")
    cmake = read("main/CMakeLists.txt")
    header = read("main/mesh/meshcore_service.h")
    assert "D1L_MESHCORE_PAYLOAD_ADVERT 0x04U" in source
    assert "D1L_MESHCORE_ADVERT_MIN_PAYLOAD" in source
    assert "ed25519_verify" in source
    assert "d1l_node_store_upsert_advert" in source
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
    assert "d1l_settings_next_mesh_timestamp" in source
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


def test_meshcore_status_getter_is_passive_and_radio_owned_by_service_task():
    source = read("main/mesh/meshcore_service.c")
    status_body = source.split("d1l_meshcore_service_status_t d1l_meshcore_service_status", 1)[
        1
    ].split("esp_err_t d1l_meshcore_service_request_advert", 1)[0]

    forbidden = [
        "d1l_meshcore_service_ensure_identity",
        "ensure_radio_started",
        "d1l_meshcore_start_rx",
        "Radio.",
        "d1l_settings_save",
        "esp_fill_random",
        "ed25519_create_keypair",
    ]
    for token in forbidden:
        assert token not in status_body

    assert "d1l_meshcore_service_cmd_t" in source
    assert "xQueueCreate(D1L_MESHCORE_SERVICE_QUEUE_LEN" in source
    assert "meshcore_service_task" in source
    assert "meshcore_service_handle_start_rx" in source
    assert "meshcore_service_handle_send_raw" in source
    assert "meshcore_service_request_rx_async" in source
    assert "SemaphoreHandle_t s_status_mutex" in source
    assert "status_lock()" in status_body
    assert "d1l_meshcore_service_status_t snapshot = s_status" in status_body


def test_console_reports_phase2_public_rf_evidence():
    console = read("main/comms/usb_console.c")
    assert 'phase2_public_rf' in console
    assert 'signed advert TX/RX enabled' in console
    assert '\\"rx_adverts\\":%lu' in console
    assert '\\"note\\":\\"%s\\"' in console
