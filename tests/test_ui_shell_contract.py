from pathlib import Path

from tools import ui_simulator


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
    assert "D1L_APP_SNAPSHOT_ROOM_PREVIEW D1L_ROOM_SERVER_PREVIEW_CAPACITY" in header
    assert "D1L_APP_SNAPSHOT_REPEATER_PREVIEW D1L_REPEATER_PREVIEW_CAPACITY" in header
    assert "D1L_HOME_MESSAGE_PREVIEW 5U" in header
    assert "D1L_HOME_REPEATER_PREVIEW 3U" in header
    assert "d1l_home_message_preview_t" in header
    assert "d1l_home_repeater_preview_t" in header
    assert "d1l_app_snapshot_t" in header
    assert "d1l_app_model_snapshot" in header
    assert "static d1l_app_snapshot_t s_snapshot" in read("main/ui/ui_phase1.c")
    assert "d1l_contact_store_copy_recent" in source
    assert "d1l_route_store_copy_recent" in source
    assert "d1l_app_model_query_nodes(&node_query" in source
    assert "d1l_packet_log_copy_recent" in source
    assert "d1l_dm_store_copy_recent" in source
    assert "d1l_app_model_copy_dm_thread" in header
    assert "d1l_app_model_copy_dm_thread_page" in header
    assert "d1l_dm_store_copy_thread_page(fingerprint, out_entries" in source
    assert "d1l_meshcore_service_status" in source
    assert "d1l_health_snapshot" in source
    assert "d1l_connectivity_status" in source
    assert "d1l_mesh_inspector_signal_summary" in source
    assert "reset_reason" in header
    assert "onboarding_complete" in header
    assert "ui_task_stack_free_words" in header
    assert "lvgl_used_pct" in header
    assert "map_page_supported" in header
    assert "map_tile_cache_ready" in header
    assert "map_tile_download_supported" in header
    assert "map_tile_sideload_supported" in header
    assert "map_tile_cache_policy" in header
    assert "map_tile_cache_path_template" in header
    assert "map_tile_download_state" in header
    assert "map_tile_download_requires" in header
    assert "map_location_set" in header
    assert "map_tile_provider_saved" in header
    assert "map_tile_url_template" in header
    assert "map_tile_attribution" in header
    assert "map_tile_zoom" in header
    assert "map_lat_e7" in header
    assert "map_lon_e7" in header
    assert "d1l_map_tile_store_sd_ready(&storage)" in source
    assert "D1L_MAP_TILE_CACHE_POLICY" in source
    assert '"provider_required"' in source
    assert '"location_required"' in source
    assert '"ready"' in source
    assert "snapshot->map_location_set = settings->map_location_set" in source
    assert "snapshot->map_tile_provider_saved = settings->map_tile_provider_saved" in source
    assert "settings->map_tile_url_template" in source
    assert "d1l_app_model_save_map_tile_provider" in header
    assert "d1l_app_model_download_center_map_tile" in header
    assert "d1l_app_radio_profile_edit_t" in header
    assert "radio_frequency_hz" in header
    assert "radio_bandwidth_tenths_khz" in header
    assert "radio_spreading_factor" in header
    assert "radio_coding_rate" in header
    assert "radio_tx_power_dbm" in header
    assert "radio_rx_boost" in header
    assert "radio_tcxo" in header
    assert "time_available" in header
    assert "time_label[8]" in header
    assert "home_messages[D1L_HOME_MESSAGE_PREVIEW]" in header
    assert "home_repeaters[D1L_HOME_REPEATER_PREVIEW]" in header
    assert "static void copy_cstr(char *dest, size_t dest_size, const char *src)" in source
    assert "populate_home_messages(snapshot)" in source
    assert "populate_home_repeaters(snapshot)" in source
    assert 'snprintf(snapshot->time_label, sizeof(snapshot->time_label), "--:--")' in source
    assert "d1l_app_model_save_radio_profile" in header
    assert "d1l_app_model_default_radio_profile" in header
    assert "d1l_app_model_current_radio_profile" in header
    assert "valid_radio_edit" in source
    assert "d1l_app_model_set_map_location" in header
    assert "d1l_app_model_clear_map_location" in header
    assert "settings.map_location_set = true" in source
    assert "settings.map_location_set = false" in source


def test_phase3_shell_replaces_diagnostic_tile_home():
    source = read("main/ui/ui_phase1.c")
    assert "D1L_UI_TAB_HOME" in source
    assert "D1L_UI_TAB_MESSAGES" in source
    assert "D1L_UI_TAB_NODES" in source
    assert "D1L_UI_TAB_MAP" in source
    assert "D1L_UI_TAB_PACKETS" in source
    assert "D1L_UI_TAB_SETTINGS" in source
    assert "render_map" in source
    assert "create_top_bar" in source
    assert "create_dock" in source
    assert "create_sheet" in source
    assert "create_toast" in source
    assert "create_lock_overlay" in source
    assert "create_onboarding_sheet" in source
    assert "Phase 1 hardware bring-up" not in source


def test_home_screen_is_user_first_companion_dashboard():
    source = read("main/ui/ui_phase1.c")
    header = read("main/app/app_model.h")

    assert "home_sd_state" in source
    assert "render_home_chip" in source
    assert '"Time"' in source
    assert '"Wi-Fi"' in source
    assert '"BLE"' in source
    assert '"SD"' in source
    assert '"Public"' in source
    assert '"DMs"' in source
    assert '"Last Messages"' in source
    assert '"Local Repeaters"' in source
    assert "render_home_message_preview" in source
    assert "render_home_repeater_preview" in source
    assert "snapshot->public_unread_count" in source
    assert "snapshot->dm_unread_count" in source
    assert "snapshot->home_message_count && i < D1L_HOME_MESSAGE_PREVIEW" in source
    assert "snapshot->home_repeater_count && i < D1L_HOME_REPEATER_PREVIEW" in source
    assert "open_messages_public_event_cb" in source
    assert "open_messages_dm_event_cb" in source
    assert "open_home_dm_preview_event_cb" in source
    assert "request_tab_event_cb" in source
    assert "open_storage_sheet_event_cb" in source
    home_body = source.split("static void render_home(const d1l_app_snapshot_t *snapshot)", 1)[1].split(
        "static void render_storage_line", 1
    )[0]
    assert "RF Packets" not in home_body
    assert "System" not in home_body
    assert "#define D1L_APP_SNAPSHOT_MESSAGE_PREVIEW 5U" in header
    assert "#define D1L_HOME_MESSAGE_PREVIEW 5U" in header
    assert "#define D1L_HOME_REPEATER_PREVIEW 3U" in header


def test_p0_message_layouts_keep_text_out_of_headers_and_dock():
    source = read("main/ui/ui_phase1.c")

    home_preview = source.split("static void render_home_message_preview", 1)[1].split(
        "static void render_home_repeater_preview", 1
    )[0]
    assert "create_panel(parent, 18, y, 424, 76)" in home_preview
    assert 'entry->text[0] ? entry->text : "No text"' in home_preview
    assert "lv_obj_set_pos(sender, 8, 4)" in home_preview
    assert "lv_obj_set_pos(text, 8, 28)" in home_preview
    assert "lv_obj_set_pos(details, 8, 52)" in home_preview
    assert "%s ago  rssi %d  snr %s  hops %u" in home_preview

    message_row = source.split("static void render_message_row", 1)[1].split(
        "static void render_dm_row", 1
    )[0]
    dm_row = source.split("static void render_dm_row", 1)[1].split(
        "static char packet_lower_ascii", 1
    )[0]
    assert "create_panel(parent, 18, y, 424, 72)" in message_row
    assert "create_panel(parent, 18, y, 424, 72)" in dm_row
    assert "lv_obj_set_pos(text, 8, 28)" in message_row
    assert "lv_obj_set_pos(details, 8, 50)" in message_row
    assert "lv_obj_set_pos(text, 8, 28)" in dm_row
    assert "lv_obj_set_pos(details, 8, 50)" in dm_row

    messages = source.split("static void render_messages(const d1l_app_snapshot_t *snapshot)", 1)[1].split(
        "static void render_nodes", 1
    )[0]
    assert "create_panel(s_content, 18, 16, 424, 108)" in messages
    assert 'create_button(header, "Compose", 70, 62, 88, 40' in messages
    assert "for (size_t i = 0; i < snapshot->recent_message_count; ++i)" in messages
    assert "for (size_t i = 0; i < snapshot->recent_dm_count; ++i)" in messages

    detail = source.split("static void render_message_detail_sheet(void)", 1)[1].split(
        "static void open_message_detail_event_cb", 1
    )[0]
    normal_detail = detail.split("if (!s_message_detail_advanced)", 1)[0]
    assert 's_message_detail_advanced ? "Normal" : "Advanced"' in detail
    assert '"%s  %s"' in normal_detail
    assert '"Path %u hop%s"' in normal_detail
    assert "#%lu" not in normal_detail
    assert "uptime" not in normal_detail
    assert '"seq %lu  uptime %lums  direction %s"' in detail
    assert '"path hash %u byte  delivered %s"' in detail

    compose = source.split("static void create_compose_sheet", 1)[1].split(
        "static void create_public_history_sheet", 1
    )[0]
    assert "static lv_obj_t *s_dock" in source
    assert "set_dock_hidden(true)" in source
    assert "set_dock_hidden(false)" in source
    assert "lv_obj_set_size(s_compose_sheet, 480, 424)" in compose
    assert "lv_obj_set_pos(s_compose_sheet, 0, 56)" in compose
    assert "lv_obj_set_size(s_compose_keyboard, 448, 244)" in compose
    assert "lv_obj_set_pos(s_compose_keyboard, 16, 168)" in compose


def test_main_content_root_is_scrollable_and_serial_tab_switchable():
    source = read("main/ui/ui_phase1.c")
    header = read("main/ui/ui_phase1.h")
    console = read("main/comms/usb_console.c")

    assert "configure_content_scroll_root" in source
    assert "lv_obj_add_flag(root, LV_OBJ_FLAG_SCROLLABLE)" in source
    assert "lv_obj_set_scroll_dir(root, LV_DIR_VER)" in source
    assert "lv_obj_set_scrollbar_mode(root, LV_SCROLLBAR_MODE_AUTO)" in source
    assert "configure_content_scroll_root(s_content)" in source
    assert "lv_obj_scroll_to_y(s_content, 0, LV_ANIM_OFF)" in source
    assert "lv_obj_clear_flag(s_content, LV_OBJ_FLAG_SCROLLABLE)" not in source
    assert "d1l_ui_phase1_request_tab" in header
    assert "d1l_ui_phase1_active_tab_name" in header
    assert "cmd_ui_status" in console
    assert "cmd_ui_tab" in console
    assert '"ui status"' in console
    assert "ui tab <home|messages|nodes|map|packets|settings>" in console


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
    assert "d1l_app_model_set_map_location(s_map_location_lat_e7" in source
    assert "d1l_app_model_clear_map_location()" in source
    assert "d1l_app_model_send_public_text" in header
    assert "d1l_app_model_send_dm_text" in header
    assert "d1l_app_model_complete_onboarding" in header
    assert "d1l_app_model_find_contact" in header
    assert "d1l_app_model_set_contact_flags" in header
    assert "d1l_app_model_export_contact_uri" in header
    assert "d1l_app_model_copy_route_trace" in header
    assert 'd1l_meshcore_service_send_public("test")' in model
    assert "d1l_meshcore_service_send_public(text)" in model
    assert "d1l_meshcore_service_send_dm(fingerprint, text)" in model
    assert "d1l_contact_store_find_by_fingerprint(fingerprint, out_contact)" in model
    assert "d1l_contact_store_set_flags(fingerprint, favorite, muted, out_contact)" in model
    assert "d1l_contact_store_export_uri(&contact, dest, dest_size)" in model
    assert "d1l_route_store_copy_for_target(fingerprint, out_entries, max_entries)" in model
    assert "d1l_meshcore_service_request_advert(flood)" in model
    assert "d1l_settings_complete_onboarding(node_name, false, false, false)" in model
    assert "d1l_settings_save(&settings)" in model
    assert "d1l_meshcore_service_ensure_identity()" in model


def test_ui_simulator_flow_names_match_lvgl_handlers():
    source = read("main/ui/ui_phase1.c")
    actions = {
        step["action"]
        for flow in ui_simulator.EXPECTED_FLOWS
        for step in flow["steps"]
    }
    flow_names = {flow["name"] for flow in ui_simulator.EXPECTED_FLOWS}

    assert {
        "first_boot_onboarding",
        "lock_overlay_unlock",
        "public_compose_and_send",
        "public_history_search",
        "dm_thread_read_and_reply",
        "node_detail_inspection",
        "contact_detail_management",
        "contact_edit_alias_and_forget",
        "map_page_policy",
        "packet_filters_search_and_details",
        "mesh_roles_browser",
        "settings_radio_storage_and_advert",
    } <= flow_names

    handler_symbols = {
        "complete_onboarding": "onboarding_start_event_cb",
        "apply_onboarding_defaults": "onboarding_defaults_event_cb",
        "unlock": "unlock_event_cb",
        "open_public_compose": "open_compose_event_cb",
        "edit_public_message": "s_compose_textarea",
        "send_public_text": "send_compose_text",
        "mark_messages_read": "mark_messages_read_event_cb",
        "open_messages_public": "open_messages_public_event_cb",
        "open_messages_dm": "open_messages_dm_event_cb",
        "open_public_history": "open_public_history_event_cb",
        "open_public_search": "open_public_search_event_cb",
        "open_message_detail": "open_message_detail_event_cb",
        "open_public_reply": "reply_message_detail_event_cb",
        "edit_public_search": "s_public_search_textarea",
        "apply_public_search": "apply_public_search_event_cb",
        "open_dm_thread": "open_dm_thread_event_cb",
        "open_dm_reply": "reply_dm_thread_event_cb",
        "mark_dm_thread_read": "read_dm_thread_event_cb",
        "open_contact_detail": "open_contact_detail_event_cb",
        "open_node_detail": "open_node_detail_event_cb",
        "close_node_detail": "close_node_detail_event_cb",
        "open_contact_edit": "open_contact_edit_event_cb",
        "edit_contact_alias": "s_contact_edit_textarea",
        "save_contact_alias": "save_contact_edit_event_cb",
        "forget_contact": "forget_contact_edit_event_cb",
        "open_map": "dock_event_cb",
        "open_map_location_picker": "open_map_location_sheet_event_cb",
        "edit_map_latitude": "s_map_lat_textarea",
        "edit_map_longitude": "s_map_lon_textarea",
        "save_map_location": "map_location_save_event_cb",
        "skip_map_location": "close_map_location_sheet_event_cb",
        "open_contact_export": "contact_detail_export_event_cb",
        "open_route_trace": "open_route_trace_event_cb",
        "open_packet_search": "open_packet_search_event_cb",
        "edit_packet_search": "s_packet_search_textarea",
        "apply_packet_search": "apply_packet_search_event_cb",
        "open_packet_detail": "open_packet_detail_event_cb",
        "open_route_detail": "open_route_detail_event_cb",
        "open_mesh_roles": "open_mesh_roles_event_cb",
        "open_radio_settings": "open_radio_settings_event_cb",
        "radio_freq_down": "radio_edit_adjust_event_cb",
        "radio_cycle_bandwidth": "radio_edit_adjust_event_cb",
        "save_radio_profile": "radio_save_event_cb",
        "open_storage_setup": "open_storage_sheet_event_cb",
        "wifi_scan": "wifi_scan_event_cb",
        "wifi_connect": "wifi_connect_event_cb",
        "open_display_settings": "open_display_sheet_event_cb",
        "open_diagnostics": "open_diagnostics_sheet_event_cb",
        "open_advert_sheet": "open_sheet_event_cb",
        "send_advert_zero": "advert_zero_event_cb",
        "send_advert_flood": "advert_flood_event_cb",
    }
    assert set(handler_symbols) <= actions
    for symbol in handler_symbols.values():
        assert symbol in source


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


def test_ui_allocation_wrappers_and_stack_budget_are_hardened():
    source = read("main/ui/ui_phase1.c")
    assert "#define D1L_UI_TASK_STACK_BYTES 6144U" in source
    assert "#define D1L_TOUCH_TASK_STACK_BYTES 4096U" in source
    assert 'xTaskCreatePinnedToCore(ui_task, "d1l_ui", D1L_UI_TASK_STACK_BYTES' in source
    assert "ui_task_stack_free_words" in source
    assert "create_screen_object" in source
    assert "create_object" in source
    assert "create_label_object" in source
    assert "create_textarea" in source
    assert "create_keyboard" in source
    assert "create_qrcode" in source
    assert 's_screen = create_screen_object("root screen")' in source
    assert 's_content = create_object(s_screen, "content root")' in source
    assert 's_compose_textarea = create_textarea(s_compose_sheet, "compose textarea")' in source
    assert 's_compose_keyboard = create_keyboard(s_compose_sheet, "compose keyboard")' in source
    assert 'create_qrcode(s_contact_export_sheet' in source


def test_public_composer_uses_lvgl_textarea_keyboard():
    source = read("main/ui/ui_phase1.c")
    assert "create_compose_sheet" in source
    assert "open_compose_event_cb" in source
    assert "show_public_compose_sheet" in source
    assert "send_compose_text" in source
    assert "static lv_obj_t *s_compose_counter" in source
    assert "update_compose_counter" in source
    assert 'label_set_fmt(s_compose_counter, "%u/%u"' in source
    assert "D1L_MESSAGE_MAX_CHARS" in source
    assert "lv_textarea_create" in source
    assert "lv_textarea_set_max_length" in source
    assert "LV_EVENT_VALUE_CHANGED" in source
    assert "compose_textarea_event_cb" in source
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
    assert "static d1l_dm_entry_t s_dm_thread_entries[D1L_DM_STORE_CAPACITY]" in source
    assert "static bool s_dm_thread_unread[D1L_DM_STORE_CAPACITY]" in source
    assert "static size_t s_dm_thread_limit" in source
    assert "dm_thread_load_older_event_cb" in source
    assert "d1l_app_model_copy_dm_thread_page(s_dm_thread_fingerprint" in source
    assert "lv_obj_set_scroll_dir(list, LV_DIR_VER)" in source
    assert "lv_obj_scroll_to_y(list, LV_COORD_MAX, LV_ANIM_OFF)" in source
    assert "lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE)" in source
    assert "lv_obj_add_event_cb(row, open_dm_thread_event_cb, LV_EVENT_CLICKED" in source
    assert "d1l_app_model_find_contact(s_dm_thread_fingerprint, &contact)" in source
    assert "open_dm_compose_for_contact(&contact)" in source
    assert 'create_button(s_dm_thread_sheet, "Reply"' in source
    assert 'create_button(s_dm_thread_sheet, "Load Older"' in source
    assert "hide_dm_thread_sheet()" in source


def test_public_message_detail_sheet_opens_from_public_rows():
    source = read("main/ui/ui_phase1.c")
    assert "static lv_obj_t *s_message_detail_sheet" in source
    assert "static d1l_message_entry_t s_message_detail_message" in source
    assert "render_message_detail_sheet" in source
    assert "open_message_detail_event_cb" in source
    assert "close_message_detail_event_cb" in source
    assert "reply_message_detail_event_cb" in source
    assert "create_message_detail_sheet" in source
    assert "lv_obj_add_event_cb(row, open_message_detail_event_cb, LV_EVENT_CLICKED" in source
    assert '"Message Detail"' in source
    assert '"Sender"' in source
    assert 'create_button(s_message_detail_sheet, "Reply"' in source
    assert 'snprintf(title, sizeof(title), "Reply %.32s"' in source
    assert 'snprintf(placeholder, sizeof(placeholder), "Reply to %.48s"' in source
    assert "show_public_compose_sheet(title, placeholder)" in source
    assert '"Signal rssi %d  snr %s%d.%d"' in source
    assert '"Path %u hop%s"' in source
    assert 's_message_detail_advanced ? "Normal" : "Advanced"' in source
    assert '"seq %lu  uptime %lums  direction %s"' in source
    assert "message_delivery_label" in source
    assert "hide_message_detail_sheet()" in source


def test_nodes_screen_renders_heard_node_rows():
    source = read("main/ui/ui_phase1.c")
    header = read("main/app/app_model.h")
    model = read("main/app/app_model.c")
    assert "render_node_row" in source
    assert "render_contact_row" in source
    assert "static d1l_node_view_t s_node_rows[D1L_NODE_STORE_CAPACITY]" in source
    assert "recent_node_count" in header
    assert "recent_nodes" in header
    assert "d1l_node_view_t recent_nodes" in header
    assert "d1l_app_model_query_nodes" in header
    assert "d1l_node_store_query(query, out_entries, max_entries)" in model
    assert "d1l_node_query_t node_query" in model
    assert "node_role_badge_text" in source
    assert "node_role_color" in source
    assert "node_view_can_dm" in source
    assert "node_view_management_gated" in source
    assert 'create_button(row, "DM", 350, -1, 52, 34, open_node_dm_event_cb' in source
    assert "render_node_role_badge" in source
    assert "lv_obj_add_event_cb(row, open_node_detail_event_cb, LV_EVENT_CLICKED" in source
    assert "recent_contact_count" in source
    assert "recent_contacts" in source
    assert "No heard nodes yet" in source
    assert "node_total_written" in source
    assert "contact_total_written" in source
    assert "route_count" in source
    assert "#define D1L_APP_SNAPSHOT_NODE_PREVIEW 4U" in header
    assert "#define D1L_APP_SNAPSHOT_CONTACT_PREVIEW 2U" in header
    assert "i < snapshot->recent_contact_count && y <= 190" in source
    assert "d1l_app_model_query_nodes(&node_query, s_node_rows" in source
    assert "D1L_NODE_STORE_CAPACITY" in source
    assert '"All Heard"' in source
    assert "for (size_t i = 0; i < node_rows; ++i)" in source


def test_node_detail_sheet_opens_from_heard_node_rows():
    source = read("main/ui/ui_phase1.c")
    assert "static lv_obj_t *s_node_detail_sheet" in source
    assert "static d1l_node_view_t s_node_detail_node" in source
    assert "create_node_detail_sheet" in source
    assert "render_node_detail_sheet" in source
    assert "open_node_detail_event_cb" in source
    assert "close_node_detail_event_cb" in source
    assert "hide_node_detail_sheet()" in source
    assert 'create_label(s_node_detail_sheet, "Node Detail"' in source
    assert '"Role %s"' in source
    assert '"Fingerprint %.16s"' in source
    assert '"Public key %s  %s  %s"' in source
    assert '"Signal rssi %d  snr %s%d.%d  %s"' in source
    assert '"Path hops %u  hash %u byte  advert %lums"' in source
    assert '"Last heard %lums  first %lums  count %lu"' in source
    assert 'create_button(s_node_detail_sheet, "DM"' in source
    assert '"Manage locked"' in source
    assert 'create_button(s_node_detail_sheet, "Close"' in source
    assert "create_node_detail_sheet(s_screen)" in source


def test_map_screen_reports_offline_tile_policy_without_rf_or_downloads():
    source = read("main/ui/ui_phase1.c")
    header = read("main/app/app_model.h")
    model = read("main/app/app_model.c")

    assert "D1L_UI_TAB_MAP" in source
    assert "render_map" in source
    assert '"Map"' in source
    assert '"Set Pin"' in source
    assert '"Move Pin"' in source
    assert '"Tiles"' in source
    assert '"Map Tiles"' in source
    assert '"Allowed provider template"' in source
    assert '"Attribution"' in source
    assert '"Download"' in source
    assert '"Downloads one center tile for your saved D1L location."' in source
    assert '"No public OSM bulk tile servers. Visible attribution is required."' in source
    assert '"Set D1L Location"' in source
    assert '"Map needs your D1L location"' in source
    assert '"Enter decimal degrees"' in source
    assert '"Latitude"' in source
    assert '"Longitude"' in source
    assert '"43.6532000"' in source
    assert '"-79.3832000"' in source
    assert '"Tile Cache"' in source
    assert '"Downloads"' in source
    assert '"Offline Cache"' in source
    assert '"No Offline Tiles"' in source
    assert '"Connect Wi-Fi and download allowed tiles for your area."' in source
    assert '"Allowed provider and visible attribution required."' in source
    assert '"Center"' in source
    assert '"Manual Location  %s, %s"' in source
    assert '"Manual Location unset"' in source
    assert '"Routes"' in source
    assert "snapshot->map_tile_cache_ready" in source
    assert "snapshot->map_tile_download_supported" in source
    assert "snapshot->map_tile_cache_policy" in source
    assert "snapshot->map_tile_cache_path_template" in source
    assert "snapshot->map_tile_download_requires" in source
    assert "snapshot->map_tile_provider_policy" in model
    assert "snapshot->map_tile_provider_attribution" in model
    assert "snapshot->map_location_set" in source
    assert "s_map_location_prompt_seen" in source
    assert "create_map_location_sheet(s_screen)" in source
    assert "open_map_location_sheet_event_cb" in source
    assert "open_map_tiles_sheet_event_cb" in source
    assert "render_map_tiles_sheet" in source
    assert "create_map_tiles_sheet(s_screen)" in source
    assert "d1l_app_model_save_map_tile_provider(url, attribution" in source
    assert "d1l_app_model_download_center_map_tile(&result)" in source
    assert "map_location_textarea_event_cb" in source
    assert "map_tiles_textarea_event_cb" in source
    assert "map_tiles_keyboard_event_cb" in source
    assert "map_location_keyboard_event_cb" in source
    assert "parse_coord_text_e7" in source
    assert "map_location_save_event_cb" in source
    assert "format_coord_e7" in source
    assert "render_metric_card(s_content, 18, 48" in source
    assert "render_metric_card(s_content, 238, 48" in source
    assert 'const char *labels[] = {"Home", "Msg", "Nodes", "Map", "Pkts", "Set"}' in source
    assert "for (int i = 0; i < 6; ++i)" in source
    assert "map_tile_download_supported" in header
    assert "map_tile_sideload_supported" in header
    assert "map_tile_provider_policy" in header
    assert "map_tile_provider_attribution" in header
    assert "snapshot->map_tile_download_supported = connectivity.wifi_build_enabled" in model
    assert '"sd_cache_required"' in model
    assert '"wifi_required"' in model
    assert "snapshot->map_tile_sideload_supported = true" in model


def test_messages_screen_renders_bounded_preview_rows():
    source = read("main/ui/ui_phase1.c")
    header = read("main/app/app_model.h")
    assert "#define D1L_APP_SNAPSHOT_MESSAGE_PREVIEW 5U" in header
    assert "#define D1L_APP_SNAPSHOT_DM_PREVIEW 5U" in header
    assert "static bool s_messages_show_dms" in source
    assert "set_messages_mode(false)" in source
    assert "set_messages_mode(true)" in source
    assert '"Public Channel"' in source
    assert '"DM Conversations"' in source
    assert "for (size_t i = 0; i < snapshot->recent_message_count; ++i)" in source
    assert "for (size_t i = 0; i < snapshot->recent_dm_count; ++i)" in source
    assert "y += 80" in source
    assert "snapshot->message_count" in source
    assert "snapshot->dm_count" in source
    assert "snapshot->public_unread_count" in source
    assert "snapshot->dm_unread_count" in source
    assert 'create_button(header, "History"' in source
    assert "static d1l_message_entry_t s_public_history_entries[D1L_MESSAGE_STORE_CAPACITY]" in source
    assert "static size_t s_public_history_limit" in source
    assert "render_public_history_sheet" in source
    assert "public_history_load_older_event_cb" in source
    assert "d1l_app_model_query_public_messages_page(s_public_history_entries" in source
    assert "D1L_PUBLIC_HISTORY_UI_LOAD_OLDER_STEP" in source
    assert "lv_obj_scroll_to_y(list, LV_COORD_MAX, LV_ANIM_OFF)" in source
    assert 'create_button(s_public_history_sheet, "Load Older"' in source
    assert '"Public History"' in source
    assert '"Public Search"' in source
    assert '"Search author or message"' in source


def test_contact_detail_sheet_exposes_dm_favorite_and_mute_actions():
    source = read("main/ui/ui_phase1.c")
    assert "static lv_obj_t *s_contact_detail_sheet" in source
    assert "static lv_obj_t *s_contact_edit_sheet" in source
    assert "static lv_obj_t *s_contact_export_sheet" in source
    assert "static lv_obj_t *s_route_trace_sheet" in source
    assert "static d1l_contact_entry_t s_contact_detail_contact" in source
    assert "static d1l_contact_entry_t s_contact_export_contact" in source
    assert "static d1l_contact_entry_t s_route_trace_contact" in source
    assert "static d1l_route_entry_t s_route_trace_entries[D1L_ROUTE_STORE_CAPACITY]" in source
    assert "open_contact_detail_event_cb" in source
    assert "open_route_trace_event_cb" in source
    assert "create_contact_detail_sheet" in source
    assert "create_contact_edit_sheet" in source
    assert "create_contact_export_sheet" in source
    assert "create_route_trace_sheet" in source
    startup = source.split("esp_err_t d1l_ui_phase1_show_home(void)", 1)[1].split(
        "esp_err_t d1l_ui_phase1_start(void)", 1
    )[0]
    assert "create_contact_edit_sheet(s_screen)" not in startup
    assert "render_contact_detail_sheet" in source
    assert "open_contact_edit_event_cb" in source
    assert "render_contact_export_sheet" in source
    assert "render_route_trace_sheet" in source
    assert "contact_detail_dm_event_cb" in source
    assert "contact_detail_export_event_cb" in source
    assert "lv_obj_add_event_cb(row, open_contact_detail_event_cb, LV_EVENT_CLICKED" in source
    assert "d1l_app_model_set_contact_flags(s_contact_detail_contact.fingerprint" in source
    assert "d1l_app_model_rename_contact(s_contact_detail_contact.fingerprint" in source
    assert "d1l_app_model_delete_contact(s_contact_detail_contact.fingerprint" in source
    assert "d1l_app_model_export_contact_uri(s_contact_detail_contact.fingerprint" in source
    assert "s_contact_action_favorite" in source
    assert "s_contact_action_mute" in source
    assert "open_dm_compose_for_contact(&s_contact_detail_contact)" in source
    assert "d1l_app_model_copy_route_trace(contact->fingerprint, s_route_trace_entries" in source
    assert "route_trace_probe_event_cb" in source
    assert "d1l_app_model_request_trace_probe(s_route_trace_contact.fingerprint" in source
    assert 'create_button(s_route_trace_sheet, "Ping"' in source
    assert 'create_button(s_contact_detail_sheet, "Trace"' in source
    assert 'create_button(s_contact_detail_sheet, "Edit"' in source
    assert '"Alias only; retained history remains"' in source
    assert '"DM-only trace probe; no Public RF"' in source
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
    assert "static bool s_packets_paused" in source
    assert "static bool s_packet_detail_advanced" in source
    assert "static d1l_packet_log_entry_t s_packet_filtered_packets[D1L_PACKET_LOG_CAPACITY]" in source
    assert "static const size_t D1L_PACKET_UI_INITIAL_ROWS = 100U" in source
    assert "static const size_t D1L_PACKET_UI_LOAD_OLDER_STEP = 100U" in source
    assert "static size_t s_packet_skip_newest" in source
    assert "static size_t s_packet_total_matches" in source
    assert "static bool s_packet_sd_history_page" in source
    assert "render_packet_detail_sheet" in source
    assert "open_packet_detail_event_cb" in source
    assert "open_packet_search_event_cb" in source
    assert "packet_filter_event_cb" in source
    assert "packet_pause_event_cb" in source
    assert "packet_load_older_event_cb" in source
    assert "packet_load_newer_event_cb" in source
    assert "packet_detail_mode_event_cb" in source
    assert "create_packet_detail_sheet" in source
    assert "create_packet_search_sheet" in source
    assert "lv_obj_add_event_cb(row, open_packet_detail_event_cb, LV_EVENT_CLICKED" in source
    assert "d1l_packet_log_query_page" in source
    assert "refresh_packet_terminal_rows" in source
    assert "packet_entry_color" in source
    assert "packet_entry_status" in source
    assert '"All"' in source
    assert '"RX"' in source
    assert '"TX"' in source
    assert '"Text"' in source
    assert '"Search"' in source
    assert '"Pause"' in source
    assert '"Resume"' in source
    assert '"Load Older"' in source
    assert '"Newer"' in source
    assert '"Packet Feed"' in source
    assert '"page %u-%u/%u%s"' in source
    assert '"live %s  rssi %d  snr %s  avg %d"' in source
    assert '"Packet Search"' in source
    assert '"Search kind, note, raw hex"' in source
    assert '"Advanced"' in source
    assert '"Normal"' in source
    assert '"Raw Hex"' in source
    assert "raw_hex" in source
    assert "recent_packet_count" in source
    assert "s_packet_filtered_packets" in source
    assert "s_packet_filtered_count" in source
    assert "packet_feed_can_load_older(packet_rows)" in source
    assert "packet_feed_can_load_newer()" in source
    assert "hide_packet_detail_sheet()" in source
    assert "hide_packet_search_sheet()" in source


def test_settings_screen_reports_companion_wireless_state():
    source = read("main/ui/ui_phase1.c")
    header = read("main/app/app_model.h")
    assert "wifi_state" in header
    assert "ble_state" in header
    assert "wifi_profile_saved" in header
    assert "wifi_password_saved" in header
    assert "wifi_scan_supported" in header
    assert "wifi_stack_active" in header
    assert "wifi_connected" in header
    assert "wifi_connecting" in header
    assert "wifi_ssid" in header
    assert "wifi_ip" in header
    assert "wifi_last_error" in header
    assert "coexistence_policy" in header
    assert '"Setup Dashboard"' in source
    assert '"SD Card"' in source
    assert '"Map Tiles"' in source
    assert '"Identity"' in source
    assert '"Advanced"' in source
    assert '"About"' in source
    assert '"Raw data and adverts"' in source
    assert "mesh stays offline" in source
    assert "FAT32 only, no format" in source
    assert "static lv_obj_t *s_wifi_sheet" in source
    assert "static lv_obj_t *s_ble_sheet" in source
    assert "static lv_obj_t *s_display_sheet" in source
    assert "static lv_obj_t *s_diagnostics_sheet" in source
    assert "create_wifi_sheet" in source
    assert "create_ble_sheet" in source
    assert "create_display_sheet" in source
    assert "create_diagnostics_sheet" in source
    assert "render_wifi_sheet" in source
    assert "render_ble_sheet" in source
    assert "render_display_sheet" in source
    assert "render_diagnostics_sheet" in source
    assert "open_wifi_sheet_event_cb" in source
    assert "open_ble_sheet_event_cb" in source
    assert "open_display_sheet_event_cb" in source
    assert "open_diagnostics_sheet_event_cb" in source
    assert '"Wi-Fi Setup"' in source
    assert '"BLE Setup"' in source
    assert '"Display"' in source
    assert '"Diagnostics"' in source
    assert "static lv_obj_t *s_wifi_ssid_textarea" in source
    assert "static lv_obj_t *s_wifi_password_textarea" in source
    assert "static lv_obj_t *s_wifi_keyboard" in source
    assert "d1l_app_model_save_wifi_profile(ssid, password && password[0] ? password : NULL)" in source
    assert "d1l_app_model_wifi_scan(&s_wifi_scan_result)" in source
    assert "d1l_app_model_wifi_connect()" in source
    assert "d1l_app_model_clear_wifi_profile()" in source
    assert "d1l_app_model_set_wifi_enabled(!s_snapshot.wifi_enabled)" in source
    assert "d1l_app_model_set_ble_enabled(!s_snapshot.ble_companion_enabled)" in source
    assert "lv_textarea_set_max_length(s_wifi_ssid_textarea, D1L_WIFI_SSID_LEN - 1U)" in source
    assert "lv_textarea_set_max_length(s_wifi_password_textarea, D1L_WIFI_PASSWORD_LEN - 1U)" in source
    assert '"Network name"' in source
    assert '"Password"' in source
    assert '"Scan to list nearby 2.4 GHz networks"' in source
    assert '"Connect"' in source
    assert "wifi_scan_event_cb" in source
    assert "wifi_connect_event_cb" in source
    assert '"Pairing controls are gated until the measured BLE runtime is enabled."' in source
    assert '"Touch display controls are staged until backlight/runtime persistence is wired."' in source
    assert '"Advanced details stay here so normal screens remain simple."' in source
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
    assert 'render_settings_tile(s_content, 238, 128, "Radio"' in source
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


def test_settings_screen_has_non_destructive_storage_setup_sheet():
    source = read("main/ui/ui_phase1.c")
    header = read("main/app/app_model.h")
    model = read("main/app/app_model.c")

    assert "static lv_obj_t *s_storage_sheet" in source
    assert "create_storage_sheet" in source
    assert "render_storage_sheet" in source
    assert "open_storage_sheet_event_cb" in source
    assert "hide_storage_sheet" in source
    assert '"SD Card"' in source
    assert '"next step: %s%s"' in source
    assert "DeskOS creates its folders automatically and never formats cards on-device." in source
    assert "storage_setup_action" in header
    assert "storage_sd_needs_fat32" in header
    assert "storage_rp2040_sd_protocol_supported" in header
    assert "snapshot->storage_setup_action = storage.setup_action" in model
    assert "snapshot->storage_sd_needs_fat32 = storage.sd_needs_fat32" in model


def test_live_packet_receive_store_reads_are_serialized_for_ui_snapshots():
    lock_header = read("main/mesh/store_lock.h")
    ui_source = read("main/ui/ui_phase1.c")
    assert "xSemaphoreCreateMutexStatic" in lock_header
    assert "d1l_store_lock_take" in lock_header
    assert "d1l_store_lock_give" in lock_header
    assert ".buffer = {0}" not in lock_header

    for path in [
        "main/mesh/packet_log.c",
        "main/mesh/message_store.c",
        "main/mesh/dm_store.c",
        "main/mesh/node_store.c",
        "main/mesh/route_store.c",
        "main/mesh/contact_store.c",
        "main/mesh/mesh_inspector.c",
    ]:
        source = read(path)
        assert '#include "mesh/store_lock.h"' in source, path
        assert "D1L_STORE_LOCK_INITIALIZER" in source, path
        assert "d1l_store_lock_take" in source, path
        assert "d1l_store_lock_give" in source, path

    refresh_body = ui_source.split("static void refresh_timer_cb", 1)[1].split(
        "static void create_top_bar", 1
    )[0]
    assert "render_active_tab()" not in refresh_body
