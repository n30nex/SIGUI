from pathlib import Path

from scripts.smoke_d1l import SMOKE_COMMANDS


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_public_message_store_is_bounded_and_retained_blob_store_backed():
    header = read("main/mesh/message_store.h")
    source = read("main/mesh/message_store.c")
    blob_store = read("main/storage/retained_blob_store.c")
    cmake = read("main/CMakeLists.txt")
    assert "D1L_MESSAGE_STORE_CAPACITY 16U" in header
    assert "D1L_MESSAGE_MAX_CHARS 138U" in header
    assert "D1L_MESSAGE_TEXT_LEN (D1L_MESSAGE_MAX_CHARS + 1U)" in header
    assert "D1L_MESSAGE_STORE_SCHEMA 2U" in source
    assert "D1L_MESSAGE_STORE_SCHEMA_V1 1U" in source
    assert "load_v1_blob_into_ram" in source
    assert 'D1L_RETAINED_PUBLIC_MESSAGE_NAMESPACE "d1l_messages"' in blob_store
    assert 'D1L_RETAINED_PUBLIC_MESSAGE_SD_DIR "stores/messages/public"' in blob_store
    assert "D1L_MESSAGE_STORE_ID D1L_RETAINED_BLOB_STORE_PUBLIC_MESSAGES" in source
    assert '#include "storage/retained_blob_store.h"' in source
    assert "d1l_retained_blob_store_read(D1L_MESSAGE_STORE_ID" in source
    assert "d1l_retained_blob_store_read_fallback(D1L_MESSAGE_STORE_ID" in source
    assert "d1l_retained_blob_store_write(D1L_MESSAGE_STORE_ID" in source
    assert "d1l_retained_blob_store_erase(D1L_MESSAGE_STORE_ID" in source
    assert "d1l_retained_blob_store_uses_sd(D1L_MESSAGE_STORE_ID)" in source
    assert "nvs_get_blob" not in source
    assert "nvs_set_blob" not in source
    assert "static d1l_message_store_blob_t s_blob_scratch" in source
    assert "d1l_message_store_copy_recent" in source
    assert "d1l_message_store_query_page" in header
    assert "d1l_message_store_query" in header
    assert "skip_newest" in source
    assert "out_total_matches" in source
    assert "contains_casefold" in source
    assert "message_matches_query" in source
    assert '"mesh/message_store.c"' in cmake


def test_public_rf_appends_to_message_store():
    source = read("main/mesh/meshcore_service.c")
    assert 'd1l_message_store_append_public("tx"' in source
    assert 'd1l_message_store_append_public("rx"' in source
    assert "remember_pending_public_tx(text)" in source
    assert "flush_pending_public_tx()" in source
    assert "append_public_message_store_rx(message" in source


def test_ui_and_console_expose_persistent_public_messages():
    app_header = read("main/app/app_model.h")
    app_source = read("main/app/app_model.c")
    ui = read("main/ui/ui_phase1.c")
    console = read("main/comms/usb_console.c")
    assert "D1L_APP_SNAPSHOT_MESSAGE_PREVIEW 5U" in app_header
    assert "recent_messages" in app_header
    assert "d1l_app_model_query_public_messages_page" in app_header
    assert "d1l_app_model_query_public_messages" in app_header
    assert "d1l_message_store_copy_recent" in app_source
    assert "d1l_message_store_query_page(out_entries, max_entries, skip_newest" in app_source
    assert "render_message_row" in ui
    assert "render_message_detail_sheet" in ui
    assert "open_message_detail_event_cb" in ui
    assert "s_message_detail_message = *entry" in ui
    assert "lv_textarea_set_max_length(s_compose_textarea, D1L_MESSAGE_MAX_CHARS)" in ui
    assert "static lv_obj_t *s_compose_counter" in ui
    assert "update_compose_counter()" in ui
    assert "LV_EVENT_VALUE_CHANGED" in ui
    assert "No Public messages" in ui
    assert "No direct messages" in ui
    assert "static lv_obj_t *s_public_history_sheet" in ui
    assert "static lv_obj_t *s_public_search_sheet" in ui
    assert "d1l_app_model_query_public_messages_page(s_public_history_entries" in ui
    assert "public_history_load_older_event_cb" in ui
    assert 'create_button(header, "History"' in ui
    assert "create_public_history_sheet" in ui
    assert "create_public_search_sheet" in ui
    assert 'lv_textarea_set_placeholder_text(s_public_search_textarea, "Search author or message")' in ui
    assert 'ok_begin("messages public")' in console
    assert 'strcmp(line, "messages public")' in console
    assert 'strncmp(args, "search ", 7)' in console
    assert "messages public [offset <n>]" in console
    assert "messages public search <text> [offset <n>]" in console
    assert '\\"page_size\\"' in console
    assert '\\"total_matches\\"' in console
    assert '\\"has_older\\"' in console
    assert '\\"next_offset\\"' in console
    assert "optional search filters retained rows and offset pages older rows" in console
    assert "Public messages are kept in bounded retained storage" in console
    assert "Public messages are kept in a bounded NVS store" not in console
    assert "d1l_message_store_query_page(entries" in console
    assert "D1L_CONSOLE_MESSAGE_PAGE_SIZE" in console
    assert "messages public" in SMOKE_COMMANDS
    assert "messages public search test" in SMOKE_COMMANDS
