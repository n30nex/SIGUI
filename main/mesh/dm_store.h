#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "mesh/contact_store.h"
#include "mesh/message_store.h"

#define D1L_DM_STORE_CAPACITY 16U
#define D1L_DM_DIRECTION_LEN 4U

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
    size_t count;
    size_t capacity;
} d1l_dm_store_stats_t;

esp_err_t d1l_dm_store_init(void);
esp_err_t d1l_dm_store_clear(void);
esp_err_t d1l_dm_store_append(const char *contact_fingerprint, const char *contact_alias,
                              const char *direction, const char *text, int rssi_dbm,
                              int snr_tenths, uint8_t path_hash_bytes, uint8_t path_hops,
                              uint8_t attempt, bool delivered, bool acked,
                              uint32_t ack_hash);
esp_err_t d1l_dm_store_mark_acked(uint32_t ack_hash, d1l_dm_entry_t *out_entry);
d1l_dm_store_stats_t d1l_dm_store_stats(void);
size_t d1l_dm_store_copy_recent(d1l_dm_entry_t *out_entries, size_t max_entries);
