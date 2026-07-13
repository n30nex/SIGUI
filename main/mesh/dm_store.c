#include "dm_store.h"

#include <string.h>

#include "esp_timer.h"

#include "mesh/store_lock.h"
#include "storage/retained_blob_store.h"

#define D1L_DM_STORE_ID D1L_RETAINED_BLOB_STORE_DM_MESSAGES
#define D1L_DM_STORE_KEY "threads"
#define D1L_DM_STORE_SCHEMA 2U
#define D1L_DM_STORE_SCHEMA_V1 1U
#define D1L_DM_TEXT_LEN_V1 96U

typedef struct {
    uint32_t schema;
    uint32_t next_seq;
    uint32_t total_written;
    uint32_t dropped_oldest;
    uint32_t head;
    uint32_t count;
    d1l_dm_entry_t entries[D1L_DM_STORE_CAPACITY];
} d1l_dm_store_blob_t;

typedef struct {
    uint32_t seq;
    uint32_t uptime_ms;
    char contact_fingerprint[D1L_NODE_FINGERPRINT_LEN];
    char contact_alias[D1L_CONTACT_ALIAS_LEN];
    char direction[D1L_DM_DIRECTION_LEN];
    char text[D1L_DM_TEXT_LEN_V1];
    int rssi_dbm;
    int snr_tenths;
    uint8_t path_hash_bytes;
    uint8_t path_hops;
    uint8_t attempt;
    bool delivered;
    bool acked;
    uint32_t ack_hash;
} d1l_dm_entry_v1_t;

typedef struct {
    uint32_t schema;
    uint32_t next_seq;
    uint32_t total_written;
    uint32_t dropped_oldest;
    uint32_t head;
    uint32_t count;
    d1l_dm_entry_v1_t entries[D1L_DM_STORE_CAPACITY];
} d1l_dm_store_blob_v1_t;

static d1l_dm_entry_t s_entries[D1L_DM_STORE_CAPACITY];
static size_t s_head;
static size_t s_count;
static uint32_t s_next_seq = 1;
static uint32_t s_total_written;
static uint32_t s_dropped_oldest;
static d1l_dm_entry_t s_volatile_entry;
static bool s_volatile_valid;
static bool s_loaded;
static d1l_dm_store_blob_t s_blob_scratch;
static d1l_dm_store_blob_v1_t s_blob_v1_scratch;
static d1l_store_lock_t s_store_lock = D1L_STORE_LOCK_INITIALIZER;

static void load_valid_blob_into_ram(void);

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
    memset(&s_volatile_entry, 0, sizeof(s_volatile_entry));
    s_volatile_valid = false;
}

static void fill_blob(d1l_dm_store_blob_t *blob)
{
    memset(blob, 0, sizeof(*blob));
    blob->schema = D1L_DM_STORE_SCHEMA;
    blob->next_seq = s_next_seq;
    blob->total_written = s_total_written;
    blob->dropped_oldest = s_dropped_oldest;
    const size_t oldest = s_count == 0 ? 0 :
        (s_head + D1L_DM_STORE_CAPACITY - s_count) % D1L_DM_STORE_CAPACITY;
    for (size_t i = 0; i < s_count; ++i) {
        const d1l_dm_entry_t *entry =
            &s_entries[(oldest + i) % D1L_DM_STORE_CAPACITY];
        if (blob->count < D1L_DM_STORE_CAPACITY) {
            blob->entries[blob->count++] = *entry;
        }
    }
    blob->head = blob->count % D1L_DM_STORE_CAPACITY;
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

static bool blob_v1_is_valid(const d1l_dm_store_blob_v1_t *blob, size_t len)
{
    return blob && len == sizeof(*blob) &&
           blob->schema == D1L_DM_STORE_SCHEMA_V1 &&
           blob->head < D1L_DM_STORE_CAPACITY &&
           blob->count <= D1L_DM_STORE_CAPACITY &&
           blob->next_seq > 0;
}

static void load_v1_blob_into_ram(const d1l_dm_store_blob_v1_t *blob)
{
    memset(s_entries, 0, sizeof(s_entries));
    for (size_t i = 0; i < D1L_DM_STORE_CAPACITY; ++i) {
        s_entries[i].seq = blob->entries[i].seq;
        s_entries[i].uptime_ms = blob->entries[i].uptime_ms;
        sanitize_ascii(s_entries[i].contact_fingerprint, sizeof(s_entries[i].contact_fingerprint),
                       blob->entries[i].contact_fingerprint);
        sanitize_ascii(s_entries[i].contact_alias, sizeof(s_entries[i].contact_alias),
                       blob->entries[i].contact_alias);
        sanitize_ascii(s_entries[i].direction, sizeof(s_entries[i].direction),
                       blob->entries[i].direction);
        sanitize_ascii(s_entries[i].text, sizeof(s_entries[i].text),
                       blob->entries[i].text);
        s_entries[i].rssi_dbm = blob->entries[i].rssi_dbm;
        s_entries[i].snr_tenths = blob->entries[i].snr_tenths;
        s_entries[i].path_hash_bytes = blob->entries[i].path_hash_bytes;
        s_entries[i].path_hops = blob->entries[i].path_hops;
        s_entries[i].attempt = blob->entries[i].attempt;
        s_entries[i].delivered = blob->entries[i].delivered;
        s_entries[i].acked = blob->entries[i].acked;
        s_entries[i].ack_hash = blob->entries[i].ack_hash;
    }
    s_head = blob->head;
    s_count = blob->count;
    s_next_seq = blob->next_seq;
    s_total_written = blob->total_written;
    s_dropped_oldest = blob->dropped_oldest;
}

static esp_err_t try_load_v1_blob(bool fallback)
{
    size_t len = sizeof(s_blob_v1_scratch);
    esp_err_t ret = fallback ?
        d1l_retained_blob_store_read_fallback(D1L_DM_STORE_ID,
                                              D1L_DM_STORE_KEY,
                                              &s_blob_v1_scratch,
                                              &len) :
        d1l_retained_blob_store_read(D1L_DM_STORE_ID,
                                     D1L_DM_STORE_KEY,
                                     &s_blob_v1_scratch,
                                     &len);
    if (ret != ESP_OK || !blob_v1_is_valid(&s_blob_v1_scratch, len)) {
        return ret == ESP_OK ? ESP_ERR_INVALID_STATE : ret;
    }
    load_v1_blob_into_ram(&s_blob_v1_scratch);
    return persist_store();
}

static esp_err_t try_load_blob_from_fallback(void)
{
    size_t len = sizeof(s_blob_scratch);
    esp_err_t ret = d1l_retained_blob_store_read_fallback(D1L_DM_STORE_ID,
                                                          D1L_DM_STORE_KEY,
                                                          &s_blob_scratch,
                                                          &len);
    if (ret == ESP_OK && blob_is_valid(&s_blob_scratch, len)) {
        load_valid_blob_into_ram();
        return ESP_OK;
    }
    return try_load_v1_blob(true);
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
    } else if (ret == ESP_OK && try_load_v1_blob(false) == ESP_OK) {
        ret = ESP_OK;
    } else if (ret == ESP_OK) {
        if (d1l_retained_blob_store_uses_sd(D1L_DM_STORE_ID) &&
            try_load_blob_from_fallback() == ESP_OK) {
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
    d1l_store_lock_take(&s_store_lock);
    clear_ram();
    s_loaded = true;

    esp_err_t ret = d1l_retained_blob_store_erase(D1L_DM_STORE_ID,
                                                  D1L_DM_STORE_KEY);
    d1l_store_lock_give(&s_store_lock);
    return ret;
}

static esp_err_t append_internal(const char *contact_fingerprint, const char *contact_alias,
                                 const char *direction, const char *text, int rssi_dbm,
                                 int snr_tenths, uint8_t path_hash_bytes, uint8_t path_hops,
                                 uint8_t attempt, bool delivered, bool acked,
                                 uint32_t ack_hash, bool persist)
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

    d1l_store_lock_take(&s_store_lock);
    d1l_dm_entry_t entry = {
        .seq = s_next_seq,
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

    if (!persist) {
        /* Keep the UI preview outside the durable ring.  In particular, a
         * canary cannot evict a real DM when the history is full. */
        s_volatile_entry = entry;
        s_volatile_valid = true;
        d1l_store_lock_give(&s_store_lock);
        return ESP_OK;
    }

    /* A durable DM supersedes the preview and consumes its borrowed seq. */
    s_volatile_valid = false;
    s_next_seq++;

    s_entries[s_head] = entry;
    s_head = (s_head + 1U) % D1L_DM_STORE_CAPACITY;
    if (s_count < D1L_DM_STORE_CAPACITY) {
        s_count++;
    } else {
        s_dropped_oldest++;
    }
    s_total_written++;
    esp_err_t ret = persist_store();
    d1l_store_lock_give(&s_store_lock);
    return ret;
}

esp_err_t d1l_dm_store_append(const char *contact_fingerprint, const char *contact_alias,
                              const char *direction, const char *text, int rssi_dbm,
                              int snr_tenths, uint8_t path_hash_bytes, uint8_t path_hops,
                              uint8_t attempt, bool delivered, bool acked,
                              uint32_t ack_hash)
{
    return append_internal(contact_fingerprint, contact_alias, direction, text, rssi_dbm,
                           snr_tenths, path_hash_bytes, path_hops, attempt, delivered,
                           acked, ack_hash, true);
}

esp_err_t d1l_dm_store_append_volatile(const char *contact_fingerprint, const char *contact_alias,
                                       const char *direction, const char *text, int rssi_dbm,
                                       int snr_tenths, uint8_t path_hash_bytes, uint8_t path_hops,
                                       uint8_t attempt, bool delivered, bool acked,
                                       uint32_t ack_hash)
{
    return append_internal(contact_fingerprint, contact_alias, direction, text, rssi_dbm,
                           snr_tenths, path_hash_bytes, path_hops, attempt, delivered,
                           acked, ack_hash, false);
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

    d1l_store_lock_take(&s_store_lock);
    for (size_t i = 0; i < s_count; ++i) {
        d1l_dm_entry_t *entry = &s_entries[i];
        if (entry->ack_hash == ack_hash && strncmp(entry->direction, "tx", sizeof(entry->direction)) == 0) {
            entry->delivered = true;
            entry->acked = true;
            if (out_entry) {
                *out_entry = *entry;
            }
            esp_err_t ret = persist_store();
            d1l_store_lock_give(&s_store_lock);
            return ret;
        }
    }
    d1l_store_lock_give(&s_store_lock);
    return ESP_ERR_NOT_FOUND;
}

d1l_dm_store_stats_t d1l_dm_store_stats(void)
{
    d1l_store_lock_take(&s_store_lock);
    d1l_dm_store_stats_t stats = {
        .next_seq = s_next_seq,
        .total_written = s_total_written,
        .dropped_oldest = s_dropped_oldest,
        .count = s_count,
        .capacity = D1L_DM_STORE_CAPACITY,
    };
    d1l_store_lock_give(&s_store_lock);
    return stats;
}

size_t d1l_dm_store_copy_recent_page(d1l_dm_entry_t *out_entries, size_t max_entries,
                                     size_t skip_newest, size_t *out_total_matches)
{
    if (out_total_matches) {
        *out_total_matches = 0;
    }
    if (out_entries == NULL || max_entries == 0) {
        return 0;
    }

    d1l_store_lock_take(&s_store_lock);
    if (s_count == 0 && !s_volatile_valid) {
        d1l_store_lock_give(&s_store_lock);
        return 0;
    }
    const size_t visible_count = s_count + (s_volatile_valid ? 1U : 0U);
    if (out_total_matches) {
        *out_total_matches = visible_count;
    }
    if (skip_newest >= visible_count) {
        d1l_store_lock_give(&s_store_lock);
        return 0;
    }
    const size_t available = visible_count - skip_newest;
    const size_t n = available < max_entries ? available : max_entries;
    const size_t first = visible_count - skip_newest - n;
    const size_t oldest = s_count == 0 ? 0 :
        (s_head + D1L_DM_STORE_CAPACITY - s_count) % D1L_DM_STORE_CAPACITY;
    for (size_t i = 0; i < n; ++i) {
        const size_t visible_index = first + i;
        out_entries[i] = visible_index < s_count ?
            s_entries[(oldest + visible_index) % D1L_DM_STORE_CAPACITY] :
            s_volatile_entry;
    }
    d1l_store_lock_give(&s_store_lock);
    return n;
}

size_t d1l_dm_store_copy_recent(d1l_dm_entry_t *out_entries, size_t max_entries)
{
    return d1l_dm_store_copy_recent_page(out_entries, max_entries, 0, NULL);
}

size_t d1l_dm_store_copy_thread_page(const char *contact_fingerprint,
                                     d1l_dm_entry_t *out_entries,
                                     size_t max_entries, size_t skip_newest,
                                     size_t *out_total_matches)
{
    if (out_total_matches) {
        *out_total_matches = 0;
    }
    if (!contact_fingerprint || contact_fingerprint[0] == '\0' ||
        out_entries == NULL || max_entries == 0) {
        return 0;
    }

    d1l_store_lock_take(&s_store_lock);
    if (s_count == 0 && !s_volatile_valid) {
        d1l_store_lock_give(&s_store_lock);
        return 0;
    }
    size_t total_matches = 0;
    const size_t oldest = s_count == 0 ? 0 :
        (s_head + D1L_DM_STORE_CAPACITY - s_count) % D1L_DM_STORE_CAPACITY;
    for (size_t i = 0; i < s_count; ++i) {
        const d1l_dm_entry_t *entry = &s_entries[(oldest + i) % D1L_DM_STORE_CAPACITY];
        if (strncmp(entry->contact_fingerprint, contact_fingerprint,
                    sizeof(entry->contact_fingerprint)) != 0) {
            continue;
        }
        total_matches++;
    }
    const bool volatile_matches = s_volatile_valid &&
        strncmp(s_volatile_entry.contact_fingerprint, contact_fingerprint,
                sizeof(s_volatile_entry.contact_fingerprint)) == 0;
    if (volatile_matches) {
        total_matches++;
    }

    if (out_total_matches) {
        *out_total_matches = total_matches;
    }
    if (skip_newest >= total_matches) {
        d1l_store_lock_give(&s_store_lock);
        return 0;
    }

    const size_t available = total_matches - skip_newest;
    const size_t copied = available < max_entries ? available : max_entries;
    const size_t first_match = total_matches - skip_newest - copied;
    const size_t last_match = first_match + copied;
    size_t match_index = 0;
    size_t out_index = 0;
    for (size_t i = 0; i < s_count && out_index < copied; ++i) {
        const d1l_dm_entry_t *entry = &s_entries[(oldest + i) % D1L_DM_STORE_CAPACITY];
        if (strncmp(entry->contact_fingerprint, contact_fingerprint,
                    sizeof(entry->contact_fingerprint)) != 0) {
            continue;
        }
        if (match_index >= first_match && match_index < last_match) {
            out_entries[out_index++] = *entry;
        }
        match_index++;
    }
    if (volatile_matches && out_index < copied) {
        if (match_index >= first_match && match_index < last_match) {
            out_entries[out_index++] = s_volatile_entry;
        }
    }
    d1l_store_lock_give(&s_store_lock);
    return out_index;
}

size_t d1l_dm_store_copy_thread(const char *contact_fingerprint, d1l_dm_entry_t *out_entries,
                                size_t max_entries)
{
    return d1l_dm_store_copy_thread_page(contact_fingerprint, out_entries, max_entries, 0, NULL);
}
