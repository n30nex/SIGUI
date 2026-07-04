#include "route_store.h"

#include <stdbool.h>
#include <string.h>

#include "esp_timer.h"

#include "mesh/store_lock.h"
#include "storage/retained_blob_store.h"

#define D1L_ROUTE_STORE_ID D1L_RETAINED_BLOB_STORE_ROUTES
#define D1L_ROUTE_STORE_KEY "routes"
#define D1L_ROUTE_STORE_SCHEMA 1U

typedef struct {
    uint32_t schema;
    uint32_t next_seq;
    uint32_t total_written;
    uint32_t dropped_oldest;
    uint32_t count;
    d1l_route_entry_t entries[D1L_ROUTE_STORE_CAPACITY];
} d1l_route_store_blob_t;

static d1l_route_entry_t s_entries[D1L_ROUTE_STORE_CAPACITY];
static size_t s_count;
static uint32_t s_next_seq = 1;
static uint32_t s_total_written;
static uint32_t s_dropped_oldest;
static bool s_loaded;
static d1l_route_store_blob_t s_blob_scratch;
static d1l_store_lock_t s_store_lock = D1L_STORE_LOCK_INITIALIZER;

static void sanitize_ascii(char *dest, size_t dest_size, const char *src)
{
    if (!dest || dest_size == 0) {
        return;
    }
    size_t out = 0;
    while (src && src[0] && out + 1U < dest_size) {
        unsigned char c = (unsigned char)*src++;
        if (c < 32 || c > 126 || c == '"' || c == '\\') {
            c = '_';
        }
        dest[out++] = (char)c;
    }
    dest[out] = '\0';
}

static uint8_t route_confidence(uint8_t path_hops)
{
    if (path_hops == 0) {
        return 100;
    }
    if (path_hops >= 4) {
        return 40;
    }
    return (uint8_t)(100U - (path_hops * 20U));
}

static void clear_ram(void)
{
    memset(s_entries, 0, sizeof(s_entries));
    s_count = 0;
    s_next_seq = 1;
    s_total_written = 0;
    s_dropped_oldest = 0;
}

static void fill_blob(d1l_route_store_blob_t *blob)
{
    memset(blob, 0, sizeof(*blob));
    blob->schema = D1L_ROUTE_STORE_SCHEMA;
    blob->next_seq = s_next_seq;
    blob->total_written = s_total_written;
    blob->dropped_oldest = s_dropped_oldest;
    blob->count = (uint32_t)s_count;
    memcpy(blob->entries, s_entries, sizeof(s_entries));
}

static esp_err_t persist_store(void)
{
    fill_blob(&s_blob_scratch);
    return d1l_retained_blob_store_write(D1L_ROUTE_STORE_ID,
                                         D1L_ROUTE_STORE_KEY,
                                         &s_blob_scratch,
                                         sizeof(s_blob_scratch));
}

static bool blob_is_valid(const d1l_route_store_blob_t *blob, size_t len)
{
    return blob && len == sizeof(*blob) &&
           blob->schema == D1L_ROUTE_STORE_SCHEMA &&
           blob->count <= D1L_ROUTE_STORE_CAPACITY &&
           blob->next_seq > 0;
}

static esp_err_t try_load_blob_from_fallback(void)
{
    size_t len = sizeof(s_blob_scratch);
    esp_err_t ret = d1l_retained_blob_store_read_fallback(D1L_ROUTE_STORE_ID,
                                                          D1L_ROUTE_STORE_KEY,
                                                          &s_blob_scratch,
                                                          &len);
    if (ret != ESP_OK || !blob_is_valid(&s_blob_scratch, len)) {
        return ret == ESP_OK ? ESP_ERR_INVALID_STATE : ret;
    }
    return ESP_OK;
}

static void load_valid_blob_into_ram(void)
{
    memcpy(s_entries, s_blob_scratch.entries, sizeof(s_entries));
    s_count = s_blob_scratch.count;
    s_next_seq = s_blob_scratch.next_seq;
    s_total_written = s_blob_scratch.total_written;
    s_dropped_oldest = s_blob_scratch.dropped_oldest;
}

static int find_index(const char *target, const char *kind, const char *direction)
{
    if (!target || target[0] == '\0') {
        return -1;
    }
    for (size_t i = 0; i < s_count; ++i) {
        if (strncmp(s_entries[i].target, target, sizeof(s_entries[i].target)) == 0 &&
            strncmp(s_entries[i].kind, kind ? kind : "", sizeof(s_entries[i].kind)) == 0 &&
            strncmp(s_entries[i].direction, direction ? direction : "", sizeof(s_entries[i].direction)) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static size_t oldest_index(void)
{
    size_t oldest = 0;
    for (size_t i = 1; i < s_count; ++i) {
        if (s_entries[i].seq < s_entries[oldest].seq) {
            oldest = i;
        }
    }
    return oldest;
}

esp_err_t d1l_route_store_init(void)
{
    clear_ram();

    size_t len = sizeof(s_blob_scratch);
    esp_err_t ret = d1l_retained_blob_store_read(D1L_ROUTE_STORE_ID,
                                                 D1L_ROUTE_STORE_KEY,
                                                 &s_blob_scratch,
                                                 &len);
    if (ret == ESP_ERR_NOT_FOUND) {
        ret = ESP_OK;
    } else if (ret == ESP_OK && blob_is_valid(&s_blob_scratch, len)) {
        load_valid_blob_into_ram();
    } else if (ret == ESP_OK) {
        if (d1l_retained_blob_store_uses_sd(D1L_ROUTE_STORE_ID) &&
            try_load_blob_from_fallback() == ESP_OK) {
            load_valid_blob_into_ram();
            ret = persist_store();
        } else {
            clear_ram();
            ret = d1l_retained_blob_store_erase(D1L_ROUTE_STORE_ID,
                                                D1L_ROUTE_STORE_KEY);
        }
    }
    s_loaded = (ret == ESP_OK);
    return ret;
}

esp_err_t d1l_route_store_clear(void)
{
    d1l_store_lock_take(&s_store_lock);
    clear_ram();
    s_loaded = true;

    esp_err_t ret = d1l_retained_blob_store_erase(D1L_ROUTE_STORE_ID,
                                                  D1L_ROUTE_STORE_KEY);
    d1l_store_lock_give(&s_store_lock);
    return ret;
}

static esp_err_t upsert_observation_internal(const char *target, const char *label,
                                             const char *kind, const char *route,
                                             const char *direction, int rssi_dbm,
                                             int snr_tenths, uint8_t path_hash_bytes,
                                             uint8_t path_hops, uint16_t payload_len,
                                             bool persist)
{
    if (!target || target[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_loaded) {
        esp_err_t ret = d1l_route_store_init();
        if (ret != ESP_OK) {
            return ret;
        }
    }

    d1l_store_lock_take(&s_store_lock);
    int existing = find_index(target, kind, direction);
    size_t index;
    bool is_new = existing < 0;
    if (!is_new) {
        index = (size_t)existing;
    } else if (s_count < D1L_ROUTE_STORE_CAPACITY) {
        index = s_count++;
    } else {
        index = oldest_index();
        s_dropped_oldest++;
    }

    d1l_route_entry_t *entry = &s_entries[index];
    const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    if (is_new) {
        memset(entry, 0, sizeof(*entry));
        entry->first_seen_ms = now_ms;
    } else if (now_ms < entry->first_seen_ms) {
        entry->first_seen_ms = now_ms;
    }
    entry->seq = s_next_seq++;
    entry->last_seen_ms = now_ms;
    entry->seen_count++;
    entry->last_rssi_dbm = rssi_dbm;
    entry->last_snr_tenths = snr_tenths;
    entry->path_hash_bytes = path_hash_bytes;
    entry->path_hops = path_hops;
    entry->confidence = route_confidence(path_hops);
    entry->payload_len = payload_len;
    sanitize_ascii(entry->target, sizeof(entry->target), target);
    sanitize_ascii(entry->label, sizeof(entry->label), label && label[0] ? label : target);
    sanitize_ascii(entry->kind, sizeof(entry->kind), kind && kind[0] ? kind : "packet");
    sanitize_ascii(entry->route, sizeof(entry->route), route && route[0] ? route : "unknown");
    sanitize_ascii(entry->direction, sizeof(entry->direction), direction && direction[0] ? direction : "rx");
    s_total_written++;
    esp_err_t ret = persist ? persist_store() : ESP_OK;
    d1l_store_lock_give(&s_store_lock);
    return ret;
}

esp_err_t d1l_route_store_upsert_observation(const char *target, const char *label,
                                             const char *kind, const char *route,
                                             const char *direction, int rssi_dbm,
                                             int snr_tenths, uint8_t path_hash_bytes,
                                             uint8_t path_hops, uint16_t payload_len)
{
    return upsert_observation_internal(target, label, kind, route, direction, rssi_dbm,
                                       snr_tenths, path_hash_bytes, path_hops,
                                       payload_len, true);
}

esp_err_t d1l_route_store_upsert_observation_volatile(const char *target, const char *label,
                                                      const char *kind, const char *route,
                                                      const char *direction, int rssi_dbm,
                                                      int snr_tenths, uint8_t path_hash_bytes,
                                                      uint8_t path_hops, uint16_t payload_len)
{
    return upsert_observation_internal(target, label, kind, route, direction, rssi_dbm,
                                       snr_tenths, path_hash_bytes, path_hops,
                                       payload_len, false);
}

d1l_route_store_stats_t d1l_route_store_stats(void)
{
    d1l_store_lock_take(&s_store_lock);
    d1l_route_store_stats_t stats = {
        .next_seq = s_next_seq,
        .total_written = s_total_written,
        .dropped_oldest = s_dropped_oldest,
        .count = s_count,
        .capacity = D1L_ROUTE_STORE_CAPACITY,
    };
    d1l_store_lock_give(&s_store_lock);
    return stats;
}

size_t d1l_route_store_copy_recent(d1l_route_entry_t *out_entries, size_t max_entries)
{
    if (out_entries == NULL || max_entries == 0) {
        return 0;
    }

    d1l_store_lock_take(&s_store_lock);
    if (s_count == 0) {
        d1l_store_lock_give(&s_store_lock);
        return 0;
    }
    const size_t n = s_count < max_entries ? s_count : max_entries;
    bool used[D1L_ROUTE_STORE_CAPACITY] = {0};
    for (size_t out = 0; out < n; ++out) {
        size_t best = 0;
        bool best_set = false;
        for (size_t i = 0; i < s_count; ++i) {
            if (!used[i] && (!best_set || s_entries[i].seq > s_entries[best].seq)) {
                best = i;
                best_set = true;
            }
        }
        used[best] = true;
        out_entries[out] = s_entries[best];
    }
    d1l_store_lock_give(&s_store_lock);
    return n;
}

size_t d1l_route_store_copy_for_target(const char *target, d1l_route_entry_t *out_entries,
                                       size_t max_entries)
{
    if (!target || target[0] == '\0' || out_entries == NULL || max_entries == 0) {
        return 0;
    }

    d1l_store_lock_take(&s_store_lock);
    if (s_count == 0) {
        d1l_store_lock_give(&s_store_lock);
        return 0;
    }
    size_t copied = 0;
    bool used[D1L_ROUTE_STORE_CAPACITY] = {0};
    while (copied < max_entries) {
        size_t best = 0;
        bool best_set = false;
        for (size_t i = 0; i < s_count; ++i) {
            if (used[i] ||
                strncmp(s_entries[i].target, target, sizeof(s_entries[i].target)) != 0) {
                continue;
            }
            if (!best_set || s_entries[i].seq > s_entries[best].seq) {
                best = i;
                best_set = true;
            }
        }
        if (!best_set) {
            break;
        }
        used[best] = true;
        out_entries[copied++] = s_entries[best];
    }
    d1l_store_lock_give(&s_store_lock);
    return copied;
}

esp_err_t d1l_route_store_find_by_seq(uint32_t seq, d1l_route_entry_t *out_entry)
{
    if (seq == 0 || out_entry == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_loaded) {
        esp_err_t ret = d1l_route_store_init();
        if (ret != ESP_OK) {
            return ret;
        }
    }

    d1l_store_lock_take(&s_store_lock);
    for (size_t i = 0; i < s_count; ++i) {
        if (s_entries[i].seq == seq) {
            *out_entry = s_entries[i];
            d1l_store_lock_give(&s_store_lock);
            return ESP_OK;
        }
    }
    d1l_store_lock_give(&s_store_lock);
    return ESP_ERR_NOT_FOUND;
}
