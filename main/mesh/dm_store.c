#include "dm_store.h"

#include <string.h>

#include "esp_timer.h"

#include "storage/retained_blob_store.h"

#define D1L_DM_STORE_ID D1L_RETAINED_BLOB_STORE_DM_MESSAGES
#define D1L_DM_STORE_KEY "threads"
#define D1L_DM_STORE_SCHEMA 1U

typedef struct {
    uint32_t schema;
    uint32_t next_seq;
    uint32_t total_written;
    uint32_t dropped_oldest;
    uint32_t head;
    uint32_t count;
    d1l_dm_entry_t entries[D1L_DM_STORE_CAPACITY];
} d1l_dm_store_blob_t;

static d1l_dm_entry_t s_entries[D1L_DM_STORE_CAPACITY];
static size_t s_head;
static size_t s_count;
static uint32_t s_next_seq = 1;
static uint32_t s_total_written;
static uint32_t s_dropped_oldest;
static bool s_loaded;
static d1l_dm_store_blob_t s_blob_scratch;

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

static void clear_ram(void)
{
    memset(s_entries, 0, sizeof(s_entries));
    s_head = 0;
    s_count = 0;
    s_next_seq = 1;
    s_total_written = 0;
    s_dropped_oldest = 0;
}

static void fill_blob(d1l_dm_store_blob_t *blob)
{
    memset(blob, 0, sizeof(*blob));
    blob->schema = D1L_DM_STORE_SCHEMA;
    blob->next_seq = s_next_seq;
    blob->total_written = s_total_written;
    blob->dropped_oldest = s_dropped_oldest;
    blob->head = (uint32_t)s_head;
    blob->count = (uint32_t)s_count;
    memcpy(blob->entries, s_entries, sizeof(s_entries));
}

static esp_err_t persist_store(void)
{
    fill_blob(&s_blob_scratch);
    return d1l_retained_blob_store_write(D1L_DM_STORE_ID,
                                         D1L_DM_STORE_KEY,
                                         &s_blob_scratch,
                                         sizeof(s_blob_scratch));
}

static bool blob_is_valid(const d1l_dm_store_blob_t *blob, size_t len)
{
    return blob && len == sizeof(*blob) &&
           blob->schema == D1L_DM_STORE_SCHEMA &&
           blob->head < D1L_DM_STORE_CAPACITY &&
           blob->count <= D1L_DM_STORE_CAPACITY &&
           blob->next_seq > 0;
}

static esp_err_t try_load_blob_from_fallback(void)
{
    size_t len = sizeof(s_blob_scratch);
    esp_err_t ret = d1l_retained_blob_store_read_fallback(D1L_DM_STORE_ID,
                                                          D1L_DM_STORE_KEY,
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
    s_head = s_blob_scratch.head;
    s_count = s_blob_scratch.count;
    s_next_seq = s_blob_scratch.next_seq;
    s_total_written = s_blob_scratch.total_written;
    s_dropped_oldest = s_blob_scratch.dropped_oldest;
}

esp_err_t d1l_dm_store_init(void)
{
    clear_ram();

    size_t len = sizeof(s_blob_scratch);
    esp_err_t ret = d1l_retained_blob_store_read(D1L_DM_STORE_ID,
                                                 D1L_DM_STORE_KEY,
                                                 &s_blob_scratch,
                                                 &len);
    if (ret == ESP_ERR_NOT_FOUND) {
        ret = ESP_OK;
    } else if (ret == ESP_OK && blob_is_valid(&s_blob_scratch, len)) {
        load_valid_blob_into_ram();
    } else if (ret == ESP_OK) {
        if (d1l_retained_blob_store_uses_sd(D1L_DM_STORE_ID) &&
            try_load_blob_from_fallback() == ESP_OK) {
            load_valid_blob_into_ram();
            ret = persist_store();
        } else {
            clear_ram();
            ret = d1l_retained_blob_store_erase(D1L_DM_STORE_ID,
                                                D1L_DM_STORE_KEY);
        }
    }
    s_loaded = (ret == ESP_OK);
    return ret;
}

esp_err_t d1l_dm_store_clear(void)
{
    clear_ram();
    s_loaded = true;

    return d1l_retained_blob_store_erase(D1L_DM_STORE_ID,
                                         D1L_DM_STORE_KEY);
}

esp_err_t d1l_dm_store_append(const char *contact_fingerprint, const char *contact_alias,
                              const char *direction, const char *text, int rssi_dbm,
                              int snr_tenths, uint8_t path_hash_bytes, uint8_t path_hops,
                              uint8_t attempt, bool delivered, bool acked,
                              uint32_t ack_hash)
{
    if (!contact_fingerprint || contact_fingerprint[0] == '\0' || !text || text[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_loaded) {
        esp_err_t ret = d1l_dm_store_init();
        if (ret != ESP_OK) {
            return ret;
        }
    }

    d1l_dm_entry_t entry = {
        .seq = s_next_seq++,
        .uptime_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
        .rssi_dbm = rssi_dbm,
        .snr_tenths = snr_tenths,
        .path_hash_bytes = path_hash_bytes,
        .path_hops = path_hops,
        .attempt = attempt,
        .delivered = delivered,
        .acked = acked,
        .ack_hash = ack_hash,
    };
    sanitize_ascii(entry.contact_fingerprint, sizeof(entry.contact_fingerprint), contact_fingerprint);
    sanitize_ascii(entry.contact_alias, sizeof(entry.contact_alias),
                   contact_alias && contact_alias[0] ? contact_alias : contact_fingerprint);
    sanitize_ascii(entry.direction, sizeof(entry.direction), direction && direction[0] ? direction : "rx");
    sanitize_ascii(entry.text, sizeof(entry.text), text);

    s_entries[s_head] = entry;
    s_head = (s_head + 1U) % D1L_DM_STORE_CAPACITY;
    if (s_count < D1L_DM_STORE_CAPACITY) {
        s_count++;
    } else {
        s_dropped_oldest++;
    }
    s_total_written++;
    return persist_store();
}

esp_err_t d1l_dm_store_mark_acked(uint32_t ack_hash, d1l_dm_entry_t *out_entry)
{
    if (ack_hash == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_loaded) {
        esp_err_t ret = d1l_dm_store_init();
        if (ret != ESP_OK) {
            return ret;
        }
    }

    for (size_t i = 0; i < s_count; ++i) {
        d1l_dm_entry_t *entry = &s_entries[i];
        if (entry->ack_hash == ack_hash && strncmp(entry->direction, "tx", sizeof(entry->direction)) == 0) {
            entry->delivered = true;
            entry->acked = true;
            if (out_entry) {
                *out_entry = *entry;
            }
            return persist_store();
        }
    }
    return ESP_ERR_NOT_FOUND;
}

d1l_dm_store_stats_t d1l_dm_store_stats(void)
{
    d1l_dm_store_stats_t stats = {
        .next_seq = s_next_seq,
        .total_written = s_total_written,
        .dropped_oldest = s_dropped_oldest,
        .count = s_count,
        .capacity = D1L_DM_STORE_CAPACITY,
    };
    return stats;
}

size_t d1l_dm_store_copy_recent(d1l_dm_entry_t *out_entries, size_t max_entries)
{
    if (out_entries == NULL || max_entries == 0 || s_count == 0) {
        return 0;
    }

    const size_t n = s_count < max_entries ? s_count : max_entries;
    size_t oldest = (s_head + D1L_DM_STORE_CAPACITY - s_count) % D1L_DM_STORE_CAPACITY;
    if (s_count > n) {
        oldest = (oldest + (s_count - n)) % D1L_DM_STORE_CAPACITY;
    }

    for (size_t i = 0; i < n; ++i) {
        out_entries[i] = s_entries[(oldest + i) % D1L_DM_STORE_CAPACITY];
    }
    return n;
}

size_t d1l_dm_store_copy_thread(const char *contact_fingerprint, d1l_dm_entry_t *out_entries,
                                size_t max_entries)
{
    if (!contact_fingerprint || contact_fingerprint[0] == '\0' ||
        out_entries == NULL || max_entries == 0 || s_count == 0) {
        return 0;
    }

    size_t matches = 0;
    size_t oldest = (s_head + D1L_DM_STORE_CAPACITY - s_count) % D1L_DM_STORE_CAPACITY;
    for (size_t i = 0; i < s_count; ++i) {
        const d1l_dm_entry_t *entry = &s_entries[(oldest + i) % D1L_DM_STORE_CAPACITY];
        if (strncmp(entry->contact_fingerprint, contact_fingerprint,
                    sizeof(entry->contact_fingerprint)) != 0) {
            continue;
        }
        if (matches < max_entries) {
            out_entries[matches] = *entry;
        } else {
            memmove(out_entries, out_entries + 1U, sizeof(out_entries[0]) * (max_entries - 1U));
            out_entries[max_entries - 1U] = *entry;
        }
        matches++;
    }

    return matches < max_entries ? matches : max_entries;
}
