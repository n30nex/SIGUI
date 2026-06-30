#include "retained_blob_store.h"

#include <stdio.h>
#include <string.h>

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

static bool store_sd_enabled(const d1l_retained_blob_store_config_t *config)
{
    return config && config->id < D1L_RETAINED_BLOB_STORE_COUNT &&
           s_store_sd_enabled[config->id];
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

static esp_err_t sd_write_blob(const d1l_retained_blob_store_config_t *config,
                               const char *key,
                               const void *src,
                               size_t len)
{
    char path[D1L_RP2040_FILE_PATH_MAX + 1U];
    char temp_path[D1L_RP2040_FILE_PATH_MAX + 1U];
    if (!build_sd_path(config, key, ".bin", path, sizeof(path)) ||
        !build_sd_path(config, key, ".tmp", temp_path, sizeof(temp_path)) ||
        !src || len == 0) {
        return ESP_ERR_INVALID_ARG;
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
            d1l_rp2040_file_result_t ignored = {0};
            (void)d1l_rp2040_bridge_file_delete(temp_path, &ignored,
                                                D1L_RETAINED_SD_WRITE_TIMEOUT_MS);
            return ret == ESP_OK ? ESP_FAIL : ret;
        }
        offset += chunk;
    }

    d1l_rp2040_file_result_t rename_result = {0};
    return d1l_rp2040_bridge_file_rename(temp_path, path, true, &rename_result,
                                         D1L_RETAINED_SD_WRITE_TIMEOUT_MS);
}

static esp_err_t sd_erase_blob(const d1l_retained_blob_store_config_t *config,
                               const char *key)
{
    char path[D1L_RP2040_FILE_PATH_MAX + 1U];
    char temp_path[D1L_RP2040_FILE_PATH_MAX + 1U];
    if (!build_sd_path(config, key, ".bin", path, sizeof(path)) ||
        !build_sd_path(config, key, ".tmp", temp_path, sizeof(temp_path))) {
        return ESP_ERR_INVALID_ARG;
    }

    d1l_rp2040_file_result_t result = {0};
    esp_err_t ret = d1l_rp2040_bridge_file_delete(path, &result,
                                                  D1L_RETAINED_SD_WRITE_TIMEOUT_MS);
    if (ret == ESP_ERR_NOT_FOUND) {
        ret = ESP_OK;
    }
    d1l_rp2040_file_result_t temp_result = {0};
    esp_err_t temp_ret = d1l_rp2040_bridge_file_delete(temp_path, &temp_result,
                                                       D1L_RETAINED_SD_WRITE_TIMEOUT_MS);
    if (temp_ret == ESP_ERR_NOT_FOUND) {
        temp_ret = ESP_OK;
    }
    return ret == ESP_OK ? temp_ret : ret;
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
    return store_sd_enabled(find_store(store_id));
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

    for (size_t i = 0; i < D1L_RETAINED_BLOB_STORE_COUNT; ++i) {
        s_store_sd_enabled[i] = can_use_retained_sd;
    }
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
            (void)nvs_write_blob(config, key, src, len);
            return sd_ret;
        }
    }

    return nvs_write_blob(config, key, src, len);
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
