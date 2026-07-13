from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_bulk_store_and_inspector_buffers_use_external_bss():
    packet_log = read("main/mesh/packet_log.c")
    node_store = read("main/mesh/node_store.c")
    mesh_inspector = read("main/mesh/mesh_inspector.c")

    for source in (packet_log, node_store, mesh_inspector):
        assert '#include "esp_attr.h"' in source

    assert (
        "static d1l_packet_log_entry_t s_entries[D1L_PACKET_LOG_CAPACITY] "
        "EXT_RAM_BSS_ATTR;"
    ) in packet_log
    assert (
        "static d1l_packet_log_primary_blob_t s_primary_blob_scratch "
        "EXT_RAM_BSS_ATTR;"
    ) in packet_log
    assert (
        "static d1l_node_entry_t s_entries[D1L_NODE_STORE_CAPACITY] "
        "EXT_RAM_BSS_ATTR;"
    ) in node_store
    assert (
        "static d1l_node_view_t s_query_scratch[D1L_NODE_STORE_CAPACITY] "
        "EXT_RAM_BSS_ATTR;"
    ) in node_store
    assert (
        "static d1l_node_entry_t s_node_scratch[D1L_NODE_STORE_CAPACITY] "
        "EXT_RAM_BSS_ATTR;"
    ) in mesh_inspector
