#include "retained_blob_store.h"

#include "nvs.h"

#define D1L_RETAINED_PACKET_LOG_NAMESPACE "d1l_packets"

typedef struct {
    d1l_retained_blob_store_id_t id;
    const char *name;
    const char *nvs_namespace;
} d1l_retained_blob_store_config_t;

static const d1l_retained_blob_store_config_t s_store_configs[] = {
    {
        .id = D1L_RETAINED_BLOB_STORE_PACKET_LOG,
        .name = "packet_log",
        .nvs_namespace = D1L_RETAINED_PACKET_LOG_NAMESPACE,
    },
};

static const d1l_retained_blob_store_config_t *find_store(d1l_retained_blob_store_id_t store_id)
{
    for (size_t i = 0; i < sizeof(s_store_configs) / sizeof(s_store_configs[0]); ++i) {
        if (s_store_configs[i].id == store_id) {
            return &s_store_configs[i];
        }
    }
    return NULL;
}

const char *d1l_retained_blob_store_backend_name(d1l_retained_blob_store_id_t store_id)
{
    return find_store(store_id) ? "nvs" : "unavailable";
}

bool d1l_retained_blob_store_is_available(d1l_retained_blob_store_id_t store_id)
{
    return find_store(store_id) != NULL;
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

esp_err_t d1l_retained_blob_store_write(d1l_retained_blob_store_id_t store_id,
                                        const char *key,
                                        const void *src,
                                        size_t len)
{
    const d1l_retained_blob_store_config_t *config = find_store(store_id);
    if (!config || !key || !src || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

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

esp_err_t d1l_retained_blob_store_erase(d1l_retained_blob_store_id_t store_id,
                                        const char *key)
{
    const d1l_retained_blob_store_config_t *config = find_store(store_id);
    if (!config || !key) {
        return ESP_ERR_INVALID_ARG;
    }

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
