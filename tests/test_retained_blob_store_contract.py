from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_retained_blob_store_has_nvs_only_packet_log_provider():
    header = read("main/storage/retained_blob_store.h")
    source = read("main/storage/retained_blob_store.c")
    cmake = read("main/CMakeLists.txt")

    assert "D1L_RETAINED_BLOB_STORE_PACKET_LOG" in header
    assert "d1l_retained_blob_store_backend_name" in header
    assert "d1l_retained_blob_store_is_available" in header
    assert "d1l_retained_blob_store_read" in header
    assert "d1l_retained_blob_store_write" in header
    assert "d1l_retained_blob_store_erase" in header
    assert '"storage/retained_blob_store.c"' in cmake

    assert 'D1L_RETAINED_PACKET_LOG_NAMESPACE "d1l_packets"' in source
    assert '.name = "packet_log"' in source
    assert 'return find_store(store_id) ? "nvs" : "unavailable";' in source
    assert "nvs_open(config->nvs_namespace, NVS_READWRITE, &handle)" in source
    assert "nvs_get_blob(handle, key, dst, len_inout)" in source
    assert "nvs_set_blob(handle, key, src, len)" in source
    assert "nvs_erase_key(handle, key)" in source
    assert "nvs_commit(handle)" in source
    assert "ESP_ERR_NVS_NOT_FOUND" in source
    assert "d1l_rp2040_bridge_file_" not in source


def test_packet_log_backend_is_reported_from_blob_store_but_remains_nvs():
    storage_status = read("main/storage/storage_status.c")
    packet_log = read("main/mesh/packet_log.c")

    assert '#include "storage/retained_blob_store.h"' in storage_status
    assert "d1l_retained_blob_store_backend_name(D1L_RETAINED_BLOB_STORE_PACKET_LOG)" in storage_status
    assert 'status->packet_log_backend = "nvs"' not in storage_status
    assert "d1l_retained_blob_store_read(D1L_RETAINED_BLOB_STORE_PACKET_LOG" in packet_log
    assert "d1l_retained_blob_store_write(D1L_RETAINED_BLOB_STORE_PACKET_LOG" in packet_log
    assert "d1l_retained_blob_store_erase(D1L_RETAINED_BLOB_STORE_PACKET_LOG" in packet_log
