#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define D1L_CRASH_LOG_CAPACITY 8U
#define D1L_CRASH_LOG_REASON_LEN 16U

typedef struct {
    uint32_t seq;
    uint32_t uptime_ms;
    uint32_t heap_free;
    uint32_t heap_min_free;
    uint32_t psram_free;
    uint8_t reset_reason_code;
    bool crash_like;
    char reset_reason[D1L_CRASH_LOG_REASON_LEN];
} d1l_crash_log_entry_t;

typedef struct {
    uint32_t next_seq;
    uint32_t total_written;
    uint32_t dropped_oldest;
    size_t count;
    size_t capacity;
} d1l_crash_log_stats_t;

esp_err_t d1l_crash_log_init(void);
esp_err_t d1l_crash_log_clear(void);
d1l_crash_log_stats_t d1l_crash_log_stats(void);
size_t d1l_crash_log_copy_recent(d1l_crash_log_entry_t *out_entries, size_t max_entries);
