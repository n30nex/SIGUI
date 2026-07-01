from pathlib import Path

from scripts.smoke_d1l import SMOKE_COMMANDS


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_settings_model_defaults_and_nvs_contract():
    header = read("main/app/settings_model.h")
    source = read("main/app/settings_model.c")
    assert "D1L_SETTINGS_SCHEMA_VERSION 4U" in header
    assert "identity_public_key" in header
    assert "identity_private_key" in header
    assert "onboarding_complete" in header
    assert "map_location_set" in header
    assert "map_lat_e7" in header
    assert "map_lon_e7" in header
    assert "D1L_MAP_LOCATION_LAT_E7_MIN" in header
    assert "D1L_MAP_LOCATION_LON_E7_MAX" in header
    assert "d1l_settings_complete_onboarding" in header
    assert "d1l_settings_reset_onboarding" in header
    assert "d1l_settings_next_mesh_timestamp" in header
    assert "d1l_settings_v2_t" in source
    assert "d1l_settings_v3_t" in source
    assert "migrate_v2_settings" in source
    assert "migrate_v3_settings" in source
    assert "if (loaded_schema == 3U)" in source
    assert "if (loaded_schema == 2U)" in source
    assert "dest->map_location_set = false" in source
    assert "dest->onboarding_complete = true" in source
    assert 'D1L_SETTINGS_NAMESPACE "d1l_settings"' in source
    assert 'D1L_SETTINGS_KEY "settings"' in source
    assert 'D1L_SETTINGS_MESH_TIMESTAMP_KEY "mesh_ts"' in source
    assert 'snprintf(settings->node_name, sizeof(settings->node_name), "D1L Desk")' in source
    assert "settings->wifi_enabled = false" in source
    assert "settings->ble_companion_enabled = false" in source
    assert "settings->observer_enabled = false" in source
    assert "settings->onboarding_complete = false" in source
    assert "settings->map_location_set = false" in source
    assert "settings->map_lat_e7 = 0" in source
    assert "settings->map_lon_e7 = 0" in source
    assert "settings->path_hash_bytes = 1" in source
    assert "settings->frequency_hz = D1L_RADIO_FREQ_HZ" in source
    assert "settings->tcxo_mode = D1L_TCXO_NONE" in source
    assert "settings->identity_ready = false" in source


def test_console_exposes_phase2_foundation_commands():
    console = read("main/comms/usb_console.c")
    for command in [
        "settings get",
        "settings reset",
        "settings set name",
        "settings set pathhash",
        "settings set location",
        "settings clear location",
        "map center",
        "map center set",
        "map center clear",
        "settings onboarding status",
        "settings onboarding complete",
        "settings onboarding reset",
        "identity status",
        "radio set freq",
        "radio set bw",
        "radio set sf",
        "radio set cr",
        "radio set txpower",
        "radio set rxboost",
        "mesh advert flood",
        "wifi status",
        "wifi off",
        "crashlog",
        "ble status",
        'strcmp(line, "health")',
    ]:
        assert command in console

    assert "arg_len >= D1L_NODE_NAME_LEN" in console
    assert "memcpy(settings.node_name, arg, arg_len + 1U)" in console
    assert '\\"onboarding_complete\\":%s' in console
    assert "d1l_settings_complete_onboarding(arg, false, false, false)" in console
    assert "bool prompt_pending = true" in console
    assert "clearerr(stdin)" in console
    assert "vTaskDelay(pdMS_TO_TICKS(20))" in console
    assert 'printf("\\n");\n        handle_line(line);' in console
    assert '\\"expected\\":[\\"0x20\\",\\"0x48\\"]' in console
    assert "stored_nvs_ed25519" in console
    assert "d1l_connectivity_set_wifi_enabled(false)" in console
    assert "d1l_connectivity_set_ble_enabled(false)" in console
    assert "disabled_by_setting" in console
    assert '\\"radio\\":{\\"frequency_hz\\":%lu' in console
    assert "cmd_radio_set_txpower" in console
    assert "cmd_radio_set_rxboost" in console
    assert "settings.tx_power_dbm = (int8_t)tx_power" in console
    assert "settings.rx_boost = enabled" in console
    assert "parse_coord_e7" in console
    assert "strtod(text, &end)" in console
    assert '\\"map_location\\"' in console
    assert "print_map_location_result" in console
    assert "d1l_app_model_set_map_location(lat_e7, lon_e7)" in console
    assert "d1l_app_model_clear_map_location()" in console


def test_smoke_includes_settings_identity_and_mesh_status():
    assert "settings get" in SMOKE_COMMANDS
    assert "identity status" in SMOKE_COMMANDS
    assert "mesh status" in SMOKE_COMMANDS
    assert "map center" in SMOKE_COMMANDS
    assert "settings onboarding status" in SMOKE_COMMANDS
    assert "wifi status" in SMOKE_COMMANDS
    assert "wifi scan" in SMOKE_COMMANDS
    assert "ble status" in SMOKE_COMMANDS
