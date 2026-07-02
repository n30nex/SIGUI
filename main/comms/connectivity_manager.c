#include "connectivity_manager.h"

#include <stdio.h>
#include <string.h>

#include "sdkconfig.h"

#ifdef CONFIG_ESP_WIFI_ENABLED
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_wifi.h"
#endif

static bool s_wifi_started;
static bool s_wifi_connected;
static bool s_wifi_connecting;
static char s_wifi_ip[16] = "";
static char s_wifi_last_error[32] = "none";

#ifdef CONFIG_ESP_WIFI_ENABLED
static bool s_wifi_initialized;
static bool s_wifi_handlers_registered;
static esp_netif_t *s_wifi_sta_netif;
#endif

static bool build_wifi_enabled(void)
{
#ifdef CONFIG_ESP_WIFI_ENABLED
    return true;
#else
    return false;
#endif
}

static bool build_ble_enabled(void)
{
#ifdef CONFIG_BT_ENABLED
    return true;
#else
    return false;
#endif
}

static const char *wifi_runtime_state(const d1l_settings_t *settings, bool build_enabled)
{
    if (!settings || !settings->wifi_enabled) {
        return "off";
    }
    if (!build_enabled) {
        return "build_disabled";
    }
    if (s_wifi_connected) {
        return "connected";
    }
    if (s_wifi_connecting) {
        return "connecting";
    }
    if (!settings->wifi_profile_saved) {
        return "profile_required";
    }
    return s_wifi_started ? "configured" : "configured_pending_stack";
}

static const char *ble_runtime_state(bool desired, bool build_enabled)
{
    if (!desired) {
        return "off";
    }
    if (!build_enabled) {
        return "build_disabled";
    }
    return "pairing_pending_stack";
}

static void set_wifi_last_error(const char *reason)
{
    snprintf(s_wifi_last_error, sizeof(s_wifi_last_error), "%s",
             reason ? reason : "unknown");
}

#ifdef CONFIG_ESP_WIFI_ENABLED
static esp_err_t ok_if_invalid_state(esp_err_t ret)
{
    return ret == ESP_ERR_INVALID_STATE ? ESP_OK : ret;
}

static const char *wifi_auth_name(wifi_auth_mode_t auth)
{
    switch (auth) {
    case WIFI_AUTH_OPEN:
        return "open";
    case WIFI_AUTH_WEP:
        return "wep";
    case WIFI_AUTH_WPA_PSK:
        return "wpa_psk";
    case WIFI_AUTH_WPA2_PSK:
        return "wpa2_psk";
    case WIFI_AUTH_WPA_WPA2_PSK:
        return "wpa_wpa2_psk";
    case WIFI_AUTH_WPA2_ENTERPRISE:
        return "wpa2_enterprise";
    case WIFI_AUTH_WPA3_PSK:
        return "wpa3_psk";
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return "wpa2_wpa3_psk";
    case WIFI_AUTH_WAPI_PSK:
        return "wapi_psk";
    default:
        return "unknown";
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_connected = false;
        s_wifi_connecting = false;
        s_wifi_ip[0] = '\0';
        set_wifi_last_error("disconnected");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
        s_wifi_connected = true;
        s_wifi_connecting = false;
        if (event) {
            snprintf(s_wifi_ip, sizeof(s_wifi_ip), IPSTR, IP2STR(&event->ip_info.ip));
        }
        set_wifi_last_error("none");
    }
}

static esp_err_t ensure_wifi_started(void)
{
    if (!build_wifi_enabled()) {
        set_wifi_last_error("build_disabled");
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!s_wifi_initialized) {
        esp_err_t ret = ok_if_invalid_state(esp_netif_init());
        if (ret != ESP_OK) {
            set_wifi_last_error(esp_err_to_name(ret));
            return ret;
        }
        ret = ok_if_invalid_state(esp_event_loop_create_default());
        if (ret != ESP_OK) {
            set_wifi_last_error(esp_err_to_name(ret));
            return ret;
        }
        if (!s_wifi_sta_netif) {
            s_wifi_sta_netif = esp_netif_create_default_wifi_sta();
            if (!s_wifi_sta_netif) {
                set_wifi_last_error("sta_netif_failed");
                return ESP_FAIL;
            }
        }
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ret = esp_wifi_init(&cfg);
        if (ret != ESP_OK) {
            set_wifi_last_error(esp_err_to_name(ret));
            return ret;
        }
        ret = esp_wifi_set_storage(WIFI_STORAGE_RAM);
        if (ret != ESP_OK) {
            set_wifi_last_error(esp_err_to_name(ret));
            return ret;
        }
        ret = esp_wifi_set_mode(WIFI_MODE_STA);
        if (ret != ESP_OK) {
            set_wifi_last_error(esp_err_to_name(ret));
            return ret;
        }
        s_wifi_initialized = true;
    }
    if (!s_wifi_handlers_registered) {
        esp_err_t ret = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                   wifi_event_handler, NULL);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            set_wifi_last_error(esp_err_to_name(ret));
            return ret;
        }
        ret = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                         wifi_event_handler, NULL);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            set_wifi_last_error(esp_err_to_name(ret));
            return ret;
        }
        s_wifi_handlers_registered = true;
    }
    if (!s_wifi_started) {
        esp_err_t ret = esp_wifi_start();
        if (ret != ESP_OK) {
            set_wifi_last_error(esp_err_to_name(ret));
            return ret;
        }
        s_wifi_started = true;
        set_wifi_last_error("none");
    }
    return ESP_OK;
}

static void stop_wifi_runtime(void)
{
    if (!s_wifi_started) {
        s_wifi_connected = false;
        s_wifi_connecting = false;
        s_wifi_ip[0] = '\0';
        return;
    }
    (void)esp_wifi_disconnect();
    (void)esp_wifi_stop();
    s_wifi_started = false;
    s_wifi_connected = false;
    s_wifi_connecting = false;
    s_wifi_ip[0] = '\0';
    set_wifi_last_error("none");
}

static void copy_scan_record(d1l_wifi_scan_ap_t *dest, const wifi_ap_record_t *src)
{
    if (!dest || !src) {
        return;
    }
    memset(dest, 0, sizeof(*dest));
    size_t len = 0;
    while (len < sizeof(src->ssid) && src->ssid[len] != '\0') {
        len++;
    }
    if (len >= sizeof(dest->ssid)) {
        len = sizeof(dest->ssid) - 1U;
    }
    memcpy(dest->ssid, src->ssid, len);
    dest->ssid[len] = '\0';
    dest->rssi_dbm = src->rssi;
    dest->channel = src->primary;
    dest->auth = wifi_auth_name(src->authmode);
}
#endif

static void fill_status(d1l_connectivity_status_t *out_status)
{
    if (!out_status) {
        return;
    }
    memset(out_status, 0, sizeof(*out_status));
    const d1l_settings_t *settings = d1l_settings_current();
    out_status->usb_console_ready = true;
    out_status->wifi_enabled_setting = settings->wifi_enabled;
    out_status->ble_companion_enabled_setting = settings->ble_companion_enabled;
    out_status->observer_enabled_setting = settings->observer_enabled;
    out_status->wifi_build_enabled = build_wifi_enabled();
    out_status->ble_build_enabled = build_ble_enabled();
    out_status->wifi_stack_active = s_wifi_started;
    out_status->wifi_connected = s_wifi_connected;
    out_status->wifi_connecting = s_wifi_connecting;
    out_status->ble_stack_active = false;
    out_status->wifi_profile_saved = settings->wifi_profile_saved;
    out_status->wifi_password_saved = settings->wifi_password[0] != '\0';
    out_status->wifi_scan_supported = out_status->wifi_build_enabled;
    out_status->wifi_state = wifi_runtime_state(settings, out_status->wifi_build_enabled);
    out_status->ble_state =
        ble_runtime_state(settings->ble_companion_enabled, out_status->ble_build_enabled);
    out_status->wifi_ssid = settings->wifi_ssid;
    out_status->wifi_ip = s_wifi_ip;
    out_status->wifi_last_error = s_wifi_last_error;
#ifdef CONFIG_ESP_WIFI_ENABLED
    if (s_wifi_started && s_wifi_connected) {
        wifi_ap_record_t ap = {0};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            out_status->wifi_rssi_dbm = ap.rssi;
            out_status->wifi_channel = ap.primary;
        }
    }
#endif
    out_status->coexistence_policy = "offline_first_one_companion_radio";
}

esp_err_t d1l_connectivity_init(void)
{
    d1l_settings_t settings = *d1l_settings_current();
    if (settings.wifi_enabled && settings.ble_companion_enabled) {
        settings.ble_companion_enabled = false;
        esp_err_t ret = d1l_settings_save(&settings);
        if (ret != ESP_OK) {
            return ret;
        }
    }
#ifdef CONFIG_ESP_WIFI_ENABLED
    const d1l_settings_t *current = d1l_settings_current();
    if (current->wifi_enabled && current->wifi_profile_saved) {
        return d1l_connectivity_wifi_connect();
    }
#endif
    return ESP_OK;
}

void d1l_connectivity_status(d1l_connectivity_status_t *out_status)
{
    fill_status(out_status);
}

esp_err_t d1l_connectivity_wifi_scan(d1l_wifi_scan_result_t *out_result)
{
    if (!out_result) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_result, 0, sizeof(*out_result));
    out_result->scan_supported = build_wifi_enabled();
    out_result->reason = "not_started";
    out_result->last_error = ESP_OK;
    const d1l_settings_t *settings = d1l_settings_current();
    if (!settings->wifi_enabled) {
        out_result->reason = "disabled_by_setting";
        out_result->last_error = ESP_ERR_INVALID_STATE;
        return ESP_ERR_INVALID_STATE;
    }
    if (!build_wifi_enabled()) {
        out_result->reason = "build_disabled";
        out_result->last_error = ESP_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }
#ifndef CONFIG_ESP_WIFI_ENABLED
    out_result->reason = "build_disabled";
    out_result->last_error = ESP_ERR_NOT_SUPPORTED;
    return ESP_ERR_NOT_SUPPORTED;
#else
    esp_err_t ret = ensure_wifi_started();
    if (ret != ESP_OK) {
        out_result->reason = s_wifi_last_error;
        out_result->last_error = ret;
        return ret;
    }
    ret = esp_wifi_scan_start(NULL, true);
    if (ret != ESP_OK) {
        out_result->reason = esp_err_to_name(ret);
        out_result->last_error = ret;
        set_wifi_last_error(out_result->reason);
        return ret;
    }
    out_result->scan_started = true;

    uint16_t ap_count = 0;
    ret = esp_wifi_scan_get_ap_num(&ap_count);
    if (ret != ESP_OK) {
        out_result->reason = esp_err_to_name(ret);
        out_result->last_error = ret;
        set_wifi_last_error(out_result->reason);
        return ret;
    }
    out_result->total_count = ap_count;
    uint16_t record_count = ap_count > D1L_WIFI_SCAN_MAX ? D1L_WIFI_SCAN_MAX : ap_count;
    out_result->truncated = ap_count > D1L_WIFI_SCAN_MAX;
    if (record_count > 0U) {
        wifi_ap_record_t records[D1L_WIFI_SCAN_MAX] = {0};
        uint16_t records_to_fetch = record_count;
        ret = esp_wifi_scan_get_ap_records(&records_to_fetch, records);
        if (ret != ESP_OK) {
            out_result->reason = esp_err_to_name(ret);
            out_result->last_error = ret;
            set_wifi_last_error(out_result->reason);
            return ret;
        }
        out_result->returned_count = records_to_fetch;
        for (uint16_t i = 0; i < records_to_fetch; ++i) {
            copy_scan_record(&out_result->aps[i], &records[i]);
        }
    } else {
        out_result->returned_count = 0;
    }
    out_result->reason = "ok";
    out_result->last_error = ESP_OK;
    set_wifi_last_error("none");
    return ESP_OK;
#endif
}

esp_err_t d1l_connectivity_wifi_connect(void)
{
    const d1l_settings_t *settings = d1l_settings_current();
    if (!settings->wifi_enabled) {
        set_wifi_last_error("disabled_by_setting");
        return ESP_ERR_INVALID_STATE;
    }
    if (!settings->wifi_profile_saved || settings->wifi_ssid[0] == '\0') {
        set_wifi_last_error("profile_required");
        return ESP_ERR_INVALID_STATE;
    }
    if (!build_wifi_enabled()) {
        set_wifi_last_error("build_disabled");
        return ESP_ERR_NOT_SUPPORTED;
    }
#ifndef CONFIG_ESP_WIFI_ENABLED
    return ESP_ERR_NOT_SUPPORTED;
#else
    esp_err_t ret = ensure_wifi_started();
    if (ret != ESP_OK) {
        return ret;
    }
    wifi_config_t config = {0};
    snprintf((char *)config.sta.ssid, sizeof(config.sta.ssid), "%s", settings->wifi_ssid);
    snprintf((char *)config.sta.password, sizeof(config.sta.password), "%s",
             settings->wifi_password);
    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;
    ret = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (ret != ESP_OK) {
        set_wifi_last_error(esp_err_to_name(ret));
        return ret;
    }
    if (s_wifi_connected) {
        (void)esp_wifi_disconnect();
    }
    ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        s_wifi_connecting = false;
        set_wifi_last_error(esp_err_to_name(ret));
        return ret;
    }
    s_wifi_connecting = true;
    set_wifi_last_error("none");
    return ESP_OK;
#endif
}

esp_err_t d1l_connectivity_wifi_disconnect(void)
{
#ifdef CONFIG_ESP_WIFI_ENABLED
    if (s_wifi_started) {
        esp_err_t ret = esp_wifi_disconnect();
        if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_CONNECT) {
            set_wifi_last_error(esp_err_to_name(ret));
            return ret;
        }
    }
#endif
    s_wifi_connected = false;
    s_wifi_connecting = false;
    s_wifi_ip[0] = '\0';
    set_wifi_last_error("none");
    return ESP_OK;
}

esp_err_t d1l_connectivity_set_wifi_enabled(bool enabled)
{
    if (enabled && !build_wifi_enabled()) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    d1l_settings_t settings = *d1l_settings_current();
    settings.wifi_enabled = enabled;
    if (enabled) {
        settings.ble_companion_enabled = false;
    }
    esp_err_t ret = d1l_settings_save(&settings);
    if (ret != ESP_OK) {
        return ret;
    }
    if (!enabled) {
#ifdef CONFIG_ESP_WIFI_ENABLED
        stop_wifi_runtime();
#endif
        return ESP_OK;
    }
#ifdef CONFIG_ESP_WIFI_ENABLED
    ret = ensure_wifi_started();
    if (ret != ESP_OK) {
        return ret;
    }
    if (settings.wifi_profile_saved) {
        ret = d1l_connectivity_wifi_connect();
        if (ret != ESP_OK) {
            return ret;
        }
    }
#endif
    return ESP_OK;
}

esp_err_t d1l_connectivity_set_ble_enabled(bool enabled)
{
    if (enabled && !build_ble_enabled()) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    d1l_settings_t settings = *d1l_settings_current();
    settings.ble_companion_enabled = enabled;
    if (enabled) {
        settings.wifi_enabled = false;
    }
    return d1l_settings_save(&settings);
}

esp_err_t d1l_connectivity_save_wifi_profile(const char *ssid, const char *password)
{
    esp_err_t ret = d1l_settings_save_wifi_profile(ssid, password);
    if (ret != ESP_OK) {
        return ret;
    }
    const d1l_settings_t *settings = d1l_settings_current();
    if (settings->wifi_enabled && build_wifi_enabled()) {
        return d1l_connectivity_wifi_connect();
    }
    return ESP_OK;
}

esp_err_t d1l_connectivity_clear_wifi_profile(void)
{
    esp_err_t ret = d1l_settings_clear_wifi_profile();
    if (ret == ESP_OK) {
        (void)d1l_connectivity_wifi_disconnect();
    }
    return ret;
}
