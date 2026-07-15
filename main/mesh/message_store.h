#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "mesh/user_text.h"

#define D1L_MESSAGE_STORE_CAPACITY 16U
#define D1L_MESSAGE_AUTHOR_LEN 24U
#define D1L_MESSAGE_MAX_BYTES D1L_USER_TEXT_MAX_BYTES
/* Compatibility alias for existing fixed buffers/UI callers.  The protocol
 * limit is encoded UTF-8 bytes, not displayed characters. */
#define D1L_MESSAGE_MAX_CHARS D1L_MESSAGE_MAX_BYTES
#define D1L_MESSAGE_TEXT_LEN (D1L_MESSAGE_MAX_BYTES + 1U)
#define D1L_MESSAGE_STORE_PERSIST_RETRY_MS 5000U

typedef struct {
    uint32_t seq;
    uint32_t uptime_ms;
    char direction[4];
    char author[D1L_MESSAGE_AUTHOR_LEN];
    char text[D1L_MESSAGE_TEXT_LEN];
    int rssi_dbm;
    int snr_tenths;
    uint8_t path_hash_bytes;
    uint8_t path_hops;
    bool delivered;
} d1l_message_entry_t;

typedef struct {
    uint32_t next_seq;
    uint32_t total_written;
    uint32_t dropped_oldest;
    uint32_t epoch;
    uint32_t content_revision;
    uint64_t clear_lineage;
    uint32_t persistence_commit_count;
    uint32_t persistence_fail_count;
    uint32_t persistence_stale_snapshot_count;
    uint32_t sd_primary_commit_count;
    uint32_t sd_primary_fail_count;
    uint32_t sd_backend_generation;
    esp_err_t sd_primary_last_error;
    uint32_t nvs_fallback_commit_count;
    uint32_t nvs_fallback_fail_count;
    esp_err_t nvs_fallback_last_error;
    uint64_t persistence_revision;
    size_t count;
    size_t capacity;
    bool loaded;
    bool persistence_dirty;
    bool sd_primary_required;
    bool sd_primary_dirty;
    bool sd_primary_reconcile_pending;
    bool nvs_fallback_dirty;
} d1l_message_store_stats_t;

esp_err_t d1l_message_store_init(void);
esp_err_t d1l_message_store_clear(void);
esp_err_t d1l_message_store_flush(void);
esp_err_t d1l_message_store_flush_if_due(void);
esp_err_t d1l_message_store_append_public(const char *direction, const char *author,
                                          const char *text, int rssi_dbm, int snr_tenths,
                                          uint8_t path_hash_bytes, uint8_t path_hops,
                                          bool delivered);
esp_err_t d1l_message_store_append_public_volatile(const char *direction, const char *author,
                                                   const char *text, int rssi_dbm, int snr_tenths,
                                                   uint8_t path_hash_bytes, uint8_t path_hops,
                                                   bool delivered);
d1l_message_store_stats_t d1l_message_store_stats(void);
size_t d1l_message_store_copy_recent(d1l_message_entry_t *out_entries, size_t max_entries);
size_t d1l_message_store_query_page(d1l_message_entry_t *out_entries, size_t max_entries,
                                    size_t skip_newest, const char *query,
                                    size_t *out_total_matches);
size_t d1l_message_store_query(d1l_message_entry_t *out_entries, size_t max_entries,
                               const char *query);
