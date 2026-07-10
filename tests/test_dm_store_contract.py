from pathlib import Path

from scripts.smoke_d1l import SMOKE_COMMANDS


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_dm_store_is_bounded_and_retained_blob_store_backed():
    header = read("main/mesh/dm_store.h")
    source = read("main/mesh/dm_store.c")
    blob_store = read("main/storage/retained_blob_store.c")
    cmake = read("main/CMakeLists.txt")
    app_main = read("main/app_main.c")
    assert "D1L_DM_STORE_CAPACITY 16U" in header
    assert "contact_fingerprint" in header
    assert "ack_hash" in header
    assert "d1l_dm_store_mark_acked" in header
    assert "d1l_dm_store_copy_recent_page" in header
    assert "d1l_dm_store_copy_thread_page" in header
    assert "d1l_dm_store_copy_thread" in header
    assert 'D1L_RETAINED_DM_MESSAGE_NAMESPACE "d1l_dms"' in blob_store
    assert 'D1L_RETAINED_DM_MESSAGE_SD_DIR "stores/messages/dm"' in blob_store
    assert "D1L_DM_STORE_ID D1L_RETAINED_BLOB_STORE_DM_MESSAGES" in source
    assert "D1L_DM_STORE_SCHEMA 2U" in source
    assert "D1L_DM_STORE_SCHEMA_V1 1U" in source
    assert "load_v1_blob_into_ram" in source
    assert 'D1L_DM_STORE_KEY "threads"' in source
    assert '#include "storage/retained_blob_store.h"' in source
    assert "d1l_retained_blob_store_read(D1L_DM_STORE_ID" in source
    assert "d1l_retained_blob_store_read_fallback(D1L_DM_STORE_ID" in source
    assert "d1l_retained_blob_store_write(D1L_DM_STORE_ID" in source
    assert "d1l_retained_blob_store_erase(D1L_DM_STORE_ID" in source
    assert "d1l_retained_blob_store_uses_sd(D1L_DM_STORE_ID)" in source
    assert "nvs_get_blob" not in source
    assert "nvs_set_blob" not in source
    assert "entry->acked = true" in source
    assert "skip_newest" in source
    assert "out_total_matches" in source
    assert "d1l_dm_store_copy_thread" in source
    assert "strncmp(entry->contact_fingerprint, contact_fingerprint" in source
    assert "static d1l_dm_store_blob_t s_blob_scratch" in source
    assert '"mesh/dm_store.c"' in cmake
    assert "d1l_dm_store_init()" in app_main


def test_meshcore_service_builds_private_text_packets_from_contacts():
    source = read("main/mesh/meshcore_service.c")
    cmake = read("main/CMakeLists.txt")
    assert "D1L_MESHCORE_PAYLOAD_TEXT 0x02U" in source
    assert "D1L_MESHCORE_PAYLOAD_ACK 0x03U" in source
    assert "D1L_MESHCORE_PAYLOAD_PATH 0x08U" in source
    assert "D1L_MESHCORE_PAYLOAD_MULTIPART 0x0AU" in source
    assert "D1L_MESHCORE_HEADER_DM_TEXT_FLOOD" in source
    assert "D1L_MESHCORE_HEADER_DM_TEXT_DIRECT" in source
    assert "ed25519_key_exchange(secret, dest_pub, settings->identity_private_key)" in source
    assert "ed25519_key_exchange(secret, sender_pub, settings->identity_private_key)" in source
    assert "build_dm_text_packet" in source
    assert "calc_dm_ack_hash" in source
    assert "parse_rx_ack_packet" in source
    assert "parse_rx_path_packet" in source
    assert "record_dm_ack" in source
    assert "d1l_contact_store_find_by_fingerprint" in source
    assert "d1l_contact_store_update_path" in source
    assert "d1l_dm_store_append" in source
    assert "d1l_dm_store_mark_acked" in source
    assert "contact.out_path_valid" in source
    assert 'append_packet_log("tx", "dm_text"' in source
    assert 'append_packet_log("rx", "dm_text"' in source
    assert 'append_packet_log("rx", "dm_ack"' in source
    assert 'append_packet_log("rx", "path_return"' in source
    assert "../third_party/MeshCore/lib/ed25519/key_exchange.c" in cmake


def test_meshcore_service_retains_dm_when_queued_not_only_tx_done():
    source = read("main/mesh/meshcore_service.c")
    send_start = source.index("esp_err_t d1l_meshcore_service_send_dm")
    send_body = source[send_start:source.index("const char *d1l_meshcore_service_state_name", send_start)]

    remember_at = send_body.index("remember_pending_dm_tx(&contact, text")
    service_send_at = send_body.index("meshcore_service_send_raw(raw, raw_len")
    append_at = send_body.index("append_dm_store_tx(&s_pending_dm_tx)")
    clear_at = send_body.index("clear_pending_dm_tx()", append_at)

    assert remember_at < service_send_at < append_at < clear_at
    assert "(void)append_dm_store_tx(&s_pending_dm_tx);\n    clear_pending_dm_tx();" in send_body
    assert "if (append_dm_store_tx(&s_pending_dm_tx))" not in send_body
    assert "Radio.Send(raw, raw_len)" not in send_body
    assert "static bool append_dm_store_tx" in source
    assert "static void clear_pending_dm_tx" in source


def test_console_and_smoke_expose_dm_workflow():
    console = read("main/comms/usb_console.c")
    assert 'ok_begin("messages dm")' in console
    assert 'strcmp(line, "messages dm")' in console
    assert 'strncmp(line, "messages dm ", 12)' in console
    assert "d1l_dm_store_copy_recent_page(entries, D1L_CONSOLE_MESSAGE_PAGE_SIZE" in console
    assert "d1l_dm_store_copy_thread_page(thread_fingerprint, entries" in console
    assert 'strcmp(line, "messages dm clear")' in console
    assert 'strncmp(line, "mesh send dm ", 13)' in console
    assert "d1l_meshcore_service_send_dm(fingerprint, text)" in console
    assert "messages dm [offset <n>]" in console
    assert "messages dm <fingerprint> [offset <n>]" in console
    assert '\\"page_size\\"' in console
    assert '\\"total_matches\\"' in console
    assert '\\"has_older\\"' in console
    assert '\\"next_offset\\"' in console
    assert "optional fingerprint filters one retained thread and offset pages older rows" in console
    assert "MeshCore direct-message rows are kept in bounded retained storage" in console
    assert "MeshCore direct-message rows are kept in a bounded NVS store" not in console
    assert "messages dm" in SMOKE_COMMANDS


def test_app_model_and_ui_preview_recent_dms():
    header = read("main/app/app_model.h")
    source = read("main/app/app_model.c")
    ui = read("main/ui/ui_phase1.c")
    thread_render = ui.split("static void render_dm_thread_sheet(void)", 1)[1].split(
        "static void show_dm_thread_for", 1
    )[0]
    thread_open = ui.split("static void show_dm_thread_for", 1)[1].split(
        "static void open_home_dm_preview_event_cb", 1
    )[0]
    assert "D1L_APP_SNAPSHOT_DM_PREVIEW 5U" in header
    assert "recent_dms" in header
    assert "dm_total_written" in header
    assert "d1l_dm_store_copy_recent" in source
    assert "d1l_app_model_copy_dm_thread_page" in header
    assert "d1l_app_model_copy_dm_thread" in header
    assert "d1l_dm_store_copy_thread_page(fingerprint, out_entries" in source
    assert "d1l_read_state_dm_entry_is_unread(&out_entries[i])" in source
    assert "d1l_app_model_send_dm_text" in source
    assert "render_dm_row" in ui
    assert "d1l_app_model_copy_dm_thread_page(s_dm_thread_fingerprint" in ui
    assert "s_dm_thread_entries[D1L_DM_STORE_CAPACITY]" in ui
    assert "dm_thread_load_older_event_cb" in ui
    assert 'create_button(body, "Load Older", 0, 0, 424, 48' in thread_render
    assert "lv_obj_scroll_to_y(body, LV_COORD_MAX, LV_ANIM_OFF)" in thread_render
    assert "d1l_app_model_mark_dm_thread_read(fingerprint)" in thread_open
    assert thread_open.index("d1l_app_model_mark_dm_thread_read(fingerprint)") < thread_open.index(
        "render_dm_thread_sheet();"
    )
    assert 'create_button(s_dm_thread_sheet, "Read"' not in thread_render
    assert "read_dm_thread_event_cb" not in ui
    assert '"DM"' in ui
    assert "No direct messages" in ui
