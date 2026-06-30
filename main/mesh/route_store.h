#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define D1L_ROUTE_STORE_CAPACITY 16U
#define D1L_ROUTE_TARGET_LEN 17U
#define D1L_ROUTE_LABEL_LEN 24U
#define D1L_ROUTE_KIND_LEN 16U
#define D1L_ROUTE_NAME_LEN 18U

typedef struct {
    uint32_t seq;
    uint32_t first_seen_ms;
    uint32_t last_seen_ms;
    uint32_t seen_count;
    char target[D1L_ROUTE_TARGET_LEN];
    char label[D1L_ROUTE_LABEL_LEN];
    char kind[D1L_ROUTE_KIND_LEN];
    char route[D1L_ROUTE_NAME_LEN];
    char direction[4];
    int last_rssi_dbm;
    int last_snr_tenths;
    uint8_t path_hash_bytes;
    uint8_t path_hops;
    uint8_t confidence;
    uint16_t payload_len;
} d1l_route_entry_t;

typedef struct {
    uint32_t next_seq;
    uint32_t total_written;
    uint32_t dropped_oldest;
    size_t count;
    size_t capacity;
} d1l_route_store_stats_t;

esp_err_t d1l_route_store_init(void);
esp_err_t d1l_route_store_clear(void);
esp_err_t d1l_route_store_upsert_observation(const char *target, const char *label,
                                             const char *kind, const char *route,
                                             const char *direction, int rssi_dbm,
                                             int snr_tenths, uint8_t path_hash_bytes,
                                             uint8_t path_hops, uint16_t payload_len);
d1l_route_store_stats_t d1l_route_store_stats(void);
size_t d1l_route_store_copy_recent(d1l_route_entry_t *out_entries, size_t max_entries);
size_t d1l_route_store_copy_for_target(const char *target, d1l_route_entry_t *out_entries,
                                       size_t max_entries);
esp_err_t d1l_route_store_find_by_seq(uint32_t seq, d1l_route_entry_t *out_entry);
