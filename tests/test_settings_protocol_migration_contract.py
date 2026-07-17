from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def test_migration_is_explicit_domain_bound_and_ordered():
    header = read("main/app/settings_protocol_migration.h")
    source = read("main/app/settings_protocol_migration.c")
    cmake = read("main/CMakeLists.txt")
    assert 'D1L_TIME_PROTOCOL_LEGACY_KEY "mesh_ts"' in header
    assert 'D1L_TIME_PROTOCOL_HIGH_WATER_KEY "mesh_hi_v2"' in header
    assert "D1L_TIME_PROTOCOL_MIGRATION_SCHEMA_VERSION" in header
    assert "D1L_TIME_PROTOCOL_MIGRATION_DOMAIN_MAGIC" in header
    assert "D1L_TIME_PROTOCOL_MIGRATION_CONFIRMATION" in header
    assert "D1L_TIME_PROTOCOL_MIGRATION_MAX_RECEIPT_SIZE" in header
    assert '"app/settings_protocol_migration.c"' in cmake
    run = source.split("esp_err_t d1l_time_protocol_migration_run", 1)[1].split(
        "const char *d1l_time_protocol_migration_state_name", 1
    )[0]
    assert run.index("D1L_TIME_PROTOCOL_MIGRATION_PHASE_INTENT") < run.index(
        "write_high_water(confirmed_upper_bound)"
    )
    assert run.index("write_high_water(confirmed_upper_bound)") < run.index(
        "erase_legacy()"
    )
    assert run.index("erase_legacy()") < run.rindex(
        "D1L_TIME_PROTOCOL_MIGRATION_PHASE_COMPLETE"
    )
    assert "d1l_settings_envelope_build" in source
    assert "d1l_settings_envelope_validate" in source
    assert "extended_schema_version" in source
    assert "D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_NEWER_SCHEMA" in source
    assert "memset(blob, 0, sizeof(blob))" in source


def test_usb_status_is_fail_closed_and_does_not_log_supplied_confirmation():
    console = read("main/comms/usb_console.c")
    service = read("main/platform/time_service.c")
    assert '"time migration status"' in console
    assert '"time migrate-legacy "' in console
    assert '\\"supplied_confirmation_logged\\":false' in console
    assert '\\"settings_reset_preserves_protocol_migration\\":true' in console
    assert '\\"factory_reset_supported\\":true' in console
    assert "wipe_console_bytes(line, sizeof(line));" in console
    normal_console = console.split("void d1l_usb_console_run(void)", 1)[1].split(
        "static void factory_reset_recovery_status", 1
    )[0]
    admission = normal_console.index("d1l_usb_command_admit_in_place(")
    dispatch = normal_console.index("handle_line(&command);")
    wipe = normal_console.index(
        "wipe_console_bytes(line, sizeof(line));", dispatch
    )
    assert "const size_t received_length = used;" in normal_console
    assert "line, received_length, sizeof(line), &command" in normal_console
    assert admission < dispatch < wipe
    assert "wall_time_inferred" in console
    assert "mesh_ts is only a predecessor lower bound" in service
    assert "d1l_time_protocol_migration_inspect" in service


def test_usb_success_uses_live_tx_status_and_failure_status_is_initialized():
    console = read("main/comms/usb_console.c")
    service = read("main/platform/time_service.c")
    command = console.split("static void cmd_time_migrate_legacy", 1)[1].split(
        "static void cmd_board", 1
    )[0]
    assert "d1l_time_service_status(&time_status);" in command
    assert "time_protocol_tx_block(&time_status)" in command
    assert '\\"protocol_tx_unblocked\\":true' not in command
    assert "bool_json(time_status.protocol_tx_ready)" in command
    assert "d1l_time_protocol_migration_status_t status = {" in command
    api = service.split(
        "esp_err_t d1l_time_service_migrate_legacy_protocol_timestamp", 1
    )[1].split("void d1l_time_service_status", 1)[0]
    assert "if (out_status)" in api
    assert "D1L_TIME_PROTOCOL_MIGRATION_UNINITIALIZED" in api
    assert "out_status->error = init_ret" in api
