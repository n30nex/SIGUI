from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_connectivity_manager_keeps_companion_radios_default_off_and_persistent():
    header = read("main/comms/connectivity_manager.h")
    source = read("main/comms/connectivity_manager.c")
    policy_header = read("main/comms/wifi_retry_policy.h")
    policy_source = read("main/comms/wifi_retry_policy.c")
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
    assert "d1l_connectivity_prepare_reboot" in header
    assert "wifi_profile_saved" in header
    assert "wifi_password_saved" in header
    assert "wifi_ssid" in header
    assert "wifi_retry_scheduled" in header
    assert "wifi_retry_attempt" in header
    assert "wifi_retry_delay_ms" in header
    assert "wifi_retry_task_stack_high_water_bytes" in header
    assert "wifi_last_disconnect_reason" in header
    assert "wifi_last_failure_class" in header
    assert '"offline_first_one_companion_radio"' in source
    assert "settings.wifi_enabled = true" in source
    assert "settings.ble_companion_enabled = enabled" in source
    assert "if (enabled && !build_wifi_enabled())" in source
    assert "if (enabled && !build_ble_enabled())" in source
    assert "settings.ble_companion_enabled = false" in source
    assert "settings.wifi_enabled = false" in source
    for state in [
        "off",
        "profile_required",
        "starting",
        "scanning",
        "connecting",
        "connected",
        "auth_failed",
        "ap_unavailable",
        "retry_backoff",
        "safe_mode_disabled",
    ]:
        assert f'"{state}"' in policy_source
    assert "D1L_WIFI_RETRY_BASE_DELAY_MS 1000U" in policy_header
    assert "D1L_WIFI_RETRY_MAX_DELAY_MS 30000U" in policy_header
    assert "D1L_WIFI_RETRY_MAX_WINDOW_MS 300000U" in policy_header
    assert "D1L_WIFI_RETRY_MAX_ATTEMPTS 8U" in policy_header
    assert "D1L_WIFI_CONTROL_LOCK_TIMEOUT_MS 10000U" in source
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
    assert '"comms/wifi_retry_policy.c"' in cmake
    assert "CONFIG_ESP_WIFI_ENABLED=y" in defaults
    assert "CONFIG_BT_ENABLED=n" in defaults
    assert "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y" in defaults
    assert "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL=y" in defaults

    reset = source[
        source.index("esp_err_t d1l_connectivity_prepare_reboot"):
        source.index("esp_err_t d1l_connectivity_init")
    ]
    assert "if (s_wifi_initialized)" in reset
    assert "esp_wifi_stop()" in reset
    assert "return ret" in reset
    assert "s_wifi_started = false" in reset
    assert "s_wifi_connected = false" in reset
    assert "s_wifi_connecting = false" in reset
    assert "s_wifi_ip[0] = '\\0'" in reset
    assert "return ESP_OK" in reset
    assert "d1l_settings" not in reset
    assert "esp_wifi_deinit" not in reset

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
    boot_recovery = source[
        source.index("esp_err_t d1l_connectivity_init"):
        source.index("void d1l_connectivity_status")
    ]
    assert "d1l_wifi_retry_policy_enter_safe_mode" in boot_recovery
    assert "Persisted repeated-crash counting" in boot_recovery
    assert "last-enabled-subsystem attribution remain a later WP-13" in boot_recovery
    assert "safe_settings" not in boot_recovery
    assert "d1l_settings_save" not in boot_recovery.split("#ifdef CONFIG_ESP_WIFI_ENABLED", 1)[1]
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


def test_disconnect_handler_classifies_then_defers_bounded_retry_to_worker():
    source = read("main/comms/connectivity_manager.c")
    policy = read("main/comms/wifi_retry_policy.c")

    handler = source[
        source.index("static void wifi_event_handler"):
        source.index("static void stop_wifi_runtime")
    ]
    disconnect = handler[
        handler.index("WIFI_EVENT_STA_DISCONNECTED"):
        handler.index("WIFI_EVENT_STA_START")
    ]
    assert "classify_disconnect_reason" in disconnect
    assert "d1l_wifi_retry_policy_on_disconnect" in disconnect
    assert "notify_wifi_retry_worker" in disconnect
    assert "esp_wifi_connect()" not in disconnect
    assert "vTaskDelay" not in disconnect
    assert "ulTaskNotifyTake" not in disconnect

    worker = source[
        source.index("static void wifi_retry_worker"):
        source.index("static esp_err_t ensure_wifi_retry_worker")
    ]
    assert "ulTaskNotifyTake" in worker
    assert "d1l_wifi_retry_policy_begin_due_retry" in worker
    assert "esp_wifi_connect()" in worker
    assert "D1L_WIFI_FAILURE_TRANSIENT" in worker
    assert worker.index("take_wifi_control()") < worker.index("esp_wifi_connect()")
    control_lock = source[
        source.index("static bool take_wifi_control"):
        source.index("static void give_wifi_control")
    ]
    assert "D1L_WIFI_CONTROL_LOCK_TIMEOUT_MS" in control_lock
    assert "portMAX_DELAY" not in control_lock

    for reason in [
        "WIFI_REASON_AUTH_FAIL",
        "WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT",
        "WIFI_REASON_HANDSHAKE_TIMEOUT",
        "WIFI_REASON_NO_AP_FOUND",
        "WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD",
    ]:
        assert reason in source
    assert "D1L_WIFI_RUNTIME_AUTH_FAILED" in policy
    assert "D1L_WIFI_RUNTIME_AP_UNAVAILABLE" in policy
    assert "D1L_WIFI_RUNTIME_RETRY_BACKOFF" in policy


def test_user_cancel_transient_retry_and_credentials_are_explicitly_bounded():
    source = read("main/comms/connectivity_manager.c")
    policy = read("main/comms/wifi_retry_policy.c")

    disconnect = source[
        source.index("esp_err_t d1l_connectivity_wifi_disconnect"):
        source.index("esp_err_t d1l_connectivity_set_wifi_enabled")
    ]
    assert "d1l_wifi_retry_policy_cancel" in disconnect
    assert disconnect.index("d1l_wifi_retry_policy_cancel") < disconnect.index(
        "esp_wifi_disconnect()"
    )
    assert disconnect.index("take_wifi_control()") < disconnect.index(
        "esp_wifi_disconnect()"
    )
    assert 'set_wifi_last_error("cancelled")' in disconnect

    runtime_failure = source[
        source.index("static esp_err_t fail_closed_wifi_runtime"):
        source.index("static const char *wifi_boot_recovery_reason")
    ]
    assert "safe_settings.wifi_enabled = false" in runtime_failure
    handler = source[
        source.index("static void wifi_event_handler"):
        source.index("static void stop_wifi_runtime")
    ]
    assert "safe_settings.wifi_enabled = false" not in handler
    assert "d1l_settings_save" not in handler

    connect = source[
        source.index("esp_err_t d1l_connectivity_wifi_connect"):
        source.index("esp_err_t d1l_connectivity_wifi_disconnect")
    ]
    assert "memset(&config, 0, sizeof(config))" in connect
    assert "d1l_wifi_retry_policy_begin_manual_connect" in connect
    assert "d1l_wifi_retry_policy_on_disconnect" in connect
    assert "wifi_password" not in policy
    assert "s_wifi_suppress_disconnect_retry" not in source
    assert "s_wifi_local_leave_token" in source
    assert "arm_local_leave_token()" in connect
    assert "generation" not in connect
    consume = source[
        source.index("static bool consume_expected_local_leave"):
        source.index("static bool take_wifi_control")
    ]
    assert "d1l_wifi_local_leave_token_consume" in consume
    assert "reason == WIFI_REASON_ASSOC_LEAVE" in consume
    disconnect_event = source[
        source.index("WIFI_EVENT_STA_DISCONNECTED"):
        source.index("WIFI_EVENT_STA_START")
    ]
    assert "consume_expected_local_leave(reason)" in disconnect_event
    assert disconnect_event.index("if (suppress_retry)") < disconnect_event.index(
        "d1l_wifi_retry_policy_on_disconnect"
    )
    after_connect = connect.split("ret = esp_wifi_connect();", 1)[1]
    before_failure = after_connect.split("if (ret != ESP_OK)", 1)[0]
    failure_branch = after_connect.split("if (ret != ESP_OK)", 1)[1]
    assert "clear_local_leave_token()" not in before_failure
    assert "clear_local_leave_token()" in failure_branch.split(
        "d1l_wifi_retry_policy_on_disconnect", 1
    )[0]
    got_ip = source[
        source.index("IP_EVENT_STA_GOT_IP"):
        source.index("static void stop_wifi_runtime")
    ]
    assert "clear_local_leave_token()" in got_ip
    assert "connection_accepted" in got_ip
    assert got_ip.index("d1l_wifi_retry_policy_connected") < got_ip.index(
        "s_wifi_connected = true"
    )
    assert "if (!connection_accepted)" in got_ip
    assert "s_wifi_ip[0] = '\\0'" in got_ip
    assert "Only an ASSOC_LEAVE event may consume it as intentional" in connect


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
    for telemetry in [
        "retry_scheduled",
        "retry_attempt",
        "retry_delay_ms",
        "user_cancelled",
        "safe_mode",
        "last_disconnect_reason",
        "last_failure_class",
        "retry_task_stack_high_water_bytes",
    ]:
        assert f'\\"{telemetry}\\"' in console
    assert '\\"coexistence_policy\\"' in console
