#include "retained_blob_store.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "hal/rp2040_bridge.h"
#include "nvs.h"

#define D1L_RETAINED_PUBLIC_MESSAGE_NAMESPACE "d1l_messages"
#define D1L_RETAINED_DM_MESSAGE_NAMESPACE "d1l_dms"
#define D1L_RETAINED_ROUTE_NAMESPACE "d1l_routes"
#define D1L_RETAINED_PACKET_LOG_NAMESPACE "d1l_packets"
#define D1L_RETAINED_PUBLIC_MESSAGE_SD_DIR "stores/messages/public"
#define D1L_RETAINED_DM_MESSAGE_SD_DIR "stores/messages/dm"
#define D1L_RETAINED_ROUTE_SD_DIR "stores/routes"
#define D1L_RETAINED_PACKET_LOG_SD_DIR "stores/packet_log"
#define D1L_RETAINED_SD_WRITE_TIMEOUT_MS 750U
#define D1L_RETAINED_SD_READ_TIMEOUT_MS 750U

typedef struct {
    d1l_retained_blob_store_id_t id;
    const char *name;
    const char *nvs_namespace;
    const char *sd_directory;
} d1l_retained_blob_store_config_t;

typedef enum {
    D1L_RETAINED_SD_OP_READ,
    D1L_RETAINED_SD_OP_WRITE,
    D1L_RETAINED_SD_OP_RENAME,
} d1l_retained_blob_store_sd_op_t;

static const d1l_retained_blob_store_config_t s_store_configs[] = {
    {
        .id = D1L_RETAINED_BLOB_STORE_PUBLIC_MESSAGES,
        .name = "public_messages",
        .nvs_namespace = D1L_RETAINED_PUBLIC_MESSAGE_NAMESPACE,
        .sd_directory = D1L_RETAINED_PUBLIC_MESSAGE_SD_DIR,
    },
    {
        .id = D1L_RETAINED_BLOB_STORE_DM_MESSAGES,
        .name = "dm_messages",
        .nvs_namespace = D1L_RETAINED_DM_MESSAGE_NAMESPACE,
        .sd_directory = D1L_RETAINED_DM_MESSAGE_SD_DIR,
    },
    {
        .id = D1L_RETAINED_BLOB_STORE_ROUTES,
        .name = "routes",
        .nvs_namespace = D1L_RETAINED_ROUTE_NAMESPACE,
        .sd_directory = D1L_RETAINED_ROUTE_SD_DIR,
    },
    {
        .id = D1L_RETAINED_BLOB_STORE_PACKET_LOG,
        .name = "packet_log",
        .nvs_namespace = D1L_RETAINED_PACKET_LOG_NAMESPACE,
        .sd_directory = D1L_RETAINED_PACKET_LOG_SD_DIR,
    },
};

static bool s_store_sd_enabled[D1L_RETAINED_BLOB_STORE_COUNT];
static uint32_t s_store_backend_generation[D1L_RETAINED_BLOB_STORE_COUNT];
static d1l_retained_blob_store_sd_stats_t s_store_sd_stats[D1L_RETAINED_BLOB_STORE_COUNT];
static portMUX_TYPE s_store_state_mux = portMUX_INITIALIZER_UNLOCKED;

static bool sd_error_latches_degraded(esp_err_t ret)
{
    return ret != ESP_OK &&
           ret != ESP_ERR_INVALID_ARG &&
           ret != ESP_ERR_INVALID_SIZE &&
           ret != ESP_ERR_NOT_FOUND;
}

static void note_sd_failure(const d1l_retained_blob_store_config_t *config,
                            d1l_retained_blob_store_sd_op_t op,
                            esp_err_t ret)
{
    if (!config || config->id >= D1L_RETAINED_BLOB_STORE_COUNT ||
        ret == ESP_OK || ret == ESP_ERR_INVALID_ARG) {
        return;
    }

    portENTER_CRITICAL(&s_store_state_mux);
    d1l_retained_blob_store_sd_stats_t *stats = &s_store_sd_stats[config->id];
    switch (op) {
    case D1L_RETAINED_SD_OP_READ:
        stats->sd_read_fail_count++;
        break;
    case D1L_RETAINED_SD_OP_WRITE:
        stats->sd_write_fail_count++;
        break;
    case D1L_RETAINED_SD_OP_RENAME:
        stats->sd_rename_fail_count++;
        break;
    default:
        break;
    }
    stats->sd_last_error = ret;
    if (sd_error_latches_degraded(ret)) {
        stats->sd_degraded_latched = true;
    }
    portEXIT_CRITICAL(&s_store_state_mux);
}

static void note_nvs_mirror_failure(const d1l_retained_blob_store_config_t *config,
                                    esp_err_t ret)
{
    if (!config || config->id >= D1L_RETAINED_BLOB_STORE_COUNT || ret == ESP_OK) {
        return;
    }
    portENTER_CRITICAL(&s_store_state_mux);
    d1l_retained_blob_store_sd_stats_t *stats = &s_store_sd_stats[config->id];
    stats->nvs_mirror_fail_count++;
    stats->nvs_mirror_last_error = ret;
    portEXIT_CRITICAL(&s_store_state_mux);
}

static const d1l_retained_blob_store_config_t *find_store(d1l_retained_blob_store_id_t store_id)
{
    for (size_t i = 0; i < sizeof(s_store_configs) / sizeof(s_store_configs[0]); ++i) {
        if (s_store_configs[i].id == store_id) {
            return &s_store_configs[i];
        }
    }
    return NULL;
}

static bool key_is_safe(const char *key)
{
    if (!key || key[0] == '\0') {
        return false;
    }
    for (const char *p = key; *p; ++p) {
        const char c = *p;
        const bool ok = (c >= 'A' && c <= 'Z') ||
                        (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') ||
                        c == '_' || c == '-' || c == '.';
        if (!ok) {
            return false;
        }
    }
    return true;
}

static bool build_sd_path(const d1l_retained_blob_store_config_t *config,
                          const char *key,
                          const char *suffix,
                          char *out_path,
                          size_t out_path_size)
{
    if (!config || !config->sd_directory || !key_is_safe(key) || !suffix ||
        !out_path || out_path_size == 0) {
        return false;
    }
    const int written = snprintf(out_path, out_path_size, "%s/%s%s",
                                 config->sd_directory, key, suffix);
    return written > 0 && (size_t)written < out_path_size;
}

static bool copy_store_backend_state(
    const d1l_retained_blob_store_config_t *config,
    d1l_retained_blob_store_backend_state_t *out_state)
{
    if (!config || config->id >= D1L_RETAINED_BLOB_STORE_COUNT || !out_state) {
        return false;
    }
    portENTER_CRITICAL(&s_store_state_mux);
    out_state->enabled = s_store_sd_enabled[config->id];
    out_state->generation = s_store_backend_generation[config->id];
    portEXIT_CRITICAL(&s_store_state_mux);
    return true;
}

static bool store_backend_generation_matches(
    const d1l_retained_blob_store_config_t *config,
    uint32_t expected_generation)
{
    d1l_retained_blob_store_backend_state_t state = {0};
    return copy_store_backend_state(config, &state) && state.enabled &&
           state.generation == expected_generation;
}

static bool store_sd_enabled(const d1l_retained_blob_store_config_t *config)
{
    d1l_retained_blob_store_backend_state_t state = {0};
    return copy_store_backend_state(config, &state) && state.enabled;
}

static esp_err_t nvs_read_blob(const d1l_retained_blob_store_config_t *config,
                               const char *key,
                               void *dst,
                               size_t *len_inout)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(config->nvs_namespace, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = nvs_get_blob(handle, key, dst, len_inout);
    nvs_close(handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_ERR_NOT_FOUND;
    }
    return ret;
}

static esp_err_t nvs_write_blob(const d1l_retained_blob_store_config_t *config,
                                const char *key,
                                const void *src,
                                size_t len)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(config->nvs_namespace, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = nvs_set_blob(handle, key, src, len);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

static esp_err_t nvs_erase_blob(const d1l_retained_blob_store_config_t *config,
                                const char *key)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(config->nvs_namespace, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = nvs_erase_key(handle, key);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ret = ESP_OK;
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

static esp_err_t sd_read_blob(const d1l_retained_blob_store_config_t *config,
                              const char *key,
                              void *dst,
                              size_t *len_inout)
{
    char path[D1L_RP2040_FILE_PATH_MAX + 1U];
    if (!build_sd_path(config, key, ".bin", path, sizeof(path)) || !dst || !len_inout) {
        return ESP_ERR_INVALID_ARG;
    }

    d1l_rp2040_file_result_t stat = {0};
    esp_err_t ret = d1l_rp2040_bridge_file_stat(path, &stat, D1L_RETAINED_SD_READ_TIMEOUT_MS);
    if (ret != ESP_OK) {
        return ret;
    }
    if (!stat.exists) {
        return ESP_ERR_NOT_FOUND;
    }
    if (stat.is_directory) {
        return ESP_ERR_INVALID_STATE;
    }
    if (stat.size > *len_inout) {
        *len_inout = stat.size;
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t *out = (uint8_t *)dst;
    size_t offset = 0;
    while (offset < stat.size) {
        const size_t remaining = stat.size - offset;
        const size_t chunk = remaining > D1L_RP2040_FILE_CHUNK_MAX ?
                             D1L_RP2040_FILE_CHUNK_MAX : remaining;
        d1l_rp2040_file_result_t read_result = {0};
        ret = d1l_rp2040_bridge_file_read(path, (uint32_t)offset,
                                          out + offset, chunk, &read_result,
                                          D1L_RETAINED_SD_READ_TIMEOUT_MS);
        if (ret != ESP_OK) {
            return ret;
        }
        if (read_result.length == 0 && !read_result.eof) {
            return ESP_FAIL;
        }
        offset += read_result.length;
        if (read_result.eof && offset < stat.size) {
            return ESP_FAIL;
        }
    }
    *len_inout = offset;
    return ESP_OK;
}

static esp_err_t sd_write_blob_for_generation(
    const d1l_retained_blob_store_config_t *config,
    const char *key,
    const void *src,
    size_t len,
    uint32_t expected_generation)
{
    char path[D1L_RP2040_FILE_PATH_MAX + 1U];
    char temp_path[D1L_RP2040_FILE_PATH_MAX + 1U];
    if (!build_sd_path(config, key, ".bin", path, sizeof(path)) ||
        !build_sd_path(config, key, ".tmp", temp_path, sizeof(temp_path)) ||
        !src || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!store_backend_generation_matches(config, expected_generation)) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t *data = (const uint8_t *)src;
    size_t offset = 0;
    while (offset < len) {
        const size_t remaining = len - offset;
        const size_t chunk = remaining > D1L_RP2040_FILE_CHUNK_MAX ?
                             D1L_RP2040_FILE_CHUNK_MAX : remaining;
        d1l_rp2040_file_result_t write_result = {0};
        esp_err_t ret = d1l_rp2040_bridge_file_write(temp_path, (uint32_t)offset,
                                                     data + offset, chunk,
                                                     offset == 0, &write_result,
                                                     D1L_RETAINED_SD_WRITE_TIMEOUT_MS);
        if (ret != ESP_OK || write_result.length != chunk) {
            const esp_err_t failure = ret == ESP_OK ? ESP_FAIL : ret;
            note_sd_failure(config, D1L_RETAINED_SD_OP_WRITE, failure);
            if (store_backend_generation_matches(config, expected_generation)) {
                d1l_rp2040_file_result_t ignored = {0};
                (void)d1l_rp2040_bridge_file_delete(
                    temp_path, &ignored, D1L_RETAINED_SD_WRITE_TIMEOUT_MS);
            }
            return failure;
        }
        offset += chunk;
    }

    /* The replace-rename is the destructive commit point. If media changed
     * while chunks were written, leave the temp path alone: it may now belong
     * to the replacement card, and the old primary must remain untouched. */
    if (!store_backend_generation_matches(config, expected_generation)) {
        return ESP_ERR_INVALID_STATE;
    }
    d1l_rp2040_file_result_t rename_result = {0};
    esp_err_t ret = d1l_rp2040_bridge_file_rename(temp_path, path, true, &rename_result,
                                                  D1L_RETAINED_SD_WRITE_TIMEOUT_MS);
    if (ret != ESP_OK) {
        note_sd_failure(config, D1L_RETAINED_SD_OP_RENAME, ret);
    }
    return ret;
}

static esp_err_t sd_write_blob(const d1l_retained_blob_store_config_t *config,
                               const char *key,
                               const void *src,
                               size_t len)
{
    d1l_retained_blob_store_backend_state_t state = {0};
    if (!copy_store_backend_state(config, &state) || !state.enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    return sd_write_blob_for_generation(config, key, src, len,
                                        state.generation);
}

static esp_err_t sd_erase_blob_for_generation(
    const d1l_retained_blob_store_config_t *config,
    const char *key,
    uint32_t expected_generation)
{
    char path[D1L_RP2040_FILE_PATH_MAX + 1U];
    char temp_path[D1L_RP2040_FILE_PATH_MAX + 1U];
    if (!build_sd_path(config, key, ".bin", path, sizeof(path)) ||
        !build_sd_path(config, key, ".tmp", temp_path, sizeof(temp_path))) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!store_backend_generation_matches(config, expected_generation)) {
        return ESP_ERR_INVALID_STATE;
    }

    d1l_rp2040_file_result_t result = {0};
    esp_err_t ret = d1l_rp2040_bridge_file_delete(path, &result,
                                                  D1L_RETAINED_SD_WRITE_TIMEOUT_MS);
    if (ret == ESP_ERR_NOT_FOUND) {
        ret = ESP_OK;
    }
    if (!store_backend_generation_matches(config, expected_generation)) {
        return ESP_ERR_INVALID_STATE;
    }
    d1l_rp2040_file_result_t temp_result = {0};
    esp_err_t temp_ret = d1l_rp2040_bridge_file_delete(temp_path, &temp_result,
                                                       D1L_RETAINED_SD_WRITE_TIMEOUT_MS);
    if (temp_ret == ESP_ERR_NOT_FOUND) {
        temp_ret = ESP_OK;
    }
    return ret == ESP_OK ? temp_ret : ret;
}

static esp_err_t sd_erase_blob(const d1l_retained_blob_store_config_t *config,
                               const char *key)
{
    d1l_retained_blob_store_backend_state_t state = {0};
    if (!copy_store_backend_state(config, &state) || !state.enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    return sd_erase_blob_for_generation(config, key, state.generation);
}

const char *d1l_retained_blob_store_backend_name(d1l_retained_blob_store_id_t store_id)
{
    const d1l_retained_blob_store_config_t *config = find_store(store_id);
    if (!config) {
        return "unavailable";
    }
    return store_sd_enabled(config) ? "sd" : "nvs";
}

bool d1l_retained_blob_store_is_available(d1l_retained_blob_store_id_t store_id)
{
    return find_store(store_id) != NULL;
}

bool d1l_retained_blob_store_uses_sd(d1l_retained_blob_store_id_t store_id)
{
    d1l_retained_blob_store_backend_state_t state = {0};
    return d1l_retained_blob_store_backend_state(store_id, &state) && state.enabled;
}

bool d1l_retained_blob_store_backend_state(
    d1l_retained_blob_store_id_t store_id,
    d1l_retained_blob_store_backend_state_t *out_state)
{
    const d1l_retained_blob_store_config_t *config = find_store(store_id);
    if (!config || !out_state) {
        return false;
    }
    return copy_store_backend_state(config, out_state);
}

bool d1l_retained_blob_store_sd_stats(d1l_retained_blob_store_id_t store_id,
                                      d1l_retained_blob_store_sd_stats_t *out_stats)
{
    const d1l_retained_blob_store_config_t *config = find_store(store_id);
    if (!config || !out_stats) {
        return false;
    }
    portENTER_CRITICAL(&s_store_state_mux);
    *out_stats = s_store_sd_stats[config->id];
    portEXIT_CRITICAL(&s_store_state_mux);
    return true;
}

bool d1l_retained_blob_store_any_sd_degraded(void)
{
    bool degraded = false;
    portENTER_CRITICAL(&s_store_state_mux);
    for (size_t i = 0; i < D1L_RETAINED_BLOB_STORE_COUNT; ++i) {
        if (s_store_sd_stats[i].sd_degraded_latched) {
            degraded = true;
            break;
        }
    }
    portEXIT_CRITICAL(&s_store_state_mux);
    return degraded;
}

void d1l_retained_blob_store_note_sd_backend(bool data_ready,
                                             bool file_ops_supported,
                                             bool atomic_rename_supported,
                                             uint32_t file_line_max,
                                             uint32_t file_chunk_max,
                                             uint32_t path_max)
{
    const bool can_use_retained_sd =
        data_ready &&
        file_ops_supported &&
        atomic_rename_supported &&
        file_line_max >= D1L_RP2040_FILE_LINE_MAX &&
        file_chunk_max >= D1L_RP2040_FILE_CHUNK_MAX &&
        path_max >= D1L_RP2040_FILE_PATH_MAX;

    portENTER_CRITICAL(&s_store_state_mux);
    for (size_t i = 0; i < D1L_RETAINED_BLOB_STORE_COUNT; ++i) {
        if (s_store_sd_enabled[i] != can_use_retained_sd) {
            s_store_sd_enabled[i] = can_use_retained_sd;
            if (s_store_backend_generation[i] < UINT32_MAX) {
                s_store_backend_generation[i]++;
            }
        }
    }
    portEXIT_CRITICAL(&s_store_state_mux);
}

esp_err_t d1l_retained_blob_store_read_sd_primary(
    d1l_retained_blob_store_id_t store_id,
    const char *key,
    void *dst,
    size_t *len_inout)
{
    const d1l_retained_blob_store_config_t *config = find_store(store_id);
    if (!config || !key || !dst || !len_inout) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!store_sd_enabled(config)) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = sd_read_blob(config, key, dst, len_inout);
    if (ret != ESP_OK && ret != ESP_ERR_NOT_FOUND) {
        note_sd_failure(config, D1L_RETAINED_SD_OP_READ, ret);
    }
    return ret;
}

esp_err_t d1l_retained_blob_store_read_nvs_fallback(
    d1l_retained_blob_store_id_t store_id,
    const char *key,
    void *dst,
    size_t *len_inout)
{
    const d1l_retained_blob_store_config_t *config = find_store(store_id);
    if (!config || !key || !dst || !len_inout) {
        return ESP_ERR_INVALID_ARG;
    }
    return nvs_read_blob(config, key, dst, len_inout);
}

esp_err_t d1l_retained_blob_store_write_sd_primary(
    d1l_retained_blob_store_id_t store_id,
    const char *key,
    const void *src,
    size_t len)
{
    const d1l_retained_blob_store_config_t *config = find_store(store_id);
    if (!config || !key || !src || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!store_sd_enabled(config)) {
        return ESP_ERR_INVALID_STATE;
    }
    return sd_write_blob(config, key, src, len);
}

esp_err_t d1l_retained_blob_store_write_sd_primary_guarded(
    d1l_retained_blob_store_id_t store_id,
    const char *key,
    const void *src,
    size_t len,
    uint32_t expected_generation)
{
    const d1l_retained_blob_store_config_t *config = find_store(store_id);
    if (!config || !key || !src || len == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    return sd_write_blob_for_generation(config, key, src, len,
                                        expected_generation);
}

esp_err_t d1l_retained_blob_store_write_nvs_fallback(
    d1l_retained_blob_store_id_t store_id,
    const char *key,
    const void *src,
    size_t len)
{
    const d1l_retained_blob_store_config_t *config = find_store(store_id);
    if (!config || !key || !src || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = nvs_write_blob(config, key, src, len);
    note_nvs_mirror_failure(config, ret);
    return ret;
}

esp_err_t d1l_retained_blob_store_erase_sd_primary(
    d1l_retained_blob_store_id_t store_id,
    const char *key)
{
    const d1l_retained_blob_store_config_t *config = find_store(store_id);
    if (!config || !key) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!store_sd_enabled(config)) {
        return ESP_ERR_INVALID_STATE;
    }
    return sd_erase_blob(config, key);
}

esp_err_t d1l_retained_blob_store_erase_sd_primary_guarded(
    d1l_retained_blob_store_id_t store_id,
    const char *key,
    uint32_t expected_generation)
{
    const d1l_retained_blob_store_config_t *config = find_store(store_id);
    if (!config || !key) {
        return ESP_ERR_INVALID_ARG;
    }
    return sd_erase_blob_for_generation(config, key, expected_generation);
}

esp_err_t d1l_retained_blob_store_erase_nvs_fallback(
    d1l_retained_blob_store_id_t store_id,
    const char *key)
{
    const d1l_retained_blob_store_config_t *config = find_store(store_id);
    if (!config || !key) {
        return ESP_ERR_INVALID_ARG;
    }
    return nvs_erase_blob(config, key);
}

esp_err_t d1l_retained_blob_store_read(d1l_retained_blob_store_id_t store_id,
                                       const char *key,
                                       void *dst,
                                       size_t *len_inout)
{
    const d1l_retained_blob_store_config_t *config = find_store(store_id);
    if (!config || !key || !dst || !len_inout) {
        return ESP_ERR_INVALID_ARG;
    }

    if (store_sd_enabled(config)) {
        const size_t requested_len = *len_inout;
        esp_err_t sd_ret = sd_read_blob(config, key, dst, len_inout);
        if (sd_ret == ESP_OK) {
            return ESP_OK;
        }
        note_sd_failure(config, D1L_RETAINED_SD_OP_READ, sd_ret);
        *len_inout = requested_len;
        esp_err_t nvs_ret = nvs_read_blob(config, key, dst, len_inout);
        if (nvs_ret == ESP_OK) {
            return ESP_OK;
        }
        return sd_ret == ESP_ERR_NOT_FOUND ? nvs_ret : sd_ret;
    }

    return nvs_read_blob(config, key, dst, len_inout);
}

esp_err_t d1l_retained_blob_store_read_fallback(d1l_retained_blob_store_id_t store_id,
                                                const char *key,
                                                void *dst,
                                                size_t *len_inout)
{
    const d1l_retained_blob_store_config_t *config = find_store(store_id);
    if (!config || !key || !dst || !len_inout) {
        return ESP_ERR_INVALID_ARG;
    }
    return nvs_read_blob(config, key, dst, len_inout);
}

esp_err_t d1l_retained_blob_store_write(d1l_retained_blob_store_id_t store_id,
                                        const char *key,
                                        const void *src,
                                        size_t len)
{
    const d1l_retained_blob_store_config_t *config = find_store(store_id);
    if (!config || !key || !src || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (store_sd_enabled(config)) {
        esp_err_t sd_ret = sd_write_blob(config, key, src, len);
        if (sd_ret == ESP_OK) {
            note_nvs_mirror_failure(config, nvs_write_blob(config, key, src, len));
            return ESP_OK;
        }
    }

    return nvs_write_blob(config, key, src, len);
}

esp_err_t d1l_retained_blob_store_write_split(d1l_retained_blob_store_id_t store_id,
                                              const char *key,
                                              const void *primary_src,
                                              size_t primary_len,
                                              const void *fallback_src,
                                              size_t fallback_len)
{
    const d1l_retained_blob_store_config_t *config = find_store(store_id);
    const bool has_primary = primary_src && primary_len > 0;
    const bool has_fallback = fallback_src && fallback_len > 0;
    if (!config || !key || (!has_primary && !has_fallback)) {
        return ESP_ERR_INVALID_ARG;
    }

    const void *nvs_src = has_fallback ? fallback_src : primary_src;
    const size_t nvs_len = has_fallback ? fallback_len : primary_len;
    if (!store_sd_enabled(config) || !has_primary) {
        return nvs_write_blob(config, key, nvs_src, nvs_len);
    }

    esp_err_t sd_ret = sd_write_blob(config, key, primary_src, primary_len);
    esp_err_t nvs_ret = nvs_write_blob(config, key, nvs_src, nvs_len);
    if (sd_ret == ESP_OK) {
        note_nvs_mirror_failure(config, nvs_ret);
        return ESP_OK;
    }
    return nvs_ret == ESP_OK ? ESP_OK : sd_ret;
}

esp_err_t d1l_retained_blob_store_erase(d1l_retained_blob_store_id_t store_id,
                                        const char *key)
{
    const d1l_retained_blob_store_config_t *config = find_store(store_id);
    if (!config || !key) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_OK;
    if (store_sd_enabled(config)) {
        ret = sd_erase_blob(config, key);
        if (ret == ESP_ERR_NOT_FOUND) {
            ret = ESP_OK;
        }
    }
    esp_err_t nvs_ret = nvs_erase_blob(config, key);
    return ret == ESP_OK ? nvs_ret : ret;
}
