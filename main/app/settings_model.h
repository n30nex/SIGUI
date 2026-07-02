#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "mesh/meshcore_radio_profile.h"
#include "storage/map_tile_store.h"

#define D1L_SETTINGS_SCHEMA_VERSION 6U
#define D1L_NODE_NAME_LEN 32U
#define D1L_WIFI_SSID_LEN 33U
#define D1L_WIFI_PASSWORD_LEN 65U
#define D1L_IDENTITY_PUBLIC_KEY_LEN 32U
#define D1L_IDENTITY_PRIVATE_KEY_LEN 64U
#define D1L_MAP_LOCATION_LAT_E7_MIN (-900000000L)
#define D1L_MAP_LOCATION_LAT_E7_MAX 900000000L
#define D1L_MAP_LOCATION_LON_E7_MIN (-1800000000L)
#define D1L_MAP_LOCATION_LON_E7_MAX 1800000000L
#define D1L_MAP_TILE_PROVIDER_TEMPLATE_LEN (D1L_MAP_TILE_URL_TEMPLATE_MAX + 1U)
#define D1L_MAP_TILE_PROVIDER_ATTRIBUTION_LEN (D1L_MAP_TILE_ATTRIBUTION_MAX + 1U)
#define D1L_MAP_TILE_DEFAULT_ZOOM 12U

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
    bool onboarding_complete;
    bool wifi_profile_saved;
    uint8_t path_hash_bytes;
    char wifi_ssid[D1L_WIFI_SSID_LEN];
    char wifi_password[D1L_WIFI_PASSWORD_LEN];
    uint32_t frequency_hz;
    uint16_t bandwidth_tenths_khz;
    uint8_t spreading_factor;
    uint8_t coding_rate;
    int8_t tx_power_dbm;
    bool rx_boost;
    uint8_t tcxo_mode;
    bool map_location_set;
    int32_t map_lat_e7;
    int32_t map_lon_e7;
    bool map_tile_provider_saved;
    char map_tile_url_template[D1L_MAP_TILE_PROVIDER_TEMPLATE_LEN];
    char map_tile_attribution[D1L_MAP_TILE_PROVIDER_ATTRIBUTION_LEN];
    uint8_t map_tile_zoom;
    bool identity_ready;
    uint8_t identity_public_key[D1L_IDENTITY_PUBLIC_KEY_LEN];
    uint8_t identity_private_key[D1L_IDENTITY_PRIVATE_KEY_LEN];
} d1l_settings_t;

void d1l_settings_defaults(d1l_settings_t *settings);
void d1l_settings_sanitize(d1l_settings_t *settings);
esp_err_t d1l_settings_load(void);
esp_err_t d1l_settings_save(const d1l_settings_t *settings);
esp_err_t d1l_settings_reset(void);
esp_err_t d1l_settings_save_wifi_profile(const char *ssid, const char *password);
esp_err_t d1l_settings_clear_wifi_profile(void);
esp_err_t d1l_settings_complete_onboarding(const char *node_name, bool wifi_enabled,
                                           bool ble_companion_enabled, bool observer_enabled);
esp_err_t d1l_settings_reset_onboarding(void);
esp_err_t d1l_settings_next_mesh_timestamp(uint32_t *timestamp);
const d1l_settings_t *d1l_settings_current(void);
d1l_radio_profile_t d1l_settings_radio_profile(const d1l_settings_t *settings);
const char *d1l_settings_role_name(uint8_t role);
const char *d1l_settings_tcxo_name(uint8_t tcxo_mode);
