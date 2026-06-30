#include "contact_store.h"

#include <stdio.h>
#include <string.h>

#include "esp_timer.h"
#include "nvs.h"

#define D1L_CONTACT_STORE_NAMESPACE "d1l_contacts"
#define D1L_CONTACT_STORE_KEY "contacts"
#define D1L_CONTACT_STORE_SCHEMA_V1 1U
#define D1L_CONTACT_STORE_SCHEMA_V2 2U
#define D1L_CONTACT_STORE_SCHEMA 3U

typedef struct {
    uint32_t seq;
    uint32_t created_ms;
    uint32_t updated_ms;
    char fingerprint[D1L_NODE_FINGERPRINT_LEN];
    char alias[D1L_CONTACT_ALIAS_LEN];
    char heard_name[D1L_HEARD_NODE_NAME_LEN];
    char type[D1L_NODE_TYPE_LEN];
    int last_rssi_dbm;
    int last_snr_tenths;
    uint8_t path_hash_bytes;
    uint8_t path_hops;
    bool favorite;
    bool muted;
} d1l_contact_entry_v1_t;

typedef struct {
    uint32_t schema;
    uint32_t next_seq;
    uint32_t total_written;
    uint32_t dropped_oldest;
    uint32_t count;
    d1l_contact_entry_v1_t entries[D1L_CONTACT_STORE_CAPACITY];
} d1l_contact_store_blob_v1_t;

typedef struct {
    uint32_t seq;
    uint32_t created_ms;
    uint32_t updated_ms;
    char fingerprint[D1L_NODE_FINGERPRINT_LEN];
    char public_key_hex[D1L_NODE_PUBLIC_KEY_HEX_LEN];
    char alias[D1L_CONTACT_ALIAS_LEN];
    char heard_name[D1L_HEARD_NODE_NAME_LEN];
    char type[D1L_NODE_TYPE_LEN];
    int last_rssi_dbm;
    int last_snr_tenths;
    uint8_t path_hash_bytes;
    uint8_t path_hops;
    bool favorite;
    bool muted;
} d1l_contact_entry_v2_t;

typedef struct {
    uint32_t schema;
    uint32_t next_seq;
    uint32_t total_written;
    uint32_t dropped_oldest;
    uint32_t count;
    d1l_contact_entry_v2_t entries[D1L_CONTACT_STORE_CAPACITY];
} d1l_contact_store_blob_v2_t;

typedef struct {
    uint32_t schema;
    uint32_t next_seq;
    uint32_t total_written;
    uint32_t dropped_oldest;
    uint32_t count;
    d1l_contact_entry_t entries[D1L_CONTACT_STORE_CAPACITY];
} d1l_contact_store_blob_t;

static d1l_contact_entry_t s_entries[D1L_CONTACT_STORE_CAPACITY];
static size_t s_count;
static uint32_t s_next_seq = 1;
static uint32_t s_total_written;
static uint32_t s_dropped_oldest;
static bool s_loaded;
static d1l_contact_store_blob_t s_blob_scratch;

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
    s_count = 0;
    s_next_seq = 1;
    s_total_written = 0;
    s_dropped_oldest = 0;
}

static void fill_blob(d1l_contact_store_blob_t *blob)
{
    memset(blob, 0, sizeof(*blob));
    blob->schema = D1L_CONTACT_STORE_SCHEMA;
    blob->next_seq = s_next_seq;
    blob->total_written = s_total_written;
    blob->dropped_oldest = s_dropped_oldest;
    blob->count = (uint32_t)s_count;
    memcpy(blob->entries, s_entries, sizeof(s_entries));
}

static esp_err_t persist_store(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(D1L_CONTACT_STORE_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    fill_blob(&s_blob_scratch);
    ret = nvs_set_blob(handle, D1L_CONTACT_STORE_KEY, &s_blob_scratch, sizeof(s_blob_scratch));
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

static bool blob_is_valid(const d1l_contact_store_blob_t *blob, size_t len)
{
    return blob && len == sizeof(*blob) &&
           blob->schema == D1L_CONTACT_STORE_SCHEMA &&
           blob->count <= D1L_CONTACT_STORE_CAPACITY &&
           blob->next_seq > 0;
}

static bool blob_v1_is_valid(const d1l_contact_store_blob_v1_t *blob, size_t len)
{
    return blob && len == sizeof(*blob) &&
           blob->schema == D1L_CONTACT_STORE_SCHEMA_V1 &&
           blob->count <= D1L_CONTACT_STORE_CAPACITY &&
           blob->next_seq > 0;
}

static bool blob_v2_is_valid(const d1l_contact_store_blob_v2_t *blob, size_t len)
{
    return blob && len == sizeof(*blob) &&
           blob->schema == D1L_CONTACT_STORE_SCHEMA_V2 &&
           blob->count <= D1L_CONTACT_STORE_CAPACITY &&
           blob->next_seq > 0;
}

static void migrate_v1_blob(const d1l_contact_store_blob_v1_t *old_blob)
{
    clear_ram();
    s_count = old_blob->count;
    s_next_seq = old_blob->next_seq;
    s_total_written = old_blob->total_written;
    s_dropped_oldest = old_blob->dropped_oldest;
    for (size_t i = 0; i < s_count; ++i) {
        s_entries[i].seq = old_blob->entries[i].seq;
        s_entries[i].created_ms = old_blob->entries[i].created_ms;
        s_entries[i].updated_ms = old_blob->entries[i].updated_ms;
        memcpy(s_entries[i].fingerprint, old_blob->entries[i].fingerprint,
               sizeof(s_entries[i].fingerprint));
        memcpy(s_entries[i].alias, old_blob->entries[i].alias, sizeof(s_entries[i].alias));
        memcpy(s_entries[i].heard_name, old_blob->entries[i].heard_name,
               sizeof(s_entries[i].heard_name));
        memcpy(s_entries[i].type, old_blob->entries[i].type, sizeof(s_entries[i].type));
        s_entries[i].last_rssi_dbm = old_blob->entries[i].last_rssi_dbm;
        s_entries[i].last_snr_tenths = old_blob->entries[i].last_snr_tenths;
        s_entries[i].path_hash_bytes = old_blob->entries[i].path_hash_bytes;
        s_entries[i].path_hops = old_blob->entries[i].path_hops;
        s_entries[i].favorite = old_blob->entries[i].favorite;
        s_entries[i].muted = old_blob->entries[i].muted;
    }
}

static void migrate_v2_blob(const d1l_contact_store_blob_v2_t *old_blob)
{
    clear_ram();
    s_count = old_blob->count;
    s_next_seq = old_blob->next_seq;
    s_total_written = old_blob->total_written;
    s_dropped_oldest = old_blob->dropped_oldest;
    for (size_t i = 0; i < s_count; ++i) {
        s_entries[i].seq = old_blob->entries[i].seq;
        s_entries[i].created_ms = old_blob->entries[i].created_ms;
        s_entries[i].updated_ms = old_blob->entries[i].updated_ms;
        memcpy(s_entries[i].fingerprint, old_blob->entries[i].fingerprint,
               sizeof(s_entries[i].fingerprint));
        memcpy(s_entries[i].public_key_hex, old_blob->entries[i].public_key_hex,
               sizeof(s_entries[i].public_key_hex));
        memcpy(s_entries[i].alias, old_blob->entries[i].alias, sizeof(s_entries[i].alias));
        memcpy(s_entries[i].heard_name, old_blob->entries[i].heard_name,
               sizeof(s_entries[i].heard_name));
        memcpy(s_entries[i].type, old_blob->entries[i].type, sizeof(s_entries[i].type));
        s_entries[i].last_rssi_dbm = old_blob->entries[i].last_rssi_dbm;
        s_entries[i].last_snr_tenths = old_blob->entries[i].last_snr_tenths;
        s_entries[i].path_hash_bytes = old_blob->entries[i].path_hash_bytes;
        s_entries[i].path_hops = old_blob->entries[i].path_hops;
        s_entries[i].favorite = old_blob->entries[i].favorite;
        s_entries[i].muted = old_blob->entries[i].muted;
    }
}

static int find_index_by_fingerprint(const char *fingerprint)
{
    if (!fingerprint || fingerprint[0] == '\0') {
        return -1;
    }
    for (size_t i = 0; i < s_count; ++i) {
        if (strncmp(s_entries[i].fingerprint, fingerprint, sizeof(s_entries[i].fingerprint)) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static bool contact_path_len_valid(uint8_t path_len)
{
    const uint8_t hash_count = path_len & 63U;
    const uint8_t hash_size = (uint8_t)((path_len >> 6) + 1U);
    return hash_size < 4U && (hash_count * hash_size) <= D1L_CONTACT_OUT_PATH_MAX;
}

static bool is_hex_char(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

static size_t url_encode_component(char *dest, size_t dest_size, const char *src)
{
    static const char hex[] = "0123456789ABCDEF";
    if (!dest || dest_size == 0) {
        return 0;
    }
    size_t out = 0;
    while (src && *src) {
        const unsigned char c = (unsigned char)*src++;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') {
            if (out + 1U >= dest_size) {
                break;
            }
            dest[out++] = (char)c;
        } else if (c == ' ') {
            if (out + 1U >= dest_size) {
                break;
            }
            dest[out++] = '+';
        } else {
            if (out + 3U >= dest_size) {
                break;
            }
            dest[out++] = '%';
            dest[out++] = hex[(c >> 4) & 0x0fU];
            dest[out++] = hex[c & 0x0fU];
        }
    }
    dest[out] = '\0';
    return out;
}

static uint8_t contact_path_byte_len(uint8_t path_len)
{
    const uint8_t hash_count = path_len & 63U;
    const uint8_t hash_size = (uint8_t)((path_len >> 6) + 1U);
    return (uint8_t)(hash_count * hash_size);
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

esp_err_t d1l_contact_store_init(void)
{
    clear_ram();
    bool migrated = false;

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(D1L_CONTACT_STORE_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        s_loaded = false;
        return ret;
    }

    size_t len = sizeof(s_blob_scratch);
    ret = nvs_get_blob(handle, D1L_CONTACT_STORE_KEY, &s_blob_scratch, &len);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ret = ESP_OK;
    } else if (ret == ESP_OK && blob_is_valid(&s_blob_scratch, len)) {
        memcpy(s_entries, s_blob_scratch.entries, sizeof(s_entries));
        s_count = s_blob_scratch.count;
        s_next_seq = s_blob_scratch.next_seq;
        s_total_written = s_blob_scratch.total_written;
        s_dropped_oldest = s_blob_scratch.dropped_oldest;
    } else if (ret == ESP_OK &&
               blob_v1_is_valid((const d1l_contact_store_blob_v1_t *)&s_blob_scratch, len)) {
        migrate_v1_blob((const d1l_contact_store_blob_v1_t *)&s_blob_scratch);
        migrated = true;
    } else if (ret == ESP_OK &&
               blob_v2_is_valid((const d1l_contact_store_blob_v2_t *)&s_blob_scratch, len)) {
        migrate_v2_blob((const d1l_contact_store_blob_v2_t *)&s_blob_scratch);
        migrated = true;
    } else if (ret == ESP_OK) {
        clear_ram();
        ret = nvs_erase_key(handle, D1L_CONTACT_STORE_KEY);
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            ret = ESP_OK;
        }
        if (ret == ESP_OK) {
            ret = nvs_commit(handle);
        }
    }
    nvs_close(handle);
    if (ret == ESP_OK && migrated) {
        ret = persist_store();
    }
    s_loaded = (ret == ESP_OK);
    return ret;
}

esp_err_t d1l_contact_store_clear(void)
{
    clear_ram();
    s_loaded = true;

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(D1L_CONTACT_STORE_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = nvs_erase_key(handle, D1L_CONTACT_STORE_KEY);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ret = ESP_OK;
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

esp_err_t d1l_contact_store_upsert_from_node(const char *fingerprint, const char *alias,
                                             const d1l_node_entry_t *heard_node)
{
    if (!fingerprint || fingerprint[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_loaded) {
        esp_err_t ret = d1l_contact_store_init();
        if (ret != ESP_OK) {
            return ret;
        }
    }

    int existing = find_index_by_fingerprint(fingerprint);
    size_t index;
    bool is_new = existing < 0;
    if (!is_new) {
        index = (size_t)existing;
    } else if (s_count < D1L_CONTACT_STORE_CAPACITY) {
        index = s_count++;
    } else {
        index = oldest_index();
        s_dropped_oldest++;
    }

    d1l_contact_entry_t *entry = &s_entries[index];
    const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    if (is_new) {
        memset(entry, 0, sizeof(*entry));
        entry->created_ms = now_ms;
    }
    entry->seq = s_next_seq++;
    entry->updated_ms = now_ms;
    sanitize_ascii(entry->fingerprint, sizeof(entry->fingerprint), fingerprint);

    if (heard_node) {
        if (heard_node->public_key_hex[0] != '\0') {
            sanitize_ascii(entry->public_key_hex, sizeof(entry->public_key_hex),
                           heard_node->public_key_hex);
        }
        if (heard_node->name[0] != '\0') {
            sanitize_ascii(entry->heard_name, sizeof(entry->heard_name), heard_node->name);
        }
        if (heard_node->type[0] != '\0') {
            sanitize_ascii(entry->type, sizeof(entry->type), heard_node->type);
        }
        entry->last_rssi_dbm = heard_node->rssi_dbm;
        entry->last_snr_tenths = heard_node->snr_tenths;
        entry->path_hash_bytes = heard_node->path_hash_bytes;
        entry->path_hops = heard_node->path_hops;
    }

    if (alias && alias[0] != '\0') {
        sanitize_ascii(entry->alias, sizeof(entry->alias), alias);
    } else if (is_new || entry->alias[0] == '\0') {
        if (heard_node && heard_node->name[0] != '\0') {
            sanitize_ascii(entry->alias, sizeof(entry->alias), heard_node->name);
        } else {
            sanitize_ascii(entry->alias, sizeof(entry->alias), fingerprint);
        }
    }

    if (entry->heard_name[0] == '\0') {
        sanitize_ascii(entry->heard_name, sizeof(entry->heard_name), entry->alias);
    }
    if (entry->type[0] == '\0') {
        sanitize_ascii(entry->type, sizeof(entry->type), "node");
    }

    s_total_written++;
    return persist_store();
}

esp_err_t d1l_contact_store_update_path(const char *fingerprint, const uint8_t *path,
                                        uint8_t path_len)
{
    if (!fingerprint || fingerprint[0] == '\0' || !contact_path_len_valid(path_len)) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint8_t bytes = contact_path_byte_len(path_len);
    if (bytes > 0 && path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_loaded) {
        esp_err_t ret = d1l_contact_store_init();
        if (ret != ESP_OK) {
            return ret;
        }
    }

    int existing = find_index_by_fingerprint(fingerprint);
    if (existing < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    d1l_contact_entry_t *entry = &s_entries[(size_t)existing];
    const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    entry->seq = s_next_seq++;
    entry->updated_ms = now_ms;
    entry->out_path_updated_ms = now_ms;
    entry->out_path_valid = true;
    entry->out_path_len = path_len;
    memset(entry->out_path, 0, sizeof(entry->out_path));
    if (bytes > 0) {
        memcpy(entry->out_path, path, bytes);
    }
    s_total_written++;
    return persist_store();
}

esp_err_t d1l_contact_store_set_flags(const char *fingerprint, bool favorite, bool muted,
                                      d1l_contact_entry_t *out_entry)
{
    if (!fingerprint || fingerprint[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_loaded) {
        esp_err_t ret = d1l_contact_store_init();
        if (ret != ESP_OK) {
            return ret;
        }
    }

    int existing = find_index_by_fingerprint(fingerprint);
    if (existing < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    d1l_contact_entry_t *entry = &s_entries[(size_t)existing];
    if (entry->favorite != favorite || entry->muted != muted) {
        const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        entry->seq = s_next_seq++;
        entry->updated_ms = now_ms;
        entry->favorite = favorite;
        entry->muted = muted;
        s_total_written++;
        esp_err_t ret = persist_store();
        if (ret != ESP_OK) {
            return ret;
        }
    }
    if (out_entry) {
        *out_entry = *entry;
    }
    return ESP_OK;
}

esp_err_t d1l_contact_store_rename(const char *fingerprint, const char *alias,
                                   d1l_contact_entry_t *out_entry)
{
    if (!fingerprint || fingerprint[0] == '\0' || !alias || alias[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_loaded) {
        esp_err_t ret = d1l_contact_store_init();
        if (ret != ESP_OK) {
            return ret;
        }
    }

    int existing = find_index_by_fingerprint(fingerprint);
    if (existing < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    d1l_contact_entry_t *entry = &s_entries[(size_t)existing];
    char sanitized[D1L_CONTACT_ALIAS_LEN] = {0};
    sanitize_ascii(sanitized, sizeof(sanitized), alias);
    if (sanitized[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (strncmp(entry->alias, sanitized, sizeof(entry->alias)) != 0) {
        const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        entry->seq = s_next_seq++;
        entry->updated_ms = now_ms;
        snprintf(entry->alias, sizeof(entry->alias), "%s", sanitized);
        s_total_written++;
        esp_err_t ret = persist_store();
        if (ret != ESP_OK) {
            return ret;
        }
    }
    if (out_entry) {
        *out_entry = *entry;
    }
    return ESP_OK;
}

esp_err_t d1l_contact_store_delete(const char *fingerprint, d1l_contact_entry_t *out_entry)
{
    if (!fingerprint || fingerprint[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_loaded) {
        esp_err_t ret = d1l_contact_store_init();
        if (ret != ESP_OK) {
            return ret;
        }
    }

    int existing = find_index_by_fingerprint(fingerprint);
    if (existing < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    const size_t index = (size_t)existing;
    if (out_entry) {
        *out_entry = s_entries[index];
    }
    if (index + 1U < s_count) {
        memmove(&s_entries[index], &s_entries[index + 1U],
                (s_count - index - 1U) * sizeof(s_entries[0]));
    }
    s_count--;
    memset(&s_entries[s_count], 0, sizeof(s_entries[s_count]));
    s_total_written++;
    return persist_store();
}

uint8_t d1l_contact_store_meshcore_type_id(const char *type)
{
    if (!type) {
        return 1U;
    }
    if (strcmp(type, "repeater") == 0) {
        return 2U;
    }
    if (strcmp(type, "room") == 0) {
        return 3U;
    }
    if (strcmp(type, "sensor") == 0) {
        return 4U;
    }
    return 1U;
}

bool d1l_contact_store_has_export_key(const d1l_contact_entry_t *entry)
{
    if (!entry) {
        return false;
    }
    for (size_t i = 0; i < D1L_NODE_PUBLIC_KEY_HEX_LEN - 1U; ++i) {
        if (!is_hex_char(entry->public_key_hex[i])) {
            return false;
        }
    }
    return entry->public_key_hex[D1L_NODE_PUBLIC_KEY_HEX_LEN - 1U] == '\0';
}

esp_err_t d1l_contact_store_export_uri(const d1l_contact_entry_t *entry, char *dest,
                                       size_t dest_size)
{
    if (!entry || !dest || dest_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    dest[0] = '\0';
    if (!d1l_contact_store_has_export_key(entry)) {
        return ESP_ERR_INVALID_STATE;
    }

    const char *name = entry->alias[0] ? entry->alias :
                       (entry->heard_name[0] ? entry->heard_name : entry->fingerprint);
    char encoded_name[D1L_CONTACT_ALIAS_LEN * 3U] = {0};
    url_encode_component(encoded_name, sizeof(encoded_name), name);
    if (encoded_name[0] == '\0') {
        url_encode_component(encoded_name, sizeof(encoded_name), entry->fingerprint);
    }

    int written = snprintf(dest, dest_size,
                           "meshcore://contact/add?name=%s&public_key=%s&type=%u",
                           encoded_name, entry->public_key_hex,
                           (unsigned)d1l_contact_store_meshcore_type_id(entry->type));
    if (written < 0 || (size_t)written >= dest_size) {
        dest[0] = '\0';
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

d1l_contact_store_stats_t d1l_contact_store_stats(void)
{
    d1l_contact_store_stats_t stats = {
        .next_seq = s_next_seq,
        .total_written = s_total_written,
        .dropped_oldest = s_dropped_oldest,
        .count = s_count,
        .capacity = D1L_CONTACT_STORE_CAPACITY,
    };
    return stats;
}

bool d1l_contact_store_find_by_fingerprint(const char *fingerprint, d1l_contact_entry_t *out_entry)
{
    if (!s_loaded && d1l_contact_store_init() != ESP_OK) {
        return false;
    }
    int index = find_index_by_fingerprint(fingerprint);
    if (index < 0) {
        return false;
    }
    if (out_entry) {
        *out_entry = s_entries[index];
    }
    return true;
}

size_t d1l_contact_store_copy_recent(d1l_contact_entry_t *out_entries, size_t max_entries)
{
    if (out_entries == NULL || max_entries == 0 || s_count == 0) {
        return 0;
    }

    const size_t n = s_count < max_entries ? s_count : max_entries;
    bool used[D1L_CONTACT_STORE_CAPACITY] = {0};
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
    return n;
}
