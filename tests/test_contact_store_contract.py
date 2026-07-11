from pathlib import Path

from scripts.smoke_d1l import SMOKE_COMMANDS


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def static_void_body(source: str, symbol: str) -> str:
    marker = f"static void {symbol}("
    start = source.rfind(marker)
    assert start >= 0, symbol
    end = source.find("\nstatic ", start + len(marker))
    return source[start : end if end >= 0 else len(source)]


def test_contact_store_is_bounded_and_nvs_backed():
    header = read("main/mesh/contact_store.h")
    source = read("main/mesh/contact_store.c")
    cmake = read("main/CMakeLists.txt")
    app_main = read("main/app_main.c")
    sdkconfig_defaults = read("sdkconfig.defaults")
    assert "D1L_CONTACT_STORE_CAPACITY 16U" in header
    assert "D1L_CONTACT_ALIAS_LEN 24U" in header
    assert "D1L_CONTACT_OUT_PATH_MAX 64U" in header
    assert "public_key_hex" in header
    assert "out_path_valid" in header
    assert "D1L_CONTACT_STORE_SCHEMA 4U" in source
    assert "D1L_CONTACT_STORE_SCHEMA_V3 3U" in source
    assert "D1L_CONTACT_STORE_LEGACY_TYPE_LEN 8U" in source
    assert "d1l_contact_store_blob_v3_t" in source
    assert "d1l_contact_store_blob_v2_t" in source
    assert "d1l_contact_store_blob_v1_t" in source
    assert "migrate_v1_blob" in source
    assert "migrate_v2_blob" in source
    assert "migrate_v3_blob" in source
    assert "migrate_legacy_advert_type" in source
    assert "contact schema v1 layout changed" in source
    assert "contact schema v2 layout changed" in source
    assert "contact schema v3 layout changed" in source
    assert 'D1L_CONTACT_STORE_NAMESPACE "d1l_contacts"' in source
    assert 'D1L_CONTACT_STORE_KEY "contacts"' in source
    assert "nvs_get_blob" in source
    assert "nvs_set_blob" in source
    assert "static d1l_contact_store_blob_t s_blob_scratch" in source
    assert "static d1l_contact_store_blob_t s_rollback_scratch" in source
    assert "persist_store_or_rollback" in source
    assert "find_index_by_fingerprint" in source
    assert "oldest_index" in source
    assert "d1l_contact_store_update_path" in source
    assert "d1l_contact_store_set_flags" in source
    assert "d1l_contact_store_rename" in header
    assert "d1l_contact_store_delete" in header
    assert "esp_err_t d1l_contact_store_rename" in source
    assert "esp_err_t d1l_contact_store_delete" in source
    assert source.count("persist_store_or_rollback(&s_rollback_scratch)") >= 5
    assert "d1l_contact_entry_t removed = s_entries[index]" in source
    assert "D1L_CONTACT_EXPORT_URI_LEN 224U" in header
    assert "d1l_contact_store_export_uri" in header
    assert "meshcore://contact/add?name=%s&public_key=%s&type=%u" in source
    assert "d1l_contact_store_meshcore_type_id" in source
    assert "d1l_contact_store_has_export_key" in source
    assert "CONFIG_LV_USE_QRCODE=y" in sdkconfig_defaults
    assert '"mesh/contact_store.c"' in cmake
    assert "d1l_contact_store_init()" in app_main


def test_contacts_can_promote_heard_nodes_by_fingerprint():
    node_header = read("main/mesh/node_store.h")
    node_source = read("main/mesh/node_store.c")
    console = read("main/comms/usb_console.c")
    assert "d1l_node_store_find_by_fingerprint" in node_header
    assert "bool d1l_node_store_find_by_fingerprint" in node_source
    assert "parse_fingerprint_token" in console
    assert "d1l_node_store_find_by_fingerprint(fingerprint, &heard)" in console
    assert "d1l_contact_store_upsert_from_node(fingerprint, alias" in console
    assert "heard_node->public_key_hex" in read("main/mesh/contact_store.c")
    assert 'ok_begin("contacts add")' in console
    assert '\\"public_key\\"' in console
    assert 'strcmp(line, "contacts")' in console
    assert 'strcmp(line, "contacts export")' in console
    assert 'strncmp(line, "contacts export ", 16)' in console
    assert 'strcmp(line, "contacts clear")' in console
    assert 'strncmp(line, "contacts add ", 13)' in console
    assert 'strncmp(line, "contacts rename ", 16)' in console
    assert 'strncmp(line, "contacts delete ", 16)' in console
    assert 'strncmp(line, "contacts set ", 13)' in console
    assert "d1l_contact_store_set_flags(fingerprint, contact.favorite, contact.muted, &contact)" in console
    assert "d1l_contact_store_rename(fingerprint, alias, &contact)" in console
    assert "d1l_contact_store_delete(fingerprint, &contact)" in console
    assert 'contacts set <fingerprint> <favorite|mute> <0|1>' in console
    assert 'contacts rename <fingerprint> <alias>' in console
    assert 'contacts delete <fingerprint>' in console


def test_ui_console_and_smoke_expose_contacts():
    app_header = read("main/app/app_model.h")
    app_source = read("main/app/app_model.c")
    ui = read("main/ui/ui_phase1.c")
    console = read("main/comms/usb_console.c")
    assert "recent_contacts" in app_header
    assert "contact_total_written" in app_header
    assert "d1l_contact_store_copy_recent" in app_source
    assert "d1l_app_model_set_contact_flags" in app_header
    assert "d1l_app_model_rename_contact" in app_header
    assert "d1l_app_model_delete_contact" in app_header
    assert "d1l_app_model_export_contact_uri" in app_header
    assert "d1l_contact_store_export_uri(&contact, dest, dest_size)" in app_source
    assert "d1l_contact_store_set_flags(fingerprint, favorite, muted, out_contact)" in app_source
    assert "d1l_contact_store_rename(fingerprint, alias, out_contact)" in app_source
    assert "d1l_contact_store_delete(fingerprint, out_contact)" in app_source
    assert "render_contact_row" in ui
    assert 'entry->public_key_hex[0] ? "key" : "no key"' in ui
    assert "create_contact_detail_sheet" in ui
    assert "create_contact_options_sheet" in ui
    assert "create_contact_forget_sheet" in ui
    assert "create_contact_edit_sheet" in ui
    assert "render_contact_detail_sheet" in ui
    assert "render_contact_options_sheet" in ui
    assert "render_contact_forget_sheet" in ui
    assert "open_contact_edit_event_cb" in ui
    assert "d1l_app_model_rename_contact(s_contact_detail_contact.fingerprint" in ui
    assert "create_contact_export_sheet" in ui
    assert "render_contact_export_sheet" in ui
    assert "lv_qrcode_create" in ui
    assert "lv_qrcode_update" in ui
    assert "update_contact_detail_flags" in ui
    assert "s_contact_action_favorite" in ui
    assert "s_contact_action_mute" in ui

    delete_call = "d1l_app_model_delete_contact(s_contact_detail_contact.fingerprint"
    confirm = static_void_body(ui, "confirm_forget_contact_event_cb")
    assert ui.count(delete_call) == 1
    assert delete_call in confirm
    assert "forget_contact_edit_event_cb" not in ui
    for symbol in (
        "cancel_contact_forget_event_cb",
        "close_contact_options_event_cb",
        "close_contact_edit_event_cb",
        "save_contact_edit_event_cb",
        "close_contact_export_event_cb",
        "close_route_trace_event_cb",
    ):
        callback = static_void_body(ui, symbol)
        assert delete_call not in callback, symbol
    for symbol in (
        "cancel_contact_forget_event_cb",
        "close_contact_edit_event_cb",
        "save_contact_edit_event_cb",
        "close_contact_export_event_cb",
        "close_route_trace_event_cb",
    ):
        assert "show_contact_options_sheet();" in static_void_body(ui, symbol), symbol

    assert '"Contacts"' in ui
    assert 'ok_begin("contacts")' in console
    assert '\\"out_path_known\\"' in console
    assert '\\"out_path_len\\"' in console
    assert '\\"meshcore_uri\\"' in console
    assert "Contacts are promoted from heard nodes into a bounded NVS store" in console
    assert "contacts" in SMOKE_COMMANDS
    assert "contacts export" in SMOKE_COMMANDS
