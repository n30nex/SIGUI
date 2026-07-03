#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef struct {
    uint32_t uptime_ms;
    uint32_t heap_free;
    uint32_t heap_min_free;
    uint32_t heap_largest_free;
    uint32_t psram_free;
    uint32_t psram_min_free;
    uint32_t psram_largest_free;
    uint32_t current_task_stack_free_words;
    uint32_t ui_task_stack_free_words;
    uint32_t lvgl_free_bytes;
    uint32_t lvgl_largest_free_bytes;
    uint8_t lvgl_used_pct;
    const char *reset_reason;
} d1l_health_snapshot_t;

const char *d1l_health_reset_reason_name(int reason);
void d1l_health_monitor_register_ui_task(TaskHandle_t task);
void d1l_health_monitor_set_lvgl_ready(bool ready);
void d1l_health_monitor_sample_lvgl(void);
d1l_health_snapshot_t d1l_health_snapshot(void);
