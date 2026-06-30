from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_retained_blob_store_has_sd_packet_log_canary_with_nvs_fallback():
    header = read("main/storage/retained_blob_store.h")
    source = read("main/storage/retained_blob_store.c")
    cmake = read("main/CMakeLists.txt")

    assert "D1L_RETAINED_BLOB_STORE_PACKET_LOG" in header
    assert "d1l_retained_blob_store_backend_name" in header
    assert "d1l_retained_blob_store_is_available" in header
    assert "d1l_retained_blob_store_uses_sd" in header
    assert "d1l_retained_blob_store_note_sd_backend" in header
    assert "d1l_retained_blob_store_read" in header
    assert "d1l_retained_blob_store_read_fallback" in header
    assert "d1l_retained_blob_store_write" in header
    assert "d1l_retained_blob_store_erase" in header
    assert '"storage/retained_blob_store.c"' in cmake

    assert 'D1L_RETAINED_PACKET_LOG_NAMESPACE "d1l_packets"' in source
    assert 'D1L_RETAINED_PACKET_LOG_SD_DIR "stores/packet_log"' in source
    assert '.name = "packet_log"' in source
    assert 'return store_sd_enabled(config) ? "sd" : "nvs";' in source
    assert "d1l_retained_blob_store_note_sd_backend" in source
    assert "data_ready &&" in source
    assert "file_ops_supported &&" in source
    assert "atomic_rename_supported &&" in source
    assert "file_line_max >= D1L_RP2040_FILE_LINE_MAX" in source
    assert "file_chunk_max >= D1L_RP2040_FILE_CHUNK_MAX" in source
    assert "path_max >= D1L_RP2040_FILE_PATH_MAX" in source
    assert "build_sd_path(config, key, \".bin\"" in source
    assert "build_sd_path(config, key, \".tmp\"" in source
    assert "d1l_rp2040_bridge_file_stat" in source
    assert "d1l_rp2040_bridge_file_read" in source
    assert "d1l_rp2040_bridge_file_write" in source
    assert "d1l_rp2040_bridge_file_rename(temp_path, path, true" in source
    assert "d1l_rp2040_bridge_file_delete" in source
    assert "D1L_RP2040_FILE_CHUNK_MAX" in source
    assert "nvs_read_blob" in source
    assert "nvs_write_blob" in source
    assert "nvs_erase_blob" in source
    assert "nvs_get_blob(handle, key, dst, len_inout)" in source
    assert "nvs_set_blob(handle, key, src, len)" in source
    assert "nvs_erase_key(handle, key)" in source
    assert "nvs_commit(handle)" in source
    assert "ESP_ERR_NVS_NOT_FOUND" in source
    assert "(void)nvs_write_blob(config, key, src, len)" in source
    assert "return sd_ret == ESP_ERR_NOT_FOUND ? nvs_ret : sd_ret;" in source


def test_packet_log_backend_is_reported_from_blob_store_and_can_switch_to_sd():
    storage_status = read("main/storage/storage_status.c")
    packet_log = read("main/mesh/packet_log.c")

    assert '#include "storage/retained_blob_store.h"' in storage_status
    assert "d1l_retained_blob_store_note_sd_backend(sd->data_ready" in storage_status
    assert "sd->file_ops_supported" in storage_status
    assert "sd->atomic_rename_supported" in storage_status
    assert "d1l_retained_blob_store_backend_name(D1L_RETAINED_BLOB_STORE_PACKET_LOG)" in storage_status
    assert 'status->data_backend = packet_log_on_sd ? "mixed" : "nvs"' in storage_status
    assert '"packet_log_canary_enabled"' in storage_status
    assert 'status->packet_log_backend = "nvs"' not in storage_status
    assert "d1l_retained_blob_store_read(D1L_RETAINED_BLOB_STORE_PACKET_LOG" in packet_log
    assert "d1l_retained_blob_store_read_fallback(D1L_RETAINED_BLOB_STORE_PACKET_LOG" in packet_log
    assert "d1l_retained_blob_store_write(D1L_RETAINED_BLOB_STORE_PACKET_LOG" in packet_log
    assert "d1l_retained_blob_store_erase(D1L_RETAINED_BLOB_STORE_PACKET_LOG" in packet_log
