#include "message_store.h"

#include <string.h>

#include "esp_timer.h"

#include "mesh/store_lock.h"
#include "storage/retained_blob_store.h"

#define D1L_MESSAGE_STORE_ID D1L_RETAINED_BLOB_STORE_PUBLIC_MESSAGES
#define D1L_MESSAGE_STORE_KEY "public"
#define D1L_MESSAGE_STORE_SCHEMA 2U
#define D1L_MESSAGE_STORE_SCHEMA_V1 1U
#define D1L_MESSAGE_TEXT_LEN_V1 96U

typedef struct {
    uint32_t schema;
    uint32_t next_seq;
    uint32_t total_written;
    uint32_t dropped_oldest;
    uint32_t head;
    uint32_t count;
    d1l_message_entry_t entries[D1L_MESSAGE_STORE_CAPACITY];
} d1l_message_store_blob_t;

typedef struct {
    uint32_t seq;
    uint32_t uptime_ms;
    char direction[4];
    char author[D1L_MESSAGE_AUTHOR_LEN];
    char text[D1L_MESSAGE_TEXT_LEN_V1];
    int rssi_dbm;
    int snr_tenths;
    uint8_t path_hash_bytes;
    uint8_t path_hops;
    bool delivered;
} d1l_message_entry_v1_t;

typedef struct {
    uint32_t schema;
    uint32_t next_seq;
    uint32_t total_written;
    uint32_t dropped_oldest;
    uint32_t head;
    uint32_t count;
    d1l_message_entry_v1_t entries[D1L_MESSAGE_STORE_CAPACITY];
} d1l_message_store_blob_v1_t;

static d1l_message_entry_t s_entries[D1L_MESSAGE_STORE_CAPACITY];
static size_t s_head;
static size_t s_count;
static uint32_t s_next_seq = 1;
static uint32_t s_total_written;
static uint32_t s_dropped_oldest;
static bool s_loaded;
static d1l_message_store_blob_t s_blob_scratch;
static d1l_message_store_blob_v1_t s_blob_v1_scratch;
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

static char ascii_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

static bool contains_casefold(const char *haystack, const char *needle)
{
    if (!needle || needle[0] == '\0') {
        return true;
    }
    if (!haystack) {
        return false;
    }
    for (size_t i = 0; haystack[i]; ++i) {
        size_t j = 0;
        while (needle[j] && haystack[i + j] &&
               ascii_lower(haystack[i + j]) == ascii_lower(needle[j])) {
            j++;
        }
        if (needle[j] == '\0') {
            return true;
        }
    }
    return false;
}

static bool message_matches_query(const d1l_message_entry_t *entry, const char *query)
{
    return !query || query[0] == '\0' ||
           contains_casefold(entry->author, query) ||
           contains_casefold(entry->text, query) ||
           contains_casefold(entry->direction, query);
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

static void fill_blob(d1l_message_store_blob_t *blob)
{
    memset(blob, 0, sizeof(*blob));
    blob->schema = D1L_MESSAGE_STORE_SCHEMA;
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
    return d1l_retained_blob_store_write(D1L_MESSAGE_STORE_ID,
                                         D1L_MESSAGE_STORE_KEY,
                                         &s_blob_scratch,
                                         sizeof(s_blob_scratch));
}

static bool blob_is_valid(const d1l_message_store_blob_t *blob, size_t len)
{
    return blob && len == sizeof(*blob) &&
           blob->schema == D1L_MESSAGE_STORE_SCHEMA &&
           blob->head < D1L_MESSAGE_STORE_CAPACITY &&
           blob->count <= D1L_MESSAGE_STORE_CAPACITY &&
           blob->next_seq > 0;
}

static bool blob_v1_is_valid(const d1l_message_store_blob_v1_t *blob, size_t len)
{
    return blob && len == sizeof(*blob) &&
           blob->schema == D1L_MESSAGE_STORE_SCHEMA_V1 &&
           blob->head < D1L_MESSAGE_STORE_CAPACITY &&
           blob->count <= D1L_MESSAGE_STORE_CAPACITY &&
           blob->next_seq > 0;
}

static void load_v1_blob_into_ram(const d1l_message_store_blob_v1_t *blob)
{
    memset(s_entries, 0, sizeof(s_entries));
    for (size_t i = 0; i < D1L_MESSAGE_STORE_CAPACITY; ++i) {
        s_entries[i].seq = blob->entries[i].seq;
        s_entries[i].uptime_ms = blob->entries[i].uptime_ms;
        sanitize_ascii(s_entries[i].direction, sizeof(s_entries[i].direction),
                       blob->entries[i].direction);
        sanitize_ascii(s_entries[i].author, sizeof(s_entries[i].author),
                       blob->entries[i].author);
        sanitize_ascii(s_entries[i].text, sizeof(s_entries[i].text),
                       blob->entries[i].text);
        s_entries[i].rssi_dbm = blob->entries[i].rssi_dbm;
        s_entries[i].snr_tenths = blob->entries[i].snr_tenths;
        s_entries[i].path_hash_bytes = blob->entries[i].path_hash_bytes;
        s_entries[i].path_hops = blob->entries[i].path_hops;
        s_entries[i].delivered = blob->entries[i].delivered;
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
        d1l_retained_blob_store_read_fallback(D1L_MESSAGE_STORE_ID,
                                              D1L_MESSAGE_STORE_KEY,
                                              &s_blob_v1_scratch,
                                              &len) :
        d1l_retained_blob_store_read(D1L_MESSAGE_STORE_ID,
                                     D1L_MESSAGE_STORE_KEY,
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
    esp_err_t ret = d1l_retained_blob_store_read_fallback(D1L_MESSAGE_STORE_ID,
                                                          D1L_MESSAGE_STORE_KEY,
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

esp_err_t d1l_message_store_init(void)
{
    clear_ram();

    size_t len = sizeof(s_blob_scratch);
    esp_err_t ret = d1l_retained_blob_store_read(D1L_MESSAGE_STORE_ID,
                                                 D1L_MESSAGE_STORE_KEY,
                                                 &s_blob_scratch,
                                                 &len);
    if (ret == ESP_ERR_NOT_FOUND) {
        ret = ESP_OK;
    } else if (ret == ESP_OK && blob_is_valid(&s_blob_scratch, len)) {
        load_valid_blob_into_ram();
    } else if (ret == ESP_OK && try_load_v1_blob(false) == ESP_OK) {
        ret = ESP_OK;
    } else if (ret == ESP_OK) {
        if (d1l_retained_blob_store_uses_sd(D1L_MESSAGE_STORE_ID) &&
            try_load_blob_from_fallback() == ESP_OK) {
            ret = persist_store();
        } else {
            clear_ram();
            ret = d1l_retained_blob_store_erase(D1L_MESSAGE_STORE_ID,
                                                D1L_MESSAGE_STORE_KEY);
        }
    }
    s_loaded = (ret == ESP_OK);
    return ret;
}

esp_err_t d1l_message_store_clear(void)
{
    d1l_store_lock_take(&s_store_lock);
    clear_ram();
    s_loaded = true;

    esp_err_t ret = d1l_retained_blob_store_erase(D1L_MESSAGE_STORE_ID,
                                                  D1L_MESSAGE_STORE_KEY);
    d1l_store_lock_give(&s_store_lock);
    return ret;
}

static esp_err_t append_public_internal(const char *direction, const char *author,
                                        const char *text, int rssi_dbm, int snr_tenths,
                                        uint8_t path_hash_bytes, uint8_t path_hops,
                                        bool delivered, bool persist)
{
    if (!text || text[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_loaded) {
        esp_err_t ret = d1l_message_store_init();
        if (ret != ESP_OK) {
            return ret;
        }
    }

    d1l_store_lock_take(&s_store_lock);
    d1l_message_entry_t entry = {
        .seq = s_next_seq++,
        .uptime_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
        .rssi_dbm = rssi_dbm,
        .snr_tenths = snr_tenths,
        .path_hash_bytes = path_hash_bytes,
        .path_hops = path_hops,
        .delivered = delivered,
    };
    sanitize_ascii(entry.direction, sizeof(entry.direction), direction ? direction : "rx");
    sanitize_ascii(entry.author, sizeof(entry.author), author ? author : "Public");
    sanitize_ascii(entry.text, sizeof(entry.text), text);
    if (entry.direction[0] == '\0') {
        strncpy(entry.direction, "rx", sizeof(entry.direction) - 1U);
    }
    if (entry.author[0] == '\0') {
        strncpy(entry.author, "Public", sizeof(entry.author) - 1U);
    }

    s_entries[s_head] = entry;
    s_head = (s_head + 1U) % D1L_MESSAGE_STORE_CAPACITY;
    if (s_count < D1L_MESSAGE_STORE_CAPACITY) {
        s_count++;
    } else {
        s_dropped_oldest++;
    }
    s_total_written++;
    esp_err_t ret = persist ? persist_store() : ESP_OK;
    d1l_store_lock_give(&s_store_lock);
    return ret;
}

esp_err_t d1l_message_store_append_public(const char *direction, const char *author,
                                          const char *text, int rssi_dbm, int snr_tenths,
                                          uint8_t path_hash_bytes, uint8_t path_hops,
                                          bool delivered)
{
    return append_public_internal(direction, author, text, rssi_dbm, snr_tenths,
                                  path_hash_bytes, path_hops, delivered, true);
}

esp_err_t d1l_message_store_append_public_volatile(const char *direction, const char *author,
                                                   const char *text, int rssi_dbm, int snr_tenths,
                                                   uint8_t path_hash_bytes, uint8_t path_hops,
                                                   bool delivered)
{
    return append_public_internal(direction, author, text, rssi_dbm, snr_tenths,
                                  path_hash_bytes, path_hops, delivered, false);
}

d1l_message_store_stats_t d1l_message_store_stats(void)
{
    d1l_store_lock_take(&s_store_lock);
    d1l_message_store_stats_t stats = {
        .next_seq = s_next_seq,
        .total_written = s_total_written,
        .dropped_oldest = s_dropped_oldest,
        .count = s_count,
        .capacity = D1L_MESSAGE_STORE_CAPACITY,
    };
    d1l_store_lock_give(&s_store_lock);
    return stats;
}

size_t d1l_message_store_copy_recent(d1l_message_entry_t *out_entries, size_t max_entries)
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
    size_t oldest = (s_head + D1L_MESSAGE_STORE_CAPACITY - s_count) % D1L_MESSAGE_STORE_CAPACITY;
    if (s_count > n) {
        oldest = (oldest + (s_count - n)) % D1L_MESSAGE_STORE_CAPACITY;
    }

    for (size_t i = 0; i < n; ++i) {
        out_entries[i] = s_entries[(oldest + i) % D1L_MESSAGE_STORE_CAPACITY];
    }
    d1l_store_lock_give(&s_store_lock);
    return n;
}

size_t d1l_message_store_query_page(d1l_message_entry_t *out_entries, size_t max_entries,
                                    size_t skip_newest, const char *query,
                                    size_t *out_total_matches)
{
    if (out_total_matches) {
        *out_total_matches = 0;
    }
    if (out_entries == NULL || max_entries == 0) {
        return 0;
    }

    d1l_store_lock_take(&s_store_lock);
    if (s_count == 0) {
        d1l_store_lock_give(&s_store_lock);
        return 0;
    }
    size_t total_matches = 0;
    size_t oldest = (s_head + D1L_MESSAGE_STORE_CAPACITY - s_count) % D1L_MESSAGE_STORE_CAPACITY;
    for (size_t i = 0; i < s_count; ++i) {
        const d1l_message_entry_t *entry = &s_entries[(oldest + i) % D1L_MESSAGE_STORE_CAPACITY];
        if (!message_matches_query(entry, query)) {
            continue;
        }
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
        const d1l_message_entry_t *entry = &s_entries[(oldest + i) % D1L_MESSAGE_STORE_CAPACITY];
        if (!message_matches_query(entry, query)) {
            continue;
        }
        if (match_index >= first_match && match_index < last_match) {
            out_entries[out_index++] = *entry;
        }
        match_index++;
    }
    d1l_store_lock_give(&s_store_lock);
    return out_index;
}

size_t d1l_message_store_query(d1l_message_entry_t *out_entries, size_t max_entries,
                               const char *query)
{
    return d1l_message_store_query_page(out_entries, max_entries, 0, query, NULL);
}
