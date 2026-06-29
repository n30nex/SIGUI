from pathlib import Path

from scripts.smoke_d1l import SMOKE_COMMANDS


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_route_store_is_bounded_and_nvs_backed():
    header = read("main/mesh/route_store.h")
    source = read("main/mesh/route_store.c")
    cmake = read("main/CMakeLists.txt")
    app_main = read("main/app_main.c")
    assert "D1L_ROUTE_STORE_CAPACITY 16U" in header
    assert "D1L_ROUTE_TARGET_LEN 17U" in header
    assert 'D1L_ROUTE_STORE_NAMESPACE "d1l_routes"' in source
    assert 'D1L_ROUTE_STORE_KEY "routes"' in source
    assert "nvs_get_blob" in source
    assert "nvs_set_blob" in source
    assert "static d1l_route_store_blob_t s_blob_scratch" in source
    assert "route_confidence" in source
    assert "now_ms < entry->first_seen_ms" in source
    assert "oldest_index" in source
    assert "d1l_route_store_find_by_seq" in header
    assert "ESP_ERR_NOT_FOUND" in source
    assert '"mesh/route_store.c"' in cmake
    assert "d1l_route_store_init()" in app_main


def test_meshcore_public_and_advert_paths_feed_routes():
    source = read("main/mesh/meshcore_service.c")
    assert '#include "mesh/route_store.h"' in source
    assert "route_name(packet.route)" in source
    assert 'd1l_route_store_upsert_observation("public", "Public", "public_text"' in source
    assert 'd1l_route_store_upsert_observation(pub_prefix, name[0] ? name : pub_prefix, "advert"' in source
    assert 'route store public tx failed' in source
    assert 'route store advert tx failed' in source
    assert "D1L_MESHCORE_ROUTE_TRANSPORT_DIRECT" in source


def test_ui_console_and_smoke_expose_routes():
    app_header = read("main/app/app_model.h")
    app_source = read("main/app/app_model.c")
    ui = read("main/ui/ui_phase1.c")
    console = read("main/comms/usb_console.c")
    roadmap = read("docs/ROADMAP.md")
    assert "recent_routes" in app_header
    assert "route_total_written" in app_header
    assert "d1l_route_store_copy_recent" in app_source
    assert "route_count" in ui
    assert 'ok_begin("routes")' in console
    assert 'ok_begin("routes detail")' in console
    assert "d1l_route_store_find_by_seq" in console
    assert 'strcmp(line, "routes")' in console
    assert 'strncmp(line, "routes detail ", 14)' in console
    assert 'strcmp(line, "routes clear")' in console
    assert "routes detail <seq>" in console
    assert "Routes are learned from MeshCore path metadata" in console
    assert "routes" in SMOKE_COMMANDS
    assert "routes clear" in roadmap


def test_usb_console_keeps_large_preview_buffers_off_main_stack():
    console = read("main/comms/usb_console.c")
    assert "static d1l_packet_log_entry_t entries[8]" in console
    assert "static d1l_message_entry_t entries[8]" in console
    assert "static d1l_node_entry_t entries[8]" in console
    assert "static d1l_contact_entry_t entries[8]" in console
    assert "static d1l_route_entry_t entries[8]" in console
    assert "static char line[256]" in console
