#include "packet_log.h"

#include <ctype.h>
#include <string.h>

#include "esp_timer.h"
#include "nvs.h"

#define D1L_PACKET_LOG_NAMESPACE "d1l_packets"
#define D1L_PACKET_LOG_KEY "ring"
#define D1L_PACKET_LOG_SCHEMA 2U

typedef struct {
    uint32_t schema;
    uint32_t next_seq;
    uint32_t total_written;
    uint32_t dropped_oldest;
    uint32_t head;
    uint32_t count;
    d1l_packet_log_entry_t entries[D1L_PACKET_LOG_PERSIST_CAPACITY];
} d1l_packet_log_blob_t;

static d1l_packet_log_entry_t s_entries[D1L_PACKET_LOG_CAPACITY];
static size_t s_head;
static size_t s_count;
static uint32_t s_next_seq = 1;
static uint32_t s_total_written;
static uint32_t s_dropped_oldest;
static bool s_loaded;
static d1l_packet_log_blob_t s_blob_scratch;

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

static void raw_to_hex_preview(char *dest, size_t dest_size, const uint8_t *raw, size_t raw_len)
{
    static const char hex[] = "0123456789ABCDEF";
    if (!dest || dest_size == 0) {
        return;
    }
    size_t out = 0;
    const size_t preview_len =
        raw_len < D1L_PACKET_LOG_RAW_PREVIEW_BYTES ? raw_len : D1L_PACKET_LOG_RAW_PREVIEW_BYTES;
    for (size_t i = 0; raw && i < preview_len && out + 2U < dest_size; ++i) {
        dest[out++] = hex[(raw[i] >> 4) & 0x0fU];
        dest[out++] = hex[raw[i] & 0x0fU];
    }
    dest[out] = '\0';
}

static int lower_ascii(int c)
{
    return isupper((unsigned char)c) ? tolower((unsigned char)c) : c;
}

static bool token_is_any(const char *token)
{
    return token == NULL || token[0] == '\0' ||
           strcmp(token, "any") == 0 || strcmp(token, "*") == 0;
}

static bool equals_ignore_case(const char *left, const char *right)
{
    if (!left || !right) {
        return false;
    }
    while (*left && *right) {
        if (lower_ascii(*left++) != lower_ascii(*right++)) {
            return false;
        }
    }
    return *left == '\0' && *right == '\0';
}

static bool contains_ignore_case(const char *haystack, const char *needle)
{
    if (token_is_any(needle)) {
        return true;
    }
    if (!haystack) {
        return false;
    }
    for (const char *h = haystack; *h; ++h) {
        const char *a = h;
        const char *b = needle;
        while (*a && *b && lower_ascii(*a) == lower_ascii(*b)) {
            ++a;
            ++b;
        }
        if (*b == '\0') {
            return true;
        }
    }
    return false;
}

static bool packet_matches(const d1l_packet_log_entry_t *entry, const char *direction,
                           const char *kind, const char *search_text)
{
    if (!entry) {
        return false;
    }
    if (!token_is_any(direction) && !equals_ignore_case(entry->direction, direction)) {
        return false;
    }
    if (!token_is_any(kind)) {
        if (equals_ignore_case(kind, "text")) {
            if (!contains_ignore_case(entry->kind, "text")) {
                return false;
            }
        } else if (!equals_ignore_case(entry->kind, kind)) {
            return false;
        }
    }
    if (!token_is_any(search_text) &&
        !contains_ignore_case(entry->kind, search_text) &&
        !contains_ignore_case(entry->direction, search_text) &&
        !contains_ignore_case(entry->note, search_text) &&
        !contains_ignore_case(entry->raw_hex, search_text)) {
        return false;
    }
    return true;
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

static void fill_blob(d1l_packet_log_blob_t *blob)
{
    memset(blob, 0, sizeof(*blob));
    blob->schema = D1L_PACKET_LOG_SCHEMA;
    blob->next_seq = s_next_seq;
    blob->total_written = s_total_written;
    blob->dropped_oldest = s_dropped_oldest;
    const size_t n = s_count < D1L_PACKET_LOG_PERSIST_CAPACITY ? s_count : D1L_PACKET_LOG_PERSIST_CAPACITY;
    blob->head = (uint32_t)(n % D1L_PACKET_LOG_PERSIST_CAPACITY);
    blob->count = (uint32_t)n;

    if (n > 0) {
        size_t oldest = (s_head + D1L_PACKET_LOG_CAPACITY - s_count) % D1L_PACKET_LOG_CAPACITY;
        if (s_count > n) {
            oldest = (oldest + (s_count - n)) % D1L_PACKET_LOG_CAPACITY;
        }
        for (size_t i = 0; i < n; ++i) {
            blob->entries[i] = s_entries[(oldest + i) % D1L_PACKET_LOG_CAPACITY];
        }
    }
}

static esp_err_t persist_store(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(D1L_PACKET_LOG_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    fill_blob(&s_blob_scratch);
    ret = nvs_set_blob(handle, D1L_PACKET_LOG_KEY, &s_blob_scratch, sizeof(s_blob_scratch));
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

static bool blob_is_valid(const d1l_packet_log_blob_t *blob, size_t len)
{
    return blob && len == sizeof(*blob) &&
           blob->schema == D1L_PACKET_LOG_SCHEMA &&
           blob->head < D1L_PACKET_LOG_PERSIST_CAPACITY &&
           blob->count <= D1L_PACKET_LOG_PERSIST_CAPACITY &&
           blob->next_seq > 0;
}

esp_err_t d1l_packet_log_init(void)
{
    clear_ram();

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(D1L_PACKET_LOG_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        s_loaded = false;
        return ret;
    }

    size_t len = sizeof(s_blob_scratch);
    ret = nvs_get_blob(handle, D1L_PACKET_LOG_KEY, &s_blob_scratch, &len);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ret = ESP_OK;
    } else if (ret == ESP_OK && blob_is_valid(&s_blob_scratch, len)) {
        memcpy(s_entries, s_blob_scratch.entries,
               s_blob_scratch.count * sizeof(s_blob_scratch.entries[0]));
        s_head = s_blob_scratch.count % D1L_PACKET_LOG_CAPACITY;
        s_count = s_blob_scratch.count;
        s_next_seq = s_blob_scratch.next_seq;
        s_total_written = s_blob_scratch.total_written;
        s_dropped_oldest = s_blob_scratch.dropped_oldest;
    } else if (ret == ESP_OK) {
        clear_ram();
        ret = nvs_erase_key(handle, D1L_PACKET_LOG_KEY);
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

esp_err_t d1l_packet_log_clear(void)
{
    clear_ram();
    s_loaded = true;

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(D1L_PACKET_LOG_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = nvs_erase_key(handle, D1L_PACKET_LOG_KEY);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ret = ESP_OK;
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

bool d1l_packet_log_append(const d1l_packet_log_entry_t *entry)
{
    return d1l_packet_log_append_raw(entry, NULL, 0);
}

bool d1l_packet_log_append_raw(const d1l_packet_log_entry_t *entry, const uint8_t *raw,
                               size_t raw_len)
{
    if (entry == NULL) {
        return false;
    }
    if (!s_loaded && d1l_packet_log_init() != ESP_OK) {
        return false;
    }

    d1l_packet_log_entry_t copy = *entry;
    copy.seq = s_next_seq++;
    if (copy.uptime_ms == 0) {
        copy.uptime_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    }
    sanitize_ascii(copy.direction, sizeof(copy.direction), entry->direction);
    sanitize_ascii(copy.kind, sizeof(copy.kind), entry->kind);
    sanitize_ascii(copy.note, sizeof(copy.note), entry->note);
    sanitize_ascii(copy.raw_hex, sizeof(copy.raw_hex), entry->raw_hex);
    if (copy.direction[0] == '\0') {
        strncpy(copy.direction, "rx", sizeof(copy.direction) - 1U);
    }
    if (copy.kind[0] == '\0') {
        strncpy(copy.kind, "packet", sizeof(copy.kind) - 1U);
    }
    if (raw && raw_len > 0) {
        copy.raw_len = raw_len > UINT16_MAX ? UINT16_MAX : (uint16_t)raw_len;
        copy.raw_truncated = raw_len > D1L_PACKET_LOG_RAW_PREVIEW_BYTES;
        raw_to_hex_preview(copy.raw_hex, sizeof(copy.raw_hex), raw, raw_len);
    }

    s_entries[s_head] = copy;
    s_head = (s_head + 1U) % D1L_PACKET_LOG_CAPACITY;
    if (s_count < D1L_PACKET_LOG_CAPACITY) {
        s_count++;
    } else {
        s_dropped_oldest++;
    }
    s_total_written++;
    return persist_store() == ESP_OK;
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

size_t d1l_packet_log_query(d1l_packet_log_entry_t *out_entries, size_t max_entries,
                            const char *direction, const char *kind, const char *search_text)
{
    if (out_entries == NULL || max_entries == 0 || s_count == 0) {
        return 0;
    }
    if (!s_loaded && d1l_packet_log_init() != ESP_OK) {
        return 0;
    }

    size_t copied = 0;
    size_t oldest = (s_head + D1L_PACKET_LOG_CAPACITY - s_count) % D1L_PACKET_LOG_CAPACITY;
    for (size_t i = 0; i < s_count; ++i) {
        const d1l_packet_log_entry_t *entry =
            &s_entries[(oldest + i) % D1L_PACKET_LOG_CAPACITY];
        if (!packet_matches(entry, direction, kind, search_text)) {
            continue;
        }
        if (copied < max_entries) {
            out_entries[copied++] = *entry;
        } else {
            memmove(out_entries, &out_entries[1], (max_entries - 1U) * sizeof(out_entries[0]));
            out_entries[max_entries - 1U] = *entry;
        }
    }
    return copied;
}

esp_err_t d1l_packet_log_find_by_seq(uint32_t seq, d1l_packet_log_entry_t *out_entry)
{
    if (seq == 0 || out_entry == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_loaded) {
        esp_err_t ret = d1l_packet_log_init();
        if (ret != ESP_OK) {
            return ret;
        }
    }

    for (size_t i = 0; i < s_count; ++i) {
        if (s_entries[i].seq == seq) {
            *out_entry = s_entries[i];
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}
