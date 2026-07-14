from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_meshcore_service_builds_public_group_text_packets():
    source = read("main/mesh/meshcore_service.c")
    wire = read("main/mesh/meshcore_wire.h")
    assert "D1L_MESHCORE_HEADER_GROUP_TEXT_FLOOD" in source
    assert "D1L_MESHCORE_PAYLOAD_GROUP_TEXT 0x05U" in wire
    assert "0x8b, 0x33, 0x87, 0xe9" in source
    assert "mbedtls_aes_crypt_ecb" in source
    assert "mbedtls_md_hmac" in source
    assert "meshcore_service_send_raw(raw, raw_len" in source
    assert "Radio.Send(cmd->raw, cmd->raw_len)" in source


def test_meshcore_service_rejects_139_char_user_text_without_truncation():
    source = read("main/mesh/meshcore_service.c")
    console = read("main/comms/usb_console.c")
    ui = read("main/ui/ui_phase1.c")

    assert "#define D1L_MESHCORE_USER_TEXT_MAX D1L_MESSAGE_MAX_CHARS" in source
    assert "D1L_MESHCORE_USER_TEXT_MAX == 138U" in source
    assert "return ESP_ERR_INVALID_SIZE;" in source
    assert "validate_user_text(text)" in source

    public_builder = source.split("static esp_err_t build_public_text_packet", 1)[1].split(
        "static esp_err_t calc_dm_ack_hash", 1
    )[0]
    dm_builder = source.split("static esp_err_t build_dm_text_packet", 1)[1].split(
        "static void parse_rx_public_packet", 1
    )[0]
    public_sender = source.split("esp_err_t d1l_meshcore_service_send_public", 1)[1].split(
        "static esp_err_t meshcore_service_send_dm_with_result", 1
    )[0]
    dm_sender = source.split(
        "static esp_err_t meshcore_service_send_dm_with_result", 1
    )[1].split(
        "esp_err_t d1l_meshcore_service_send_dm", 1
    )[0]

    assert "validate_user_text(text)" in public_builder
    assert "validate_user_text(text)" in dm_builder
    assert 'snprintf((char *)&plain[5]' not in public_builder
    assert 'snprintf((char *)&plain[5]' not in dm_builder
    assert "memcpy(&plain[5], text, message_len)" in public_builder
    assert "memcpy(&plain[5], text, message_len)" in dm_builder
    assert "validate_user_text(text)" in public_sender
    assert public_sender.index("validate_user_text(text)") < public_sender.index(
        "meshcore_service_send_command(&start_cmd"
    )
    assert "validate_user_text(text)" in dm_sender
    assert dm_sender.index("validate_user_text(text)") < dm_sender.index(
        "d1l_meshcore_service_ensure_identity()"
    )

    assert "MESSAGE_TOO_LONG" in console
    assert "max 138 characters" in console
    assert "lv_textarea_set_max_length(s_compose_textarea, D1L_MESSAGE_MAX_CHARS)" in ui


def test_meshcore_service_decodes_verified_adverts():
    source = read("main/mesh/meshcore_service.c")
    wire = read("main/mesh/meshcore_wire.h")
    cmake = read("main/CMakeLists.txt")
    header = read("main/mesh/meshcore_service.h")
    assert "D1L_MESHCORE_PAYLOAD_ADVERT 0x04U" in wire
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


def test_meshcore_identity_generation_preserves_inconsistent_persisted_material():
    source = read("main/mesh/meshcore_service.c")
    body = source.split("esp_err_t d1l_meshcore_service_ensure_identity(void)", 1)[
        1
    ].split("d1l_meshcore_service_status_t d1l_meshcore_service_status", 1)[0]

    load_status = body.index("d1l_settings_load_status()")
    current = body.index("d1l_settings_current()")
    classify = body.index("d1l_settings_identity_state(&settings)")
    reject = body.index("persisted_state == D1L_IDENTITY_STATE_INCONSISTENT")
    random = body.index("esp_fill_random")
    save = body.index("d1l_settings_save")
    assert load_status < current < classify < reject < random < save

    unreadable = body.split("if (load_status != ESP_OK)", 1)[1].split(
        "d1l_settings_t settings", 1
    )[0]
    assert "s_status.identity_ready = false;" in unreadable
    assert "return load_status;" in unreadable
    assert "d1l_settings_current" not in unreadable
    assert "d1l_settings_save" not in unreadable

    inconsistent = body.split(
        "persisted_state == D1L_IDENTITY_STATE_INCONSISTENT", 1
    )[1].split("uint8_t seed", 1)[0]
    assert "s_status.identity_ready = false;" in inconsistent
    assert "return ESP_ERR_INVALID_STATE;" in inconsistent
    assert "d1l_settings_save" not in inconsistent
    assert "esp_fill_random" not in inconsistent

    generation = body.split("uint8_t seed", 1)[1]
    assert "d1l_settings_identity_state(&settings)" in generation
    assert "D1L_IDENTITY_STATE_CONSISTENT" in generation
    assert "identity_public_key[0]" not in generation


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
    header = read("main/mesh/meshcore_service.h")
    app_main = read("main/app_main.c")
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
    assert "radio_apply_pending" in source
    assert "radio_apply_error" in source
    assert "radio_profile_strings_match(lhs->tcxo, rhs->tcxo)" in source
    assert "mark_radio_apply_result(&profile, ret)" in source
    assert "radio_profiles_match(&applied_profile, &current_profile)" in status_body
    assert "esp_err_t d1l_meshcore_service_start_rx_async(void);" in header
    assert "esp_err_t d1l_meshcore_service_start_rx_async(void)" in source
    start_async = source.split("esp_err_t d1l_meshcore_service_start_rx_async(void)", 1)[1].split(
        "static esp_err_t meshcore_service_send_raw", 1
    )[0]
    assert "meshcore_service_start_task()" in start_async
    assert ".type = D1L_MESHCORE_SERVICE_CMD_START_RX" in start_async
    assert "xQueueSend(s_service_queue, &cmd, 0)" in start_async
    assert "Radio." not in start_async
    assert 'ESP_LOGW(TAG, "asynchronous MeshCore RX start failed: %s"' in source
    ensure_radio = source.split("static esp_err_t ensure_radio_started(void)", 1)[1].split(
        "static void d1l_meshcore_start_rx(void)", 1
    )[0]
    assert "s_status.identity_ready = d1l_settings_current()->identity_ready;" in ensure_radio
    assert app_main.index("d1l_board_init()") < app_main.index(
        "d1l_meshcore_service_start_rx_async()"
    ) < app_main.index("d1l_connectivity_init()")


def test_meshcore_service_reinitialization_preserves_live_radio_and_pending_work():
    source = read("main/mesh/meshcore_service.c")
    assert "static bool s_service_initialized;" in source
    init_body = source.split("void d1l_meshcore_service_init(void)", 1)[1].split(
        "esp_err_t d1l_meshcore_service_ensure_identity", 1
    )[0]
    repeated_init = init_body.split("if (s_service_initialized)", 1)[1].split(
        "memset(&s_status", 1
    )[0]
    assert "s_status.path_hash_bytes = settings->path_hash_bytes;" in repeated_init
    assert "s_status.identity_ready = settings->identity_ready;" in repeated_init
    assert "status_unlock();" in repeated_init
    assert "return;" in repeated_init
    for destructive_reset in [
        "s_radio_started = false",
        "s_tx_busy = false",
        "s_pending_public_tx = false",
        "memset(&s_pending_dm_tx",
    ]:
        assert destructive_reset not in repeated_init
    assert "s_service_initialized = task_ret == ESP_OK;" in init_body


def test_console_reports_phase2_public_rf_evidence():
    console = read("main/comms/usb_console.c")
    assert 'phase2_public_rf' in console
    assert 'signed advert TX/RX enabled' in console
    assert '\\"rx_adverts\\":%lu' in console
    assert '\\"note\\":\\"%s\\"' in console
