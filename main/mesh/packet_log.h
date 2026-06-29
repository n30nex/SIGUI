#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define D1L_PACKET_LOG_CAPACITY 32U
#define D1L_PACKET_LOG_PERSIST_CAPACITY 8U
#define D1L_PACKET_LOG_NOTE_LEN 48U

typedef struct {
    uint32_t seq;
    uint32_t uptime_ms;
    char direction[4];
    char kind[16];
    int rssi_dbm;
    int snr_tenths;
    uint8_t path_hash_bytes;
    uint8_t path_hops;
    uint16_t payload_len;
    char note[D1L_PACKET_LOG_NOTE_LEN];
} d1l_packet_log_entry_t;

typedef struct {
    uint32_t next_seq;
    uint32_t total_written;
    uint32_t dropped_oldest;
    size_t count;
    size_t capacity;
} d1l_packet_log_stats_t;

esp_err_t d1l_packet_log_init(void);
esp_err_t d1l_packet_log_clear(void);
bool d1l_packet_log_append(const d1l_packet_log_entry_t *entry);
d1l_packet_log_stats_t d1l_packet_log_stats(void);
size_t d1l_packet_log_copy_recent(d1l_packet_log_entry_t *out_entries, size_t max_entries);
esp_err_t d1l_packet_log_find_by_seq(uint32_t seq, d1l_packet_log_entry_t *out_entry);
