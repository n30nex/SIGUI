#include "health_monitor.h"

#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"

static const char *reset_reason_name(esp_reset_reason_t reason)
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

d1l_health_snapshot_t d1l_health_snapshot(void)
{
    d1l_health_snapshot_t snapshot = {
        .uptime_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
        .heap_free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT),
        .heap_min_free = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
        .psram_free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        .reset_reason = reset_reason_name(esp_reset_reason()),
    };
    return snapshot;
}
