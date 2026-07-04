#include "packet_log.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "esp_timer.h"

#include "hal/rp2040_bridge.h"
#include "mesh/store_lock.h"
#include "storage/retained_blob_store.h"

#define D1L_PACKET_LOG_KEY "ring"
#define D1L_PACKET_LOG_SCHEMA_V2 2U
#define D1L_PACKET_LOG_SCHEMA 3U
#define D1L_PACKET_LOG_HISTORY_MAGIC 0x314B5044UL
#define D1L_PACKET_LOG_HISTORY_SCHEMA 1U
#define D1L_PACKET_LOG_HISTORY_PATH_FMT "stores/packet_log/segments/s%02lu.bin"
#define D1L_PACKET_LOG_HISTORY_WRITE_TIMEOUT_MS 750U
#define D1L_PACKET_LOG_HISTORY_MAX_CONSECUTIVE_MISSES 4U

_Static_assert((D1L_PACKET_LOG_SD_CAPACITY % D1L_PACKET_LOG_SD_SEGMENT_COUNT) == 0,
               "packet SD history capacity must divide evenly into segments");

typedef struct {
    uint32_t schema;
    uint32_t next_seq;
    uint32_t total_written;
    uint32_t dropped_oldest;
    uint32_t head;
    uint32_t count;
    d1l_packet_log_entry_t entries[D1L_PACKET_LOG_NVS_FALLBACK_CAPACITY];
} d1l_packet_log_fallback_blob_t;

typedef struct {
    uint32_t schema;
    uint32_t next_seq;
    uint32_t total_written;
    uint32_t dropped_oldest;
    uint32_t head;
    uint32_t count;
    d1l_packet_log_entry_t entries[D1L_PACKET_LOG_RAM_CAPACITY];
} d1l_packet_log_primary_blob_t;

typedef struct {
    uint32_t magic;
    uint16_t schema;
    uint16_t entry_len;
    uint32_t checksum;
    d1l_packet_log_entry_t entry;
} d1l_packet_log_history_record_t;

_Static_assert(sizeof(d1l_packet_log_history_record_t) <= D1L_RP2040_FILE_CHUNK_MAX,
               "packet SD history record must fit in one RP2040 file chunk");

static d1l_packet_log_entry_t s_entries[D1L_PACKET_LOG_CAPACITY];
static size_t s_head;
static size_t s_count;
static uint32_t s_next_seq = 1;
static uint32_t s_total_written;
static uint32_t s_dropped_oldest;
static uint32_t s_sd_dirty_count;
static uint32_t s_last_sd_flush_ms;
static uint32_t s_sd_history_records;
static uint32_t s_sd_history_failed_writes;
static bool s_loaded;
static d1l_packet_log_primary_blob_t s_primary_blob_scratch;
static d1l_packet_log_fallback_blob_t s_fallback_blob_scratch;
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

static bool is_volatile_ui_canary(const d1l_packet_log_entry_t *entry)
{
    return entry &&
           (strncmp(entry->kind, "ui_canary", sizeof(entry->kind)) == 0 ||
            strncmp(entry->note, "ui-canary ", strlen("ui-canary ")) == 0);
}

static uint32_t history_record_checksum(const d1l_packet_log_entry_t *entry)
{
    const uint8_t *data = (const uint8_t *)entry;
    uint32_t hash = 2166136261UL;
    for (size_t i = 0; entry && i < sizeof(*entry); ++i) {
        hash ^= data[i];
        hash *= 16777619UL;
    }
    return hash;
}

static bool history_segment_path(uint32_t seq, char *dest, size_t dest_size)
{
    if (seq == 0 || !dest || dest_size == 0) {
        return false;
    }
    const uint32_t segment =
        ((seq - 1U) / D1L_PACKET_LOG_SD_SEGMENT_CAPACITY) % D1L_PACKET_LOG_SD_SEGMENT_COUNT;
    const int written = snprintf(dest, dest_size, D1L_PACKET_LOG_HISTORY_PATH_FMT,
                                 (unsigned long)segment);
    return written > 0 && (size_t)written < dest_size;
}

static void fill_history_record(d1l_packet_log_history_record_t *record,
                                const d1l_packet_log_entry_t *entry)
{
    memset(record, 0, sizeof(*record));
    record->magic = D1L_PACKET_LOG_HISTORY_MAGIC;
    record->schema = D1L_PACKET_LOG_HISTORY_SCHEMA;
    record->entry_len = sizeof(record->entry);
    record->entry = *entry;
    record->checksum = history_record_checksum(&record->entry);
}

static bool history_record_is_valid(const d1l_packet_log_history_record_t *record,
                                    uint32_t expected_seq)
{
    return record &&
           record->magic == D1L_PACKET_LOG_HISTORY_MAGIC &&
           record->schema == D1L_PACKET_LOG_HISTORY_SCHEMA &&
           record->entry_len == sizeof(record->entry) &&
           record->entry.seq == expected_seq &&
           record->checksum == history_record_checksum(&record->entry);
}

static bool read_sd_history_entry(uint32_t seq, d1l_packet_log_entry_t *out_entry)
{
    if (seq == 0 || !out_entry ||
        !d1l_retained_blob_store_uses_sd(D1L_RETAINED_BLOB_STORE_PACKET_LOG)) {
        return false;
    }

    char path[D1L_RP2040_FILE_PATH_MAX + 1U];
    if (!history_segment_path(seq, path, sizeof(path))) {
        return false;
    }
    const uint32_t offset =
        ((seq - 1U) % D1L_PACKET_LOG_SD_SEGMENT_CAPACITY) *
        (uint32_t)sizeof(d1l_packet_log_history_record_t);
    d1l_packet_log_history_record_t record = {0};
    d1l_rp2040_file_result_t result = {0};
    const esp_err_t ret = d1l_rp2040_bridge_file_read(path, offset,
                                                       (uint8_t *)&record,
                                                       sizeof(record), &result,
                                                       D1L_PACKET_LOG_HISTORY_WRITE_TIMEOUT_MS);
    if (ret != ESP_OK || result.length != sizeof(record) ||
        !history_record_is_valid(&record, seq)) {
        return false;
    }
    *out_entry = record.entry;
    return true;
}

static void restore_sd_history_count_from_total(void)
{
    s_sd_history_records = s_total_written < D1L_PACKET_LOG_SD_CAPACITY ?
                           s_total_written : D1L_PACKET_LOG_SD_CAPACITY;
    s_sd_history_failed_writes = 0;
}

static esp_err_t append_sd_history_locked(const d1l_packet_log_entry_t *entry)
{
    if (!entry || !d1l_retained_blob_store_uses_sd(D1L_RETAINED_BLOB_STORE_PACKET_LOG)) {
        return ESP_OK;
    }

    char path[D1L_RP2040_FILE_PATH_MAX + 1U];
    if (!history_segment_path(entry->seq, path, sizeof(path))) {
        return ESP_ERR_INVALID_ARG;
    }

    d1l_packet_log_history_record_t record;
    fill_history_record(&record, entry);
    d1l_rp2040_file_result_t result = {0};
    const bool segment_start =
        ((entry->seq - 1U) % D1L_PACKET_LOG_SD_SEGMENT_CAPACITY) == 0;
    esp_err_t ret;
    if (segment_start) {
        ret = d1l_rp2040_bridge_file_write(path, 0U, (const uint8_t *)&record,
                                           sizeof(record), true, &result,
                                           D1L_PACKET_LOG_HISTORY_WRITE_TIMEOUT_MS);
    } else {
        ret = d1l_rp2040_bridge_file_append(path, (const uint8_t *)&record,
                                            sizeof(record), &result,
                                            D1L_PACKET_LOG_HISTORY_WRITE_TIMEOUT_MS);
    }
    if (ret != ESP_OK || result.length != sizeof(record)) {
        return ret == ESP_OK ? ESP_FAIL : ret;
    }
    return ESP_OK;
}

static esp_err_t clear_sd_history_locked(void)
{
    if (!d1l_retained_blob_store_uses_sd(D1L_RETAINED_BLOB_STORE_PACKET_LOG)) {
        s_sd_history_records = 0;
        s_sd_history_failed_writes = 0;
        return ESP_OK;
    }

    esp_err_t first_error = ESP_OK;
    for (uint32_t segment = 0; segment < D1L_PACKET_LOG_SD_SEGMENT_COUNT; ++segment) {
        char path[D1L_RP2040_FILE_PATH_MAX + 1U];
        const int written = snprintf(path, sizeof(path), D1L_PACKET_LOG_HISTORY_PATH_FMT,
                                     (unsigned long)segment);
        if (written <= 0 || (size_t)written >= sizeof(path)) {
            if (first_error == ESP_OK) {
                first_error = ESP_ERR_INVALID_SIZE;
            }
            continue;
        }
        d1l_rp2040_file_result_t result = {0};
        esp_err_t ret = d1l_rp2040_bridge_file_delete(path, &result,
                                                      D1L_PACKET_LOG_HISTORY_WRITE_TIMEOUT_MS);
        if (ret == ESP_ERR_NOT_FOUND || strcmp(result.err, "not_found") == 0) {
            ret = ESP_OK;
        }
        if (ret != ESP_OK && first_error == ESP_OK) {
            first_error = ret;
        }
    }
    s_sd_history_records = 0;
    s_sd_history_failed_writes = 0;
    return first_error;
}

static void clear_ram(void)
{
    memset(s_entries, 0, sizeof(s_entries));
    s_head = 0;
    s_count = 0;
    s_next_seq = 1;
    s_total_written = 0;
    s_dropped_oldest = 0;
    s_sd_dirty_count = 0;
    s_last_sd_flush_ms = 0;
    s_sd_history_records = 0;
    s_sd_history_failed_writes = 0;
}

static void fill_entries(d1l_packet_log_entry_t *dest, size_t dest_capacity,
                         uint32_t *out_head, uint32_t *out_count)
{
    if (!dest || dest_capacity == 0 || !out_head || !out_count) {
        return;
    }
    memset(dest, 0, dest_capacity * sizeof(dest[0]));
    if (s_count == 0) {
        *out_head = 0;
        *out_count = 0;
        return;
    }

    size_t eligible = 0;
    size_t oldest = (s_head + D1L_PACKET_LOG_CAPACITY - s_count) % D1L_PACKET_LOG_CAPACITY;
    for (size_t i = 0; i < s_count; ++i) {
        if (!is_volatile_ui_canary(&s_entries[(oldest + i) % D1L_PACKET_LOG_CAPACITY])) {
            eligible++;
        }
    }

    const size_t skip = eligible > dest_capacity ? eligible - dest_capacity : 0;
    size_t seen = 0;
    size_t copied = 0;
    for (size_t i = 0; i < s_count; ++i) {
        const d1l_packet_log_entry_t *entry =
            &s_entries[(oldest + i) % D1L_PACKET_LOG_CAPACITY];
        if (is_volatile_ui_canary(entry)) {
            continue;
        }
        if (seen++ < skip) {
            continue;
        }
        if (copied < dest_capacity) {
            dest[copied++] = *entry;
        }
    }
    *out_head = (uint32_t)(copied % dest_capacity);
    *out_count = (uint32_t)copied;
}

static void fill_fallback_blob(d1l_packet_log_fallback_blob_t *blob)
{
    memset(blob, 0, sizeof(*blob));
    blob->schema = D1L_PACKET_LOG_SCHEMA;
    blob->next_seq = s_next_seq;
    blob->total_written = s_total_written;
    blob->dropped_oldest = s_dropped_oldest;
    fill_entries(blob->entries, D1L_PACKET_LOG_NVS_FALLBACK_CAPACITY,
                 &blob->head, &blob->count);
}

static void fill_primary_blob(d1l_packet_log_primary_blob_t *blob)
{
    memset(blob, 0, sizeof(*blob));
    blob->schema = D1L_PACKET_LOG_SCHEMA;
    blob->next_seq = s_next_seq;
    blob->total_written = s_total_written;
    blob->dropped_oldest = s_dropped_oldest;
    fill_entries(blob->entries, D1L_PACKET_LOG_RAM_CAPACITY,
                 &blob->head, &blob->count);
}

static esp_err_t persist_store(bool flush_primary)
{
    fill_fallback_blob(&s_fallback_blob_scratch);
    if (flush_primary) {
        fill_primary_blob(&s_primary_blob_scratch);
        esp_err_t ret =
            d1l_retained_blob_store_write_split(D1L_RETAINED_BLOB_STORE_PACKET_LOG,
                                                D1L_PACKET_LOG_KEY,
                                                &s_primary_blob_scratch,
                                                sizeof(s_primary_blob_scratch),
                                                &s_fallback_blob_scratch,
                                                sizeof(s_fallback_blob_scratch));
        if (ret == ESP_OK) {
            s_sd_dirty_count = 0;
            s_last_sd_flush_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        }
        return ret;
    }

    return d1l_retained_blob_store_write_split(D1L_RETAINED_BLOB_STORE_PACKET_LOG,
                                               D1L_PACKET_LOG_KEY,
                                               NULL,
                                               0,
                                               &s_fallback_blob_scratch,
                                               sizeof(s_fallback_blob_scratch));
}

static bool fallback_blob_is_valid(const d1l_packet_log_fallback_blob_t *blob, size_t len)
{
    return blob && len == sizeof(*blob) &&
           blob->schema == D1L_PACKET_LOG_SCHEMA &&
           blob->head < D1L_PACKET_LOG_NVS_FALLBACK_CAPACITY &&
           blob->count <= D1L_PACKET_LOG_NVS_FALLBACK_CAPACITY &&
           blob->next_seq > 0;
}

static bool fallback_blob_v2_is_valid(const d1l_packet_log_fallback_blob_t *blob, size_t len)
{
    return blob && len == sizeof(*blob) &&
           blob->schema == D1L_PACKET_LOG_SCHEMA_V2 &&
           blob->head < D1L_PACKET_LOG_NVS_FALLBACK_CAPACITY &&
           blob->count <= D1L_PACKET_LOG_NVS_FALLBACK_CAPACITY &&
           blob->next_seq > 0;
}

static bool primary_blob_is_valid(const d1l_packet_log_primary_blob_t *blob, size_t len)
{
    return blob && len == sizeof(*blob) &&
           blob->schema == D1L_PACKET_LOG_SCHEMA &&
           blob->head < D1L_PACKET_LOG_RAM_CAPACITY &&
           blob->count <= D1L_PACKET_LOG_RAM_CAPACITY &&
           blob->next_seq > 0;
}

static void load_fallback_blob_into_ram(void)
{
    memcpy(s_entries, s_fallback_blob_scratch.entries,
           s_fallback_blob_scratch.count * sizeof(s_fallback_blob_scratch.entries[0]));
    s_head = s_fallback_blob_scratch.count % D1L_PACKET_LOG_CAPACITY;
    s_count = s_fallback_blob_scratch.count;
    s_next_seq = s_fallback_blob_scratch.next_seq;
    s_total_written = s_fallback_blob_scratch.total_written;
    s_dropped_oldest = s_fallback_blob_scratch.dropped_oldest;
    s_sd_dirty_count = 0;
    restore_sd_history_count_from_total();
}

static void load_primary_blob_into_ram(void)
{
    memcpy(s_entries, s_primary_blob_scratch.entries,
           s_primary_blob_scratch.count * sizeof(s_primary_blob_scratch.entries[0]));
    s_head = s_primary_blob_scratch.count % D1L_PACKET_LOG_CAPACITY;
    s_count = s_primary_blob_scratch.count;
    s_next_seq = s_primary_blob_scratch.next_seq;
    s_total_written = s_primary_blob_scratch.total_written;
    s_dropped_oldest = s_primary_blob_scratch.dropped_oldest;
    s_sd_dirty_count = 0;
    restore_sd_history_count_from_total();
}

esp_err_t d1l_packet_log_init(void)
{
    clear_ram();

    const bool use_sd = d1l_retained_blob_store_uses_sd(D1L_RETAINED_BLOB_STORE_PACKET_LOG);
    esp_err_t ret;
    if (use_sd) {
        size_t len = sizeof(s_primary_blob_scratch);
        ret = d1l_retained_blob_store_read(D1L_RETAINED_BLOB_STORE_PACKET_LOG,
                                           D1L_PACKET_LOG_KEY,
                                           &s_primary_blob_scratch,
                                           &len);
        if (ret == ESP_OK && primary_blob_is_valid(&s_primary_blob_scratch, len)) {
            load_primary_blob_into_ram();
            s_loaded = true;
            return ESP_OK;
        }
        if (ret == ESP_ERR_NOT_FOUND) {
            ret = ESP_OK;
        } else if (ret == ESP_OK) {
            ret = ESP_ERR_INVALID_STATE;
        }
    } else {
        ret = ESP_ERR_NOT_FOUND;
    }

    size_t len = sizeof(s_fallback_blob_scratch);
    esp_err_t fallback_ret =
        d1l_retained_blob_store_read_fallback(D1L_RETAINED_BLOB_STORE_PACKET_LOG,
                                              D1L_PACKET_LOG_KEY,
                                              &s_fallback_blob_scratch,
                                              &len);
    if (fallback_ret == ESP_ERR_NOT_FOUND) {
        if (ret == ESP_ERR_INVALID_STATE) {
            clear_ram();
            ret = d1l_retained_blob_store_erase(D1L_RETAINED_BLOB_STORE_PACKET_LOG,
                                                D1L_PACKET_LOG_KEY);
        } else {
            ret = ESP_OK;
        }
    } else if (fallback_ret == ESP_OK &&
               fallback_blob_v2_is_valid(&s_fallback_blob_scratch, len)) {
        s_fallback_blob_scratch.schema = D1L_PACKET_LOG_SCHEMA;
        load_fallback_blob_into_ram();
        ret = persist_store(use_sd);
    } else if (fallback_ret == ESP_OK &&
               fallback_blob_is_valid(&s_fallback_blob_scratch, len)) {
        load_fallback_blob_into_ram();
        ret = ESP_OK;
    } else if (fallback_ret != ESP_ERR_NOT_FOUND) {
        clear_ram();
        ret = d1l_retained_blob_store_erase(D1L_RETAINED_BLOB_STORE_PACKET_LOG,
                                            D1L_PACKET_LOG_KEY);
    }
    s_loaded = (ret == ESP_OK);
    return ret;
}

esp_err_t d1l_packet_log_clear(void)
{
    d1l_store_lock_take(&s_store_lock);
    clear_ram();
    s_loaded = true;

    esp_err_t history_ret = clear_sd_history_locked();
    esp_err_t ret = d1l_retained_blob_store_erase(D1L_RETAINED_BLOB_STORE_PACKET_LOG,
                                                  D1L_PACKET_LOG_KEY);
    d1l_store_lock_give(&s_store_lock);
    return ret == ESP_OK ? history_ret : ret;
}

bool d1l_packet_log_append(const d1l_packet_log_entry_t *entry)
{
    return d1l_packet_log_append_raw(entry, NULL, 0);
}

static bool append_raw_internal(const d1l_packet_log_entry_t *entry, const uint8_t *raw,
                                size_t raw_len, bool persist)
{
    if (entry == NULL) {
        return false;
    }
    if (!s_loaded && d1l_packet_log_init() != ESP_OK) {
        return false;
    }

    d1l_store_lock_take(&s_store_lock);
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
    const bool use_sd = persist &&
        d1l_retained_blob_store_uses_sd(D1L_RETAINED_BLOB_STORE_PACKET_LOG);
    if (use_sd) {
        s_sd_dirty_count++;
    }
    esp_err_t history_ret = ESP_OK;
    if (use_sd) {
        history_ret = append_sd_history_locked(&copy);
        if (history_ret == ESP_OK) {
            if (s_sd_history_records < D1L_PACKET_LOG_SD_CAPACITY) {
                s_sd_history_records++;
            }
        } else {
            s_sd_history_failed_writes++;
        }
    }
    const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    const bool flush_primary =
        use_sd &&
        (s_sd_dirty_count >= D1L_PACKET_LOG_SD_FLUSH_DIRTY_THRESHOLD ||
         s_last_sd_flush_ms == 0 ||
         now_ms - s_last_sd_flush_ms >= D1L_PACKET_LOG_SD_FLUSH_INTERVAL_MS);
    esp_err_t ret = persist ? persist_store(flush_primary) : ESP_OK;
    d1l_store_lock_give(&s_store_lock);
    return ret == ESP_OK;
}

bool d1l_packet_log_append_raw(const d1l_packet_log_entry_t *entry, const uint8_t *raw,
                               size_t raw_len)
{
    return append_raw_internal(entry, raw, raw_len, true);
}

bool d1l_packet_log_append_raw_volatile(const d1l_packet_log_entry_t *entry,
                                        const uint8_t *raw, size_t raw_len)
{
    return append_raw_internal(entry, raw, raw_len, false);
}

esp_err_t d1l_packet_log_flush(void)
{
    if (!s_loaded) {
        esp_err_t ret = d1l_packet_log_init();
        if (ret != ESP_OK) {
            return ret;
        }
    }
    d1l_store_lock_take(&s_store_lock);
    esp_err_t ret = persist_store(d1l_retained_blob_store_uses_sd(D1L_RETAINED_BLOB_STORE_PACKET_LOG));
    d1l_store_lock_give(&s_store_lock);
    return ret;
}

d1l_packet_log_stats_t d1l_packet_log_stats(void)
{
    d1l_store_lock_take(&s_store_lock);
    const bool sd_history_enabled =
        d1l_retained_blob_store_uses_sd(D1L_RETAINED_BLOB_STORE_PACKET_LOG);
    d1l_packet_log_stats_t stats = {
        .next_seq = s_next_seq,
        .total_written = s_total_written,
        .dropped_oldest = s_dropped_oldest,
        .sd_history_records = sd_history_enabled ? s_sd_history_records : 0,
        .sd_history_failed_writes = s_sd_history_failed_writes,
        .count = s_count,
        .capacity = D1L_PACKET_LOG_CAPACITY,
        .sd_capacity = D1L_PACKET_LOG_SD_CAPACITY,
        .sd_history_enabled = sd_history_enabled,
    };
    d1l_store_lock_give(&s_store_lock);
    return stats;
}

size_t d1l_packet_log_copy_recent(d1l_packet_log_entry_t *out_entries, size_t max_entries)
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
    size_t oldest = (s_head + D1L_PACKET_LOG_CAPACITY - s_count) % D1L_PACKET_LOG_CAPACITY;
    if (s_count > n) {
        oldest = (oldest + (s_count - n)) % D1L_PACKET_LOG_CAPACITY;
    }

    for (size_t i = 0; i < n; ++i) {
        out_entries[i] = s_entries[(oldest + i) % D1L_PACKET_LOG_CAPACITY];
    }
    d1l_store_lock_give(&s_store_lock);
    return n;
}

static void reverse_entries(d1l_packet_log_entry_t *entries, size_t count)
{
    if (!entries) {
        return;
    }
    for (size_t i = 0; i < count / 2U; ++i) {
        d1l_packet_log_entry_t tmp = entries[i];
        entries[i] = entries[count - 1U - i];
        entries[count - 1U - i] = tmp;
    }
}

static bool packet_query_unfiltered(const char *direction, const char *kind,
                                    const char *search_text)
{
    return token_is_any(direction) && token_is_any(kind) && token_is_any(search_text);
}

static size_t query_ram_locked(d1l_packet_log_entry_t *out_entries, size_t max_entries,
                               size_t skip_newest, const char *direction,
                               const char *kind, const char *search_text,
                               size_t *out_total_matches)
{
    if (out_total_matches) {
        *out_total_matches = 0;
    }
    if (s_count == 0) {
        return 0;
    }
    size_t total_matches = 0;
    size_t oldest = (s_head + D1L_PACKET_LOG_CAPACITY - s_count) % D1L_PACKET_LOG_CAPACITY;
    for (size_t i = 0; i < s_count; ++i) {
        const d1l_packet_log_entry_t *entry =
            &s_entries[(oldest + i) % D1L_PACKET_LOG_CAPACITY];
        if (packet_matches(entry, direction, kind, search_text)) {
            total_matches++;
        }
    }

    if (out_total_matches) {
        *out_total_matches = total_matches;
    }
    if (skip_newest >= total_matches) {
        return 0;
    }

    const size_t available = total_matches - skip_newest;
    const size_t copied = available < max_entries ? available : max_entries;
    const size_t first_match = total_matches - skip_newest - copied;
    const size_t last_match = first_match + copied;
    size_t match_index = 0;
    size_t out_index = 0;
    for (size_t i = 0; i < s_count && out_index < copied; ++i) {
        const d1l_packet_log_entry_t *entry =
            &s_entries[(oldest + i) % D1L_PACKET_LOG_CAPACITY];
        if (!packet_matches(entry, direction, kind, search_text)) {
            continue;
        }
        if (match_index >= first_match && match_index < last_match) {
            out_entries[out_index++] = *entry;
        }
        match_index++;
    }
    return out_index;
}

static size_t query_sd_history(d1l_packet_log_entry_t *out_entries, size_t max_entries,
                               size_t skip_newest, const char *direction,
                               const char *kind, const char *search_text,
                               uint32_t newest_seq, uint32_t history_records,
                               size_t *out_total_matches, size_t *out_valid_records)
{
    if (out_total_matches) {
        *out_total_matches = 0;
    }
    if (out_valid_records) {
        *out_valid_records = 0;
    }
    if (history_records == 0 || newest_seq == 0) {
        return 0;
    }

    size_t copied = 0;
    size_t total_matches = 0;
    size_t valid_records = 0;
    size_t consecutive_misses = 0;
    const bool unfiltered = packet_query_unfiltered(direction, kind, search_text);
    if (unfiltered && out_total_matches) {
        *out_total_matches = history_records;
    }

    for (uint32_t scanned = 0; scanned < history_records; ++scanned) {
        if (newest_seq <= scanned) {
            break;
        }
        const uint32_t seq = newest_seq - scanned;
        d1l_packet_log_entry_t entry = {0};
        if (!read_sd_history_entry(seq, &entry)) {
            consecutive_misses++;
            if (consecutive_misses >= D1L_PACKET_LOG_HISTORY_MAX_CONSECUTIVE_MISSES) {
                break;
            }
            continue;
        }
        consecutive_misses = 0;
        valid_records++;
        if (!packet_matches(&entry, direction, kind, search_text)) {
            continue;
        }
        if (!unfiltered) {
            total_matches++;
        }
        const size_t newest_match_index = unfiltered ? (size_t)scanned : (total_matches - 1U);
        if (newest_match_index < skip_newest) {
            continue;
        }
        if (copied < max_entries) {
            out_entries[copied++] = entry;
        } else if (!out_total_matches) {
            break;
        }
        if (unfiltered && copied >= max_entries) {
            break;
        }
    }

    if (!unfiltered && out_total_matches) {
        *out_total_matches = total_matches;
    }
    if (out_valid_records) {
        *out_valid_records = valid_records;
    }
    reverse_entries(out_entries, copied);
    return copied;
}

size_t d1l_packet_log_query_page(d1l_packet_log_entry_t *out_entries, size_t max_entries,
                                 size_t skip_newest, const char *direction,
                                 const char *kind, const char *search_text,
                                 size_t *out_total_matches, bool *out_sd_used)
{
    if (out_total_matches) {
        *out_total_matches = 0;
    }
    if (out_sd_used) {
        *out_sd_used = false;
    }
    if (out_entries == NULL || max_entries == 0) {
        return 0;
    }
    if (!s_loaded && d1l_packet_log_init() != ESP_OK) {
        return 0;
    }

    d1l_store_lock_take(&s_store_lock);
    const bool sd_history_enabled =
        d1l_retained_blob_store_uses_sd(D1L_RETAINED_BLOB_STORE_PACKET_LOG);
    const uint32_t newest_seq = s_next_seq > 0 ? s_next_seq - 1U : 0;
    const uint32_t history_records = sd_history_enabled ? s_sd_history_records : 0;
    const bool use_sd = sd_history_enabled && history_records > s_count;
    if (!use_sd) {
        const size_t copied = query_ram_locked(out_entries, max_entries, skip_newest,
                                              direction, kind, search_text,
                                              out_total_matches);
        d1l_store_lock_give(&s_store_lock);
        return copied;
    }
    d1l_store_lock_give(&s_store_lock);

    size_t valid_records = 0;
    const size_t copied = query_sd_history(out_entries, max_entries, skip_newest,
                                           direction, kind, search_text,
                                           newest_seq, history_records,
                                           out_total_matches, &valid_records);
    if (out_sd_used) {
        *out_sd_used = valid_records > 0;
    }
    if (valid_records > 0 || skip_newest > 0) {
        return copied;
    }

    d1l_store_lock_take(&s_store_lock);
    const size_t fallback = query_ram_locked(out_entries, max_entries, skip_newest,
                                            direction, kind, search_text,
                                            out_total_matches);
    d1l_store_lock_give(&s_store_lock);
    return fallback;
}

size_t d1l_packet_log_query(d1l_packet_log_entry_t *out_entries, size_t max_entries,
                            const char *direction, const char *kind, const char *search_text)
{
    return d1l_packet_log_query_page(out_entries, max_entries, 0, direction, kind,
                                     search_text, NULL, NULL);
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

    d1l_store_lock_take(&s_store_lock);
    for (size_t i = 0; i < s_count; ++i) {
        if (s_entries[i].seq == seq) {
            *out_entry = s_entries[i];
            d1l_store_lock_give(&s_store_lock);
            return ESP_OK;
        }
    }
    d1l_store_lock_give(&s_store_lock);
    const d1l_packet_log_stats_t stats = d1l_packet_log_stats();
    const uint32_t newest_seq = stats.next_seq > 0 ? stats.next_seq - 1U : 0;
    const uint32_t oldest_seq =
        stats.sd_history_records > newest_seq ? 1U : newest_seq - stats.sd_history_records + 1U;
    if (stats.sd_history_enabled && seq >= oldest_seq && seq <= newest_seq &&
        read_sd_history_entry(seq, out_entry)) {
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}
