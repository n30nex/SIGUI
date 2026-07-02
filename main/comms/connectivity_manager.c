#include "connectivity_manager.h"

#include <string.h>

#include "sdkconfig.h"

#include "app/settings_model.h"

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
    if (!settings->wifi_profile_saved) {
        return "profile_required";
    }
    return "configured_pending_stack";
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
    out_status->wifi_stack_active = false;
    out_status->ble_stack_active = false;
    out_status->wifi_profile_saved = settings->wifi_profile_saved;
    out_status->wifi_password_saved = settings->wifi_password[0] != '\0';
    out_status->wifi_scan_supported = false;
    out_status->wifi_state = wifi_runtime_state(settings, out_status->wifi_build_enabled);
    out_status->ble_state =
        ble_runtime_state(settings->ble_companion_enabled, out_status->ble_build_enabled);
    out_status->wifi_ssid = settings->wifi_ssid;
    out_status->coexistence_policy = "offline_first_one_companion_radio";
}

esp_err_t d1l_connectivity_init(void)
{
    d1l_settings_t settings = *d1l_settings_current();
    if (settings.wifi_enabled && settings.ble_companion_enabled) {
        settings.ble_companion_enabled = false;
        return d1l_settings_save(&settings);
    }
    return ESP_OK;
}

void d1l_connectivity_status(d1l_connectivity_status_t *out_status)
{
    fill_status(out_status);
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
    return d1l_settings_save(&settings);
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
    return d1l_settings_save_wifi_profile(ssid, password);
}

esp_err_t d1l_connectivity_clear_wifi_profile(void)
{
    return d1l_settings_clear_wifi_profile();
}
