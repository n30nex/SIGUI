#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef struct {
    bool usb_console_ready;
    bool wifi_enabled_setting;
    bool ble_companion_enabled_setting;
    bool observer_enabled_setting;
    bool wifi_build_enabled;
    bool ble_build_enabled;
    bool wifi_scan_supported;
    bool wifi_stack_active;
    bool ble_stack_active;
    bool wifi_profile_saved;
    bool wifi_password_saved;
    const char *wifi_state;
    const char *ble_state;
    const char *wifi_ssid;
    const char *coexistence_policy;
} d1l_connectivity_status_t;

esp_err_t d1l_connectivity_init(void);
void d1l_connectivity_status(d1l_connectivity_status_t *out_status);
esp_err_t d1l_connectivity_set_wifi_enabled(bool enabled);
esp_err_t d1l_connectivity_set_ble_enabled(bool enabled);
esp_err_t d1l_connectivity_save_wifi_profile(const char *ssid, const char *password);
esp_err_t d1l_connectivity_clear_wifi_profile(void);
