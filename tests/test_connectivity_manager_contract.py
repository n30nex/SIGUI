from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_connectivity_manager_keeps_companion_radios_default_off_and_persistent():
    header = read("main/comms/connectivity_manager.h")
    source = read("main/comms/connectivity_manager.c")
    cmake = read("main/CMakeLists.txt")
    assert "d1l_connectivity_status_t" in header
    assert "d1l_connectivity_set_wifi_enabled" in header
    assert "d1l_connectivity_set_ble_enabled" in header
    assert "d1l_connectivity_save_wifi_profile" in header
    assert "d1l_connectivity_clear_wifi_profile" in header
    assert "wifi_profile_saved" in header
    assert "wifi_password_saved" in header
    assert "wifi_ssid" in header
    assert '"offline_first_one_companion_radio"' in source
    assert "settings.wifi_enabled = enabled" in source
    assert "settings.ble_companion_enabled = enabled" in source
    assert "if (enabled && !build_wifi_enabled())" in source
    assert "if (enabled && !build_ble_enabled())" in source
    assert "settings.ble_companion_enabled = false" in source
    assert "settings.wifi_enabled = false" in source
    assert "configured_pending_stack" in source
    assert "profile_required" in source
    assert "pairing_pending_stack" in source
    assert "d1l_settings_save_wifi_profile(ssid, password)" in source
    assert "d1l_settings_clear_wifi_profile()" in source
    assert "ESP_ERR_NOT_SUPPORTED" in source
    assert "CONFIG_ESP_WIFI_ENABLED" in source
    assert "CONFIG_BT_ENABLED" in source
    assert '"comms/connectivity_manager.c"' in cmake


def test_console_reports_wifi_ble_status_without_enabling_stacks():
    console = read("main/comms/usb_console.c")
    assert "cmd_wifi_status" in console
    assert "cmd_wifi_scan" in console
    assert "cmd_wifi_save" in console
    assert "cmd_wifi_clear" in console
    assert "cmd_ble_status" in console
    assert "WIFI_BUILD_DISABLED" in console
    assert "WIFI_RUNTIME_PENDING" in console
    assert "BLE_BUILD_DISABLED" in console
    assert "BLE_RUNTIME_PENDING" in console
    assert '\\"profile_saved\\":%s' in console
    assert '\\"password_saved\\":%s' in console
    assert "wifi save <ssid> [password]" in console
    assert "password is not printed" in console
    assert '\\"scan_started\\":false' in console
    assert '\\"networks\\":[]' in console
    assert '\\"coexistence_policy\\"' in console
