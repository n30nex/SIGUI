from pathlib import Path

from scripts.smoke_d1l import SMOKE_COMMANDS


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_heard_node_store_is_bounded_and_nvs_backed():
    header = read("main/mesh/node_store.h")
    source = read("main/mesh/node_store.c")
    cmake = read("main/CMakeLists.txt")
    app_main = read("main/app_main.c")
    assert "D1L_NODE_STORE_CAPACITY 16U" in header
    assert "D1L_HEARD_NODE_NAME_LEN 24U" in header
    assert 'D1L_NODE_STORE_NAMESPACE "d1l_nodes"' in source
    assert 'D1L_NODE_STORE_KEY "heard"' in source
    assert "nvs_get_blob" in source
    assert "nvs_set_blob" in source
    assert "static d1l_node_store_blob_t s_blob_scratch" in source
    assert "find_by_fingerprint" in source
    assert "oldest_index" in source
    assert '"mesh/node_store.c"' in cmake
    assert "d1l_node_store_init()" in app_main


def test_verified_adverts_upsert_heard_nodes():
    source = read("main/mesh/meshcore_service.c")
    assert '#include "mesh/node_store.h"' in source
    assert "verify_advert_signature" in source
    assert "d1l_node_store_upsert_advert(pub_prefix, name, type" in source
    assert "read_le32(timestamp)" in source
    assert "node store upsert failed" in source


def test_ui_console_and_smoke_expose_heard_nodes():
    app_header = read("main/app/app_model.h")
    app_source = read("main/app/app_model.c")
    ui = read("main/ui/ui_phase1.c")
    console = read("main/comms/usb_console.c")
    assert "recent_nodes" in app_header
    assert "node_total_written" in app_header
    assert "d1l_node_store_copy_recent" in app_source
    assert "render_node_row" in ui
    assert 'ok_begin("nodes")' in console
    assert 'strcmp(line, "nodes")' in console
    assert 'strcmp(line, "nodes clear")' in console
    assert "Verified MeshCore adverts populate this bounded heard-node store" in console
    assert "nodes" in SMOKE_COMMANDS
