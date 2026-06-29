#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "mesh/meshcore_radio_profile.h"

#define D1L_SETTINGS_SCHEMA_VERSION 1U
#define D1L_NODE_NAME_LEN 32U

typedef enum {
    D1L_ROLE_DESK_COMPANION = 0,
} d1l_role_t;

typedef enum {
    D1L_TCXO_NONE = 0,
} d1l_tcxo_mode_t;

typedef struct {
    uint32_t schema_version;
    char node_name[D1L_NODE_NAME_LEN];
    uint8_t role;
    bool wifi_enabled;
    bool ble_companion_enabled;
    bool observer_enabled;
    bool high_contrast;
    bool night_mode;
    uint8_t path_hash_bytes;
    uint32_t frequency_hz;
    uint16_t bandwidth_tenths_khz;
    uint8_t spreading_factor;
    uint8_t coding_rate;
    int8_t tx_power_dbm;
    bool rx_boost;
    uint8_t tcxo_mode;
} d1l_settings_t;

void d1l_settings_defaults(d1l_settings_t *settings);
void d1l_settings_sanitize(d1l_settings_t *settings);
esp_err_t d1l_settings_load(void);
esp_err_t d1l_settings_save(const d1l_settings_t *settings);
esp_err_t d1l_settings_reset(void);
const d1l_settings_t *d1l_settings_current(void);
d1l_radio_profile_t d1l_settings_radio_profile(const d1l_settings_t *settings);
const char *d1l_settings_role_name(uint8_t role);
const char *d1l_settings_tcxo_name(uint8_t tcxo_mode);
