#pragma once

#include <stdbool.h>

#include "esp_err.h"

#include "app/settings_model.h"

#define D1L_WIFI_SCAN_MAX 8U

typedef struct {
    char ssid[D1L_WIFI_SSID_LEN];
    int8_t rssi_dbm;
    uint8_t channel;
    const char *auth;
} d1l_wifi_scan_ap_t;

typedef struct {
    bool scan_started;
    bool scan_supported;
    bool truncated;
    uint16_t total_count;
    uint16_t returned_count;
    const char *reason;
    esp_err_t last_error;
    d1l_wifi_scan_ap_t aps[D1L_WIFI_SCAN_MAX];
} d1l_wifi_scan_result_t;

typedef struct {
    bool usb_console_ready;
    bool wifi_enabled_setting;
    bool ble_companion_enabled_setting;
    bool observer_enabled_setting;
    bool wifi_build_enabled;
    bool ble_build_enabled;
    bool wifi_scan_supported;
    bool wifi_stack_active;
    bool wifi_connected;
    bool wifi_connecting;
    bool ble_stack_active;
    bool wifi_profile_saved;
    bool wifi_password_saved;
    const char *wifi_state;
    const char *ble_state;
    const char *wifi_ssid;
    const char *wifi_ip;
    int8_t wifi_rssi_dbm;
    uint8_t wifi_channel;
    const char *wifi_last_error;
    const char *coexistence_policy;
} d1l_connectivity_status_t;

esp_err_t d1l_connectivity_init(void);
void d1l_connectivity_status(d1l_connectivity_status_t *out_status);
esp_err_t d1l_connectivity_wifi_scan(d1l_wifi_scan_result_t *out_result);
esp_err_t d1l_connectivity_wifi_connect(void);
esp_err_t d1l_connectivity_wifi_disconnect(void);
esp_err_t d1l_connectivity_set_wifi_enabled(bool enabled);
esp_err_t d1l_connectivity_set_ble_enabled(bool enabled);
esp_err_t d1l_connectivity_save_wifi_profile(const char *ssid, const char *password);
esp_err_t d1l_connectivity_clear_wifi_profile(void);
