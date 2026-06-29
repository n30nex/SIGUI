from pathlib import Path

from scripts.smoke_d1l import SMOKE_COMMANDS


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_public_message_store_is_bounded_and_nvs_backed():
    header = read("main/mesh/message_store.h")
    source = read("main/mesh/message_store.c")
    cmake = read("main/CMakeLists.txt")
    assert "D1L_MESSAGE_STORE_CAPACITY 16U" in header
    assert "D1L_MESSAGE_TEXT_LEN 96U" in header
    assert 'D1L_MESSAGE_STORE_NAMESPACE "d1l_messages"' in source
    assert "nvs_get_blob" in source
    assert "nvs_set_blob" in source
    assert "static d1l_message_store_blob_t s_blob_scratch" in source
    assert "d1l_message_store_copy_recent" in source
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
    assert "D1L_APP_SNAPSHOT_MESSAGE_PREVIEW 4U" in app_header
    assert "recent_messages" in app_header
    assert "d1l_message_store_copy_recent" in app_source
    assert "render_message_row" in ui
    assert "No stored messages" in ui
    assert 'ok_begin("messages public")' in console
    assert 'strcmp(line, "messages public")' in console
    assert "messages public" in SMOKE_COMMANDS
