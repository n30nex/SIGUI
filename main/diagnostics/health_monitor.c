#include "health_monitor.h"

#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "lvgl.h"

static TaskHandle_t s_ui_task;
static bool s_lvgl_ready;

const char *d1l_health_reset_reason_name(int reason)
{
    switch (reason) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXT";
    case ESP_RST_SW: return "SW";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    default: return "UNKNOWN";
    }
}

void d1l_health_monitor_register_ui_task(TaskHandle_t task)
{
    s_ui_task = task;
}

void d1l_health_monitor_set_lvgl_ready(bool ready)
{
    s_lvgl_ready = ready;
}

d1l_health_snapshot_t d1l_health_snapshot(void)
{
    d1l_health_snapshot_t snapshot = {
        .uptime_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
        .heap_free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT),
        .heap_min_free = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
        .heap_largest_free = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
        .psram_free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        .psram_min_free = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),
        .psram_largest_free = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
        .current_task_stack_free_words = (uint32_t)uxTaskGetStackHighWaterMark(NULL),
        .ui_task_stack_free_words = s_ui_task ? (uint32_t)uxTaskGetStackHighWaterMark(s_ui_task) : 0U,
        .reset_reason = d1l_health_reset_reason_name(esp_reset_reason()),
    };
    if (s_lvgl_ready) {
        lv_mem_monitor_t monitor;
        lv_mem_monitor(&monitor);
        snapshot.lvgl_free_bytes = (uint32_t)monitor.free_size;
        snapshot.lvgl_largest_free_bytes = (uint32_t)monitor.free_biggest_size;
        snapshot.lvgl_used_pct = monitor.used_pct;
    }
    return snapshot;
}
