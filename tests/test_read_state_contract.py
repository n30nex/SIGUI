from pathlib import Path

from scripts.smoke_d1l import SMOKE_COMMANDS


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_read_state_is_persisted_and_counts_muted_dm_separately():
    header = read("main/mesh/read_state.h")
    source = read("main/mesh/read_state.c")
    cmake = read("main/CMakeLists.txt")
    app_main = read("main/app_main.c")
    assert "d1l_read_state_stats_t" in header
    assert "public_unread_count" in header
    assert "muted_dm_unread_count" in header
    assert 'D1L_READ_STATE_NAMESPACE "d1l_read"' in source
    assert 'D1L_READ_STATE_KEY "state"' in source
    assert "nvs_get_blob" in source
    assert "nvs_set_blob" in source
    assert "static d1l_message_entry_t s_message_scratch" in source
    assert "static d1l_dm_entry_t s_dm_scratch" in source
    assert "d1l_contact_store_find_by_fingerprint" in source
    assert "contact.muted" in source
    assert '"mesh/read_state.c"' in cmake
    assert "d1l_read_state_init()" in app_main


def test_console_and_smoke_expose_unread_state():
    console = read("main/comms/usb_console.c")
    assert 'ok_begin("messages unread")' in console
    assert 'strncmp(line, "messages read ", 14)' in console
    assert "d1l_read_state_mark_public_read()" in console
    assert "d1l_read_state_mark_dm_read()" in console
    assert "d1l_read_state_mark_all_read()" in console
    assert '"messages unread"' in console
    assert "messages read <public|dm|all>" in console
    assert "messages unread" in SMOKE_COMMANDS


def test_app_snapshot_and_ui_show_unread_badges():
    header = read("main/app/app_model.h")
    source = read("main/app/app_model.c")
    ui = read("main/ui/ui_phase1.c")
    assert "public_unread_count" in header
    assert "dm_unread_count" in header
    assert "muted_dm_unread_count" in header
    assert "last_public_read_seq" in header
    assert "last_dm_read_seq" in header
    assert "d1l_read_state_stats()" in source
    assert "d1l_app_model_mark_messages_read" in header
    assert "d1l_read_state_mark_all_read()" in source
    assert "mark_messages_read_event_cb" in ui
    assert 'create_button(header, "Read"' in ui
    assert 'unread ? "new"' in ui
    assert "snapshot->muted_dm_unread_count" in ui
