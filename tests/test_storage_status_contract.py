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
    assert "D1L_STORAGE_RP2040_SD_PROBE_TIMEOUT_MS 10000U" in header
    assert "D1L_STORAGE_RP2040_SD_BOOT_PROBE_TIMEOUT_MS 1500U" in header
    assert "D1L_STORAGE_RP2040_SD_FORMAT_TIMEOUT_MS 30000U" in header
    assert "d1l_storage_format_sd_confirmed" in header
    assert "d1l_storage_status_init" in header
    assert "d1l_storage_status_note_rp2040" in header
    assert "d1l_storage_status_refresh" in header
    assert "rp2040_sd_protocol_supported" in header
    assert "setup_action" in header
    assert "format_action" in header
    assert '"storage/storage_status.c"' in cmake
    assert '"storage/export_store.c"' in cmake
    assert '"storage"' in cmake
    assert "esp_err_t storage_ret = d1l_storage_status_init()" in app_main
    assert app_main.index("d1l_storage_status_init()") < app_main.index("d1l_message_store_init()")
    assert app_main.index("d1l_rp2040_bridge_init()") < app_main.index("d1l_message_store_init()")
    assert app_main.index("D1L_STORAGE_RP2040_SD_BOOT_PROBE_TIMEOUT_MS") < app_main.index("d1l_message_store_init()")
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
    assert "file_ops_supported" in header
    assert "atomic_rename_supported" in header
    assert "file_line_max" in header
    assert "file_chunk_max" in header
    assert "path_max" in header
    assert "s_status.file_ops_supported = sd->file_ops_supported" in source
    assert "s_status.atomic_rename_supported = sd->atomic_rename_supported" in source
    assert "s_status.file_line_max = sd->file_line_max" in source
    assert "s_status.file_chunk_max = sd->file_chunk_max" in source
    assert "s_status.path_max = sd->path_max" in source
    assert "D1L_RETAINED_BLOB_STORE_PUBLIC_MESSAGES" in source
    assert "D1L_RETAINED_BLOB_STORE_DM_MESSAGES" in source
    assert "D1L_RETAINED_BLOB_STORE_ROUTES" in source
    assert "D1L_RETAINED_BLOB_STORE_PACKET_LOG" in source
    assert "d1l_retained_blob_store_backend_name(D1L_RETAINED_BLOB_STORE_PUBLIC_MESSAGES)" in source
    assert "d1l_retained_blob_store_backend_name(D1L_RETAINED_BLOB_STORE_DM_MESSAGES)" in source
    assert "d1l_retained_blob_store_backend_name(D1L_RETAINED_BLOB_STORE_ROUTES)" in source
    assert "d1l_retained_blob_store_backend_name(D1L_RETAINED_BLOB_STORE_PACKET_LOG)" in source
    assert 'status->message_store_backend = "nvs"' not in source
    assert 'status->dm_store_backend = "nvs"' not in source
    assert 'status->route_store_backend = "nvs"' not in source
    assert "d1l_retained_blob_store_note_sd_backend(sd->data_ready" in source
    assert "sd->file_ops_supported" in source
    assert "sd->atomic_rename_supported" in source
    assert 'status->data_backend = any_retained_sd ? "mixed" : "nvs"' in source
    assert 'status->map_tile_backend = "unavailable"' in source
    assert '"sd_diagnostic_exports_ready" : "serial"' in source
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
    bridge_unavailable_branch = source.split("if (s_status.rp2040_bridge_required && !s_status.rp2040_bridge_ready)", 1)[1].split("} else if", 1)[0]
    assert bridge_unavailable_branch.index("clear_sd_runtime_fields(&s_status)") < bridge_unavailable_branch.index("set_store_backends(&s_status)")
    assert "status->data_enabled = any_retained_sd" in source
    assert 's_status.map_tile_backend = "sd_pending_store_migration"' in source
    assert '"retained_history_sd_enabled"' in source


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
        "export_backend",
    ]:
        assert field in app_header
        assert field in app_source

    assert '#include "storage/storage_status.h"' in console
    assert 'ok_begin("storage status")' in console
    assert "d1l_storage_status_refresh(D1L_STORAGE_RP2040_SD_PROBE_TIMEOUT_MS)" in console
    assert 'ok_begin("storage setup")' in console
    assert 'ok_begin("storage filecanary")' in console
    assert 'ok_begin("storage export-canary")' in console
    assert '"storage status"' in console
    assert '"storage setup"' in console
    assert "storage setup confirm FORMAT-DESKOS-SD" in console
    assert "storage filecanary" in console
    assert "storage export-canary <token>" in console
    assert 'strcmp(line, "storage status")' in console
    assert 'strcmp(line, "storage setup")' in console
    assert 'strcmp(line, "storage filecanary")' in console
    assert 'strncmp(line, "storage export-canary "' in console
    assert "will_format" in console
    assert "false" in console
    assert "ESP_ERR_NOT_SUPPORTED" in console
    assert '\\"fallback\\":\\"nvs\\"' in console
    assert "storage status" in SMOKE_COMMANDS
    assert "storage setup" in SMOKE_COMMANDS
    assert not any(command.startswith("storage export-canary") for command in SMOKE_COMMANDS)
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
    assert "d1l_storage_format_sd_confirmed(" in console
    assert "D1L_STORAGE_RP2040_SD_FORMAT_TIMEOUT_MS" in console
    assert '\\"file_ops\\":%s' in console
    assert '\\"file_line_max\\":%lu' in console
    assert '\\"file_chunk_max\\":%lu' in console
    assert '\\"path_max\\":%lu' in console
    assert '\\"atomic_rename\\":%s' in console


def test_storage_filecanary_is_serial_only_and_uses_atomic_sd_file_ops():
    console = read("main/comms/usb_console.c")
    runner = read("scripts/sd_file_canary_d1l.py")
    retained_runner = read("scripts/sd_retained_history_acceptance_d1l.py")
    docs = read("docs/TEST_PLAN_D1L.md")
    runbook = read("docs/RP2040_SD_BRIDGE_FLASH_D1L.md")

    assert "cmd_storage_filecanary" in console
    assert "d1l_storage_status_refresh(D1L_STORAGE_RP2040_SD_PROBE_TIMEOUT_MS)" in console
    assert "d1l_rp2040_bridge_file_write(tmp_path, 0U, payload" in console
    assert "d1l_rp2040_bridge_file_read(tmp_path" in console
    assert "d1l_rp2040_bridge_file_rename(tmp_path, final_path, true" in console
    assert "d1l_rp2040_bridge_file_stat(final_path" in console
    assert "d1l_rp2040_bridge_file_delete(final_path" in console
    assert '"canary/filecanary.tmp"' in console
    assert '"canary/filecanary.bin"' in console
    assert "D1L_RP2040_FILE_LINE_MAX" in console
    assert "D1L_RP2040_FILE_CHUNK_MAX" in console
    assert "D1L_RP2040_FILE_PATH_MAX" in console
    assert "no Public RF or format command was used" in console
    assert "cmd_storage_retained_canary" in console
    assert "storage_retained_history_sd_ready" in console
    assert "d1l_message_store_append_public" in console
    assert "d1l_dm_store_append" in console
    assert "d1l_route_store_upsert_observation" in console
    assert "d1l_packet_log_append_raw" in console
    assert "storage retained-canary <token>" in console
    assert "cmd_storage_setup" in console
    assert "storage filecanary" in runner
    assert "mesh send public" not in runner
    assert "FORMAT-DESKOS-SD" not in runner
    assert "retained_history_backends_ready" in runner
    assert '"message_store_backend"' in runner
    assert '"dm_store_backend"' in runner
    assert '"route_store_backend"' in runner
    assert '"packet_log_backend"' in runner
    assert "storage retained-canary" in retained_runner
    assert "mesh send public" not in retained_runner
    assert "FORMAT-DESKOS-SD" not in retained_runner
    assert "COM11" not in retained_runner
    assert "COM29" not in retained_runner
    assert "storage filecanary" in docs
    assert "sd_retained_history_acceptance_d1l.py" in docs
    assert "docs/RP2040_SD_BRIDGE_FLASH_D1L.md" in docs
    assert "COM12" in runbook
    assert "Do not use COM11 or COM29" in runbook
    assert "Do not send Public RF" in runbook
    assert "flash_d1l.ps1" in runbook
    assert "flash_rp2040_sd_bridge_uf2.py" in runbook
    assert "sd_retained_history_acceptance_d1l.py" in runbook
    assert "--copy" in runbook
    assert "deskos_sd_bridge.ino.uf2" in runbook


def test_storage_export_canary_is_serial_only_and_uses_atomic_sd_file_ops():
    console = read("main/comms/usb_console.c")
    store_header = read("main/storage/export_store.h")
    store_source = read("main/storage/export_store.c")
    runner = read("scripts/sd_export_canary_d1l.py")
    diagnostic_runner = read("scripts/sd_diagnostic_export_d1l.py")
    protocol = read("tools/rp2040_sd_protocol.py")
    workflow = read(".github/workflows/d1l-ci.yml")
    docs = read("docs/TEST_PLAN_D1L.md")

    assert "D1L_EXPORT_CANARY_TOKEN_MAX 31U" in store_header
    assert "D1L_EXPORT_DIAGNOSTIC_PAYLOAD_MAX 4096U" in store_header
    assert "d1l_export_store_token_valid" in store_header
    assert "d1l_export_store_sd_ready" in store_header
    assert "d1l_export_store_write_canary" in store_header
    assert "d1l_export_store_write_diagnostics" in store_header
    assert "exports/diagnostics/export-canary-%s.tmp" in store_source
    assert "exports/diagnostics/export-canary-%s.json" in store_source
    assert "exports/diagnostics/diagnostic-export-%s.tmp" in store_source
    assert "exports/diagnostics/diagnostic-export-%s.json" in store_source
    assert "diagnostic_export_canary" in store_source
    assert "write_payload_atomic" in store_source
    assert "verify_payload" in store_source
    assert "d1l_rp2040_bridge_file_write(result->tmp_path" in store_source
    assert "verify_payload(result->tmp_path" in store_source
    assert "d1l_rp2040_bridge_file_rename(result->tmp_path, result->path, true" in store_source
    assert "d1l_rp2040_bridge_file_stat(result->path" in store_source
    assert "verify_payload(result->path" in store_source
    assert "d1l_rp2040_bridge_file_delete(result->path" not in store_source
    assert "public_rf_tx" in store_source
    assert "formats_sd" in store_source
    assert "D1L_RP2040_SD_FORMAT_CONFIRMATION" not in store_source
    assert "cmd_storage_export_canary" in console
    assert "cmd_storage_export_diagnostics" in console
    assert "storage export-canary <token>" in console
    assert "storage export-diagnostics <token>" in console
    assert "d1l_export_store_write_canary" in console
    assert "d1l_export_store_write_diagnostics" in console
    assert '\\"map_tiles\\"' in console
    assert '\\"exported\\":false' in console
    assert '\\"reason\\":\\"pending\\"' in console
    assert '\\"limits\\"' in console
    assert "storage export-canary" in runner
    assert "export_backend_ready" in runner
    assert "storage export-diagnostics" in diagnostic_runner
    assert "diagnostic_export_passed" in diagnostic_runner
    assert "mesh send public" not in diagnostic_runner
    assert "FORMAT-DESKOS-SD" not in diagnostic_runner
    assert "COM11" not in diagnostic_runner
    assert "COM29" not in diagnostic_runner
    assert "mesh send public" not in runner
    assert "FORMAT-DESKOS-SD" not in runner
    assert "COM11" not in runner
    assert "COM29" not in runner
    assert "export_canary_transcript" in protocol
    assert "diagnostic_export_transcript" in protocol
    assert "EXPORT_CANARY_START_ID" in protocol
    assert "DIAGNOSTIC_EXPORT_START_ID" in protocol
    assert "python ./scripts/sd_export_canary_d1l.py --dry-run --token ci-dry-run" in workflow
    assert "python ./scripts/sd_diagnostic_export_d1l.py --dry-run --token ci-dry-run" in workflow
    assert "sd_export_canary_d1l.py" in docs


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
    assert "retained Public/DM message history" in user_guide
    assert "Retained Public/DM message history" in readme
