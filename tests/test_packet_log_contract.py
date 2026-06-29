from pathlib import Path

from scripts.smoke_d1l import SMOKE_COMMANDS


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_packet_log_is_bounded_and_nvs_backed():
    header = read("main/mesh/packet_log.h")
    source = read("main/mesh/packet_log.c")
    app_main = read("main/app_main.c")
    assert "D1L_PACKET_LOG_CAPACITY 32U" in header
    assert "D1L_PACKET_LOG_PERSIST_CAPACITY 8U" in header
    assert 'D1L_PACKET_LOG_NAMESPACE "d1l_packets"' in source
    assert 'D1L_PACKET_LOG_KEY "ring"' in source
    assert "nvs_get_blob" in source
    assert "nvs_set_blob" in source
    assert "static d1l_packet_log_blob_t s_blob_scratch" in source
    assert "D1L_PACKET_LOG_PERSIST_CAPACITY" in source
    assert "sanitize_ascii" in source
    assert "d1l_packet_log_find_by_seq" in header
    assert "ESP_ERR_NOT_FOUND" in source
    assert "esp_err_t packet_log_ret = d1l_packet_log_init()" in app_main


def test_console_exposes_packet_detail_and_clear():
    console = read("main/comms/usb_console.c")
    assert 'ok_begin("packets")' in console
    assert 'ok_begin("packets detail")' in console
    assert 'ok_begin("packets clear")' in console
    assert "d1l_packet_log_find_by_seq" in console
    assert "d1l_packet_log_clear()" in console
    assert 'strncmp(line, "packets detail ", 15)' in console
    assert 'strcmp(line, "packets clear")' in console
    assert "packets detail <seq>" in console
    assert "packets clear" in console
    assert '\\"persisted\\":true' in console
    assert "packets" in SMOKE_COMMANDS
