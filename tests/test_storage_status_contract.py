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
    sdkconfig = read("sdkconfig.defaults")

    assert "D1L_STORAGE_SD_MOUNT_POINT \"/sdcard\"" in header
    assert "D1L_STORAGE_SD_DATA_ROOT \"/sdcard/deskos\"" in header
    assert "D1L_STORAGE_RP2040_SD_PROBE_TIMEOUT_MS 10000U" in header
    assert "D1L_STORAGE_RP2040_SD_BOOT_PROBE_TIMEOUT_MS 1500U" in header
    assert "d1l_storage_status_init" in header
    assert "d1l_storage_status_note_rp2040" in header
    assert "d1l_storage_boot_prepare" in header
    assert "d1l_storage_manager_start" in header
    assert "d1l_storage_manager_request_remount" in header
    assert "d1l_storage_manager_reset_bridge" in header
    assert "d1l_storage_manager_force_nvs" in header
    assert "d1l_storage_status_refresh" in header
    assert "rp2040_sd_protocol_supported" in header
    assert "sd_needs_fat32" in header
    assert "manager_state" in header
    assert "manager_running" in header
    assert "force_nvs" in header
    assert "manager_backoff_ms" in header
    assert "setup_action" in header
    assert '"storage/storage_status.c"' in cmake
    assert '"storage/export_store.c"' in cmake
    assert '"storage/map_tile_store.c"' in cmake
    assert '"storage"' in cmake
    assert "esp_err_t storage_ret = d1l_storage_status_init()" in app_main
    assert app_main.index("d1l_storage_status_init()") < app_main.index("d1l_message_store_init()")
    assert app_main.index("d1l_rp2040_bridge_init()") < app_main.index("d1l_message_store_init()")
    assert app_main.index("d1l_storage_boot_prepare") < app_main.index("d1l_message_store_init()")
    assert app_main.index("d1l_packet_log_init()") < app_main.index("d1l_storage_manager_start()")
    assert app_main.index("d1l_storage_manager_start()") < app_main.index("d1l_board_init()")
    assert app_main.index("D1L_STORAGE_RP2040_SD_BOOT_PROBE_TIMEOUT_MS") < app_main.index("d1l_message_store_init()")
    assert "d1l_storage_status_note_rp2040(rp2040_ret)" in app_main
    assert "storage manager start failed" in app_main
    assert "CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192" in sdkconfig
    assert "CONFIG_LCD_BOARD_SENSECAP_INDICATOR_D1L" in source
    for state in [
        "BRIDGE_WAIT",
        "PING",
        "STATUS",
        "MOUNT",
        "READY_SD",
        "READY_NVS",
        "NEEDS_FAT32",
        "NO_CARD",
        "ERROR_BACKOFF",
    ]:
        assert state in source
    assert "storage_manager_task" in source
    assert 'xTaskCreate(storage_manager_task' in source
    assert "d1l_rp2040_bridge_ping" in source
    assert "d1l_rp2040_bridge_reset" in source
    assert "storage_sd_ready_for_files" in source
    assert "apply_force_nvs_status" in source
    assert "d1l_storage_manager_request_remount" in source
    assert "d1l_storage_manager_reset_bridge" in source
    assert "d1l_storage_manager_force_nvs" in source
    assert 's_status.sd_interface = "rp2040"' in source
    assert 's_status.sd_state = "pending_bridge"' in source
    assert '"protocol_pending"' in source
    assert '"bridge_protocol_pending"' in source
    assert '"mount_pending"' in source
    assert '"wait_for_storage_mount"' in source
    assert "d1l_rp2040_bridge_probe_sd(&sd, timeout_ms)" in source
    assert "d1l_storage_boot_prepare" in source
    assert 'strcmp(s_status.sd_state, "mount_required")' in source
    assert "prepare_fat32_on_computer" in source
    assert "inspect_rp2040_sd_mount_error_firmware_path" in source
    assert "inspect_rp2040_sd_cmd0_firmware_path" in source
    assert "probe_rejected_card" in source
    assert "sd->probe_error == 3U || sd->probe_error == 4U" in source
    assert "mount_failed_with_diag" in source
    assert source.index("mount_failed_with_diag") < source.index("prepare_fat32_on_computer")
    assert "backup_reformat_fat32_on_computer" in source
    assert '"store_migration_pending"' in source
    assert "file_ops_supported" in header
    assert "atomic_rename_supported" in header
    assert "file_line_max" in header
    assert "file_chunk_max" in header
    assert "path_max" in header
    assert "sd_probe_power" in header
    assert "sd_probe_error" in header
    assert "sd_mount_error" in header
    assert "retained_sd_degraded" in header
    assert "retained_sd_stats" in header
    assert "s_status.file_ops_supported = sd->file_ops_supported" in source
    assert "s_status.atomic_rename_supported = sd->atomic_rename_supported" in source
    assert "s_status.file_line_max = sd->file_line_max" in source
    assert "s_status.file_chunk_max = sd->file_chunk_max" in source
    assert "s_status.path_max = sd->path_max" in source
    assert "s_status.sd_probe_error = sd->probe_error" in source
    assert "s_status.sd_mount_error = sd->mount_error" in source
    assert "D1L_RETAINED_BLOB_STORE_PUBLIC_MESSAGES" in source
    assert "D1L_RETAINED_BLOB_STORE_DM_MESSAGES" in source
    assert "D1L_RETAINED_BLOB_STORE_ROUTES" in source
    assert "D1L_RETAINED_BLOB_STORE_PACKET_LOG" in source
    assert "d1l_retained_blob_store_backend_name(D1L_RETAINED_BLOB_STORE_PUBLIC_MESSAGES)" in source
    assert "d1l_retained_blob_store_backend_name(D1L_RETAINED_BLOB_STORE_DM_MESSAGES)" in source
    assert "d1l_retained_blob_store_backend_name(D1L_RETAINED_BLOB_STORE_ROUTES)" in source
    assert "d1l_retained_blob_store_backend_name(D1L_RETAINED_BLOB_STORE_PACKET_LOG)" in source
    assert "refresh_retained_sd_health" in source
    assert "d1l_retained_blob_store_sd_stats(D1L_RETAINED_BLOB_STORE_PUBLIC_MESSAGES" in source
    assert "d1l_retained_blob_store_any_sd_degraded()" in source
    assert "D1L_RETAINED_BLOB_STORE_SD_DEGRADED_NOTE" in source
    assert 'status->message_store_backend = "nvs"' not in source
    assert 'status->dm_store_backend = "nvs"' not in source
    assert 'status->route_store_backend = "nvs"' not in source
    assert "d1l_retained_blob_store_note_sd_backend(sd->data_ready" in source
    assert "sd->file_ops_supported" in source
    assert "sd->atomic_rename_supported" in source
    assert 'status->data_backend = any_retained_sd ? "mixed" : "nvs"' in source
    assert '#include "storage/map_tile_store.h"' in source
    assert "d1l_map_tile_store_sd_ready(status)" in source
    assert '"sd_map_tiles_ready" : "unavailable"' in source
    assert '"sd_diagnostic_exports_ready" : "serial"' in source
    assert "bsp_sdcard_init" not in source
    assert "format_if_mount_failed" not in source
    boot_prepare = source.split("esp_err_t d1l_storage_boot_prepare", 1)[1].split(
        "esp_err_t d1l_storage_status_mount", 1
    )[0]
    assert "d1l_storage_status_mount(timeout_ms)" in boot_prepare
    assert "D1L_STORAGE_BOOT_POLL_ATTEMPTS" in source
    assert "d1l_storage_status_refresh(D1L_STORAGE_BOOT_POLL_TIMEOUT_MS)" in source
    assert 'strcmp(s_status.sd_state, "mount_pending")' in source
    assert "poll_mount_pending()" in boot_prepare
    assert "d1l_storage_format_sd_confirmed" not in boot_prepare
    assert "d1l_rp2040_bridge_format_sd" not in boot_prepare
    mount_body = source.split("esp_err_t d1l_storage_status_mount", 1)[1].split(
        "void d1l_storage_status", 1
    )[0]
    assert "storage_sd_ready_for_files()" in mount_body
    assert mount_body.index("storage_sd_ready_for_files()") < mount_body.index(
        "d1l_rp2040_bridge_mount_sd(&sd, timeout_ms)"
    )
    assert "s_status.last_error = ESP_OK" in mount_body


def test_storage_format_request_is_guarded_before_bridge_command():
    old_request = "DESKOS_SD_" + "FORMAT"
    old_confirmation = "D1L_RP2040_SD_" + "FORMAT_CONFIRMATION"
    old_phrase = "FORMAT-" + "DESKOS-SD"
    source = read("main/storage/storage_status.c")
    header = read("main/storage/storage_status.h")
    bridge_header = read("main/hal/rp2040_bridge.h")
    bridge_source = read("main/hal/rp2040_bridge.c")
    console = read("main/comms/usb_console.c")
    sketch = read("firmware/rp2040_sd_bridge/deskos_sd_bridge/deskos_sd_bridge.ino")

    for text in [source, header, bridge_header, bridge_source, console, sketch]:
        assert old_request not in text
        assert old_confirmation not in text
        assert old_phrase not in text
        assert "storage setup confirm" not in text
        assert "d1l_storage_format_sd_confirmed" not in text
        assert "d1l_rp2040_bridge_format_sd" not in text

    assert source.count("set_store_backends(&s_status)") >= 3
    bridge_unavailable_branch = source.split("if (s_status.rp2040_bridge_required && !s_status.rp2040_bridge_ready)", 1)[1].split("} else if", 1)[0]
    assert bridge_unavailable_branch.index("clear_sd_runtime_fields(&s_status)") < bridge_unavailable_branch.index("set_store_backends(&s_status)")
    assert "status->data_enabled = any_retained_sd" in source
    assert "d1l_map_tile_store_sd_ready(&s_status)" in source
    assert '"sd_map_tiles_ready" : "sd_pending_store_migration"' in source
    assert '"retained_history_sd_enabled"' in source


def test_rp2040_format_command_waits_for_format_reply_only():
    source = read("main/hal/rp2040_bridge.c")
    assert '"needs_fat32"' in source
    assert '"format_required"' in source  # legacy RP2040 status is mapped to needs_fat32.
    assert "d1l_rp2040_bridge_file_stat" in source
    assert "d1l_rp2040_bridge_format_sd" not in source


def test_rp2040_bridge_serializes_uart_transactions():
    source = read("main/hal/rp2040_bridge.c")

    assert '#include "freertos/semphr.h"' in source
    assert "static StaticSemaphore_t s_bridge_mutex_storage" in source
    assert "D1L_RP2040_BRIDGE_LOCK_GRACE_MS 15000U" in source
    assert "xSemaphoreCreateMutexStatic(&s_bridge_mutex_storage)" in source
    assert "xSemaphoreTake(s_bridge_mutex" in source
    assert "xSemaphoreGive(s_bridge_mutex)" in source

    exchange_body = source.split("static esp_err_t exchange_prefixed_line_internal", 1)[1].split(
        "static esp_err_t exchange_prefixed_line(", 1
    )[0]
    assert "take_bridge_lock(timeout_ms)" in exchange_body
    assert exchange_body.index("take_bridge_lock(timeout_ms)") < exchange_body.index("uart_flush_input")
    assert exchange_body.index("uart_write_bytes") < exchange_body.index("uart_read_bytes")
    assert "give_bridge_lock()" in exchange_body

    file_command_body = source.split("static esp_err_t send_file_command", 1)[1].split(
        "esp_err_t d1l_rp2040_bridge_init", 1
    )[0]
    assert "exchange_prefixed_line(command, command_len" in file_command_body

    serialized_functions = [
        ("esp_err_t d1l_rp2040_bridge_set_baud", "esp_err_t d1l_rp2040_bridge_reset"),
        ("esp_err_t d1l_rp2040_bridge_reset", "esp_err_t d1l_rp2040_bridge_double_reset"),
        ("esp_err_t d1l_rp2040_bridge_double_reset", "esp_err_t d1l_rp2040_bridge_ping"),
        ("esp_err_t d1l_rp2040_bridge_stock_probe", "esp_err_t d1l_rp2040_bridge_baud_probe"),
    ]
    for start, end in serialized_functions:
        body = source.split(start, 1)[1].split(end, 1)[0]
        assert "take_bridge_lock" in body
        assert "give_bridge_lock()" in body


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
        "storage_sd_needs_fat32",
        "storage_backend",
        "message_store_backend",
        "packet_log_backend",
        "route_store_backend",
        "map_tile_backend",
        "export_backend",
        "map_tile_cache_ready",
        "map_tile_cache_policy",
        "map_tile_cache_path_template",
        "map_tile_download_state",
        "map_tile_download_requires",
        "map_tile_provider_policy",
        "map_tile_provider_attribution",
    ]:
        assert field in app_header
        assert field in app_source

    assert '#include "storage/storage_status.h"' in console
    assert 'ok_begin("storage status")' in console
    assert 'ok_begin("storage map-policy")' in console
    status_body = console.split("static void cmd_storage_status(void)", 1)[1].split(
        "static void cmd_storage_mount", 1
    )[0]
    map_policy_body = console.split("static void cmd_storage_map_policy(void)", 1)[1].split(
        "static void print_storage_setup_payload", 1
    )[0]
    setup_body = console.split("static void cmd_storage_setup", 1)[1].split(
        "static void print_packet_entry_json", 1
    )[0]
    assert "d1l_storage_status(&status)" in status_body
    assert "d1l_storage_status(&status)" in map_policy_body
    assert "d1l_storage_status(&status)" in setup_body
    assert "d1l_storage_status_refresh(" not in status_body
    assert "d1l_storage_status_refresh(" not in map_policy_body
    assert "d1l_storage_status_refresh(" not in setup_body
    assert "d1l_storage_status_refresh(D1L_STORAGE_RP2040_SD_PROBE_TIMEOUT_MS)" in console
    assert "d1l_storage_status_mount(D1L_STORAGE_RP2040_SD_PROBE_TIMEOUT_MS)" in console
    assert "d1l_storage_manager_request_remount" in console
    assert "d1l_storage_manager_reset_bridge" in console
    assert "d1l_storage_manager_force_nvs" in console
    assert '\\"manager\\":{\\"running\\":%s' in console
    assert '\\"backoff_ms\\":%lu' in console
    assert '\\"force_nvs\\":%s' in console
    assert '\\"retained_sd\\":{\\"degraded\\":%s' in console
    assert '\\"sd_read_fail_count\\":%lu' in console
    assert '\\"sd_write_fail_count\\":%lu' in console
    assert '\\"sd_rename_fail_count\\":%lu' in console
    assert "D1L_RETAINED_BLOB_STORE_SD_DEGRADED_NOTE" in console
    assert 'cmd_storage_diag' in console
    assert 'ok_begin("storage setup")' in console
    assert 'ok_begin("storage filecanary")' in console
    assert 'ok_begin("storage map-tile-canary")' in console
    assert 'ok_begin("storage export-canary")' in console
    assert '"storage status"' in console
    assert '"storage mount"' in console
    assert '"storage remount"' in console
    assert '"storage reset-bridge"' in console
    assert "storage force-nvs [on|off]" in console
    assert '"storage diag"' in console
    assert '"storage setup"' in console
    assert "storage map-policy" in console
    assert "storage filecanary" in console
    assert "storage map-tile-canary <token>" in console
    assert "storage map-tile-download <z> <x> <y> <url-template> <attribution>" in console
    assert "bool_json(download_supported)" in console
    assert "bool_json(live_network_download)" in console
    assert "storage export-canary <token>" in console
    assert 'strcmp(line, "storage status")' in console
    assert 'strcmp(line, "storage mount")' in console
    assert 'strcmp(line, "storage remount")' in console
    assert 'strcmp(line, "storage reset-bridge")' in console
    assert 'strncmp(line, "storage force-nvs"' in console
    assert 'strcmp(line, "storage diag")' in console
    assert 'strcmp(line, "storage map-policy")' in console
    assert 'strcmp(line, "storage setup")' in console
    assert 'strcmp(line, "storage filecanary")' in console
    assert 'strncmp(line, "storage map-tile-canary "' in console
    assert 'strncmp(line, "storage map-tile-download "' in console
    assert 'strncmp(line, "storage export-canary "' in console
    assert "will_format" in console
    assert "no_device_format" in console
    assert "false" in console
    assert "ESP_ERR_NOT_SUPPORTED" in console
    assert '\\"fallback\\":\\"nvs\\"' in console
    assert "storage status" in SMOKE_COMMANDS
    assert "storage map-policy" in SMOKE_COMMANDS
    assert "storage setup" in SMOKE_COMMANDS
    assert not any(command.startswith("storage map-tile-canary") for command in SMOKE_COMMANDS)
    assert not any(command.startswith("storage export-canary") for command in SMOKE_COMMANDS)
    assert '"Storage %s  SD %s"' in ui
    assert "static lv_obj_t *s_storage_sheet" in ui
    assert "render_storage_sheet" in ui
    assert "open_storage_sheet_event_cb" in ui
    assert '"SD Card"' in ui
    assert "DeskOS creates its folders automatically and never formats cards on-device." in ui
    assert 'snapshot->storage_data_enabled ? "Ready"' in ui
    assert "FAT32 only, no format" in ui
    assert '"SD Card"' in simulator
    assert "FAT32 only, no format" in simulator
    assert "NVS fallback" in simulator
    assert "storage_setup_sheet" in simulator
    assert "Prepare FAT32 on a computer" in simulator
    assert "Inspect RP2040 mount diagnostics" in simulator
    assert "d1l_rp2040_sd_status_t" in rp2040_header
    assert "char state[32]" in rp2040_header
    assert "d1l_rp2040_ping_t" in rp2040_header
    assert "d1l_rp2040_bridge_ping" in rp2040_header
    assert "d1l_rp2040_bridge_probe_sd" in rp2040_header
    assert "d1l_rp2040_bridge_mount_sd" in rp2040_header
    assert "D1L_RP2040_FILE_LINE_MAX 512U" in rp2040_header
    assert "D1L_RP2040_FILE_CHUNK_MAX 192U" in rp2040_header
    assert "d1l_rp2040_file_result_t" in rp2040_header
    assert "d1l_rp2040_bridge_file_stat" in rp2040_header
    assert "d1l_rp2040_bridge_file_read" in rp2040_header
    assert "d1l_rp2040_bridge_file_write" in rp2040_header
    assert "d1l_rp2040_bridge_file_append" in rp2040_header
    assert "d1l_rp2040_bridge_file_rename" in rp2040_header
    assert "DESKOS_SD_STATUS" in rp2040_source
    assert "DESKOS_SD_MOUNT" in rp2040_source
    assert "DESKOS_SD_PING" in rp2040_source
    assert "DESKOS_SD_FILE" in rp2040_source
    assert "base64url_encode" in rp2040_source
    assert "base64url_decode" in rp2040_source
    assert "crc32_bytes" in rp2040_source
    assert "validate_relative_path" in rp2040_source
    assert "line_has_prefix" in rp2040_source
    assert "token_value_span" in rp2040_source
    assert "uart_write_bytes" in rp2040_source
    assert "uart_read_bytes" in rp2040_source
    assert '"rp2040 ping"' in console
    assert "cmd_rp2040_ping" in console
    assert "format_requested" in console
    assert "format_performed" in console
    assert '\\"file_ops\\":%s' in console
    assert '\\"file_line_max\\":%lu' in console
    assert '\\"file_chunk_max\\":%lu' in console
    assert '\\"path_max\\":%lu' in console
    assert '\\"atomic_rename\\":%s' in console
    assert '\\"probe_power\\":' in console
    assert '\\"probe_error\\":%lu' in console
    assert '\\"mount_error\\":%lu' in console
    assert "d1l_rp2040_bridge_sd_diag" in console
    assert "cmd_storage_diag_raw" in console
    assert '"storage diag raw"' in console
    assert 'strcmp(line, "storage diag raw")' in console
    assert '\\"cmd\\":\\"storage diag raw\\"' in console
    assert '\\"raw_line\\":' in console
    assert "char raw_line[D1L_RP2040_FILE_LINE_MAX + 1U]" in rp2040_header
    assert "snprintf(diag->raw_line, sizeof(diag->raw_line), \"%s\", line)" in rp2040_source
    assert '\\"formats_sd\\":false' in console
    assert '\\"public_rf_tx\\":false' in console
    assert "D1L_MAP_TILE_CACHE_POLICY" in console
    assert "D1L_MAP_TILE_CACHE_PATH_TEMPLATE" in console
    assert "D1L_MAP_TILE_DOWNLOAD_STATE" in console
    assert "D1L_MAP_TILE_DOWNLOAD_REQUIRES" in console
    assert "D1L_MAP_TILE_PROVIDER_POLICY" in console
    assert "D1L_MAP_TILE_PROVIDER_ATTRIBUTION" in console
    assert "download_supported = connectivity.wifi_build_enabled && cache_ready" in console
    assert "live_network_download = download_supported && connectivity.wifi_connected" in console
    assert "cmd_storage_map_tile_download" in console
    assert "d1l_map_tile_store_download(" in console
    assert '\\"public_rf_tx\\":false,\\"formats_sd\\":false' in console


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
    filecanary_body = console.split("static void cmd_storage_filecanary(void)", 1)[1].split(
        "static void cmd_storage_retained_canary", 1
    )[0]
    preflight_body = filecanary_body.split("print_storage_filecanary_error", 1)[0]
    assert "!status.data_enabled" not in preflight_body
    assert "packet_log_backend" not in preflight_body
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
    assert "setup confirm" not in runner
    assert "retained_history_backends_ready" in runner
    assert '"message_store_backend"' in runner
    assert '"dm_store_backend"' in runner
    assert '"route_store_backend"' in runner
    assert '"packet_log_backend"' in runner
    assert "storage retained-canary" in retained_runner
    assert "mesh send public" not in retained_runner
    assert "setup confirm" not in retained_runner
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
    data_runner = read("scripts/sd_data_export_d1l.py")
    protocol = read("tools/rp2040_sd_protocol.py")
    workflow = read(".github/workflows/d1l-ci.yml")
    docs = read("docs/TEST_PLAN_D1L.md")

    assert "D1L_EXPORT_CANARY_TOKEN_MAX 31U" in store_header
    assert "D1L_EXPORT_DIAGNOSTIC_PAYLOAD_MAX 4096U" in store_header
    assert "D1L_EXPORT_DATA_PAYLOAD_MAX 12288U" in store_header
    assert "d1l_export_store_token_valid" in store_header
    assert "d1l_export_store_sd_ready" in store_header
    assert "d1l_export_store_write_canary" in store_header
    assert "d1l_export_store_write_diagnostics" in store_header
    assert "d1l_export_store_write_data" in store_header
    assert "exports/diagnostics/export-canary-%s.tmp" in store_source
    assert "exports/diagnostics/export-canary-%s.json" in store_source
    assert "exports/diagnostics/diagnostic-export-%s.tmp" in store_source
    assert "exports/diagnostics/diagnostic-export-%s.json" in store_source
    assert "exports/data/data-export-%s.tmp" in store_source
    assert "exports/data/data-export-%s.json" in store_source
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
    assert "D1L_RP2040_SD_" + "FORMAT_CONFIRMATION" not in store_source
    assert "cmd_storage_export_canary" in console
    assert "cmd_storage_export_diagnostics" in console
    assert "cmd_storage_export_data" in console
    assert "storage export-canary <token>" in console
    assert "storage export-diagnostics <token>" in console
    assert "storage export-data <token>" in console
    assert "d1l_export_store_write_canary" in console
    assert "d1l_export_store_write_diagnostics" in console
    assert "d1l_export_store_write_data" in console
    assert "build_data_export_payload" in console
    assert "append_export_json_string_field" in console
    assert '\\"kind\\":\\"data_export\\"' in console
    assert '\\"private_identity_exported\\":false' in console
    assert "identity_private_key" not in console
    assert '\\"map_location\\":' in console
    assert '\\"lat_e7\\":%ld' in console
    assert '\\"lon_e7\\":%ld' in console
    assert '\\"map_tiles\\"' in console
    assert '\\"exported\\":false' in console
    assert '\\"cache_ready\\":%s' in console
    assert '\\"backend\\":\\"%s\\"' in console
    assert "diagnostic_export_does_not_bundle_map_tiles" in console
    assert '\\"limits\\"' in console
    assert "storage export-canary" in runner
    assert "export_backend_ready" in runner
    assert "storage export-diagnostics" in diagnostic_runner
    assert "diagnostic_export_passed" in diagnostic_runner
    assert "storage export-data" in data_runner
    assert "data_export_passed" in data_runner
    assert "private_identity_exported" in data_runner
    assert "mesh send public" not in data_runner
    assert "setup confirm" not in data_runner
    assert "COM11" not in data_runner
    assert "COM29" not in data_runner
    assert "mesh send public" not in diagnostic_runner
    assert "setup confirm" not in diagnostic_runner
    assert "COM11" not in diagnostic_runner
    assert "COM29" not in diagnostic_runner
    assert "mesh send public" not in runner
    assert "setup confirm" not in runner
    assert "COM11" not in runner
    assert "COM29" not in runner
    assert "export_canary_transcript" in protocol
    assert "diagnostic_export_transcript" in protocol
    assert "data_export_transcript" in protocol
    assert "EXPORT_CANARY_START_ID" in protocol
    assert "DIAGNOSTIC_EXPORT_START_ID" in protocol
    assert "DATA_EXPORT_START_ID" in protocol
    assert "python ./scripts/sd_export_canary_d1l.py --dry-run --token ci-dry-run" in workflow
    assert "python ./scripts/sd_diagnostic_export_d1l.py --dry-run --token ci-dry-run" in workflow
    assert "python ./scripts/sd_data_export_d1l.py --dry-run --token ci-dry-run" in workflow
    assert "sd_export_canary_d1l.py" in docs
    assert "sd_data_export_d1l.py" in docs


def test_storage_map_tile_canary_is_serial_only_and_uses_atomic_sd_file_ops():
    console = read("main/comms/usb_console.c")
    store_header = read("main/storage/map_tile_store.h")
    store_source = read("main/storage/map_tile_store.c")
    cmake = read("main/CMakeLists.txt")
    runner = read("scripts/sd_map_tile_canary_d1l.py")
    protocol = read("tools/rp2040_sd_protocol.py")
    workflow = read(".github/workflows/d1l-ci.yml")
    docs = read("docs/TEST_PLAN_D1L.md")

    assert "D1L_MAP_TILE_CANARY_TOKEN_MAX 31U" in store_header
    assert "D1L_MAP_TILE_ZOOM_MAX 18U" in store_header
    assert 'D1L_MAP_TILE_CACHE_POLICY "sd_offline_cache_when_ready"' in store_header
    assert 'D1L_MAP_TILE_CACHE_PATH_TEMPLATE "map/tiles/z{z}/x{x}/y{y}.tile"' in store_header
    assert 'D1L_MAP_TILE_DOWNLOAD_STATE "tile_render_pending"' in store_header
    assert "tile rendering proof" in store_header
    assert "D1L_MAP_TILE_URL_TEMPLATE_MAX 192U" in store_header
    assert "D1L_MAP_TILE_ATTRIBUTION_MAX 64U" in store_header
    assert "D1L_MAP_TILE_DOWNLOAD_MAX_BYTES" in store_header
    assert 'D1L_MAP_TILE_PROVIDER_POLICY "provider_config_required_no_public_osm_bulk"' in store_header
    assert "d1l_map_tile_provider_template_allowed" in store_header
    assert "d1l_map_tile_attribution_valid" in store_header
    assert "d1l_map_tile_store_download" in store_header
    assert "d1l_map_tile_store_token_valid" in store_header
    assert "d1l_map_tile_store_sd_ready" in store_header
    assert "d1l_map_tile_store_coord_valid" in store_header
    assert "d1l_map_tile_store_path" in store_header
    assert "d1l_map_tile_store_write_canary" in store_header
    assert "d1l_map_tile_store_check_canary" in store_header
    assert "z > D1L_MAP_TILE_ZOOM_MAX" in store_source
    assert 'strncmp(url_template, "https://", strlen("https://")) != 0' in store_source
    assert 'strstr(url_template, "{z}")' in store_source
    assert 'strstr(url_template, "{x}")' in store_source
    assert 'strstr(url_template, "{y}")' in store_source
    assert 'contains_case_insensitive(url_template, "tile.openstreetmap.org")' in store_source
    assert 'contains_case_insensitive(url_template, "openstreetmap.org")' in store_source
    assert '#include "esp_http_client.h"' in store_source
    assert '#include "esp_crt_bundle.h"' in store_source
    assert ".crt_bundle_attach = esp_crt_bundle_attach" in store_source
    assert "esp_http_client_init(&config)" in store_source
    assert "esp_http_client_open(client, 0)" in store_source
    assert "esp_http_client_fetch_headers(client)" in store_source
    assert "esp_http_client_read(client" in store_source
    assert "esp_http_client_is_complete_data_received(client)" in store_source
    assert "esp_http_client_cleanup(client)" in store_source
    assert "D1L_MAP_TILE_DOWNLOAD_MAX_BYTES" in store_source
    assert "D1L_MAP_TILE_USER_AGENT" in store_source
    assert "map/tiles/attribution.json" in store_source
    assert '"map/tiles/z%u/x%lu/y%lu.tile"' in store_source
    assert "map/tiles/z%u/x%lu/y%lu-%s.tmp" in store_source
    assert "map/tiles/z%u/x%lu/y%lu-%s.tile" in store_source
    assert "map_tile_cache_canary" in store_source
    assert "d1l_rp2040_bridge_file_write(result.tmp_path" in store_source
    assert "d1l_rp2040_bridge_file_read(result.tmp_path" in store_source
    assert "d1l_rp2040_bridge_file_rename(result.tmp_path, result.path, true" in store_source
    assert "d1l_rp2040_bridge_file_stat(result.path" in store_source
    assert "D1L_RP2040_SD_" + "FORMAT_CONFIRMATION" not in store_source
    assert "cmd_storage_map_tile_canary" in console
    assert "cmd_storage_map_tile_check" in console
    assert "cmd_storage_map_tile_download" in console
    assert "cmd_storage_map_policy" in console
    assert "storage map-policy" in console
    assert "map_tile_policy" in console
    assert "d1l_map_tile_store_path(0U, 0U, 0U" in console
    assert "storage map-tile-canary <token>" in console
    assert "storage map-tile-check <token>" in console
    assert "storage map-tile-download <z> <x> <y> <url-template> <attribution>" in console
    assert "d1l_map_tile_store_download" in console
    assert "d1l_map_tile_store_write_canary" in console
    assert "d1l_map_tile_store_check_canary" in console
    assert "Map tile SD cache canary committed" in console
    assert "Map tile SD cache canary verified read-only" in console
    assert "storage map-tile-canary" in runner
    assert "map_tile_backend_ready" in runner
    assert "mesh send public" not in runner
    assert "setup confirm" not in runner
    assert "COM11" not in runner
    assert "COM29" not in runner
    assert "map_tile_canary_transcript" in protocol
    assert "map_tile_check_transcript" in protocol
    assert "MAP_TILE_CANARY_START_ID" in protocol
    assert "python ./scripts/sd_map_tile_canary_d1l.py --dry-run --token ci-dry-run" in workflow
    assert "python ./scripts/sd_reboot_remount_acceptance_d1l.py --dry-run --token ci-dry-run" in workflow
    assert "sd_map_tile_canary_d1l.py" in docs
    assert "esp_http_client" in cmake


def test_current_d1l_bsp_keeps_esp32_direct_sd_disabled():
    board = read("third_party/sensecap_indicator_esp32/components/bsp/src/boards/sensecap_indicator_board.c")
    bsp_sd = read("third_party/sensecap_indicator_esp32/components/bsp/src/storage/bsp_sdcard.c")
    roadmap = read("docs/ROADMAP.md")

    assert ".FUNC_SDMMC_EN =   (0)" in board
    assert ".FUNC_SDSPI_EN =       (0)" in board
    assert "return ESP_ERR_NOT_SUPPORTED" in bsp_sd
    assert ".format_if_mount_failed = false" in bsp_sd
    assert "MicroSD support is handled by the RP2040 side" in roadmap


def test_sd_validation_docs_do_not_require_public_rf_or_reserved_ports():
    validation = read("docs/D1L_SD_CARD_GUIDED_INSTALL.md")

    assert "mesh send public" not in validation
    assert "Do not use `COM11` or `COM29`" in validation
    assert "COM7" not in validation
    assert "flash_d1l" not in validation
    assert "monitor_d1l" not in validation
    assert "backup_flash" not in validation
    assert "soak_d1l.py --active-public-text" not in validation
    assert "probe_d1l_dm.py --bot-port" not in validation
    assert "COM12" in validation
    assert "COM16" in validation


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
