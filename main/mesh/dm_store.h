#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "mesh/contact_store.h"
#include "mesh/message_store.h"

#define D1L_DM_STORE_CAPACITY 16U
#define D1L_DM_DIRECTION_LEN 4U
#define D1L_DM_STORE_PERSIST_RETRY_INTERVAL_MS 5000U

typedef struct {
    uint32_t seq;
    uint32_t uptime_ms;
    char contact_fingerprint[D1L_NODE_FINGERPRINT_LEN];
    char contact_alias[D1L_CONTACT_ALIAS_LEN];
    char direction[D1L_DM_DIRECTION_LEN];
    char text[D1L_MESSAGE_TEXT_LEN];
    int rssi_dbm;
    int snr_tenths;
    uint8_t path_hash_bytes;
    uint8_t path_hops;
    uint8_t attempt;
    bool delivered;
    bool acked;
    uint32_t ack_hash;
} d1l_dm_entry_t;

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
} d1l_dm_store_stats_t;

esp_err_t d1l_dm_store_init(void);
esp_err_t d1l_dm_store_clear(void);
esp_err_t d1l_dm_store_flush(void);
esp_err_t d1l_dm_store_flush_if_due(void);
esp_err_t d1l_dm_store_append(const char *contact_fingerprint, const char *contact_alias,
                              const char *direction, const char *text, int rssi_dbm,
                              int snr_tenths, uint8_t path_hash_bytes, uint8_t path_hops,
                              uint8_t attempt, bool delivered, bool acked,
                              uint32_t ack_hash);
esp_err_t d1l_dm_store_append_volatile(const char *contact_fingerprint, const char *contact_alias,
                                       const char *direction, const char *text, int rssi_dbm,
                                       int snr_tenths, uint8_t path_hash_bytes, uint8_t path_hops,
                                       uint8_t attempt, bool delivered, bool acked,
                                       uint32_t ack_hash);
esp_err_t d1l_dm_store_mark_acked(uint32_t ack_hash, d1l_dm_entry_t *out_entry);
d1l_dm_store_stats_t d1l_dm_store_stats(void);
size_t d1l_dm_store_copy_recent_page(d1l_dm_entry_t *out_entries, size_t max_entries,
                                     size_t skip_newest, size_t *out_total_matches);
size_t d1l_dm_store_copy_recent(d1l_dm_entry_t *out_entries, size_t max_entries);
size_t d1l_dm_store_copy_thread_page(const char *contact_fingerprint,
                                     d1l_dm_entry_t *out_entries,
                                     size_t max_entries, size_t skip_newest,
                                     size_t *out_total_matches);
size_t d1l_dm_store_copy_thread(const char *contact_fingerprint, d1l_dm_entry_t *out_entries,
                                size_t max_entries);
