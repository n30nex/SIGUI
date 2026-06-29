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

static const char *runtime_state(bool desired, bool build_enabled)
{
    if (!desired) {
        return "off";
    }
    if (!build_enabled) {
        return "build_disabled";
    }
    return "enabled_pending_stack";
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
    out_status->wifi_scan_supported = false;
    out_status->wifi_state =
        runtime_state(settings->wifi_enabled, out_status->wifi_build_enabled);
    out_status->ble_state =
        runtime_state(settings->ble_companion_enabled, out_status->ble_build_enabled);
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
    if (enabled) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    d1l_settings_t settings = *d1l_settings_current();
    settings.wifi_enabled = enabled;
    return d1l_settings_save(&settings);
}

esp_err_t d1l_connectivity_set_ble_enabled(bool enabled)
{
    if (enabled) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    d1l_settings_t settings = *d1l_settings_current();
    settings.ble_companion_enabled = enabled;
    return d1l_settings_save(&settings);
}
