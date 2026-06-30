#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define D1L_MESSAGE_STORE_CAPACITY 16U
#define D1L_MESSAGE_AUTHOR_LEN 24U
#define D1L_MESSAGE_TEXT_LEN 96U

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
    size_t count;
    size_t capacity;
} d1l_message_store_stats_t;

esp_err_t d1l_message_store_init(void);
esp_err_t d1l_message_store_clear(void);
esp_err_t d1l_message_store_append_public(const char *direction, const char *author,
                                          const char *text, int rssi_dbm, int snr_tenths,
                                          uint8_t path_hash_bytes, uint8_t path_hops,
                                          bool delivered);
d1l_message_store_stats_t d1l_message_store_stats(void);
size_t d1l_message_store_copy_recent(d1l_message_entry_t *out_entries, size_t max_entries);
size_t d1l_message_store_query(d1l_message_entry_t *out_entries, size_t max_entries,
                               const char *query);
