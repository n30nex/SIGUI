#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "hal/rp2040_bridge.h"
#include "nvs.h"
#include "storage/retained_blob_store.h"

static bool s_toggle_backend_during_write;
static bool s_toggle_backend_during_delete;
static uint32_t s_rename_count;
static uint32_t s_delete_count;

esp_err_t nvs_open(const char *namespace_name, nvs_open_mode_t open_mode,
                   nvs_handle_t *out_handle)
{
    (void)namespace_name;
    (void)open_mode;
    (void)out_handle;
    return ESP_FAIL;
}

void nvs_close(nvs_handle_t handle)
{
    (void)handle;
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out_value,
                       size_t *length)
{
    (void)handle;
    (void)key;
    (void)out_value;
    (void)length;
    return ESP_ERR_NVS_NOT_FOUND;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value,
                       size_t length)
{
    (void)handle;
    (void)key;
    (void)value;
    (void)length;
    return ESP_FAIL;
}

esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key)
{
    (void)handle;
    (void)key;
    return ESP_FAIL;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    (void)handle;
    return ESP_FAIL;
}

esp_err_t d1l_rp2040_bridge_file_stat(const char *path,
                                      d1l_rp2040_file_result_t *out_result,
                                      uint32_t timeout_ms)
{
    (void)path;
    (void)out_result;
    (void)timeout_ms;
    return ESP_FAIL;
}

esp_err_t d1l_rp2040_bridge_file_read(const char *path, uint32_t offset,
                                      uint8_t *out_data, size_t max_len,
                                      d1l_rp2040_file_result_t *out_result,
                                      uint32_t timeout_ms)
{
    (void)path;
    (void)offset;
    (void)out_data;
    (void)max_len;
    (void)out_result;
    (void)timeout_ms;
    return ESP_FAIL;
}

esp_err_t d1l_rp2040_bridge_file_write(const char *path, uint32_t offset,
                                       const uint8_t *data, size_t len,
                                       bool truncate,
                                       d1l_rp2040_file_result_t *out_result,
                                       uint32_t timeout_ms)
{
    (void)path;
    (void)offset;
    (void)data;
    (void)len;
    (void)truncate;
    assert(out_result);
    (void)timeout_ms;
    out_result->length = (uint32_t)len;
    if (s_toggle_backend_during_write) {
        s_toggle_backend_during_write = false;
        d1l_retained_blob_store_note_sd_backend(false, false, false,
                                                0U, 0U, 0U);
        d1l_retained_blob_store_note_sd_backend(
            true, true, true, D1L_RP2040_FILE_LINE_MAX,
            D1L_RP2040_FILE_CHUNK_MAX, D1L_RP2040_FILE_PATH_MAX);
    }
    return ESP_OK;
}

esp_err_t d1l_rp2040_bridge_file_delete(const char *path,
                                        d1l_rp2040_file_result_t *out_result,
                                        uint32_t timeout_ms)
{
    (void)path;
    (void)out_result;
    (void)timeout_ms;
    s_delete_count++;
    if (s_toggle_backend_during_delete) {
        s_toggle_backend_during_delete = false;
        d1l_retained_blob_store_note_sd_backend(false, false, false,
                                                0U, 0U, 0U);
        d1l_retained_blob_store_note_sd_backend(
            true, true, true, D1L_RP2040_FILE_LINE_MAX,
            D1L_RP2040_FILE_CHUNK_MAX, D1L_RP2040_FILE_PATH_MAX);
    }
    return ESP_OK;
}

esp_err_t d1l_rp2040_bridge_file_rename(const char *from_path, const char *to_path,
                                        bool replace,
                                        d1l_rp2040_file_result_t *out_result,
                                        uint32_t timeout_ms)
{
    (void)from_path;
    (void)to_path;
    (void)replace;
    (void)out_result;
    (void)timeout_ms;
    s_rename_count++;
    return ESP_OK;
}

static d1l_retained_blob_store_backend_state_t state_for(
    d1l_retained_blob_store_id_t store_id)
{
    d1l_retained_blob_store_backend_state_t state = {0};
    assert(d1l_retained_blob_store_backend_state(store_id, &state));
    return state;
}

static void assert_all_states(bool enabled, uint32_t generation)
{
    for (int id = 0; id < D1L_RETAINED_BLOB_STORE_COUNT; ++id) {
        const d1l_retained_blob_store_backend_state_t state =
            state_for((d1l_retained_blob_store_id_t)id);
        assert(state.enabled == enabled);
        assert(state.generation == generation);
        assert(d1l_retained_blob_store_uses_sd(
                   (d1l_retained_blob_store_id_t)id) == enabled);
    }
}

int main(void)
{
    assert_all_states(false, 0U);

    d1l_retained_blob_store_backend_state_t invalid = {
        .enabled = true,
        .generation = 99U,
    };
    assert(!d1l_retained_blob_store_backend_state(
        D1L_RETAINED_BLOB_STORE_COUNT, &invalid));
    assert(invalid.enabled && invalid.generation == 99U);
    assert(!d1l_retained_blob_store_backend_state(
        D1L_RETAINED_BLOB_STORE_ROUTES, NULL));

    d1l_retained_blob_store_note_sd_backend(false, false, false, 0U, 0U, 0U);
    assert_all_states(false, 0U);

    d1l_retained_blob_store_note_sd_backend(
        true, true, true, D1L_RP2040_FILE_LINE_MAX,
        D1L_RP2040_FILE_CHUNK_MAX, D1L_RP2040_FILE_PATH_MAX);
    assert_all_states(true, 1U);

    d1l_retained_blob_store_note_sd_backend(
        true, true, true, D1L_RP2040_FILE_LINE_MAX,
        D1L_RP2040_FILE_CHUNK_MAX, D1L_RP2040_FILE_PATH_MAX);
    assert_all_states(true, 1U);

    /* A complete false -> true cycle remains visible to a later consumer even
     * when it never sampled the intermediate disabled state. */
    d1l_retained_blob_store_note_sd_backend(false, false, false, 0U, 0U, 0U);
    d1l_retained_blob_store_note_sd_backend(
        true, true, true, D1L_RP2040_FILE_LINE_MAX,
        D1L_RP2040_FILE_CHUNK_MAX, D1L_RP2040_FILE_PATH_MAX);
    assert_all_states(true, 3U);

    static const uint8_t payload[] = "guarded retained blob";
    s_toggle_backend_during_write = true;
    assert(d1l_retained_blob_store_write_sd_primary_guarded(
               D1L_RETAINED_BLOB_STORE_ROUTES, "routes_v2",
               payload, sizeof(payload), 3U) == ESP_ERR_INVALID_STATE);
    assert_all_states(true, 5U);
    assert(s_rename_count == 0U);
    assert(s_delete_count == 0U);

    assert(d1l_retained_blob_store_write_sd_primary_guarded(
               D1L_RETAINED_BLOB_STORE_ROUTES, "routes_v2",
               payload, sizeof(payload), 5U) == ESP_OK);
    assert(s_rename_count == 1U);

    /* Generic Public/DM/packet split writers share the same guarded internal
     * commit point even though they do not supply an external generation. */
    s_toggle_backend_during_write = true;
    assert(d1l_retained_blob_store_write_split(
               D1L_RETAINED_BLOB_STORE_PACKET_LOG, "ring",
               payload, sizeof(payload), payload, sizeof(payload)) ==
           ESP_ERR_INVALID_STATE);
    assert_all_states(true, 7U);
    assert(s_rename_count == 1U);
    assert(s_delete_count == 0U);

    s_toggle_backend_during_delete = true;
    assert(d1l_retained_blob_store_erase_sd_primary_guarded(
               D1L_RETAINED_BLOB_STORE_ROUTES, "routes_v2", 7U) ==
           ESP_ERR_INVALID_STATE);
    assert_all_states(true, 9U);
    assert(s_delete_count == 1U);

    puts("native retained backend generation: ok");
    return 0;
}
