from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_nodes_root_has_owned_bounded_view_model_and_action_lifetime():
    cmake = read("main/CMakeLists.txt")
    phase1 = read("main/ui/ui_phase1.c")
    header = read("main/ui/ui_nodes.h")
    source = read("main/ui/ui_nodes.c")

    assert '"ui/ui_nodes.c"' in cmake
    assert '#include "ui_nodes.h"' in phase1
    assert "d1l_ui_nodes_view_model_t" in header
    assert "d1l_ui_nodes_action_event_t" in header
    assert "d1l_ui_nodes_controller_t" in header
    assert "d1l_ui_nodes_render" in header
    assert "d1l_ui_nodes_deactivate" in header
    assert "d1l_contact_entry_t contact_rows[D1L_APP_SNAPSHOT_CONTACT_PREVIEW]" in header
    assert "d1l_node_view_t node_rows[D1L_NODE_STORE_CAPACITY]" in header
    assert "bool contact_can_dm[D1L_APP_SNAPSHOT_CONTACT_PREVIEW]" in header
    assert "bool node_can_dm[D1L_NODE_STORE_CAPACITY]" in header

    assert "view_model != &controller->rendered" in source
    assert "controller->rendered = *view_model;" in source
    assert "contact_row_count > D1L_APP_SNAPSHOT_CONTACT_PREVIEW" in source
    assert "node_row_count > D1L_NODE_STORE_CAPACITY" in source
    assert "binding->row_index >= controller->rendered.contact_row_count" in source
    assert "binding->row_index >= controller->rendered.node_row_count" in source
    assert "!controller->rendered.contact_can_dm[binding->row_index]" in source
    assert "!controller->rendered.node_can_dm[binding->row_index]" in source
    assert "&controller->rendered.contact_rows[binding->row_index]" in source
    assert "&controller->rendered.node_rows[binding->row_index]" in source
    assert "memset(&controller->rendered, 0, sizeof(controller->rendered));" in source
    assert "controller->action_handler = NULL;" in source

    assert "nodes_view_model_from_snapshot(snapshot, &s_nodes_controller.rendered);" in phase1
    assert "snapshot->recent_contact_count" in phase1
    assert "D1L_APP_SNAPSHOT_CONTACT_PREVIEW" in phase1
    assert "d1l_app_model_query_nodes(" in phase1
    assert "D1L_NODE_STORE_CAPACITY" in phase1
    assert "contact_can_dm(&view_model->contact_rows[i])" in phase1
    assert "node_view_can_dm(&view_model->node_rows[i])" in phase1
    assert "handle_nodes_action" in phase1
    assert "show_contact_detail_for(event->contact);" in phase1
    assert "open_dm_compose_for_contact(event->contact);" in phase1
    assert "show_node_detail_view(event->node, false);" in phase1
    assert "open_node_dm_for(event->node);" in phase1
    assert "d1l_ui_nodes_deactivate(&s_nodes_controller);" in phase1
    assert "static d1l_ui_nodes_controller_t s_nodes_controller EXT_RAM_BSS_ATTR;" in phase1
    assert "static d1l_node_view_t s_map_node_rows[D1L_NODE_STORE_CAPACITY] " in phase1


def test_nodes_root_module_preserves_rows_roles_empty_state_and_boundaries():
    source = read("main/ui/ui_nodes.c")

    for label in (
        '"Heard Nodes"',
        '"Contacts"',
        '"Heard"',
        '"All Heard"',
        '"No heard nodes yet"',
        '"DM"',
        '"ROOM"',
        '"RPT"',
        '"SNS"',
        '"CMP"',
    ):
        assert label in source
    assert "nodes_render_contact_row" in source
    assert "nodes_render_node_row" in source
    assert "static const char *nodes_role_badge_text" in source
    assert "static uint32_t nodes_role_color" in source
    assert 'entry->public_key_hex[0] ? "key" : "no key"' in source
    assert 'entry->out_path_valid ? "path" : "flood"' in source
    assert 'view->reachable ? "reachable" : "quiet"' in source
    assert "D1L_UI_NODES_ACTION_OPEN_CONTACT_DM" in source
    assert "D1L_UI_NODES_ACTION_OPEN_NODE_DM" in source

    assert "static lv_obj_t *s_" not in source
    assert "lv_timer_" not in source
    assert "d1l_app_model_" not in source
    assert "d1l_meshcore_service_" not in source
    assert "d1l_storage_" not in source
    assert "freertos/" not in source
