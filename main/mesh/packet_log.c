#include "packet_log.h"

#include <string.h>

#include "esp_timer.h"

static d1l_packet_log_entry_t s_entries[D1L_PACKET_LOG_CAPACITY];
static size_t s_head;
static size_t s_count;
static uint32_t s_next_seq;
static uint32_t s_total_written;
static uint32_t s_dropped_oldest;

void d1l_packet_log_init(void)
{
    d1l_packet_log_clear();
}

void d1l_packet_log_clear(void)
{
    memset(s_entries, 0, sizeof(s_entries));
    s_head = 0;
    s_count = 0;
    s_next_seq = 1;
    s_total_written = 0;
    s_dropped_oldest = 0;
}

bool d1l_packet_log_append(const d1l_packet_log_entry_t *entry)
{
    if (entry == NULL) {
        return false;
    }

    d1l_packet_log_entry_t copy = *entry;
    copy.seq = s_next_seq++;
    if (copy.uptime_ms == 0) {
        copy.uptime_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    }

    s_entries[s_head] = copy;
    s_head = (s_head + 1U) % D1L_PACKET_LOG_CAPACITY;
    if (s_count < D1L_PACKET_LOG_CAPACITY) {
        s_count++;
    } else {
        s_dropped_oldest++;
    }
    s_total_written++;
    return true;
}

d1l_packet_log_stats_t d1l_packet_log_stats(void)
{
    d1l_packet_log_stats_t stats = {
        .next_seq = s_next_seq,
        .total_written = s_total_written,
        .dropped_oldest = s_dropped_oldest,
        .count = s_count,
        .capacity = D1L_PACKET_LOG_CAPACITY,
    };
    return stats;
}

size_t d1l_packet_log_copy_recent(d1l_packet_log_entry_t *out_entries, size_t max_entries)
{
    if (out_entries == NULL || max_entries == 0 || s_count == 0) {
        return 0;
    }

    const size_t n = s_count < max_entries ? s_count : max_entries;
    size_t oldest = (s_head + D1L_PACKET_LOG_CAPACITY - s_count) % D1L_PACKET_LOG_CAPACITY;
    if (s_count > n) {
        oldest = (oldest + (s_count - n)) % D1L_PACKET_LOG_CAPACITY;
    }

    for (size_t i = 0; i < n; ++i) {
        out_entries[i] = s_entries[(oldest + i) % D1L_PACKET_LOG_CAPACITY];
    }
    return n;
}
