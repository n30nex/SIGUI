#include "contact_store.h"

#include <string.h>

#include "esp_timer.h"
#include "nvs.h"

#define D1L_CONTACT_STORE_NAMESPACE "d1l_contacts"
#define D1L_CONTACT_STORE_KEY "contacts"
#define D1L_CONTACT_STORE_SCHEMA 1U

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
