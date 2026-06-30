#include "message_store.h"

#include <string.h>

#include "esp_timer.h"
#include "nvs.h"

#define D1L_MESSAGE_STORE_NAMESPACE "d1l_messages"
#define D1L_MESSAGE_STORE_KEY "public"
#define D1L_MESSAGE_STORE_SCHEMA 1U

typedef struct {
    uint32_t schema;
    uint32_t next_seq;
    uint32_t total_written;
    uint32_t dropped_oldest;
    uint32_t head;
    uint32_t count;
    d1l_message_entry_t entries[D1L_MESSAGE_STORE_CAPACITY];
} d1l_message_store_blob_t;

static d1l_message_entry_t s_entries[D1L_MESSAGE_STORE_CAPACITY];
static size_t s_head;
static size_t s_count;
static uint32_t s_next_seq = 1;
static uint32_t s_total_written;
static uint32_t s_dropped_oldest;
static bool s_loaded;
static d1l_message_store_blob_t s_blob_scratch;

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
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(D1L_MESSAGE_STORE_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    fill_blob(&s_blob_scratch);
    ret = nvs_set_blob(handle, D1L_MESSAGE_STORE_KEY, &s_blob_scratch, sizeof(s_blob_scratch));
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

static bool blob_is_valid(const d1l_message_store_blob_t *blob, size_t len)
{
    return blob && len == sizeof(*blob) &&
           blob->schema == D1L_MESSAGE_STORE_SCHEMA &&
           blob->head < D1L_MESSAGE_STORE_CAPACITY &&
           blob->count <= D1L_MESSAGE_STORE_CAPACITY &&
           blob->next_seq > 0;
}

esp_err_t d1l_message_store_init(void)
{
    clear_ram();

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(D1L_MESSAGE_STORE_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        s_loaded = false;
        return ret;
    }

    size_t len = sizeof(s_blob_scratch);
    ret = nvs_get_blob(handle, D1L_MESSAGE_STORE_KEY, &s_blob_scratch, &len);
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
        ret = nvs_erase_key(handle, D1L_MESSAGE_STORE_KEY);
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            ret = ESP_OK;
        }
        if (ret == ESP_OK) {
            ret = nvs_commit(handle);
        }
    }
    nvs_close(handle);
    s_loaded = (ret == ESP_OK);
    return ret;
}

esp_err_t d1l_message_store_clear(void)
{
    clear_ram();
    s_loaded = true;

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(D1L_MESSAGE_STORE_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = nvs_erase_key(handle, D1L_MESSAGE_STORE_KEY);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ret = ESP_OK;
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

esp_err_t d1l_message_store_append_public(const char *direction, const char *author,
                                          const char *text, int rssi_dbm, int snr_tenths,
                                          uint8_t path_hash_bytes, uint8_t path_hops,
                                          bool delivered)
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
    return persist_store();
}

d1l_message_store_stats_t d1l_message_store_stats(void)
{
    d1l_message_store_stats_t stats = {
        .next_seq = s_next_seq,
        .total_written = s_total_written,
        .dropped_oldest = s_dropped_oldest,
        .count = s_count,
        .capacity = D1L_MESSAGE_STORE_CAPACITY,
    };
    return stats;
}

size_t d1l_message_store_copy_recent(d1l_message_entry_t *out_entries, size_t max_entries)
{
    if (out_entries == NULL || max_entries == 0 || s_count == 0) {
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
    return n;
}

size_t d1l_message_store_query(d1l_message_entry_t *out_entries, size_t max_entries,
                               const char *query)
{
    if (out_entries == NULL || max_entries == 0 || s_count == 0) {
        return 0;
    }

    size_t matches = 0;
    size_t oldest = (s_head + D1L_MESSAGE_STORE_CAPACITY - s_count) % D1L_MESSAGE_STORE_CAPACITY;
    for (size_t i = 0; i < s_count; ++i) {
        const d1l_message_entry_t *entry = &s_entries[(oldest + i) % D1L_MESSAGE_STORE_CAPACITY];
        if (!message_matches_query(entry, query)) {
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
