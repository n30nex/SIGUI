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


def test_console_and_smoke_expose_dm_workflow():
    console = read("main/comms/usb_console.c")
    assert 'ok_begin("messages dm")' in console
    assert 'strcmp(line, "messages dm")' in console
    assert 'strncmp(line, "messages dm ", 12)' in console
    assert "d1l_dm_store_copy_thread(thread_fingerprint, entries, D1L_DM_STORE_CAPACITY)" in console
    assert 'strcmp(line, "messages dm clear")' in console
    assert 'strncmp(line, "mesh send dm ", 13)' in console
    assert "d1l_meshcore_service_send_dm(fingerprint, text)" in console
    assert "messages dm [fingerprint]" in console
    assert "optional fingerprint filters one retained thread" in console
    assert "MeshCore direct-message rows are kept in bounded retained storage" in console
    assert "MeshCore direct-message rows are kept in a bounded NVS store" not in console
    assert "messages dm" in SMOKE_COMMANDS


def test_app_model_and_ui_preview_recent_dms():
    header = read("main/app/app_model.h")
    source = read("main/app/app_model.c")
    ui = read("main/ui/ui_phase1.c")
    assert "D1L_APP_SNAPSHOT_DM_PREVIEW 5U" in header
    assert "recent_dms" in header
    assert "dm_total_written" in header
    assert "d1l_dm_store_copy_recent" in source
    assert "d1l_app_model_copy_dm_thread" in header
    assert "d1l_dm_store_copy_thread(fingerprint, out_entries, max_entries)" in source
    assert "d1l_read_state_dm_entry_is_unread(&out_entries[i])" in source
    assert "d1l_app_model_send_dm_text" in source
    assert "render_dm_row" in ui
    assert "d1l_app_model_copy_dm_thread(s_dm_thread_fingerprint" in ui
    assert "s_dm_thread_entries[D1L_DM_STORE_CAPACITY]" in ui
    assert "lv_obj_scroll_to_y(list, LV_COORD_MAX, LV_ANIM_OFF)" in ui
    assert '"DM"' in ui
    assert "No stored messages" in ui
