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
    assert "d1l_storage_format_sd_confirmed" in header
    assert "d1l_storage_status_init" in header
    assert "d1l_storage_status_note_rp2040" in header
    assert "d1l_storage_status_refresh" in header
    assert "rp2040_sd_protocol_supported" in header
    assert "setup_action" in header
    assert "format_action" in header
    assert '"storage/storage_status.c"' in cmake
    assert '"storage"' in cmake
    assert "esp_err_t storage_ret = d1l_storage_status_init()" in app_main
    assert app_main.index("d1l_storage_status_init()") < app_main.index("d1l_message_store_init()")
    assert app_main.index("d1l_rp2040_bridge_init()") < app_main.index("d1l_message_store_init()")
    assert app_main.index("d1l_storage_status_refresh(120U)") < app_main.index("d1l_message_store_init()")
    assert "d1l_storage_status_note_rp2040(rp2040_ret)" in app_main
    assert "CONFIG_LCD_BOARD_SENSECAP_INDICATOR_D1L" in source
    assert 's_status.sd_interface = "rp2040"' in source
    assert 's_status.sd_state = "pending_bridge"' in source
    assert '"protocol_pending"' in source
    assert '"bridge_protocol_pending"' in source
    assert "d1l_rp2040_bridge_probe_sd(&sd, timeout_ms)" in source
    assert "d1l_rp2040_bridge_format_sd(&sd, confirmation, timeout_ms)" in source
    assert "D1L_RP2040_SD_FORMAT_CONFIRMATION" in source
    assert '"format_confirmation_required"' in source
    assert '"store_migration_pending"' in source
    assert 'status->message_store_backend = "nvs"' in source
    assert "d1l_retained_blob_store_note_sd_backend(sd->data_ready" in source
    assert "sd->file_ops_supported" in source
    assert "sd->atomic_rename_supported" in source
    assert "d1l_retained_blob_store_backend_name(D1L_RETAINED_BLOB_STORE_PACKET_LOG)" in source
    assert "d1l_retained_blob_store_uses_sd(D1L_RETAINED_BLOB_STORE_PACKET_LOG)" in source
    assert 'status->data_backend = packet_log_on_sd ? "mixed" : "nvs"' in source
    assert 'status->route_store_backend = "nvs"' in source
    assert 'status->map_tile_backend = "unavailable"' in source
    assert "bsp_sdcard_init" not in source
    assert "format_if_mount_failed" not in source


def test_storage_format_request_is_guarded_before_bridge_command():
    source = read("main/storage/storage_status.c")
    guard_order = [
        "strcmp(confirmation, D1L_RP2040_SD_FORMAT_CONFIRMATION)",
        "!s_status.rp2040_bridge_required || !s_status.rp2040_sd_protocol_supported",
        "!s_status.sd_present",
        "!s_status.format_supported",
        "!s_status.setup_required",
        "d1l_rp2040_bridge_format_sd(&sd, confirmation, timeout_ms)",
    ]

    positions = [source.index(token) for token in guard_order]
    assert positions == sorted(positions)
    assert source.count("set_store_backends(&s_status)") >= 3
    assert "status->data_enabled = packet_log_on_sd" in source
    assert 's_status.map_tile_backend = "sd_pending_store_migration"' in source
    assert '"packet_log_canary_enabled"' in source


def test_storage_status_is_visible_in_snapshot_console_smoke_and_ui():
    app_header = read("main/app/app_model.h")
    app_source = read("main/app/app_model.c")
    console = read("main/comms/usb_console.c")
    ui = read("main/ui/ui_phase1.c")
    simulator = read("tools/ui_simulator.py")
    rp2040_header = read("main/hal/rp2040_bridge.h")
    rp2040_source = read("main/hal/rp2040_bridge.c")

    for field in [
        "storage_sd_state",
        "storage_sd_interface",
        "storage_rp2040_sd_protocol_supported",
        "storage_setup_action",
        "storage_format_action",
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
    assert "d1l_storage_status_refresh(120U)" in console
    assert 'ok_begin("storage setup")' in console
    assert '"storage status"' in console
    assert '"storage setup"' in console
    assert "storage setup confirm FORMAT-DESKOS-SD" in console
    assert 'strcmp(line, "storage status")' in console
    assert 'strcmp(line, "storage setup")' in console
    assert "will_format" in console
    assert "false" in console
    assert "ESP_ERR_NOT_SUPPORTED" in console
    assert '\\"fallback\\":\\"nvs\\"' in console
    assert "storage status" in SMOKE_COMMANDS
    assert "storage setup" in SMOKE_COMMANDS
    assert '"Storage %s  SD %s"' in ui
    assert "static lv_obj_t *s_storage_sheet" in ui
    assert "render_storage_sheet" in ui
    assert "open_storage_sheet_event_cb" in ui
    assert '"Storage Setup"' in ui
    assert '"No automatic format. Formatting will require explicit confirmation."' in ui
    assert 'snapshot->storage_backend ? snapshot->storage_backend : "nvs"' in ui
    assert '"Storage"' in simulator
    assert "NVS fallback" in simulator
    assert "storage_setup_sheet" in simulator
    assert "No automatic format" in simulator
    assert "d1l_rp2040_sd_status_t" in rp2040_header
    assert "D1L_RP2040_SD_FORMAT_CONFIRMATION" in rp2040_header
    assert "d1l_rp2040_bridge_probe_sd" in rp2040_header
    assert "d1l_rp2040_bridge_format_sd" in rp2040_header
    assert "D1L_RP2040_FILE_LINE_MAX 512U" in rp2040_header
    assert "D1L_RP2040_FILE_CHUNK_MAX 192U" in rp2040_header
    assert "d1l_rp2040_file_result_t" in rp2040_header
    assert "d1l_rp2040_bridge_file_stat" in rp2040_header
    assert "d1l_rp2040_bridge_file_read" in rp2040_header
    assert "d1l_rp2040_bridge_file_write" in rp2040_header
    assert "d1l_rp2040_bridge_file_append" in rp2040_header
    assert "d1l_rp2040_bridge_file_rename" in rp2040_header
    assert "DESKOS_SD_STATUS" in rp2040_source
    assert "DESKOS_SD_FORMAT" in rp2040_source
    assert "DESKOS_SD_FILE" in rp2040_source
    assert "D1L_RP2040_SD_FORMAT_CONFIRMATION" in rp2040_source
    assert "base64url_encode" in rp2040_source
    assert "base64url_decode" in rp2040_source
    assert "crc32_bytes" in rp2040_source
    assert "validate_relative_path" in rp2040_source
    assert "line_has_prefix" in rp2040_source
    assert "token_value_span" in rp2040_source
    assert "uart_write_bytes" in rp2040_source
    assert "uart_read_bytes" in rp2040_source
    assert "storage_format_hint" in console
    assert "format_requested" in console
    assert "format_performed" in console
    assert "d1l_storage_format_sd_confirmed(phrase, 3000U)" in console


def test_current_d1l_bsp_keeps_esp32_direct_sd_disabled():
    board = read("third_party/sensecap_indicator_esp32/components/bsp/src/boards/sensecap_indicator_board.c")
    bsp_sd = read("third_party/sensecap_indicator_esp32/components/bsp/src/storage/bsp_sdcard.c")
    roadmap = read("docs/ROADMAP.md")

    assert ".FUNC_SDMMC_EN =   (0)" in board
    assert ".FUNC_SDSPI_EN =       (0)" in board
    assert "return ESP_ERR_NOT_SUPPORTED" in bsp_sd
    assert ".format_if_mount_failed = false" in bsp_sd
    assert "MicroSD support is handled by the RP2040 side" in roadmap


def test_sd_checkpoint_validation_does_not_require_public_rf_or_reserved_ports():
    checkpoint = read("docs/PHASE6_SD_BRIDGE_SETUP_CHECKPOINT.md")
    validation = checkpoint.split("## Validation Rules", 1)[1].split("## Remaining SD Work", 1)[0]

    assert "mesh send public" not in validation
    assert "COM11" not in validation
    assert "COM29" not in validation
    assert "COM7" not in validation
    assert "flash_d1l" not in validation
    assert "monitor_d1l" not in validation
    assert "backup_flash" not in validation
    assert "soak_d1l.py --active-public-text" not in validation
    assert "probe_d1l_dm.py --bot-port" not in validation
    assert "COM12" in validation


def test_docs_keep_sd_backed_store_claims_pending_until_hardware_proof():
    readme = read("README.md")
    user_guide = read("docs/USER_GUIDE_D1L.md")
    limitations = read("docs/KNOWN_LIMITATIONS.md")
    checklist = read("docs/RELEASE_CHECKLIST.md")

    for doc in [readme, user_guide, limitations, checklist]:
        assert "retained stores" in doc or "Retained DeskOS stores" in doc or "Optional SD-card" in doc
        assert "pending" in doc.lower() or "fallback" in doc.lower()

    assert "SD-backed message/packet/route/export/map-tile stores" in checklist
    assert "- [ ] Optional SD-card data storage implemented" in checklist
    assert "packet-log canary" in user_guide
    assert "packet-log canary" in readme
