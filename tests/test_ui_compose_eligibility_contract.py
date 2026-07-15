from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def test_compose_runtime_eligibility_is_live_fail_closed_and_rf_silent() -> None:
    cmake = read("main/CMakeLists.txt")
    app_header = read("main/app/app_model.h")
    app_source = read("main/app/app_model.c")
    helper_header = read("main/ui/ui_compose_eligibility.h")
    helper = read("main/ui/ui_compose_eligibility.c")
    phase1 = read("main/ui/ui_phase1.c")

    assert '"ui/ui_compose_eligibility.c"' in cmake
    assert "protocol_tx_ready" in app_header
    assert "protocol_tx_error" in app_header
    assert "settings_load_status" in app_header
    assert "identity_state" in app_header
    assert "dm_delivery_active" in app_header
    assert "d1l_time_service_status(&time_status)" in app_source
    assert "d1l_settings_load_status()" in app_source
    assert "d1l_settings_persisted_identity_state()" in app_source
    assert "d1l_dm_delivery_state_terminal(delivery_state)" in app_source

    assert "previous_send_error" in helper_header
    assert "protocol_tx_ready" in helper_header
    assert "settings_load_status" in helper_header
    assert "identity_state" in helper_header
    assert "dm_delivery_active" in helper_header
    assert "channel_found" in helper_header
    assert "channel_sendable" in helper_header
    assert "radio_applied" not in helper_header
    assert "out_path" not in helper_header
    assert 'strcmp(mesh_state, "tx_busy")' in helper
    assert "path_hash_bytes < 1U" in helper
    assert "D1L_UI_COMPOSE_PROTOCOL_TIME_UNAVAILABLE" in helper
    assert "D1L_IDENTITY_STATE_INCONSISTENT" in helper
    assert "D1L_UI_COMPOSE_DM_DELIVERY_ACTIVE" in helper
    assert "D1L_UI_COMPOSE_CHANNEL_MISSING" in helper
    assert "D1L_UI_COMPOSE_CHANNEL_NOT_SENDABLE" in helper

    projection = phase1.split(
        "static d1l_ui_compose_eligibility_t compose_eligibility_for_text", 1
    )[1].split("static void update_compose_counter", 1)[0]
    assert "d1l_app_model_find_contact(" in projection
    assert "d1l_contact_store_can_dm(&current)" in projection
    assert "s_snapshot.protocol_tx_ready" in projection
    assert "s_snapshot.settings_load_status" in projection
    assert "s_snapshot.identity_state" in projection
    assert "s_snapshot.dm_delivery_active" in projection
    assert "snapshot_find_channel(" in projection
    assert "channel.enabled" in projection
    assert "out_path" not in projection
    assert "radio_applied" not in projection

    refresh = phase1.split("static void refresh_timer_cb", 1)[1].split(
        "static void create_top_bar", 1
    )[0]
    assert "d1l_ui_modal_visible(s_compose_sheet)" in refresh
    assert "update_compose_counter()" in refresh
    assert "d1l_app_model_send_channel_text" not in refresh
    assert "d1l_app_model_send_dm_text" not in refresh

    send = phase1.split("static void send_compose_text(void)", 1)[1].split(
        "static void send_compose_event_cb", 1
    )[0]
    assert send.index("d1l_app_model_snapshot(&s_snapshot)") < send.index(
        "compose_eligibility_for_text(&info)"
    )
    assert "if (!eligibility.send_enabled)" in send
    assert "s_compose_last_send_error = ret" in send
    assert "d1l_app_model_send_channel_text(" in send
    assert "s_compose_channel_id" in send
    assert "show_toast_text(retry.status, false)" in send
    assert "hide_compose_sheet();" in send.split("if (ret == ESP_OK)", 1)[1].split(
        "} else {", 1
    )[0]
    failure = send.split("} else {", 1)[1]
    assert "hide_compose_sheet();" not in failure
    assert "lv_textarea_set_text(s_compose_textarea, \"\")" not in failure
    assert "if (s_compose_dm)" in failure
    assert "request_content_refresh();" in failure

    assert '"Send", 252, 8, 62, 44' in phase1
    assert '"Clear", 322, 8, 62, 44' in phase1
    assert '"Close", 392, 8, 72, 44' in phase1


def test_compose_retry_is_explicit_and_never_automatic() -> None:
    phase1 = read("main/ui/ui_phase1.c")
    helper = read("main/ui/ui_compose_eligibility.c")

    assert "D1L_UI_COMPOSE_RETRY_TIMEOUT" in helper
    assert "D1L_UI_COMPOSE_RETRY_MEMORY" in helper
    assert "D1L_UI_COMPOSE_RETRY_REJECTED" in helper
    assert "D1L_UI_COMPOSE_RECOVERY_REQUIRED" in helper

    send_callbacks = (
        phase1.split("static void send_compose_event_cb", 1)[1].split(
            "static void compose_textarea_event_cb", 1
        )[0]
    )
    assert "send_compose_text();" in send_callbacks
    textarea = phase1.split("static void compose_textarea_event_cb", 1)[1].split(
        "static void complete_onboarding_from_ui", 1
    )[0]
    assert "send_compose_text();" not in textarea
    assert "d1l_ui_compose_error_clears_on_text_change" in textarea
    assert "s_compose_last_send_error = ESP_OK" in textarea


def test_compose_text_changes_preserve_persistent_failure_latches() -> None:
    helper = read("main/ui/ui_compose_eligibility.c")
    phase1 = read("main/ui/ui_phase1.c")

    clear_policy = helper.split(
        "bool d1l_ui_compose_error_clears_on_text_change", 1
    )[1].split("d1l_ui_compose_eligibility_t d1l_ui_compose_eligibility", 1)[0]
    assert "ESP_ERR_INVALID_ARG" in clear_policy
    assert "ESP_ERR_INVALID_SIZE" in clear_policy
    assert "ESP_ERR_TIMEOUT" in clear_policy
    assert "ESP_ERR_NO_MEM" in clear_policy
    assert "ESP_ERR_INVALID_STATE" in clear_policy
    assert "ESP_ERR_NOT_FOUND" not in clear_policy
    assert "ESP_ERR_NOT_SUPPORTED" not in clear_policy
    assert "ESP_FAIL" not in clear_policy

    clear_handler = phase1.split("static void clear_compose_event_cb", 1)[1].split(
        "static void send_compose_text", 1
    )[0]
    assert "d1l_ui_compose_error_clears_on_text_change" in clear_handler
