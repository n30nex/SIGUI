from pathlib import Path

from scripts.smoke_d1l import SMOKE_COMMANDS


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_packet_log_is_bounded_and_retained_blob_store_backed():
    header = read("main/mesh/packet_log.h")
    source = read("main/mesh/packet_log.c")
    app_main = read("main/app_main.c")
    blob_store = read("main/storage/retained_blob_store.c")
    cmake = read("main/CMakeLists.txt")
    assert "D1L_PACKET_LOG_CAPACITY 32U" in header
    assert "D1L_PACKET_LOG_PERSIST_CAPACITY 8U" in header
    assert 'D1L_RETAINED_PACKET_LOG_NAMESPACE "d1l_packets"' in blob_store
    assert 'D1L_RETAINED_PACKET_LOG_SD_DIR "stores/packet_log"' in blob_store
    assert 'D1L_PACKET_LOG_KEY "ring"' in source
    assert "nvs_get_blob" in blob_store
    assert "nvs_set_blob" in blob_store
    assert "d1l_rp2040_bridge_file_write" in blob_store
    assert "d1l_rp2040_bridge_file_rename" in blob_store
    assert "nvs_get_blob" not in source
    assert "nvs_set_blob" not in source
    assert '#include "storage/retained_blob_store.h"' in source
    assert "d1l_retained_blob_store_read" in source
    assert "d1l_retained_blob_store_read_fallback" in source
    assert "d1l_retained_blob_store_write" in source
    assert "d1l_retained_blob_store_erase" in source
    assert "d1l_retained_blob_store_uses_sd" in source
    assert "D1L_RETAINED_BLOB_STORE_PACKET_LOG" in source
    assert '"storage/retained_blob_store.c"' in cmake
    assert "static d1l_packet_log_blob_t s_blob_scratch" in source
    assert "D1L_PACKET_LOG_PERSIST_CAPACITY" in source
    assert "sanitize_ascii" in source
    assert "d1l_packet_log_find_by_seq" in header
    assert "D1L_PACKET_LOG_RAW_PREVIEW_BYTES 32U" in header
    assert "D1L_PACKET_LOG_RAW_HEX_LEN" in header
    assert "raw_hex" in header
    assert "raw_truncated" in header
    assert "d1l_packet_log_append_raw" in header
    assert "d1l_packet_log_query" in header
    assert "raw_to_hex_preview" in source
    assert "packet_matches" in source
    assert "D1L_PACKET_LOG_SCHEMA 2U" in source
    assert "ESP_ERR_NOT_FOUND" in source
    assert "try_load_blob_from_fallback" in source
    assert '\\"packet_log_backend\\"' in read("main/comms/usb_console.c")
    assert "esp_err_t packet_log_ret = d1l_packet_log_init()" in app_main


def test_console_exposes_packet_detail_and_clear():
    console = read("main/comms/usb_console.c")
    assert 'print_packet_entries_json("packets"' in console
    assert 'print_packet_entries_json("packets filter"' in console
    assert 'print_packet_entries_json("packets search"' in console
    assert 'ok_begin("packets detail")' in console
    assert 'ok_begin("packets raw")' in console
    assert 'ok_begin("packets clear")' in console
    assert "d1l_packet_log_find_by_seq" in console
    assert "d1l_packet_log_query" in console
    assert "d1l_packet_log_clear()" in console
    assert 'strncmp(line, "packets filter ", 15)' in console
    assert 'strncmp(line, "packets search ", 15)' in console
    assert 'strncmp(line, "packets detail ", 15)' in console
    assert 'strncmp(line, "packets raw ", 12)' in console
    assert 'strcmp(line, "packets clear")' in console
    assert "packets filter <any|rx|tx> <any|text|kind>" in console
    assert "packets search <text>" in console
    assert "packets detail <seq>" in console
    assert "packets raw <seq>" in console
    assert "packets clear" in console
    assert '\\"persisted\\":true' in console
    assert '\\"raw_hex\\":\\"%s\\"' in console
    assert "packets" in SMOKE_COMMANDS
    assert "packets filter any any" in SMOKE_COMMANDS
    assert "packets search test" in SMOKE_COMMANDS
    assert "storage status" in SMOKE_COMMANDS
