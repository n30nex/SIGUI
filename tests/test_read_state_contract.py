from pathlib import Path

from scripts.smoke_d1l import SMOKE_COMMANDS


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_read_state_tracks_bounded_per_thread_dm_cursors():
    header = read("main/mesh/read_state.h")
    source = read("main/mesh/read_state.c")
    cmake = read("main/CMakeLists.txt")
    app_main = read("main/app_main.c")
    app_header = read("main/app/app_model.h")
    app_source = read("main/app/app_model.c")

    assert "d1l_read_state_stats_t" in header
    assert "public_unread_count" in header
    assert "muted_dm_unread_count" in header
    assert "D1L_READ_STATE_DM_THREAD_CAPACITY 16U" in header
    assert "d1l_read_state_dm_thread_t" in header
    assert "d1l_read_state_mark_dm_thread_read" in header
    assert "d1l_read_state_dm_entry_is_unread" in header
    assert "d1l_read_state_copy_dm_threads" in header

    assert 'D1L_READ_STATE_NAMESPACE "d1l_read"' in source
    assert 'D1L_READ_STATE_KEY "state"' in source
    assert "nvs_get_blob" in source
    assert "nvs_set_blob" in source
    assert "static d1l_message_entry_t s_message_scratch" in source
    assert "static d1l_dm_entry_t s_dm_scratch" in source
    assert "d1l_contact_store_find_by_fingerprint" in source
    assert "contact.muted" in source
    assert "D1L_READ_STATE_SCHEMA 2U" in source
    assert "D1L_READ_STATE_SCHEMA_V1 1U" in source
    assert "dm_cursors[D1L_READ_STATE_DM_THREAD_CAPACITY]" in source
    assert "blob_v1_is_valid" in source
    assert "thread_seq > s_state.last_dm_read_seq ? thread_seq : s_state.last_dm_read_seq" in source
    assert "build_dm_thread_stats" in source
    assert "upsert_dm_cursor(fingerprint, newest_rx_seq)" in source
    assert '"mesh/read_state.c"' in cmake
    assert "d1l_read_state_init()" in app_main

    assert "public_unread_count" in app_header
    assert "dm_unread_count" in app_header
    assert "muted_dm_unread_count" in app_header
    assert "last_public_read_seq" in app_header
    assert "last_dm_read_seq" in app_header
    assert "recent_dm_unread[D1L_APP_SNAPSHOT_DM_PREVIEW]" in app_header
    assert "d1l_app_model_mark_messages_read" in app_header
    assert "d1l_app_model_mark_dm_thread_read" in app_header
    assert "d1l_read_state_stats()" in app_source
    assert "d1l_read_state_mark_all_read()" in app_source
    assert "d1l_read_state_dm_entry_is_unread(&snapshot->recent_dms[i])" in app_source
    assert "d1l_read_state_mark_dm_thread_read(fingerprint)" in app_source


def test_console_and_ui_expose_thread_read_controls():
    console = read("main/comms/usb_console.c")
    ui = read("main/ui/ui_phase1.c")
    simulator = read("tools/ui_simulator.py")

    assert 'ok_begin("messages unread")' in console
    assert 'strncmp(line, "messages read ", 14)' in console
    assert "d1l_read_state_mark_public_read()" in console
    assert "d1l_read_state_mark_dm_read()" in console
    assert "d1l_read_state_mark_all_read()" in console
    assert '"messages unread"' in console
    assert "messages read <public|dm|dm <fingerprint>|all>" in console
    assert "d1l_read_state_mark_dm_thread_read(thread_fingerprint)" in console
    assert "d1l_read_state_copy_dm_threads(threads, 8)" in console
    assert "dm_threads" in console
    assert "dm_thread_count" in console
    assert "messages unread" in SMOKE_COMMANDS

    assert "mark_messages_read_event_cb" in ui
    assert 'create_button(header, "Read"' in ui
    assert 'unread ? "new"' in ui
    assert "snapshot->muted_dm_unread_count" in ui
    assert "read_dm_thread_event_cb" in ui
    assert "d1l_app_model_mark_dm_thread_read(s_dm_thread_fingerprint)" in ui
    assert 'create_button(s_dm_thread_sheet, "Read"' in ui
    assert "render_dm_row(s_content, y, &snapshot->recent_dms[i], snapshot->recent_dm_unread[i])" in ui
    assert "dm_row_state(entry, unread)" in ui

    assert 'draw_button(s, (174, 304, 290, 356), "Read", ACCENT)' in simulator
