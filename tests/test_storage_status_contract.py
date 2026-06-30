from pathlib import Path

from scripts.smoke_d1l import SMOKE_COMMANDS


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_storage_status_service_is_boot_safe_and_nvs_fallback():
    header = read("main/storage/storage_status.h")
    source = read("main/storage/storage_status.c")
    cmake = read("main/CMakeLists.txt")
    app_main = read("main/app_main.c")

    assert "D1L_STORAGE_SD_MOUNT_POINT \"/sdcard\"" in header
    assert "D1L_STORAGE_SD_DATA_ROOT \"/sdcard/deskos\"" in header
    assert "d1l_storage_status_init" in header
    assert "d1l_storage_status_note_rp2040" in header
    assert '"storage/storage_status.c"' in cmake
    assert '"storage"' in cmake
    assert "esp_err_t storage_ret = d1l_storage_status_init()" in app_main
    assert app_main.index("d1l_storage_status_init()") < app_main.index("d1l_message_store_init()")
    assert "d1l_storage_status_note_rp2040(rp2040_ret)" in app_main
    assert "CONFIG_LCD_BOARD_SENSECAP_INDICATOR_D1L" in source
    assert 's_status.sd_interface = "rp2040"' in source
    assert 's_status.sd_state = "pending_bridge"' in source
    assert 'status->message_store_backend = "nvs"' in source
    assert 'status->packet_log_backend = "nvs"' in source
    assert 'status->route_store_backend = "nvs"' in source
    assert 'status->map_tile_backend = "unavailable"' in source
    assert "bsp_sdcard_init" not in source
    assert "format_if_mount_failed" not in source


def test_storage_status_is_visible_in_snapshot_console_smoke_and_ui():
    app_header = read("main/app/app_model.h")
    app_source = read("main/app/app_model.c")
    console = read("main/comms/usb_console.c")
    ui = read("main/ui/ui_phase1.c")
    simulator = read("tools/ui_simulator.py")

    for field in [
        "storage_sd_state",
        "storage_sd_interface",
        "storage_backend",
        "message_store_backend",
        "packet_log_backend",
        "route_store_backend",
        "map_tile_backend",
    ]:
        assert field in app_header
        assert field in app_source

    assert '#include "storage/storage_status.h"' in console
    assert 'ok_begin("storage status")' in console
    assert '"storage status"' in console
    assert 'strcmp(line, "storage status")' in console
    assert '\\"format_action\\":\\"not_available\\"' in console
    assert '\\"fallback\\":\\"nvs\\"' in console
    assert "storage status" in SMOKE_COMMANDS
    assert '"Storage %s  SD %s"' in ui
    assert 'snapshot->storage_backend ? snapshot->storage_backend : "nvs"' in ui
    assert '"Storage"' in simulator
    assert "NVS fallback" in simulator


def test_current_d1l_bsp_keeps_esp32_direct_sd_disabled():
    board = read("third_party/sensecap_indicator_esp32/components/bsp/src/boards/sensecap_indicator_board.c")
    bsp_sd = read("third_party/sensecap_indicator_esp32/components/bsp/src/storage/bsp_sdcard.c")
    roadmap = read("docs/ROADMAP.md")

    assert ".FUNC_SDMMC_EN =   (0)" in board
    assert ".FUNC_SDSPI_EN =       (0)" in board
    assert "return ESP_ERR_NOT_SUPPORTED" in bsp_sd
    assert ".format_if_mount_failed = false" in bsp_sd
    assert "MicroSD support is handled by the RP2040 side" in roadmap
