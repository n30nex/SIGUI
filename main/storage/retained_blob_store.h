#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define D1L_RETAINED_BLOB_STORE_SD_DEGRADED_NOTE "SD degraded; using internal fallback"

typedef enum {
    D1L_RETAINED_BLOB_STORE_PUBLIC_MESSAGES = 0,
    D1L_RETAINED_BLOB_STORE_DM_MESSAGES,
    D1L_RETAINED_BLOB_STORE_ROUTES,
    D1L_RETAINED_BLOB_STORE_PACKET_LOG,
    D1L_RETAINED_BLOB_STORE_COUNT,
} d1l_retained_blob_store_id_t;

typedef struct {
    uint32_t sd_read_fail_count;
    uint32_t sd_write_fail_count;
    uint32_t sd_rename_fail_count;
    esp_err_t sd_last_error;
    bool sd_degraded_latched;
    uint32_t nvs_mirror_fail_count;
    esp_err_t nvs_mirror_last_error;
} d1l_retained_blob_store_sd_stats_t;

const char *d1l_retained_blob_store_backend_name(d1l_retained_blob_store_id_t store_id);
bool d1l_retained_blob_store_is_available(d1l_retained_blob_store_id_t store_id);
bool d1l_retained_blob_store_uses_sd(d1l_retained_blob_store_id_t store_id);
bool d1l_retained_blob_store_sd_stats(d1l_retained_blob_store_id_t store_id,
                                      d1l_retained_blob_store_sd_stats_t *out_stats);
bool d1l_retained_blob_store_any_sd_degraded(void);
void d1l_retained_blob_store_note_sd_backend(bool data_ready,
                                             bool file_ops_supported,
                                             bool atomic_rename_supported,
                                             uint32_t file_line_max,
                                             uint32_t file_chunk_max,
                                             uint32_t path_max);
esp_err_t d1l_retained_blob_store_read(d1l_retained_blob_store_id_t store_id,
                                       const char *key,
                                       void *dst,
                                       size_t *len_inout);
esp_err_t d1l_retained_blob_store_read_fallback(d1l_retained_blob_store_id_t store_id,
                                                const char *key,
                                                void *dst,
                                                size_t *len_inout);
esp_err_t d1l_retained_blob_store_write(d1l_retained_blob_store_id_t store_id,
                                        const char *key,
                                        const void *src,
                                        size_t len);
esp_err_t d1l_retained_blob_store_write_split(d1l_retained_blob_store_id_t store_id,
                                              const char *key,
                                              const void *primary_src,
                                              size_t primary_len,
                                              const void *fallback_src,
                                              size_t fallback_len);
esp_err_t d1l_retained_blob_store_erase(d1l_retained_blob_store_id_t store_id,
                                        const char *key);
