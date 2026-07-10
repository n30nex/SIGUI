from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_connectivity_manager_keeps_companion_radios_default_off_and_persistent():
    header = read("main/comms/connectivity_manager.h")
    source = read("main/comms/connectivity_manager.c")
    cmake = read("main/CMakeLists.txt")
    defaults = read("sdkconfig.defaults")
    assert "d1l_connectivity_status_t" in header
    assert "d1l_connectivity_set_wifi_enabled" in header
    assert "d1l_connectivity_wifi_scan" in header
    assert "d1l_connectivity_wifi_connect" in header
    assert "d1l_connectivity_wifi_disconnect" in header
    assert "D1L_WIFI_SCAN_MAX" in header
    assert "d1l_connectivity_set_ble_enabled" in header
    assert "d1l_connectivity_save_wifi_profile" in header
    assert "d1l_connectivity_clear_wifi_profile" in header
    assert "wifi_profile_saved" in header
    assert "wifi_password_saved" in header
    assert "wifi_ssid" in header
    assert '"offline_first_one_companion_radio"' in source
    assert "settings.wifi_enabled = true" in source
    assert "settings.ble_companion_enabled = enabled" in source
    assert "if (enabled && !build_wifi_enabled())" in source
    assert "if (enabled && !build_ble_enabled())" in source
    assert "settings.ble_companion_enabled = false" in source
    assert "settings.wifi_enabled = false" in source
    assert "configured_pending_stack" in source
    assert "profile_required" in source
    assert "connected" in source
    assert "connecting" in source
    assert "pairing_pending_stack" in source
    assert "d1l_settings_save_wifi_profile(ssid, password)" in source
    assert "d1l_settings_clear_wifi_profile()" in source
    assert "ESP_NETIF_DEFAULT_WIFI_STA()" in source
    assert "esp_netif_new(&netif_config)" in source
    assert "esp_netif_attach_wifi_station" in source
    assert "esp_wifi_set_default_wifi_sta_handlers" in source
    assert "esp_netif_create_default_wifi_sta" not in source
    assert "esp_wifi_set_mode(WIFI_MODE_STA)" in source
    assert "esp_wifi_scan_start(NULL, true)" in source
    assert "esp_wifi_scan_get_ap_records" in source
    assert "esp_wifi_set_config(WIFI_IF_STA, &config)" in source
    assert "copy_wifi_config_field(config.sta.ssid" in source
    assert "copy_wifi_config_field(config.sta.password" in source
    assert "snprintf((char *)config.sta.ssid" not in source
    assert "snprintf((char *)config.sta.password" not in source
    assert "esp_wifi_connect()" in source
    assert "WIFI_STORAGE_RAM" in source
    assert "ESP_ERR_NOT_SUPPORTED" in source
    assert "CONFIG_ESP_WIFI_ENABLED" in source
    assert "CONFIG_BT_ENABLED" in source
    assert "esp_wifi" in cmake
    assert "esp_netif" in cmake
    assert "esp_event" in cmake
    assert '"comms/connectivity_manager.c"' in cmake
    assert "CONFIG_ESP_WIFI_ENABLED=y" in defaults
    assert "CONFIG_BT_ENABLED=n" in defaults
    assert "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y" in defaults
    assert "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL=y" in defaults

    connect = source[
        source.index("esp_err_t d1l_connectivity_wifi_connect"):
        source.index("esp_err_t d1l_connectivity_wifi_disconnect")
    ]
    assert connect.index("esp_wifi_disconnect()") < connect.index("esp_wifi_set_config(WIFI_IF_STA, &config)")


def test_wifi_memory_policy_and_failed_start_are_fail_closed():
    source = read("main/comms/connectivity_manager.c")
    app_main = read("main/app_main.c")
    defaults = read("sdkconfig.defaults")

    for setting in [
        "CONFIG_SPIRAM_USE_MALLOC=y",
        "CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=1024",
        "CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y",
        "CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=65536",
        "CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y",
        "CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=6",
        "CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=32",
        "CONFIG_ESP_WIFI_STATIC_TX_BUFFER=y",
        "CONFIG_ESP_WIFI_STATIC_TX_BUFFER_NUM=6",
        "CONFIG_ESP_WIFI_RX_BA_WIN=6",
        "# CONFIG_ESP_WIFI_IRAM_OPT is not set",
        "# CONFIG_ESP_WIFI_RX_IRAM_OPT is not set",
    ]:
        assert setting in defaults

    assert "fail_closed_wifi_runtime" in source
    assert "safe_settings = *current" in source
    assert "safe_settings.wifi_enabled = false" in source
    rollback = source[source.index("fail_closed_wifi_runtime"):source.index("wifi_boot_recovery_reason")]
    assert "d1l_settings_clear_wifi_profile" not in rollback
    assert "rollback_save_failed" in source
    assert "boot_recovery_panic" in source
    assert "boot_recovery_wdt" in source
    assert "boot_recovery_brownout" in source
    assert "esp_event_handler_instance_unregister" in source
    assert "esp_wifi_deinit()" in source
    assert "esp_netif_destroy_default_wifi" in source
    teardown = source[source.index("static void stop_wifi_runtime"):source.index("static esp_err_t fail_closed_wifi_runtime")]
    initialized_cleanup = teardown[teardown.index("if (s_wifi_initialized)"):]
    assert initialized_cleanup.index("esp_wifi_stop()") < initialized_cleanup.index("esp_wifi_deinit()")

    enable = source[
        source.index("esp_err_t d1l_connectivity_set_wifi_enabled"):
        source.index("esp_err_t d1l_connectivity_set_ble_enabled")
    ]
    assert enable.index("ensure_wifi_started()") < enable.index("settings.wifi_enabled = true")
    assert app_main.index("d1l_board_init()") < app_main.index("d1l_connectivity_init()")
    assert app_main.index("d1l_connectivity_init()") < app_main.index("d1l_ui_phase1_start()")


def test_console_reports_wifi_ble_status_scan_and_connect_without_password_echo():
    console = read("main/comms/usb_console.c")
    assert "cmd_wifi_status" in console
    assert "cmd_wifi_scan" in console
    assert "cmd_wifi_connect" in console
    assert "cmd_wifi_save" in console
    assert "cmd_wifi_clear" in console
    assert "cmd_ble_status" in console
    assert "WIFI_BUILD_DISABLED" in console
    assert "BLE_BUILD_DISABLED" in console
    assert "BLE_RUNTIME_PENDING" in console
    assert '\\"profile_saved\\":%s' in console
    assert '\\"password_saved\\":%s' in console
    assert "wifi save <ssid> [password]" in console
    assert "wifi connect" in console
    assert "password is not printed" in console
    assert "d1l_connectivity_wifi_scan(&scan)" in console
    assert "d1l_connectivity_wifi_connect()" in console
    assert '\\"networks\\":[' in console
    assert '\\"password_printed\\":false' in console
    assert '\\"connected\\":%s' in console
    assert '\\"ip\\":' in console
    assert '\\"coexistence_policy\\"' in console
