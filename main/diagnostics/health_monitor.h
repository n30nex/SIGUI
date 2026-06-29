#pragma once

#include <stdint.h>

typedef struct {
    uint32_t uptime_ms;
    uint32_t heap_free;
    uint32_t heap_min_free;
    uint32_t psram_free;
    const char *reset_reason;
} d1l_health_snapshot_t;

d1l_health_snapshot_t d1l_health_snapshot(void);
