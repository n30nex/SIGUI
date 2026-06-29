#include "settings_model.h"

#include <stdio.h>
#include <string.h>

#include "nvs.h"

#include "d1l_config.h"

#define D1L_SETTINGS_NAMESPACE "d1l_settings"
#define D1L_SETTINGS_KEY "settings"

static d1l_settings_t s_current;
static bool s_loaded;

static uint16_t bandwidth_to_tenths(float khz)
{
    return (uint16_t)((khz * 10.0f) + 0.5f);
}

void d1l_settings_defaults(d1l_settings_t *settings)
{
    if (settings == NULL) {
        return;
    }
    memset(settings, 0, sizeof(*settings));
    settings->schema_version = D1L_SETTINGS_SCHEMA_VERSION;
    snprintf(settings->node_name, sizeof(settings->node_name), "D1L Desk");
    settings->role = D1L_ROLE_DESK_COMPANION;
    settings->wifi_enabled = false;
    settings->ble_companion_enabled = false;
    settings->observer_enabled = false;
    settings->high_contrast = false;
    settings->night_mode = false;
    settings->path_hash_bytes = 1;
    settings->frequency_hz = D1L_RADIO_FREQ_HZ;
    settings->bandwidth_tenths_khz = bandwidth_to_tenths(D1L_RADIO_BW_KHZ);
    settings->spreading_factor = D1L_RADIO_SF;
    settings->coding_rate = D1L_RADIO_CR;
    settings->tx_power_dbm = D1L_RADIO_TX_POWER_DBM;
    settings->rx_boost = true;
    settings->tcxo_mode = D1L_TCXO_NONE;
}

void d1l_settings_sanitize(d1l_settings_t *settings)
{
    if (settings == NULL) {
        return;
    }
    if (settings->schema_version != D1L_SETTINGS_SCHEMA_VERSION) {
        d1l_settings_defaults(settings);
        return;
    }
    settings->node_name[D1L_NODE_NAME_LEN - 1U] = '\0';
    if (settings->node_name[0] == '\0') {
        snprintf(settings->node_name, sizeof(settings->node_name), "D1L Desk");
    }
    for (size_t i = 0; i < D1L_NODE_NAME_LEN && settings->node_name[i] != '\0'; ++i) {
        const unsigned char c = (unsigned char)settings->node_name[i];
        if (c < 32 || c > 126 || c == '"' || c == '\\') {
            settings->node_name[i] = '_';
        }
    }
    if (settings->role != D1L_ROLE_DESK_COMPANION) {
        settings->role = D1L_ROLE_DESK_COMPANION;
    }
    if (settings->path_hash_bytes < 1 || settings->path_hash_bytes > 3) {
        settings->path_hash_bytes = 1;
    }
    if (settings->frequency_hz < 902000000UL || settings->frequency_hz > 928000000UL) {
        settings->frequency_hz = D1L_RADIO_FREQ_HZ;
    }
    if (settings->bandwidth_tenths_khz < 78 || settings->bandwidth_tenths_khz > 5000) {
        settings->bandwidth_tenths_khz = bandwidth_to_tenths(D1L_RADIO_BW_KHZ);
    }
    if (settings->spreading_factor < 5 || settings->spreading_factor > 12) {
        settings->spreading_factor = D1L_RADIO_SF;
    }
    if (settings->coding_rate < 5 || settings->coding_rate > 8) {
        settings->coding_rate = D1L_RADIO_CR;
    }
    if (settings->tx_power_dbm > D1L_RADIO_TX_POWER_DBM) {
        settings->tx_power_dbm = D1L_RADIO_TX_POWER_DBM;
    }
    if (settings->tx_power_dbm < -9) {
        settings->tx_power_dbm = -9;
    }
    settings->tcxo_mode = D1L_TCXO_NONE;
}

esp_err_t d1l_settings_load(void)
{
    d1l_settings_defaults(&s_current);

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(D1L_SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        s_loaded = false;
        return ret;
    }

    size_t len = sizeof(s_current);
    ret = nvs_get_blob(handle, D1L_SETTINGS_KEY, &s_current, &len);
    if (ret == ESP_ERR_NVS_NOT_FOUND || len != sizeof(s_current)) {
        d1l_settings_defaults(&s_current);
        ret = nvs_set_blob(handle, D1L_SETTINGS_KEY, &s_current, sizeof(s_current));
        if (ret == ESP_OK) {
            ret = nvs_commit(handle);
        }
    } else if (ret == ESP_OK) {
        d1l_settings_sanitize(&s_current);
    }
    nvs_close(handle);
    s_loaded = (ret == ESP_OK);
    return ret;
}

esp_err_t d1l_settings_save(const d1l_settings_t *settings)
{
    if (settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    d1l_settings_t copy = *settings;
    d1l_settings_sanitize(&copy);

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(D1L_SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = nvs_set_blob(handle, D1L_SETTINGS_KEY, &copy, sizeof(copy));
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    if (ret == ESP_OK) {
        s_current = copy;
        s_loaded = true;
    }
    return ret;
}

esp_err_t d1l_settings_reset(void)
{
    d1l_settings_t defaults;
    d1l_settings_defaults(&defaults);
    return d1l_settings_save(&defaults);
}

const d1l_settings_t *d1l_settings_current(void)
{
    if (!s_loaded) {
        d1l_settings_defaults(&s_current);
    }
    return &s_current;
}

d1l_radio_profile_t d1l_settings_radio_profile(const d1l_settings_t *settings)
{
    const d1l_settings_t *src = settings ? settings : d1l_settings_current();
    d1l_radio_profile_t profile = {
        .profile_id = D1L_RADIO_PROFILE_ID,
        .region_label = D1L_RADIO_REGION_LABEL,
        .frequency_hz = src->frequency_hz,
        .bandwidth_khz = ((float)src->bandwidth_tenths_khz) / 10.0f,
        .spreading_factor = src->spreading_factor,
        .coding_rate = src->coding_rate,
        .tx_power_dbm = src->tx_power_dbm,
        .tcxo = d1l_settings_tcxo_name(src->tcxo_mode),
        .rx_boost = src->rx_boost,
    };
    return profile;
}

const char *d1l_settings_role_name(uint8_t role)
{
    switch (role) {
    case D1L_ROLE_DESK_COMPANION:
        return "desk_companion";
    default:
        return "unknown";
    }
}

const char *d1l_settings_tcxo_name(uint8_t tcxo_mode)
{
    switch (tcxo_mode) {
    case D1L_TCXO_NONE:
        return "NONE";
    default:
        return "NONE";
    }
}
