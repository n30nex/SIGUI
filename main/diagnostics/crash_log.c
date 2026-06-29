#include "crash_log.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_system.h"
#include "nvs.h"

#include "diagnostics/health_monitor.h"

#define D1L_CRASH_LOG_NAMESPACE "d1l_crash"
#define D1L_CRASH_LOG_KEY "ring"
#define D1L_CRASH_LOG_SCHEMA 1U

typedef struct {
    uint32_t schema;
    uint32_t next_seq;
    uint32_t total_written;
    uint32_t dropped_oldest;
    uint32_t head;
    uint32_t count;
    d1l_crash_log_entry_t entries[D1L_CRASH_LOG_CAPACITY];
} d1l_crash_log_blob_t;

static d1l_crash_log_entry_t s_entries[D1L_CRASH_LOG_CAPACITY];
static size_t s_head;
static size_t s_count;
static uint32_t s_next_seq = 1;
static uint32_t s_total_written;
static uint32_t s_dropped_oldest;
static bool s_loaded;
static d1l_crash_log_blob_t s_blob_scratch;

static bool is_crash_like(int reason)
{
    return reason == ESP_RST_PANIC ||
           reason == ESP_RST_INT_WDT ||
           reason == ESP_RST_TASK_WDT ||
           reason == ESP_RST_WDT ||
           reason == ESP_RST_BROWNOUT;
}

static void clear_ram(void)
{
    memset(s_entries, 0, sizeof(s_entries));
    s_head = 0;
    s_count = 0;
    s_next_seq = 1;
    s_total_written = 0;
    s_dropped_oldest = 0;
}

static void fill_blob(d1l_crash_log_blob_t *blob)
{
    memset(blob, 0, sizeof(*blob));
    blob->schema = D1L_CRASH_LOG_SCHEMA;
    blob->next_seq = s_next_seq;
    blob->total_written = s_total_written;
    blob->dropped_oldest = s_dropped_oldest;
    blob->head = (uint32_t)s_head;
    blob->count = (uint32_t)s_count;
    memcpy(blob->entries, s_entries, sizeof(s_entries));
}

static bool blob_is_valid(const d1l_crash_log_blob_t *blob, size_t len)
{
    return blob && len == sizeof(*blob) &&
           blob->schema == D1L_CRASH_LOG_SCHEMA &&
           blob->head < D1L_CRASH_LOG_CAPACITY &&
           blob->count <= D1L_CRASH_LOG_CAPACITY &&
           blob->next_seq > 0;
}

static esp_err_t persist_store(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(D1L_CRASH_LOG_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }
    fill_blob(&s_blob_scratch);
    ret = nvs_set_blob(handle, D1L_CRASH_LOG_KEY, &s_blob_scratch, sizeof(s_blob_scratch));
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

static esp_err_t append_boot_reset(void)
{
    d1l_health_snapshot_t health = d1l_health_snapshot();
    const int reason = esp_reset_reason();
    d1l_crash_log_entry_t entry = {
        .seq = s_next_seq++,
        .uptime_ms = health.uptime_ms,
        .heap_free = health.heap_free,
        .heap_min_free = health.heap_min_free,
        .psram_free = health.psram_free,
        .reset_reason_code = (uint8_t)reason,
        .crash_like = is_crash_like(reason),
    };
    snprintf(entry.reset_reason, sizeof(entry.reset_reason), "%s",
             d1l_health_reset_reason_name(reason));

    s_entries[s_head] = entry;
    s_head = (s_head + 1U) % D1L_CRASH_LOG_CAPACITY;
    if (s_count < D1L_CRASH_LOG_CAPACITY) {
        s_count++;
    } else {
        s_dropped_oldest++;
    }
    s_total_written++;
    return persist_store();
}

esp_err_t d1l_crash_log_init(void)
{
    clear_ram();

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(D1L_CRASH_LOG_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        s_loaded = false;
        return ret;
    }

    size_t len = sizeof(s_blob_scratch);
    ret = nvs_get_blob(handle, D1L_CRASH_LOG_KEY, &s_blob_scratch, &len);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ret = ESP_OK;
    } else if (ret == ESP_OK && blob_is_valid(&s_blob_scratch, len)) {
        memcpy(s_entries, s_blob_scratch.entries, sizeof(s_entries));
        s_head = s_blob_scratch.head;
        s_count = s_blob_scratch.count;
        s_next_seq = s_blob_scratch.next_seq;
        s_total_written = s_blob_scratch.total_written;
        s_dropped_oldest = s_blob_scratch.dropped_oldest;
    } else if (ret == ESP_OK) {
        clear_ram();
        ret = nvs_erase_key(handle, D1L_CRASH_LOG_KEY);
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            ret = ESP_OK;
        }
        if (ret == ESP_OK) {
            ret = nvs_commit(handle);
        }
    }
    nvs_close(handle);
    if (ret != ESP_OK) {
        s_loaded = false;
        return ret;
    }
    s_loaded = true;
    return append_boot_reset();
}

esp_err_t d1l_crash_log_clear(void)
{
    clear_ram();
    s_loaded = true;

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(D1L_CRASH_LOG_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = nvs_erase_key(handle, D1L_CRASH_LOG_KEY);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ret = ESP_OK;
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

d1l_crash_log_stats_t d1l_crash_log_stats(void)
{
    d1l_crash_log_stats_t stats = {
        .next_seq = s_next_seq,
        .total_written = s_total_written,
        .dropped_oldest = s_dropped_oldest,
        .count = s_count,
        .capacity = D1L_CRASH_LOG_CAPACITY,
    };
    return stats;
}

size_t d1l_crash_log_copy_recent(d1l_crash_log_entry_t *out_entries, size_t max_entries)
{
    if (out_entries == NULL || max_entries == 0 || s_count == 0) {
        return 0;
    }

    const size_t n = s_count < max_entries ? s_count : max_entries;
    size_t oldest = (s_head + D1L_CRASH_LOG_CAPACITY - s_count) % D1L_CRASH_LOG_CAPACITY;
    if (s_count > n) {
        oldest = (oldest + (s_count - n)) % D1L_CRASH_LOG_CAPACITY;
    }
    for (size_t i = 0; i < n; ++i) {
        out_entries[i] = s_entries[(oldest + i) % D1L_CRASH_LOG_CAPACITY];
    }
    return n;
}
