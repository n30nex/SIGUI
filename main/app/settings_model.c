#include "settings_model.h"

#include <stdio.h>
#include <string.h>

#include "ed_25519.h"
#include "nvs.h"

#include "d1l_config.h"

#define D1L_SETTINGS_NAMESPACE "d1l_settings"
#define D1L_SETTINGS_KEY "settings"
#define D1L_SETTINGS_MESH_TIMESTAMP_KEY "mesh_ts"
#define D1L_SETTINGS_MESH_TIMESTAMP_BASE 1767225600UL

static d1l_settings_t s_current;
static bool s_loaded;
static uint32_t s_mesh_timestamp_last = D1L_SETTINGS_MESH_TIMESTAMP_BASE;

_Static_assert(D1L_IDENTITY_PUBLIC_KEY_LEN == D1L_IDENTITY_STATE_PUBLIC_KEY_LEN,
               "identity public key lengths must match");
_Static_assert(D1L_IDENTITY_PRIVATE_KEY_LEN == D1L_IDENTITY_STATE_PRIVATE_KEY_LEN,
               "identity private key lengths must match");

static bool identity_bytes_all_zero(const uint8_t *bytes, size_t length)
{
    if (!bytes) {
        return false;
    }
    uint8_t combined = 0U;
    for (size_t i = 0; i < length; ++i) {
        combined |= bytes[i];
    }
    return combined == 0U;
}

static bool identity_bytes_uniform(const uint8_t *bytes, size_t length)
{
    if (!bytes || length == 0U) {
        return false;
    }
    uint8_t differences = 0U;
    for (size_t i = 1U; i < length; ++i) {
        differences |= (uint8_t)(bytes[i] ^ bytes[0]);
    }
    return differences == 0U;
}

static void wipe_derived_identity_key(
    uint8_t key[D1L_IDENTITY_STATE_PUBLIC_KEY_LEN])
{
    volatile uint8_t *cursor = key;
    for (size_t i = 0; i < D1L_IDENTITY_STATE_PUBLIC_KEY_LEN; ++i) {
        cursor[i] = 0U;
    }
}

d1l_identity_state_t d1l_identity_state_classify(
    bool identity_ready,
    const uint8_t public_key[D1L_IDENTITY_STATE_PUBLIC_KEY_LEN],
    const uint8_t private_key[D1L_IDENTITY_STATE_PRIVATE_KEY_LEN])
{
    if (!public_key || !private_key) {
        return D1L_IDENTITY_STATE_INCONSISTENT;
    }

    const bool public_absent = identity_bytes_all_zero(
        public_key, D1L_IDENTITY_STATE_PUBLIC_KEY_LEN);
    const bool private_absent = identity_bytes_all_zero(
        private_key, D1L_IDENTITY_STATE_PRIVATE_KEY_LEN);
    if (!identity_ready) {
        return public_absent && private_absent
                   ? D1L_IDENTITY_STATE_ABSENT
                   : D1L_IDENTITY_STATE_INCONSISTENT;
    }
    if (public_absent || private_absent) {
        return D1L_IDENTITY_STATE_INCONSISTENT;
    }
    if ((private_key[0] & 0x07U) != 0U ||
        (private_key[31] & 0xc0U) != 0x40U) {
        /* ECDH reclamps the scalar; reject state whose stored scalar would
         * represent a different key after that operation. */
        return D1L_IDENTITY_STATE_INCONSISTENT;
    }
    if (identity_bytes_uniform(
            &private_key[32], D1L_IDENTITY_STATE_PRIVATE_KEY_LEN - 32U)) {
        /* Signing derives its nonce from this prefix. Uniform persisted data
         * is fail-closed because a predictable nonce can expose the scalar. */
        return D1L_IDENTITY_STATE_INCONSISTENT;
    }

    uint8_t derived_public[D1L_IDENTITY_STATE_PUBLIC_KEY_LEN] = {0};
    ed25519_derive_pub(derived_public, private_key);
    const bool exact_match = memcmp(
        public_key, derived_public, sizeof(derived_public)) == 0;
    wipe_derived_identity_key(derived_public);
    return exact_match ? D1L_IDENTITY_STATE_CONSISTENT
                       : D1L_IDENTITY_STATE_INCONSISTENT;
}

const char *d1l_identity_state_name(d1l_identity_state_t state)
{
    switch (state) {
        case D1L_IDENTITY_STATE_ABSENT:
            return "absent";
        case D1L_IDENTITY_STATE_CONSISTENT:
            return "consistent";
        case D1L_IDENTITY_STATE_INCONSISTENT:
        default:
            return "inconsistent";
    }
}

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
    char map_tile_url_template[D1L_MAP_TILE_URL_TEMPLATE_MAX + 1U];
    char map_tile_attribution[D1L_MAP_TILE_ATTRIBUTION_MAX + 1U];
    uint8_t map_tile_zoom;
    bool identity_ready;
    uint8_t identity_public_key[D1L_IDENTITY_PUBLIC_KEY_LEN];
    uint8_t identity_private_key[D1L_IDENTITY_PRIVATE_KEY_LEN];
} d1l_settings_v6_t;

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
    bool identity_ready;
    uint8_t identity_public_key[D1L_IDENTITY_PUBLIC_KEY_LEN];
    uint8_t identity_private_key[D1L_IDENTITY_PRIVATE_KEY_LEN];
} d1l_settings_v5_t;

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
    uint8_t path_hash_bytes;
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
    bool identity_ready;
    uint8_t identity_public_key[D1L_IDENTITY_PUBLIC_KEY_LEN];
    uint8_t identity_private_key[D1L_IDENTITY_PRIVATE_KEY_LEN];
} d1l_settings_v4_t;

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
    uint8_t path_hash_bytes;
    uint32_t frequency_hz;
    uint16_t bandwidth_tenths_khz;
    uint8_t spreading_factor;
    uint8_t coding_rate;
    int8_t tx_power_dbm;
    bool rx_boost;
    uint8_t tcxo_mode;
    bool identity_ready;
    uint8_t identity_public_key[D1L_IDENTITY_PUBLIC_KEY_LEN];
    uint8_t identity_private_key[D1L_IDENTITY_PRIVATE_KEY_LEN];
} d1l_settings_v3_t;

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
    bool identity_ready;
    uint8_t identity_public_key[D1L_IDENTITY_PUBLIC_KEY_LEN];
    uint8_t identity_private_key[D1L_IDENTITY_PRIVATE_KEY_LEN];
} d1l_settings_v2_t;

static uint16_t bandwidth_to_tenths(float khz)
{
    return (uint16_t)((khz * 10.0f) + 0.5f);
}

static void sanitize_printable(char *text, size_t size, bool allow_quotes)
{
    if (!text || size == 0) {
        return;
    }
    text[size - 1U] = '\0';
    for (size_t i = 0; i < size && text[i] != '\0'; ++i) {
        const unsigned char c = (unsigned char)text[i];
        if (c < 32 || c > 126 || (!allow_quotes && (c == '"' || c == '\\'))) {
            text[i] = '_';
        }
    }
}

static bool mesh_timestamp_can_fallback(esp_err_t ret)
{
    return ret == ESP_ERR_NVS_NOT_ENOUGH_SPACE || ret == ESP_ERR_NVS_NO_FREE_PAGES;
}

static uint32_t next_ram_mesh_timestamp(uint32_t candidate)
{
    if (candidate <= s_mesh_timestamp_last && s_mesh_timestamp_last < UINT32_MAX) {
        candidate = s_mesh_timestamp_last + 1U;
    }
    if (candidate < D1L_SETTINGS_MESH_TIMESTAMP_BASE) {
        candidate = D1L_SETTINGS_MESH_TIMESTAMP_BASE + 1U;
    }
    s_mesh_timestamp_last = candidate;
    return candidate;
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
    settings->onboarding_complete = false;
    settings->wifi_profile_saved = false;
    settings->path_hash_bytes = 1;
    settings->wifi_ssid[0] = '\0';
    settings->wifi_password[0] = '\0';
    settings->frequency_hz = D1L_RADIO_FREQ_HZ;
    settings->bandwidth_tenths_khz = bandwidth_to_tenths(D1L_RADIO_BW_KHZ);
    settings->spreading_factor = D1L_RADIO_SF;
    settings->coding_rate = D1L_RADIO_CR;
    settings->tx_power_dbm = D1L_RADIO_TX_POWER_DBM;
    settings->rx_boost = true;
    settings->tcxo_mode = D1L_TCXO_NONE;
    settings->map_location_set = false;
    settings->map_lat_e7 = 0;
    settings->map_lon_e7 = 0;
    settings->map_tile_zoom = D1L_MAP_TILE_DEFAULT_ZOOM;
    settings->identity_ready = false;
    memset(settings->identity_public_key, 0, sizeof(settings->identity_public_key));
    memset(settings->identity_private_key, 0, sizeof(settings->identity_private_key));
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
    sanitize_printable(settings->node_name, sizeof(settings->node_name), false);
    sanitize_printable(settings->wifi_ssid, sizeof(settings->wifi_ssid), false);
    sanitize_printable(settings->wifi_password, sizeof(settings->wifi_password), true);
    settings->wifi_profile_saved = settings->wifi_profile_saved ? true : false;
    if (settings->wifi_ssid[0] == '\0') {
        settings->wifi_profile_saved = false;
        settings->wifi_password[0] = '\0';
    }
    if (settings->role != D1L_ROLE_DESK_COMPANION) {
        settings->role = D1L_ROLE_DESK_COMPANION;
    }
    settings->onboarding_complete = settings->onboarding_complete ? true : false;
    settings->wifi_enabled = settings->wifi_enabled ? true : false;
    settings->ble_companion_enabled = settings->ble_companion_enabled ? true : false;
    if (settings->wifi_enabled && settings->ble_companion_enabled) {
        settings->ble_companion_enabled = false;
    }
    settings->observer_enabled = settings->observer_enabled ? true : false;
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
    settings->map_location_set = settings->map_location_set ? true : false;
    if (!settings->map_location_set ||
        settings->map_lat_e7 < D1L_MAP_LOCATION_LAT_E7_MIN ||
        settings->map_lat_e7 > D1L_MAP_LOCATION_LAT_E7_MAX ||
        settings->map_lon_e7 < D1L_MAP_LOCATION_LON_E7_MIN ||
        settings->map_lon_e7 > D1L_MAP_LOCATION_LON_E7_MAX) {
        settings->map_location_set = false;
        settings->map_lat_e7 = 0;
        settings->map_lon_e7 = 0;
    }
    settings->map_tile_zoom = D1L_MAP_TILE_DEFAULT_ZOOM;
    settings->identity_ready = settings->identity_ready ? true : false;
    if (d1l_settings_identity_state(settings) != D1L_IDENTITY_STATE_CONSISTENT) {
        /* Fail closed without destroying forensic/recovery evidence. */
        settings->identity_ready = false;
    }
}

d1l_identity_state_t d1l_settings_identity_state(const d1l_settings_t *settings)
{
    if (!settings) {
        return D1L_IDENTITY_STATE_INCONSISTENT;
    }
    return d1l_identity_state_classify(
        settings->identity_ready,
        settings->identity_public_key,
        settings->identity_private_key);
}

static void migrate_v2_settings(d1l_settings_t *dest, const d1l_settings_v2_t *src)
{
    if (!dest) {
        return;
    }
    d1l_settings_defaults(dest);
    if (!src || src->schema_version != 2U) {
        return;
    }
    memcpy(dest->node_name, src->node_name, sizeof(dest->node_name));
    dest->node_name[D1L_NODE_NAME_LEN - 1U] = '\0';
    dest->role = src->role;
    dest->wifi_enabled = src->wifi_enabled;
    dest->ble_companion_enabled = src->ble_companion_enabled;
    dest->observer_enabled = src->observer_enabled;
    dest->high_contrast = src->high_contrast;
    dest->night_mode = src->night_mode;
    dest->onboarding_complete = true;
    dest->path_hash_bytes = src->path_hash_bytes;
    dest->frequency_hz = src->frequency_hz;
    dest->bandwidth_tenths_khz = src->bandwidth_tenths_khz;
    dest->spreading_factor = src->spreading_factor;
    dest->coding_rate = src->coding_rate;
    dest->tx_power_dbm = src->tx_power_dbm;
    dest->rx_boost = src->rx_boost;
    dest->tcxo_mode = src->tcxo_mode;
    dest->identity_ready = src->identity_ready;
    memcpy(dest->identity_public_key, src->identity_public_key, sizeof(dest->identity_public_key));
    memcpy(dest->identity_private_key, src->identity_private_key, sizeof(dest->identity_private_key));
    dest->schema_version = D1L_SETTINGS_SCHEMA_VERSION;
    d1l_settings_sanitize(dest);
}

static void migrate_v3_settings(d1l_settings_t *dest, const d1l_settings_v3_t *src)
{
    if (!dest) {
        return;
    }
    d1l_settings_defaults(dest);
    if (!src || src->schema_version != 3U) {
        return;
    }
    memcpy(dest->node_name, src->node_name, sizeof(dest->node_name));
    dest->node_name[D1L_NODE_NAME_LEN - 1U] = '\0';
    dest->role = src->role;
    dest->wifi_enabled = src->wifi_enabled;
    dest->ble_companion_enabled = src->ble_companion_enabled;
    dest->observer_enabled = src->observer_enabled;
    dest->high_contrast = src->high_contrast;
    dest->night_mode = src->night_mode;
    dest->onboarding_complete = src->onboarding_complete;
    dest->path_hash_bytes = src->path_hash_bytes;
    dest->frequency_hz = src->frequency_hz;
    dest->bandwidth_tenths_khz = src->bandwidth_tenths_khz;
    dest->spreading_factor = src->spreading_factor;
    dest->coding_rate = src->coding_rate;
    dest->tx_power_dbm = src->tx_power_dbm;
    dest->rx_boost = src->rx_boost;
    dest->tcxo_mode = src->tcxo_mode;
    dest->map_location_set = false;
    dest->map_lat_e7 = 0;
    dest->map_lon_e7 = 0;
    dest->identity_ready = src->identity_ready;
    memcpy(dest->identity_public_key, src->identity_public_key, sizeof(dest->identity_public_key));
    memcpy(dest->identity_private_key, src->identity_private_key, sizeof(dest->identity_private_key));
    dest->schema_version = D1L_SETTINGS_SCHEMA_VERSION;
    d1l_settings_sanitize(dest);
}

static void migrate_v5_settings(d1l_settings_t *dest, const d1l_settings_v5_t *src)
{
    if (!dest) {
        return;
    }
    d1l_settings_defaults(dest);
    if (!src || src->schema_version != 5U) {
        return;
    }
    memcpy(dest->node_name, src->node_name, sizeof(dest->node_name));
    dest->node_name[D1L_NODE_NAME_LEN - 1U] = '\0';
    dest->role = src->role;
    dest->wifi_enabled = src->wifi_enabled;
    dest->ble_companion_enabled = src->ble_companion_enabled;
    dest->observer_enabled = src->observer_enabled;
    dest->high_contrast = src->high_contrast;
    dest->night_mode = src->night_mode;
    dest->onboarding_complete = src->onboarding_complete;
    dest->wifi_profile_saved = src->wifi_profile_saved;
    memcpy(dest->wifi_ssid, src->wifi_ssid, sizeof(dest->wifi_ssid));
    dest->wifi_ssid[D1L_WIFI_SSID_LEN - 1U] = '\0';
    memcpy(dest->wifi_password, src->wifi_password, sizeof(dest->wifi_password));
    dest->wifi_password[D1L_WIFI_PASSWORD_LEN - 1U] = '\0';
    dest->path_hash_bytes = src->path_hash_bytes;
    dest->frequency_hz = src->frequency_hz;
    dest->bandwidth_tenths_khz = src->bandwidth_tenths_khz;
    dest->spreading_factor = src->spreading_factor;
    dest->coding_rate = src->coding_rate;
    dest->tx_power_dbm = src->tx_power_dbm;
    dest->rx_boost = src->rx_boost;
    dest->tcxo_mode = src->tcxo_mode;
    dest->map_location_set = src->map_location_set;
    dest->map_lat_e7 = src->map_lat_e7;
    dest->map_lon_e7 = src->map_lon_e7;
    dest->map_tile_zoom = D1L_MAP_TILE_DEFAULT_ZOOM;
    dest->identity_ready = src->identity_ready;
    memcpy(dest->identity_public_key, src->identity_public_key, sizeof(dest->identity_public_key));
    memcpy(dest->identity_private_key, src->identity_private_key, sizeof(dest->identity_private_key));
    dest->schema_version = D1L_SETTINGS_SCHEMA_VERSION;
    d1l_settings_sanitize(dest);
}

static void migrate_v6_settings(d1l_settings_t *dest, const d1l_settings_v6_t *src)
{
    if (!dest) {
        return;
    }
    d1l_settings_defaults(dest);
    if (!src || src->schema_version != 6U) {
        return;
    }
    memcpy(dest->node_name, src->node_name, sizeof(dest->node_name));
    dest->node_name[D1L_NODE_NAME_LEN - 1U] = '\0';
    dest->role = src->role;
    dest->wifi_enabled = src->wifi_enabled;
    dest->ble_companion_enabled = src->ble_companion_enabled;
    dest->observer_enabled = src->observer_enabled;
    dest->high_contrast = src->high_contrast;
    dest->night_mode = src->night_mode;
    dest->onboarding_complete = src->onboarding_complete;
    dest->wifi_profile_saved = src->wifi_profile_saved;
    memcpy(dest->wifi_ssid, src->wifi_ssid, sizeof(dest->wifi_ssid));
    dest->wifi_ssid[D1L_WIFI_SSID_LEN - 1U] = '\0';
    memcpy(dest->wifi_password, src->wifi_password, sizeof(dest->wifi_password));
    dest->wifi_password[D1L_WIFI_PASSWORD_LEN - 1U] = '\0';
    dest->path_hash_bytes = src->path_hash_bytes;
    dest->frequency_hz = src->frequency_hz;
    dest->bandwidth_tenths_khz = src->bandwidth_tenths_khz;
    dest->spreading_factor = src->spreading_factor;
    dest->coding_rate = src->coding_rate;
    dest->tx_power_dbm = src->tx_power_dbm;
    dest->rx_boost = src->rx_boost;
    dest->tcxo_mode = src->tcxo_mode;
    dest->map_location_set = src->map_location_set;
    dest->map_lat_e7 = src->map_lat_e7;
    dest->map_lon_e7 = src->map_lon_e7;
    dest->map_tile_zoom = D1L_MAP_TILE_DEFAULT_ZOOM;
    dest->identity_ready = src->identity_ready;
    memcpy(dest->identity_public_key, src->identity_public_key,
           sizeof(dest->identity_public_key));
    memcpy(dest->identity_private_key, src->identity_private_key,
           sizeof(dest->identity_private_key));
    dest->schema_version = D1L_SETTINGS_SCHEMA_VERSION;
    d1l_settings_sanitize(dest);
}

static void migrate_v4_settings(d1l_settings_t *dest, const d1l_settings_v4_t *src)
{
    if (!dest) {
        return;
    }
    d1l_settings_defaults(dest);
    if (!src || src->schema_version != 4U) {
        return;
    }
    memcpy(dest->node_name, src->node_name, sizeof(dest->node_name));
    dest->node_name[D1L_NODE_NAME_LEN - 1U] = '\0';
    dest->role = src->role;
    dest->wifi_enabled = src->wifi_enabled;
    dest->ble_companion_enabled = src->ble_companion_enabled;
    dest->observer_enabled = src->observer_enabled;
    dest->high_contrast = src->high_contrast;
    dest->night_mode = src->night_mode;
    dest->onboarding_complete = src->onboarding_complete;
    dest->path_hash_bytes = src->path_hash_bytes;
    dest->frequency_hz = src->frequency_hz;
    dest->bandwidth_tenths_khz = src->bandwidth_tenths_khz;
    dest->spreading_factor = src->spreading_factor;
    dest->coding_rate = src->coding_rate;
    dest->tx_power_dbm = src->tx_power_dbm;
    dest->rx_boost = src->rx_boost;
    dest->tcxo_mode = src->tcxo_mode;
    dest->map_location_set = src->map_location_set;
    dest->map_lat_e7 = src->map_lat_e7;
    dest->map_lon_e7 = src->map_lon_e7;
    dest->identity_ready = src->identity_ready;
    memcpy(dest->identity_public_key, src->identity_public_key, sizeof(dest->identity_public_key));
    memcpy(dest->identity_private_key, src->identity_private_key, sizeof(dest->identity_private_key));
    dest->schema_version = D1L_SETTINGS_SCHEMA_VERSION;
    d1l_settings_sanitize(dest);
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

    size_t len = 0;
    ret = nvs_get_blob(handle, D1L_SETTINGS_KEY, NULL, &len);
    bool should_save = false;
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        d1l_settings_defaults(&s_current);
        should_save = true;
        ret = ESP_OK;
    } else if (ret == ESP_OK && len == sizeof(s_current)) {
        /*
         * Schema 5 and schema 7 have the same ABI size on this target.  The
         * blob length therefore cannot identify the schema: inspect the
         * stored header before treating the bytes as the current layout.
         */
        d1l_settings_t loaded = {0};
        ret = nvs_get_blob(handle, D1L_SETTINGS_KEY, &loaded, &len);
        if (ret == ESP_OK) {
            switch (loaded.schema_version) {
            case D1L_SETTINGS_SCHEMA_VERSION:
                s_current = loaded;
                d1l_settings_sanitize(&s_current);
                break;
            case 5U: {
                d1l_settings_v5_t old_v5 = {0};
                memcpy(&old_v5, &loaded, sizeof(old_v5));
                migrate_v5_settings(&s_current, &old_v5);
                should_save = true;
                break;
            }
            case 4U: {
                d1l_settings_v4_t old_v4 = {0};
                memcpy(&old_v4, &loaded, sizeof(old_v4));
                migrate_v4_settings(&s_current, &old_v4);
                should_save = true;
                break;
            }
            case 3U: {
                d1l_settings_v3_t old_v3 = {0};
                memcpy(&old_v3, &loaded, sizeof(old_v3));
                migrate_v3_settings(&s_current, &old_v3);
                should_save = true;
                break;
            }
            case 2U: {
                d1l_settings_v2_t old_v2 = {0};
                memcpy(&old_v2, &loaded, sizeof(old_v2));
                migrate_v2_settings(&s_current, &old_v2);
                should_save = true;
                break;
            }
            default:
                /* Unknown same-size blobs are never sanitized as v7. */
                d1l_settings_defaults(&s_current);
                should_save = true;
                break;
            }
        }
    } else if (ret == ESP_OK && len == sizeof(d1l_settings_v6_t)) {
        d1l_settings_v6_t old_settings = {0};
        ret = nvs_get_blob(handle, D1L_SETTINGS_KEY, &old_settings, &len);
        if (ret == ESP_OK) {
            migrate_v6_settings(&s_current, &old_settings);
            should_save = true;
        }
    } else if (ret == ESP_OK && len == sizeof(d1l_settings_v5_t)) {
        d1l_settings_v5_t old_settings = {0};
        ret = nvs_get_blob(handle, D1L_SETTINGS_KEY, &old_settings, &len);
        if (ret == ESP_OK) {
            if (old_settings.schema_version == 5U) {
                migrate_v5_settings(&s_current, &old_settings);
            } else if (old_settings.schema_version == 4U) {
                d1l_settings_v4_t old_v4 = {0};
                memcpy(&old_v4, &old_settings, sizeof(old_v4));
                migrate_v4_settings(&s_current, &old_v4);
            } else if (old_settings.schema_version == 3U) {
                d1l_settings_v3_t old_v3 = {0};
                memcpy(&old_v3, &old_settings, sizeof(old_v3));
                migrate_v3_settings(&s_current, &old_v3);
            } else if (old_settings.schema_version == 2U) {
                d1l_settings_v2_t old_v2 = {0};
                memcpy(&old_v2, &old_settings, sizeof(old_v2));
                migrate_v2_settings(&s_current, &old_v2);
            } else {
                d1l_settings_defaults(&s_current);
            }
            should_save = true;
        }
    } else if (ret == ESP_OK && len == sizeof(d1l_settings_v4_t)) {
        d1l_settings_v4_t old_settings = {0};
        ret = nvs_get_blob(handle, D1L_SETTINGS_KEY, &old_settings, &len);
        if (ret == ESP_OK) {
            if (old_settings.schema_version == 4U) {
                migrate_v4_settings(&s_current, &old_settings);
            } else if (old_settings.schema_version == 3U) {
                d1l_settings_v3_t old_v3 = {0};
                memcpy(&old_v3, &old_settings, sizeof(old_v3));
                migrate_v3_settings(&s_current, &old_v3);
            } else if (old_settings.schema_version == 2U) {
                d1l_settings_v2_t old_v2 = {0};
                memcpy(&old_v2, &old_settings, sizeof(old_v2));
                migrate_v2_settings(&s_current, &old_v2);
            } else {
                d1l_settings_defaults(&s_current);
            }
            should_save = true;
        }
    } else if (ret == ESP_OK && len == sizeof(d1l_settings_v3_t)) {
        d1l_settings_v3_t old_settings = {0};
        ret = nvs_get_blob(handle, D1L_SETTINGS_KEY, &old_settings, &len);
        if (ret == ESP_OK) {
            if (old_settings.schema_version == 3U) {
                migrate_v3_settings(&s_current, &old_settings);
            } else if (old_settings.schema_version == 2U) {
                d1l_settings_v2_t old_v2 = {0};
                memcpy(&old_v2, &old_settings, sizeof(old_v2));
                migrate_v2_settings(&s_current, &old_v2);
            } else {
                d1l_settings_defaults(&s_current);
            }
            should_save = true;
        }
    } else if (ret == ESP_OK && len == sizeof(d1l_settings_v2_t)) {
        d1l_settings_v2_t old_settings = {0};
        ret = nvs_get_blob(handle, D1L_SETTINGS_KEY, &old_settings, &len);
        if (ret == ESP_OK) {
            migrate_v2_settings(&s_current, &old_settings);
            should_save = true;
        }
    } else if (ret == ESP_OK) {
        d1l_settings_defaults(&s_current);
        should_save = true;
    }
    if (ret == ESP_OK && should_save) {
        ret = nvs_set_blob(handle, D1L_SETTINGS_KEY, &s_current, sizeof(s_current));
        if (ret == ESP_OK) {
            ret = nvs_commit(handle);
        }
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

esp_err_t d1l_settings_save_wifi_profile(const char *ssid, const char *password)
{
    if (!ssid || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(ssid) >= D1L_WIFI_SSID_LEN ||
        (password && strlen(password) >= D1L_WIFI_PASSWORD_LEN)) {
        return ESP_ERR_INVALID_SIZE;
    }
    d1l_settings_t settings = *d1l_settings_current();
    snprintf(settings.wifi_ssid, sizeof(settings.wifi_ssid), "%s", ssid);
    if (password) {
        snprintf(settings.wifi_password, sizeof(settings.wifi_password), "%s", password);
    }
    settings.wifi_profile_saved = true;
    return d1l_settings_save(&settings);
}

esp_err_t d1l_settings_clear_wifi_profile(void)
{
    d1l_settings_t settings = *d1l_settings_current();
    settings.wifi_profile_saved = false;
    settings.wifi_ssid[0] = '\0';
    settings.wifi_password[0] = '\0';
    settings.wifi_enabled = false;
    return d1l_settings_save(&settings);
}

esp_err_t d1l_settings_complete_onboarding(const char *node_name, bool wifi_enabled,
                                           bool ble_companion_enabled, bool observer_enabled)
{
    d1l_settings_t settings = *d1l_settings_current();
    if (node_name && node_name[0] != '\0') {
        snprintf(settings.node_name, sizeof(settings.node_name), "%s", node_name);
    }
    settings.role = D1L_ROLE_DESK_COMPANION;
    settings.wifi_enabled = wifi_enabled;
    settings.ble_companion_enabled = wifi_enabled ? false : ble_companion_enabled;
    settings.observer_enabled = observer_enabled;
    settings.onboarding_complete = true;
    return d1l_settings_save(&settings);
}

esp_err_t d1l_settings_reset_onboarding(void)
{
    d1l_settings_t settings = *d1l_settings_current();
    settings.onboarding_complete = false;
    return d1l_settings_save(&settings);
}

esp_err_t d1l_settings_next_mesh_timestamp(uint32_t *timestamp)
{
    if (timestamp == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(D1L_SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        if (mesh_timestamp_can_fallback(ret)) {
            *timestamp = next_ram_mesh_timestamp(D1L_SETTINGS_MESH_TIMESTAMP_BASE + 1U);
            return ESP_OK;
        }
        return ret;
    }

    uint32_t next = D1L_SETTINGS_MESH_TIMESTAMP_BASE;
    uint32_t stored = 0;
    ret = nvs_get_u32(handle, D1L_SETTINGS_MESH_TIMESTAMP_KEY, &stored);
    if (ret == ESP_OK && stored >= D1L_SETTINGS_MESH_TIMESTAMP_BASE && stored < UINT32_MAX) {
        next = stored + 1U;
    } else if (ret == ESP_ERR_NVS_NOT_FOUND ||
               (ret == ESP_OK && stored < D1L_SETTINGS_MESH_TIMESTAMP_BASE)) {
        next = D1L_SETTINGS_MESH_TIMESTAMP_BASE + 1U;
        ret = ESP_OK;
    } else if (mesh_timestamp_can_fallback(ret)) {
        next = next_ram_mesh_timestamp(next);
        nvs_close(handle);
        *timestamp = next;
        return ESP_OK;
    }

    if (ret == ESP_OK) {
        next = next_ram_mesh_timestamp(next);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u32(handle, D1L_SETTINGS_MESH_TIMESTAMP_KEY, next);
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);

    if (mesh_timestamp_can_fallback(ret)) {
        *timestamp = next;
        return ESP_OK;
    }
    if (ret == ESP_OK) {
        *timestamp = next;
    }
    return ret;
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
