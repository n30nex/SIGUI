from pathlib import Path

from scripts.smoke_d1l import SMOKE_COMMANDS


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


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
    assert "D1L_CONTACT_STORE_SCHEMA 3U" in source
    assert "d1l_contact_store_blob_v2_t" in source
    assert "d1l_contact_store_blob_v1_t" in source
    assert "migrate_v1_blob" in source
    assert "migrate_v2_blob" in source
    assert 'D1L_CONTACT_STORE_NAMESPACE "d1l_contacts"' in source
    assert 'D1L_CONTACT_STORE_KEY "contacts"' in source
    assert "nvs_get_blob" in source
    assert "nvs_set_blob" in source
    assert "static d1l_contact_store_blob_t s_blob_scratch" in source
    assert "find_index_by_fingerprint" in source
    assert "oldest_index" in source
    assert "d1l_contact_store_update_path" in source
    assert "d1l_contact_store_set_flags" in source
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
    assert 'strncmp(line, "contacts set ", 13)' in console
    assert "d1l_contact_store_set_flags(fingerprint, contact.favorite, contact.muted, &contact)" in console
    assert 'contacts set <fingerprint> <favorite|mute> <0|1>' in console


def test_ui_console_and_smoke_expose_contacts():
    app_header = read("main/app/app_model.h")
    app_source = read("main/app/app_model.c")
    ui = read("main/ui/ui_phase1.c")
    console = read("main/comms/usb_console.c")
    assert "recent_contacts" in app_header
    assert "contact_total_written" in app_header
    assert "d1l_contact_store_copy_recent" in app_source
    assert "d1l_app_model_set_contact_flags" in app_header
    assert "d1l_app_model_export_contact_uri" in app_header
    assert "d1l_contact_store_export_uri(&contact, dest, dest_size)" in app_source
    assert "d1l_contact_store_set_flags(fingerprint, favorite, muted, out_contact)" in app_source
    assert "render_contact_row" in ui
    assert 'entry->public_key_hex[0] ? "key" : "no key"' in ui
    assert "create_contact_detail_sheet" in ui
    assert "render_contact_detail_sheet" in ui
    assert "create_contact_export_sheet" in ui
    assert "render_contact_export_sheet" in ui
    assert "lv_qrcode_create" in ui
    assert "lv_qrcode_update" in ui
    assert "update_contact_detail_flags" in ui
    assert "s_contact_action_favorite" in ui
    assert "s_contact_action_mute" in ui
    assert '"Contacts"' in ui
    assert 'ok_begin("contacts")' in console
    assert '\\"out_path_known\\"' in console
    assert '\\"out_path_len\\"' in console
    assert '\\"meshcore_uri\\"' in console
    assert "Contacts are promoted from heard nodes into a bounded NVS store" in console
    assert "contacts" in SMOKE_COMMANDS
    assert "contacts export" in SMOKE_COMMANDS
