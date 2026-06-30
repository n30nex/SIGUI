from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_app_model_exposes_bounded_ui_snapshot():
    header = read("main/app/app_model.h")
    source = read("main/app/app_model.c")
    assert "D1L_APP_SNAPSHOT_PACKET_PREVIEW 4U" in header
    assert "D1L_APP_SNAPSHOT_DM_PREVIEW 5U" in header
    assert "D1L_APP_SNAPSHOT_NODE_PREVIEW 4U" in header
    assert "D1L_APP_SNAPSHOT_CONTACT_PREVIEW 2U" in header
    assert "D1L_APP_SNAPSHOT_ROUTE_PREVIEW 2U" in header
    assert "D1L_APP_SNAPSHOT_ROOM_PREVIEW 4U" in header
    assert "D1L_APP_SNAPSHOT_REPEATER_PREVIEW 4U" in header
    assert "d1l_app_snapshot_t" in header
    assert "d1l_app_model_snapshot" in header
    assert "static d1l_app_snapshot_t s_snapshot" in read("main/ui/ui_phase1.c")
    assert "d1l_contact_store_copy_recent" in source
    assert "d1l_route_store_copy_recent" in source
    assert "d1l_node_store_copy_recent" in source
    assert "d1l_packet_log_copy_recent" in source
    assert "d1l_dm_store_copy_recent" in source
    assert "d1l_meshcore_service_status" in source
    assert "d1l_health_snapshot" in source
    assert "d1l_connectivity_status" in source
    assert "d1l_mesh_inspector_signal_summary" in source
    assert "reset_reason" in header
    assert "onboarding_complete" in header
    assert "ui_task_stack_free_words" in header
    assert "lvgl_used_pct" in header
    assert "d1l_app_radio_profile_edit_t" in header
    assert "radio_frequency_hz" in header
    assert "radio_bandwidth_tenths_khz" in header
    assert "radio_spreading_factor" in header
    assert "radio_coding_rate" in header
    assert "radio_tx_power_dbm" in header
    assert "radio_rx_boost" in header
    assert "radio_tcxo" in header
    assert "d1l_app_model_save_radio_profile" in header
    assert "d1l_app_model_default_radio_profile" in header
    assert "d1l_app_model_current_radio_profile" in header
    assert "valid_radio_edit" in source


def test_phase3_shell_replaces_diagnostic_tile_home():
    source = read("main/ui/ui_phase1.c")
    assert "D1L_UI_TAB_HOME" in source
    assert "D1L_UI_TAB_MESSAGES" in source
    assert "D1L_UI_TAB_NODES" in source
    assert "D1L_UI_TAB_PACKETS" in source
    assert "D1L_UI_TAB_SETTINGS" in source
    assert "create_top_bar" in source
    assert "create_dock" in source
    assert "create_sheet" in source
    assert "create_toast" in source
    assert "create_lock_overlay" in source
    assert "create_onboarding_sheet" in source
    assert "Phase 1 hardware bring-up" not in source


def test_touch_ui_actions_route_through_app_model():
    source = read("main/ui/ui_phase1.c")
    header = read("main/app/app_model.h")
    model = read("main/app/app_model.c")
    assert "d1l_app_model_send_public_test()" in source
    assert "d1l_app_model_send_public_text(text)" in source
    assert "d1l_app_model_send_dm_text(s_compose_contact.fingerprint, text)" in source
    assert "d1l_app_model_request_advert(false)" in source
    assert "d1l_app_model_request_advert(true)" in source
    assert "d1l_app_model_complete_onboarding(name)" in source
    assert "d1l_app_model_send_public_text" in header
    assert "d1l_app_model_send_dm_text" in header
    assert "d1l_app_model_complete_onboarding" in header
    assert "d1l_app_model_find_contact" in header
    assert "d1l_app_model_set_contact_flags" in header
    assert "d1l_app_model_export_contact_uri" in header
    assert 'd1l_meshcore_service_send_public("test")' in model
    assert "d1l_meshcore_service_send_public(text)" in model
    assert "d1l_meshcore_service_send_dm(fingerprint, text)" in model
    assert "d1l_contact_store_find_by_fingerprint(fingerprint, out_contact)" in model
    assert "d1l_contact_store_set_flags(fingerprint, favorite, muted, out_contact)" in model
    assert "d1l_contact_store_export_uri(&contact, dest, dest_size)" in model
    assert "d1l_meshcore_service_request_advert(flood)" in model
    assert "d1l_settings_complete_onboarding(node_name, false, false, false)" in model
    assert "d1l_meshcore_service_ensure_identity()" in model


def test_first_boot_onboarding_sheet_is_touch_backed_and_persisted():
    source = read("main/ui/ui_phase1.c")
    assert "static lv_obj_t *s_onboarding_sheet" in source
    assert "static lv_obj_t *s_onboarding_name_textarea" in source
    assert "create_onboarding_sheet" in source
    assert "update_onboarding_visibility" in source
    assert "complete_onboarding_from_ui" in source
    assert '"MeshCore DeskOS D1L"' in source
    assert '"First boot setup"' in source
    assert '"Node name"' in source
    assert '"Canada/USA preset confirmed  910.525 BW62.5 SF7 CR5"' in source
    assert '"Role Desk Companion  Wi-Fi off  BLE off  Observer off"' in source
    assert 'create_button(s_onboarding_sheet, "Start"' in source
    assert 'create_button(s_onboarding_sheet, "Use Defaults"' in source
    assert "lv_textarea_set_max_length(s_onboarding_name_textarea, D1L_NODE_NAME_LEN - 1U)" in source
    assert "lv_keyboard_set_textarea(s_onboarding_keyboard, s_onboarding_name_textarea)" in source
    assert "snapshot->onboarding_complete" in source


def test_public_composer_uses_lvgl_textarea_keyboard():
    source = read("main/ui/ui_phase1.c")
    assert "create_compose_sheet" in source
    assert "open_compose_event_cb" in source
    assert "send_compose_text" in source
    assert "lv_textarea_create" in source
    assert "lv_textarea_set_max_length" in source
    assert "lv_textarea_get_text" in source
    assert "lv_keyboard_create" in source
    assert "lv_keyboard_set_textarea" in source
    assert "LV_EVENT_READY" in source
    assert "LV_EVENT_CANCEL" in source
    assert "hide_compose_sheet()" in source
    assert '"Test"' in source


def test_dm_composer_opens_from_contact_rows():
    source = read("main/ui/ui_phase1.c")
    assert "static bool s_compose_dm" in source
    assert "static d1l_contact_entry_t s_compose_contact" in source
    assert "open_dm_compose_event_cb" in source
    assert 'create_button(row, "DM"' in source
    assert "d1l_contact_entry_t selected = *entry" in source
    assert "s_compose_contact = selected" in source
    assert 'lv_label_set_text(s_compose_title, title)' in source
    assert 'lv_textarea_set_placeholder_text(s_compose_textarea, "Direct message")' in source
    assert 'show_toast(s_compose_dm ? "DM" : "Public message", ret)' in source


def test_dm_thread_sheet_opens_from_recent_dm_rows():
    source = read("main/ui/ui_phase1.c")
    assert "static lv_obj_t *s_dm_thread_sheet" in source
    assert "static char s_dm_thread_fingerprint" in source
    assert "create_dm_thread_sheet" in source
    assert "render_dm_thread_sheet" in source
    assert "open_dm_thread_event_cb" in source
    assert "reply_dm_thread_event_cb" in source
    assert "lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE)" in source
    assert "lv_obj_add_event_cb(row, open_dm_thread_event_cb, LV_EVENT_CLICKED" in source
    assert "d1l_app_model_find_contact(s_dm_thread_fingerprint, &contact)" in source
    assert "open_dm_compose_for_contact(&contact)" in source
    assert 'create_button(s_dm_thread_sheet, "Reply"' in source
    assert "hide_dm_thread_sheet()" in source


def test_nodes_screen_renders_heard_node_rows():
    source = read("main/ui/ui_phase1.c")
    header = read("main/app/app_model.h")
    assert "render_node_row" in source
    assert "render_contact_row" in source
    assert "recent_node_count" in source
    assert "recent_nodes" in source
    assert "recent_contact_count" in source
    assert "recent_contacts" in source
    assert "No heard nodes yet" in source
    assert "node_total_written" in source
    assert "contact_total_written" in source
    assert "route_count" in source
    assert "#define D1L_APP_SNAPSHOT_NODE_PREVIEW 4U" in header
    assert "#define D1L_APP_SNAPSHOT_CONTACT_PREVIEW 2U" in header
    assert "i < snapshot->recent_contact_count && y <= 190" in source
    assert "i < snapshot->recent_node_count && y <= 300" in source


def test_messages_screen_renders_bounded_preview_rows():
    source = read("main/ui/ui_phase1.c")
    header = read("main/app/app_model.h")
    assert "#define D1L_APP_SNAPSHOT_MESSAGE_PREVIEW 4U" in header
    assert "#define D1L_APP_SNAPSHOT_DM_PREVIEW 5U" in header
    assert "i < snapshot->recent_message_count && y <= 188" in source
    assert "i < snapshot->recent_dm_count && y <= 302" in source
    assert "snapshot->message_count" in source
    assert "snapshot->dm_count" in source
    assert "snapshot->public_unread_count" in source
    assert "snapshot->dm_unread_count" in source


def test_contact_detail_sheet_exposes_dm_favorite_and_mute_actions():
    source = read("main/ui/ui_phase1.c")
    assert "static lv_obj_t *s_contact_detail_sheet" in source
    assert "static lv_obj_t *s_contact_export_sheet" in source
    assert "static d1l_contact_entry_t s_contact_detail_contact" in source
    assert "static d1l_contact_entry_t s_contact_export_contact" in source
    assert "open_contact_detail_event_cb" in source
    assert "create_contact_detail_sheet" in source
    assert "create_contact_export_sheet" in source
    assert "render_contact_detail_sheet" in source
    assert "render_contact_export_sheet" in source
    assert "contact_detail_dm_event_cb" in source
    assert "contact_detail_export_event_cb" in source
    assert "lv_obj_add_event_cb(row, open_contact_detail_event_cb, LV_EVENT_CLICKED" in source
    assert "d1l_app_model_set_contact_flags(s_contact_detail_contact.fingerprint" in source
    assert "d1l_app_model_export_contact_uri(s_contact_detail_contact.fingerprint" in source
    assert "s_contact_action_favorite" in source
    assert "s_contact_action_mute" in source
    assert "open_dm_compose_for_contact(&s_contact_detail_contact)" in source
    assert '"Contact Export"' in source
    assert '"MeshCore QR  %.16s  type %u"' in source
    assert "lv_qrcode_create" in source
    assert "lv_qrcode_update" in source


def test_route_detail_sheet_opens_from_route_rows():
    source = read("main/ui/ui_phase1.c")
    assert "static lv_obj_t *s_route_detail_sheet" in source
    assert "static d1l_route_entry_t s_route_detail_route" in source
    assert "render_route_row" in source
    assert "render_route_detail_sheet" in source
    assert "open_route_detail_event_cb" in source
    assert "create_route_detail_sheet" in source
    assert "lv_obj_add_event_cb(row, open_route_detail_event_cb, LV_EVENT_CLICKED" in source
    assert "recent_route_count" in source
    assert "recent_routes" in source
    assert '"Routes"' in source
    assert "hide_route_detail_sheet()" in source


def test_packet_detail_sheet_opens_from_packet_rows():
    source = read("main/ui/ui_phase1.c")
    assert "static lv_obj_t *s_packet_detail_sheet" in source
    assert "static lv_obj_t *s_packet_search_sheet" in source
    assert "static d1l_packet_log_entry_t s_packet_detail_packet" in source
    assert "static d1l_packet_filter_mode_t s_packet_filter_mode" in source
    assert "render_packet_detail_sheet" in source
    assert "open_packet_detail_event_cb" in source
    assert "open_packet_search_event_cb" in source
    assert "packet_filter_event_cb" in source
    assert "create_packet_detail_sheet" in source
    assert "create_packet_search_sheet" in source
    assert "lv_obj_add_event_cb(row, open_packet_detail_event_cb, LV_EVENT_CLICKED" in source
    assert "d1l_packet_log_query" in source
    assert '"All"' in source
    assert '"RX"' in source
    assert '"TX"' in source
    assert '"Text"' in source
    assert '"Search"' in source
    assert '"Packet Search"' in source
    assert '"Search kind, note, raw hex"' in source
    assert '"Raw Hex"' in source
    assert "raw_hex" in source
    assert "recent_packet_count" in source
    assert "s_packet_filtered_packets" in source
    assert "hide_packet_detail_sheet()" in source
    assert "hide_packet_search_sheet()" in source


def test_settings_screen_reports_companion_wireless_state():
    source = read("main/ui/ui_phase1.c")
    header = read("main/app/app_model.h")
    assert "wifi_state" in header
    assert "ble_state" in header
    assert "coexistence_policy" in header
    assert '"Companion"' in source
    assert '"Wi-Fi %s  BLE %s"' in source
    assert '"USB ready  %s"' in source
    assert "render_health_line" in source
    assert '"reset %s  heap %luK/%luK  ui stk %lu"' in source


def test_settings_screen_has_safe_touch_radio_editor():
    source = read("main/ui/ui_phase1.c")
    model = read("main/app/app_model.c")

    assert "static lv_obj_t *s_radio_settings_sheet" in source
    assert "static d1l_app_radio_profile_edit_t s_radio_edit" in source
    assert "create_radio_settings_sheet" in source
    assert "render_radio_settings_sheet" in source
    assert "open_radio_settings_event_cb" in source
    assert "radio_edit_adjust_event_cb" in source
    assert "radio_save_event_cb" in source
    assert "radio_defaults_event_cb" in source
    assert "radio_edit_from_snapshot" in source
    assert "format_radio_profile_line" in source
    assert 'create_button(s_content, "Radio"' in source
    assert '"Radio Settings"' in source
    assert '"Saved profile applies after reboot; live RF unchanged"' in source
    assert '"US/CAN"' in source
    assert '"Save"' in source
    assert '"RX Boost On"' in source
    assert "D1L_RADIO_EDIT_FREQ_DOWN" in source
    assert "s_radio_edit.frequency_hz -= 25000UL" in source
    assert "s_radio_edit.frequency_hz += 25000UL" in source
    assert "next_radio_bandwidth" in source
    assert "s_radio_edit.rx_boost = !s_radio_edit.rx_boost" in source
    assert "d1l_app_model_save_radio_profile(&s_radio_edit)" in source
    assert "d1l_app_model_default_radio_profile(&s_radio_edit)" in source
    assert "d1l_settings_save(&settings)" in model
    assert "settings.tcxo_mode = D1L_TCXO_NONE" in model
