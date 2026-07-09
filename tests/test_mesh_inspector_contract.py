from pathlib import Path

from scripts.smoke_d1l import SMOKE_COMMANDS


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_mesh_inspector_is_bounded_and_read_only():
    header = read("main/mesh/mesh_inspector.h")
    source = read("main/mesh/mesh_inspector.c")
    cmake = read("main/CMakeLists.txt")
    assert "D1L_MESH_INSPECTOR_LABEL_LEN 24U" in header
    assert "D1L_REPEATER_PREVIEW_CAPACITY 8U" in header
    assert "D1L_ROOM_SERVER_PREVIEW_CAPACITY 8U" in header
    assert "d1l_mesh_signal_summary_t" in header
    assert "d1l_mesh_room_server_t" in header
    assert "d1l_mesh_repeater_candidate_t" in header
    assert "d1l_mesh_inspector_signal_summary" in header
    assert "static d1l_packet_log_entry_t s_packet_scratch[8]" in source
    assert "static d1l_route_entry_t s_route_scratch[D1L_ROUTE_STORE_CAPACITY]" in source
    assert "static d1l_node_entry_t s_node_scratch[D1L_NODE_STORE_CAPACITY]" in source
    assert "d1l_packet_log_copy_recent(s_packet_scratch, 8)" in source
    assert "d1l_route_store_copy_recent(s_route_scratch, D1L_ROUTE_STORE_CAPACITY)" in source
    assert "d1l_node_store_copy_recent(s_node_scratch, D1L_NODE_STORE_CAPACITY)" in source
    assert 'strcmp(node->type, "room") == 0' in source
    assert 'strcmp(route->target, "public") != 0' in source
    assert "nvs_" not in source
    assert '"mesh/mesh_inspector.c"' in cmake


def test_console_exposes_signal_rooms_and_repeaters():
    console = read("main/comms/usb_console.c")
    assert '#include "mesh/mesh_inspector.h"' in console
    assert 'ok_begin("signal")' in console
    assert 'ok_begin("roomservers")' in console
    assert 'ok_begin("repeaters")' in console
    assert 'strcmp(line, "signal")' in console
    assert 'strcmp(line, "roomservers")' in console
    assert 'strcmp(line, "repeaters")' in console
    assert "Signal is derived from recent packet, route, and heard-node evidence" in console
    assert "Room servers are derived from signed heard-node adverts with room role" in console
    assert "Repeater candidates are inferred from nonzero path-hop route" in console
    assert "signal" in SMOKE_COMMANDS
    assert "roomservers" in SMOKE_COMMANDS
    assert "repeaters" in SMOKE_COMMANDS


def test_app_snapshot_and_ui_surface_mesh_visibility():
    header = read("main/app/app_model.h")
    source = read("main/app/app_model.c")
    ui = read("main/ui/ui_phase1.c")
    assert "D1L_APP_SNAPSHOT_ROOM_PREVIEW D1L_ROOM_SERVER_PREVIEW_CAPACITY" in header
    assert "D1L_APP_SNAPSHOT_REPEATER_PREVIEW D1L_REPEATER_PREVIEW_CAPACITY" in header
    assert "signal_summary" in header
    assert "recent_rooms" in header
    assert "recent_repeaters" in header
    assert "d1l_mesh_inspector_signal_summary(&snapshot->signal_summary)" in source
    assert "d1l_mesh_inspector_copy_room_servers" in source
    assert "d1l_mesh_inspector_copy_repeater_candidates" in source
    assert '"Signal"' in ui
    assert '"Mesh Roles"' in ui
    assert "static lv_obj_t *s_mesh_roles_sheet" in ui
    assert "render_mesh_roles_sheet" in ui
    assert "render_room_server_role_row" in ui
    assert "render_repeater_role_row" in ui
    assert "open_mesh_roles_event_cb" in ui
    assert 'create_button(content, "Mesh Roles"' in ui
    assert "open_mesh_roles_event_cb" in ui
    assert "create_mesh_roles_sheet(s_screen)" in ui
    assert "LV_SCROLLBAR_MODE_AUTO" in ui
    assert '"rooms %lu  rpt %lu  writes %lu"' in ui
    assert "format_snr_tenths" in ui
