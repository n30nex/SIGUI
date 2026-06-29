from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_app_model_exposes_bounded_ui_snapshot():
    header = read("main/app/app_model.h")
    source = read("main/app/app_model.c")
    assert "D1L_APP_SNAPSHOT_PACKET_PREVIEW 4U" in header
    assert "D1L_APP_SNAPSHOT_NODE_PREVIEW 4U" in header
    assert "D1L_APP_SNAPSHOT_CONTACT_PREVIEW 2U" in header
    assert "D1L_APP_SNAPSHOT_ROUTE_PREVIEW 2U" in header
    assert "d1l_app_snapshot_t" in header
    assert "d1l_app_model_snapshot" in header
    assert "static d1l_app_snapshot_t s_snapshot" in read("main/ui/ui_phase1.c")
    assert "d1l_contact_store_copy_recent" in source
    assert "d1l_route_store_copy_recent" in source
    assert "d1l_node_store_copy_recent" in source
    assert "d1l_packet_log_copy_recent" in source
    assert "d1l_meshcore_service_status" in source
    assert "d1l_health_snapshot" in source


def test_phase3_shell_replaces_diagnostic_tile_home():
    source = read("main/ui/ui_phase1.c")
    assert "D1L_UI_TAB_HOME" in source
    assert "D1L_UI_TAB_MESSAGES" in source
    assert "D1L_UI_TAB_NODES" in source
    assert "D1L_UI_TAB_PACKETS" in source
    assert "D1L_UI_TAB_SETTINGS" in source
    assert "create_top_bar" in source
    assert "create_dock" in source
    assert "create_sheet" in source
    assert "create_toast" in source
    assert "create_lock_overlay" in source
    assert "Phase 1 hardware bring-up" not in source


def test_touch_ui_actions_route_through_app_model():
    source = read("main/ui/ui_phase1.c")
    header = read("main/app/app_model.h")
    model = read("main/app/app_model.c")
    assert "d1l_app_model_send_public_test()" in source
    assert "d1l_app_model_send_public_text(text)" in source
    assert "d1l_app_model_request_advert(false)" in source
    assert "d1l_app_model_request_advert(true)" in source
    assert "d1l_app_model_send_public_text" in header
    assert 'd1l_meshcore_service_send_public("test")' in model
    assert "d1l_meshcore_service_send_public(text)" in model
    assert "d1l_meshcore_service_request_advert(flood)" in model


def test_public_composer_uses_lvgl_textarea_keyboard():
    source = read("main/ui/ui_phase1.c")
    assert "create_compose_sheet" in source
    assert "open_compose_event_cb" in source
    assert "send_compose_text" in source
    assert "lv_textarea_create" in source
    assert "lv_textarea_set_max_length" in source
    assert "lv_textarea_get_text" in source
    assert "lv_keyboard_create" in source
    assert "lv_keyboard_set_textarea" in source
    assert "LV_EVENT_READY" in source
    assert "LV_EVENT_CANCEL" in source
    assert "hide_compose_sheet()" in source
    assert '"Test"' in source


def test_nodes_screen_renders_heard_node_rows():
    source = read("main/ui/ui_phase1.c")
    assert "render_node_row" in source
    assert "render_contact_row" in source
    assert "recent_node_count" in source
    assert "recent_nodes" in source
    assert "recent_contact_count" in source
    assert "recent_contacts" in source
    assert "No heard nodes yet" in source
    assert "node_total_written" in source
    assert "contact_total_written" in source
    assert "route_count" in source
