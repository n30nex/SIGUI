#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define D1L_PACKET_LOG_RAM_CAPACITY 128U
#define D1L_PACKET_LOG_NVS_FALLBACK_CAPACITY 8U
#define D1L_PACKET_LOG_SD_RETENTION_HOURS 24U
#define D1L_PACKET_LOG_SD_CAPACITY 4096U
#define D1L_PACKET_LOG_SD_SEGMENT_COUNT 64U
#define D1L_PACKET_LOG_SD_SEGMENT_CAPACITY (D1L_PACKET_LOG_SD_CAPACITY / D1L_PACKET_LOG_SD_SEGMENT_COUNT)
#define D1L_PACKET_LOG_CAPACITY D1L_PACKET_LOG_RAM_CAPACITY
#define D1L_PACKET_LOG_PERSIST_CAPACITY D1L_PACKET_LOG_NVS_FALLBACK_CAPACITY
#define D1L_PACKET_LOG_SD_FLUSH_DIRTY_THRESHOLD 16U
#define D1L_PACKET_LOG_SD_FLUSH_INTERVAL_MS 5000U
#define D1L_PACKET_LOG_NOTE_LEN 48U
#define D1L_PACKET_LOG_RAW_PREVIEW_BYTES 32U
#define D1L_PACKET_LOG_RAW_HEX_LEN ((D1L_PACKET_LOG_RAW_PREVIEW_BYTES * 2U) + 1U)
#define D1L_PACKET_LOG_QUERY_TEXT_LEN 32U

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
    uint16_t raw_len;
    bool raw_truncated;
    char note[D1L_PACKET_LOG_NOTE_LEN];
    char raw_hex[D1L_PACKET_LOG_RAW_HEX_LEN];
} d1l_packet_log_entry_t;

typedef struct {
    uint32_t next_seq;
    uint32_t total_written;
    uint32_t dropped_oldest;
    uint32_t sd_history_records;
    uint32_t sd_history_failed_writes;
    size_t count;
    size_t capacity;
    size_t sd_capacity;
    bool sd_history_enabled;
} d1l_packet_log_stats_t;

esp_err_t d1l_packet_log_init(void);
esp_err_t d1l_packet_log_clear(void);
bool d1l_packet_log_append(const d1l_packet_log_entry_t *entry);
bool d1l_packet_log_append_raw(const d1l_packet_log_entry_t *entry, const uint8_t *raw,
                               size_t raw_len);
bool d1l_packet_log_append_raw_volatile(const d1l_packet_log_entry_t *entry,
                                        const uint8_t *raw, size_t raw_len);
esp_err_t d1l_packet_log_flush(void);
d1l_packet_log_stats_t d1l_packet_log_stats(void);
size_t d1l_packet_log_copy_recent(d1l_packet_log_entry_t *out_entries, size_t max_entries);
size_t d1l_packet_log_query_page(d1l_packet_log_entry_t *out_entries, size_t max_entries,
                                 size_t skip_newest, const char *direction,
                                 const char *kind, const char *search_text,
                                 size_t *out_total_matches, bool *out_sd_used);
size_t d1l_packet_log_query(d1l_packet_log_entry_t *out_entries, size_t max_entries,
                            const char *direction, const char *kind, const char *search_text);
esp_err_t d1l_packet_log_find_by_seq(uint32_t seq, d1l_packet_log_entry_t *out_entry);
