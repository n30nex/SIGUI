from pathlib import Path

from scripts.smoke_d1l import SMOKE_COMMANDS


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_settings_model_defaults_and_nvs_contract():
    header = read("main/app/settings_model.h")
    source = read("main/app/settings_model.c")
    time_service = read("main/platform/time_service.c")
    assert "D1L_SETTINGS_SCHEMA_VERSION 7U" in header
    assert "D1L_WIFI_SSID_LEN 33U" in header
    assert "D1L_WIFI_PASSWORD_LEN 65U" in header
    assert "wifi_profile_saved" in header
    assert "wifi_ssid" in header
    assert "wifi_password" in header
    assert "identity_public_key" in header
    assert "identity_private_key" in header
    assert "onboarding_complete" in header
    assert "map_location_set" in header
    assert "map_tile_provider_saved" not in header
    assert "map_tile_url_template" not in header
    assert "map_tile_attribution" not in header
    assert "map_tile_zoom" in header
    assert "D1L_MAP_TILE_DEFAULT_ZOOM 10U" in header
    assert "map_lat_e7" in header
    assert "map_lon_e7" in header
    assert "D1L_MAP_LOCATION_LAT_E7_MIN" in header
    assert "D1L_MAP_LOCATION_LON_E7_MAX" in header
    assert "d1l_settings_complete_onboarding" in header
    assert "d1l_settings_reset_onboarding" in header
    assert "d1l_settings_next_mesh_timestamp" in header
    assert "d1l_settings_load_status" in header
    assert "d1l_settings_v2_t" in source
    assert "d1l_settings_v3_t" in source
    assert "d1l_settings_v4_t" in source
    assert "d1l_settings_v5_t" in source
    assert "d1l_settings_v6_t" in source
    assert "migrate_v2_settings" in source
    assert "migrate_v3_settings" in source
    assert "migrate_v4_settings" in source
    assert "migrate_v5_settings" in source
    assert "migrate_v6_settings" in source
    assert "switch (loaded.schema_version)" in source
    assert "case D1L_SETTINGS_SCHEMA_VERSION:" in source
    assert "case 5U:" in source
    assert "Unknown same-size blobs are never sanitized as v7" in source
    assert "d1l_settings_save_wifi_profile" in header
    assert "d1l_settings_clear_wifi_profile" in header
    assert "strlen(ssid) >= D1L_WIFI_SSID_LEN" in source
    assert "settings.wifi_profile_saved = true" in source
    assert "settings.wifi_enabled = false" in source
    assert "dest->map_location_set = false" in source
    assert "dest->onboarding_complete = true" in source
    assert 'D1L_SETTINGS_NAMESPACE "d1l_settings"' in source
    assert 'D1L_SETTINGS_KEY "settings"' in source
    assert 'D1L_TIME_PROTOCOL_LEGACY_KEY "mesh_ts"' in time_service
    assert 'D1L_TIME_PROTOCOL_HIGH_WATER_KEY "mesh_hi_v2"' in time_service
    assert "d1l_settings_next_mesh_timestamp" not in source
    assert "d1l_settings_next_mesh_timestamp" in time_service
    assert "d1l_time_service_next_protocol_timestamp(timestamp)" in time_service
    assert "mesh_timestamp_can_fallback" not in source
    assert "next_ram_mesh_timestamp" not in source
    assert 'snprintf(settings->node_name, sizeof(settings->node_name), "D1L Desk")' in source
    assert "settings->wifi_enabled = false" in source
    assert "settings->ble_companion_enabled = false" in source
    assert "settings->observer_enabled = false" in source
    assert "settings->onboarding_complete = false" in source
    assert "settings->wifi_profile_saved = false" in source
    assert "settings->wifi_ssid[0] = '\\0'" in source
    assert "settings->wifi_password[0] = '\\0'" in source
    assert "settings->map_location_set = false" in source
    assert "settings->map_lat_e7 = 0" in source
    assert "settings->map_lon_e7 = 0" in source
    assert "settings->map_tile_zoom = D1L_MAP_TILE_DEFAULT_ZOOM" in source
    assert "settings->map_tile_zoom = D1L_MAP_TILE_DEFAULT_ZOOM;" in source
    assert "settings->path_hash_bytes = 1" in source
    assert "settings->frequency_hz = D1L_RADIO_FREQ_HZ" in source
    assert "settings->tcxo_mode = D1L_TCXO_NONE" in source
    assert "settings->identity_ready = false" in source
    assert "static esp_err_t s_load_status = ESP_ERR_INVALID_STATE" in source
    load = source.split("esp_err_t d1l_settings_load(void)", 1)[1].split(
        "esp_err_t d1l_settings_save", 1
    )[0]
    assert "s_load_status = ret;" in load
    save = source.split("esp_err_t d1l_settings_save", 1)[1].split(
        "esp_err_t d1l_settings_reset", 1
    )[0]
    assert "s_load_status" not in save


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
        "wifi save",
        "wifi connect",
        "wifi clear",
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
    assert "int ch = fgetc(stdin)" in console
    assert "line[used++] = (char)ch" in console
    assert "LINE_TOO_LONG" in console
    assert "handle_line(line)" in console
    assert '\\"expected\\":[\\"0x20\\",\\"0x48\\"]' in console
    assert "stored_nvs_ed25519" in console
    assert "d1l_connectivity_set_wifi_enabled(false)" in console
    assert "d1l_connectivity_set_ble_enabled(false)" in console
    assert "d1l_connectivity_save_wifi_profile(ssid, password_to_save)" in console
    assert "d1l_connectivity_clear_wifi_profile()" in console
    assert "password is not printed" in console
    assert "WIFI_DISABLED" in console
    assert '\\"radio\\":{\\"frequency_hz\\":%lu' in console
    assert '\\"applied_to_radio\\":%s' in console
    assert '\\"radio_apply_pending\\":%s' in console
    assert '\\"radio_apply_error\\":\\"%s\\"' in console
    assert '\\"applied_to_radio\\":false' not in console
    assert "cmd_radio_set_txpower" in console
    assert "cmd_radio_set_rxboost" in console
    assert "settings.tx_power_dbm = (int8_t)tx_power" in console
    assert "settings.rx_boost = enabled" in console
    assert "parse_coord_e7" in console
    assert "strtod(text, &end)" in console
    assert '\\"map_location\\"' in console
    assert '\\"map_tiles\\"' in console
    assert '\\"source\\":' in console
    assert "D1L_MAP_TILE_SOURCE_ID" in console
    assert "D1L_MAP_TILE_SOURCE_URL_TEMPLATE" in console
    assert "settings->map_tile_provider_saved" not in console
    assert "provider_saved" not in console
    assert "D1L_MAP_TILE_PROVIDER_POLICY" in console
    assert "print_map_location_result" in console
    assert "d1l_app_model_set_map_location(lat_e7, lon_e7)" in console
    assert "d1l_app_model_clear_map_location()" in console


def test_v5_and_v6_migrations_preserve_wifi_location_and_identity():
    source = read("main/app/settings_model.c")

    v5 = source.split("static void migrate_v5_settings", 1)[1].split(
        "static void migrate_v6_settings", 1
    )[0]
    v6 = source.split("static void migrate_v6_settings", 1)[1].split(
        "static void migrate_v4_settings", 1
    )[0]
    for migration in (v5, v6):
        assert "dest->wifi_enabled = src->wifi_enabled" in migration
        assert "dest->wifi_profile_saved = src->wifi_profile_saved" in migration
        assert "memcpy(dest->wifi_ssid, src->wifi_ssid" in migration
        assert "memcpy(dest->wifi_password, src->wifi_password" in migration
        assert "dest->map_location_set = src->map_location_set" in migration
        assert "dest->map_lat_e7 = src->map_lat_e7" in migration
        assert "dest->map_lon_e7 = src->map_lon_e7" in migration
        assert "dest->identity_ready = src->identity_ready" in migration
        assert "memcpy(dest->identity_public_key, src->identity_public_key" in migration
        assert "memcpy(dest->identity_private_key, src->identity_private_key" in migration
        assert "dest->map_tile_zoom = D1L_MAP_TILE_DEFAULT_ZOOM" in migration

    load = source.split("esp_err_t d1l_settings_load(void)", 1)[1].split(
        "esp_err_t d1l_settings_save", 1
    )[0]
    same_size = load.split("len == sizeof(s_current)", 1)[1].split(
        "len == sizeof(d1l_settings_v6_t)", 1
    )[0]
    assert "switch (loaded.schema_version)" in same_size
    assert "case 5U:" in same_size
    assert "migrate_v5_settings(&s_current, &old_v5)" in same_size
    v6_blob = load.split("len == sizeof(d1l_settings_v6_t)", 1)[1].split(
        "len == sizeof(d1l_settings_v5_t)", 1
    )[0]
    assert "migrate_v6_settings(&s_current, &old_settings)" in v6_blob


def test_smoke_includes_settings_identity_and_mesh_status():
    assert "settings get" in SMOKE_COMMANDS
    assert "identity status" in SMOKE_COMMANDS
    assert "mesh status" in SMOKE_COMMANDS
    assert "map center" in SMOKE_COMMANDS
    assert "settings onboarding status" in SMOKE_COMMANDS
    assert "wifi status" in SMOKE_COMMANDS
    assert "wifi scan" in SMOKE_COMMANDS
    assert "ble status" in SMOKE_COMMANDS
