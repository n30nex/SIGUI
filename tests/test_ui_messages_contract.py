from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_messages_root_has_owned_view_model_action_and_lifetime_boundary():
    cmake = read("main/CMakeLists.txt")
    phase1 = read("main/ui/ui_phase1.c")
    header = read("main/ui/ui_messages.h")
    source = read("main/ui/ui_messages.c")

    assert '"ui/ui_messages.c"' in cmake
    assert '#include "ui_messages.h"' in phase1
    assert "d1l_ui_messages_view_model_t" in header
    assert "d1l_ui_messages_action_event_t" in header
    assert "d1l_ui_messages_controller_t" in header
    assert "d1l_ui_messages_render" in header
    assert "d1l_ui_messages_deactivate" in header
    assert '#include "app/app_model.h"' not in header
    assert '#include "mesh/dm_store.h"' in header
    assert '#include "mesh/message_store.h"' in header

    assert "controller->rendered = *view_model;" in source
    assert "binding->row_index >= controller->rendered.public_row_count" in source
    assert "binding->row_index >= controller->rendered.dm_row_count" in source
    assert "&controller->rendered.public_rows[binding->row_index]" in source
    assert "&controller->rendered.dm_rows[binding->row_index]" in source
    assert "memset(&controller->rendered, 0, sizeof(controller->rendered));" in source
    assert "controller->action_handler = NULL;" in source
    assert "static lv_obj_t *s_" not in source
    assert "lv_timer_" not in source
    assert "d1l_app_model_" not in source
    assert "d1l_meshcore_service_" not in source
    assert "d1l_storage_" not in source
    assert "freertos/" not in source

    assert "messages_view_model_from_snapshot(snapshot, &s_messages_controller.rendered);" in phase1
    assert "snapshot->recent_message_count" in phase1
    assert "snapshot->recent_dm_count" in phase1
    assert "D1L_UI_MESSAGES_PUBLIC_PREVIEW_ROWS" in phase1
    assert "D1L_UI_MESSAGES_DM_PREVIEW_ROWS" in phase1
    assert "handle_messages_action" in phase1
    assert "show_message_detail_for(event->public_message);" in phase1
    assert "show_dm_thread_for(event->dm_message->contact_fingerprint" in phase1
    assert "d1l_ui_messages_deactivate(&s_messages_controller);" in phase1
