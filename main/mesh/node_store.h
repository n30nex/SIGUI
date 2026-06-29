#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define D1L_NODE_STORE_CAPACITY 16U
#define D1L_NODE_FINGERPRINT_LEN 17U
#define D1L_HEARD_NODE_NAME_LEN 24U
#define D1L_NODE_TYPE_LEN 8U

typedef struct {
    uint32_t seq;
    uint32_t first_heard_ms;
    uint32_t last_heard_ms;
    uint32_t advert_timestamp;
    uint32_t heard_count;
    char fingerprint[D1L_NODE_FINGERPRINT_LEN];
    char name[D1L_HEARD_NODE_NAME_LEN];
    char type[D1L_NODE_TYPE_LEN];
    int rssi_dbm;
    int snr_tenths;
    uint8_t path_hash_bytes;
    uint8_t path_hops;
} d1l_node_entry_t;

typedef struct {
    uint32_t next_seq;
    uint32_t total_written;
    uint32_t dropped_oldest;
    size_t count;
    size_t capacity;
} d1l_node_store_stats_t;

esp_err_t d1l_node_store_init(void);
esp_err_t d1l_node_store_clear(void);
esp_err_t d1l_node_store_upsert_advert(const char *fingerprint, const char *name,
                                       char type_code, int rssi_dbm, int snr_tenths,
                                       uint8_t path_hash_bytes, uint8_t path_hops,
                                       uint32_t advert_timestamp);
d1l_node_store_stats_t d1l_node_store_stats(void);
size_t d1l_node_store_copy_recent(d1l_node_entry_t *out_entries, size_t max_entries);
