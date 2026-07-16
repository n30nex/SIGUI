#include "node_store.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_timer.h"
#include "nvs.h"

#include "mesh/contact_store.h"
#include "mesh/store_lock.h"

#define D1L_NODE_STORE_NAMESPACE "d1l_nodes"
#define D1L_NODE_STORE_KEY "heard"
#define D1L_NODE_STORE_LEGACY_CAPACITY D1L_NODE_NVS_FALLBACK_CAPACITY
#define D1L_NODE_STORE_LEGACY_TYPE_LEN 8U
#define D1L_NODE_STORE_SCHEMA_V1 1U
#define D1L_NODE_STORE_SCHEMA_V2 2U
#define D1L_NODE_STORE_SCHEMA_V3 3U
#define D1L_NODE_STORE_SCHEMA 4U

typedef struct {
    uint32_t seq;
    uint32_t first_heard_ms;
    uint32_t last_heard_ms;
    uint32_t advert_timestamp;
    uint32_t heard_count;
    char fingerprint[D1L_NODE_FINGERPRINT_LEN];
    char name[D1L_HEARD_NODE_NAME_LEN];
    char type[D1L_NODE_STORE_LEGACY_TYPE_LEN];
    int rssi_dbm;
    int snr_tenths;
    uint8_t path_hash_bytes;
    uint8_t path_hops;
} d1l_node_entry_v1_t;

/* Schema v2/v3 entries predate signed-advert locations. Keep this layout exact. */
typedef struct {
    uint32_t seq;
    uint32_t first_heard_ms;
    uint32_t last_heard_ms;
    uint32_t advert_timestamp;
    uint32_t heard_count;
    char fingerprint[D1L_NODE_FINGERPRINT_LEN];
    char public_key_hex[D1L_NODE_PUBLIC_KEY_HEX_LEN];
    char name[D1L_HEARD_NODE_NAME_LEN];
    char type[D1L_NODE_STORE_LEGACY_TYPE_LEN];
    int rssi_dbm;
    int snr_tenths;
    uint8_t path_hash_bytes;
    uint8_t path_hops;
} d1l_node_entry_v3_t;

typedef struct {
    uint32_t schema;
    uint32_t next_seq;
    uint32_t total_written;
    uint32_t dropped_oldest;
    uint32_t count;
    d1l_node_entry_v1_t entries[D1L_NODE_STORE_LEGACY_CAPACITY];
} d1l_node_store_blob_v1_t;

typedef struct {
    uint32_t schema;
    uint32_t next_seq;
    uint32_t total_written;
    uint32_t dropped_oldest;
    uint32_t count;
    d1l_node_entry_v3_t entries[D1L_NODE_STORE_LEGACY_CAPACITY];
} d1l_node_store_blob_v2_t;

typedef struct {
    uint32_t schema;
    uint32_t next_seq;
    uint32_t total_written;
    uint32_t dropped_oldest;
    uint32_t count;
    d1l_node_entry_v3_t entries[D1L_NODE_STORE_LEGACY_CAPACITY];
} d1l_node_store_blob_v3_t;

_Static_assert(sizeof(d1l_node_entry_v1_t) == 84U,
               "node schema v1 layout changed");
_Static_assert(sizeof(d1l_node_entry_v3_t) == 148U,
               "node schema v2/v3 layout changed");
_Static_assert(offsetof(d1l_node_entry_v1_t, rssi_dbm) == 72U,
               "node schema v1 type offset changed");
_Static_assert(offsetof(d1l_node_entry_v3_t, rssi_dbm) == 136U,
               "node schema v2/v3 type offset changed");

typedef struct {
    uint32_t schema;
    uint32_t next_seq;
    uint32_t total_written;
    uint32_t dropped_oldest;
    uint32_t count;
    d1l_node_entry_t entries[D1L_NODE_NVS_FALLBACK_CAPACITY];
} d1l_node_store_blob_t;

static d1l_node_entry_t s_entries[D1L_NODE_STORE_CAPACITY] EXT_RAM_BSS_ATTR;
static size_t s_count;
static uint32_t s_next_seq = 1;
static uint32_t s_total_written;
static uint32_t s_dropped_oldest;
static uint32_t s_marker_generation = 1U;
static bool s_loaded;
static d1l_node_store_blob_t s_blob_scratch;
static d1l_node_view_t s_query_scratch[D1L_NODE_STORE_CAPACITY] EXT_RAM_BSS_ATTR;
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

static char lower_hex_char(char value)
{
    if (value >= 'A' && value <= 'F') {
        return (char)(value + ('a' - 'A'));
    }
    return value;
}

static bool public_keys_equal(const char *left, const char *right)
{
    if (!left || !right) {
        return false;
    }
    for (size_t i = 0U; i < D1L_NODE_PUBLIC_KEY_HEX_LEN - 1U; ++i) {
        if (left[i] == '\0' || right[i] == '\0' ||
            lower_hex_char(left[i]) != lower_hex_char(right[i])) {
            return false;
        }
    }
    return left[D1L_NODE_PUBLIC_KEY_HEX_LEN - 1U] == '\0' &&
           right[D1L_NODE_PUBLIC_KEY_HEX_LEN - 1U] == '\0';
}

static void migrate_legacy_advert_type(
    char dest[D1L_NODE_TYPE_LEN],
    const char legacy_type[D1L_NODE_STORE_LEGACY_TYPE_LEN])
{
    char legacy[D1L_NODE_STORE_LEGACY_TYPE_LEN + 1U] = {0};
    memcpy(legacy, legacy_type, D1L_NODE_STORE_LEGACY_TYPE_LEN);
    const char *canonical = "unknown";
    if (strcmp(legacy, "chat") == 0) {
        canonical = "chat";
    } else if (strcmp(legacy, "room") == 0 ||
               strcmp(legacy, "repeater") == 0) {
        /* The legacy advert decoder mapped upstream type 2 to room. */
        canonical = "repeater";
    }
    /* Legacy sensor is ambiguous: the old decoder used it for upstream room
     * type 3 and sensor type 4. Keep it unknown until a fresh signed advert. */
    snprintf(dest, D1L_NODE_TYPE_LEN, "%s", canonical);
}

static const char *type_name(char type_code)
{
    switch (type_code) {
    case 'C':
        return "chat";
    case 'P':
        return "repeater";
    case 'R':
        return "room";
    case 'S':
        return "sensor";
    default:
        return "node";
    }
}

static void clear_ram(void)
{
    memset(s_entries, 0, sizeof(s_entries));
    s_count = 0;
    s_next_seq = 1;
    s_total_written = 0;
    s_dropped_oldest = 0;
}

static void bump_marker_generation(void)
{
    s_marker_generation++;
    if (s_marker_generation == 0U) {
        s_marker_generation = 1U;
    }
}

static bool location_in_bounds(int32_t lat_e6, int32_t lon_e6)
{
    return lat_e6 >= -90000000 && lat_e6 <= 90000000 &&
           lon_e6 >= -180000000 && lon_e6 <= 180000000;
}

static bool marker_material_changed(const d1l_node_entry_t *before,
                                    const d1l_node_entry_t *after)
{
    const bool before_valid = before && before->location_valid;
    const bool after_valid = after && after->location_valid;
    if (before_valid != after_valid) {
        return true;
    }
    if (!before_valid) {
        return false;
    }
    return before->lat_e6 != after->lat_e6 || before->lon_e6 != after->lon_e6 ||
           before->location_seq != after->location_seq ||
           strncmp(before->fingerprint, after->fingerprint, D1L_NODE_FINGERPRINT_LEN) != 0 ||
           strncmp(before->name, after->name, D1L_HEARD_NODE_NAME_LEN) != 0 ||
           strncmp(before->type, after->type, D1L_NODE_TYPE_LEN) != 0;
}

static void fill_blob(d1l_node_store_blob_t *blob)
{
    memset(blob, 0, sizeof(*blob));
    blob->schema = D1L_NODE_STORE_SCHEMA;
    blob->next_seq = s_next_seq;
    blob->total_written = s_total_written;
    blob->dropped_oldest = s_dropped_oldest;
    const size_t n = s_count < D1L_NODE_NVS_FALLBACK_CAPACITY ?
        s_count : D1L_NODE_NVS_FALLBACK_CAPACITY;
    blob->count = (uint32_t)n;
    bool used[D1L_NODE_STORE_CAPACITY] = {0};
    for (size_t out = 0; out < n; ++out) {
        size_t best = 0;
        bool best_set = false;
        for (size_t i = 0; i < s_count; ++i) {
            if (!used[i] && (!best_set || s_entries[i].seq > s_entries[best].seq)) {
                best = i;
                best_set = true;
            }
        }
        if (!best_set) {
            break;
        }
        used[best] = true;
        blob->entries[out] = s_entries[best];
    }
}

static esp_err_t persist_store(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(D1L_NODE_STORE_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    fill_blob(&s_blob_scratch);
    ret = nvs_set_blob(handle, D1L_NODE_STORE_KEY, &s_blob_scratch, sizeof(s_blob_scratch));
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

static bool blob_is_valid(const d1l_node_store_blob_t *blob, size_t len)
{
    return blob && len == sizeof(*blob) &&
           blob->schema == D1L_NODE_STORE_SCHEMA &&
           blob->count <= D1L_NODE_NVS_FALLBACK_CAPACITY &&
           blob->next_seq > 0;
}

static bool blob_v1_is_valid(const d1l_node_store_blob_v1_t *blob, size_t len)
{
    return blob && len == sizeof(*blob) &&
           blob->schema == D1L_NODE_STORE_SCHEMA_V1 &&
           blob->count <= D1L_NODE_STORE_LEGACY_CAPACITY &&
           blob->next_seq > 0;
}

static bool blob_v2_is_valid(const d1l_node_store_blob_v2_t *blob, size_t len)
{
    return blob && len == sizeof(*blob) &&
           blob->schema == D1L_NODE_STORE_SCHEMA_V2 &&
           blob->count <= D1L_NODE_STORE_LEGACY_CAPACITY &&
           blob->next_seq > 0;
}

static bool blob_v3_is_valid(const d1l_node_store_blob_v3_t *blob, size_t len)
{
    return blob && len == sizeof(*blob) &&
           blob->schema == D1L_NODE_STORE_SCHEMA_V3 &&
           blob->count <= D1L_NODE_STORE_LEGACY_CAPACITY &&
           blob->next_seq > 0;
}

static void migrate_v1_blob(const d1l_node_store_blob_v1_t *old_blob)
{
    clear_ram();
    s_count = old_blob->count;
    s_next_seq = old_blob->next_seq;
    s_total_written = old_blob->total_written;
    s_dropped_oldest = old_blob->dropped_oldest;
    for (size_t i = 0; i < s_count; ++i) {
        s_entries[i].seq = old_blob->entries[i].seq;
        s_entries[i].first_heard_ms = old_blob->entries[i].first_heard_ms;
        s_entries[i].last_heard_ms = old_blob->entries[i].last_heard_ms;
        s_entries[i].advert_timestamp = old_blob->entries[i].advert_timestamp;
        s_entries[i].heard_count = old_blob->entries[i].heard_count;
        memcpy(s_entries[i].fingerprint, old_blob->entries[i].fingerprint,
               sizeof(s_entries[i].fingerprint));
        memcpy(s_entries[i].name, old_blob->entries[i].name, sizeof(s_entries[i].name));
        migrate_legacy_advert_type(s_entries[i].type, old_blob->entries[i].type);
        s_entries[i].rssi_dbm = old_blob->entries[i].rssi_dbm;
        s_entries[i].snr_tenths = old_blob->entries[i].snr_tenths;
        s_entries[i].path_hash_bytes = old_blob->entries[i].path_hash_bytes;
        s_entries[i].path_hops = old_blob->entries[i].path_hops;
    }
}

static void migrate_v3_entries(const d1l_node_entry_v3_t *old_entries, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        s_entries[i].seq = old_entries[i].seq;
        s_entries[i].first_heard_ms = old_entries[i].first_heard_ms;
        s_entries[i].last_heard_ms = old_entries[i].last_heard_ms;
        s_entries[i].advert_timestamp = old_entries[i].advert_timestamp;
        s_entries[i].heard_count = old_entries[i].heard_count;
        memcpy(s_entries[i].fingerprint, old_entries[i].fingerprint,
               sizeof(s_entries[i].fingerprint));
        memcpy(s_entries[i].public_key_hex, old_entries[i].public_key_hex,
               sizeof(s_entries[i].public_key_hex));
        memcpy(s_entries[i].name, old_entries[i].name, sizeof(s_entries[i].name));
        migrate_legacy_advert_type(s_entries[i].type, old_entries[i].type);
        s_entries[i].rssi_dbm = old_entries[i].rssi_dbm;
        s_entries[i].snr_tenths = old_entries[i].snr_tenths;
        s_entries[i].path_hash_bytes = old_entries[i].path_hash_bytes;
        s_entries[i].path_hops = old_entries[i].path_hops;
    }
}

static void migrate_v2_blob(const d1l_node_store_blob_v2_t *old_blob)
{
    clear_ram();
    s_count = old_blob->count;
    s_next_seq = old_blob->next_seq;
    s_total_written = old_blob->total_written;
    s_dropped_oldest = old_blob->dropped_oldest;
    migrate_v3_entries(old_blob->entries, s_count);
}

static void migrate_v3_blob(const d1l_node_store_blob_v3_t *old_blob)
{
    clear_ram();
    s_count = old_blob->count;
    s_next_seq = old_blob->next_seq;
    s_total_written = old_blob->total_written;
    s_dropped_oldest = old_blob->dropped_oldest;
    migrate_v3_entries(old_blob->entries, s_count);
}

static int find_by_fingerprint(const char *fingerprint)
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

static char ascii_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

static int ascii_casecmp(const char *a, const char *b)
{
    while ((a && *a) || (b && *b)) {
        const char ca = ascii_lower(a && *a ? *a++ : '\0');
        const char cb = ascii_lower(b && *b ? *b++ : '\0');
        if (ca != cb) {
            return (int)(unsigned char)ca - (int)(unsigned char)cb;
        }
    }
    return 0;
}

static bool contains_casefold(const char *haystack, const char *needle)
{
    if (!needle || needle[0] == '\0') {
        return true;
    }
    if (!haystack) {
        return false;
    }
    for (size_t i = 0; haystack[i] != '\0'; ++i) {
        size_t h = i;
        size_t n = 0;
        while (haystack[h] != '\0' && needle[n] != '\0' &&
               ascii_lower(haystack[h]) == ascii_lower(needle[n])) {
            h++;
            n++;
        }
        if (needle[n] == '\0') {
            return true;
        }
    }
    return false;
}

static bool node_has_key(const d1l_node_entry_t *node, const d1l_contact_entry_t *contact)
{
    return (node && node->public_key_hex[0] != '\0') ||
           (contact && contact->public_key_hex[0] != '\0');
}

static const char *node_role_name(const d1l_node_entry_t *node)
{
    if (!node) {
        return "unknown";
    }
    if (strcmp(node->type, "room") == 0) {
        return "room";
    }
    if (strcmp(node->type, "sensor") == 0) {
        return "sensor";
    }
    if (strcmp(node->type, "repeater") == 0) {
        return "repeater";
    }
    if (strcmp(node->type, "chat") == 0) {
        return "companion";
    }
    return "unknown";
}

static uint8_t node_role_order(const char *role)
{
    if (!role) {
        return 5U;
    }
    if (strcmp(role, "companion") == 0) {
        return 0U;
    }
    if (strcmp(role, "repeater") == 0) {
        return 1U;
    }
    if (strcmp(role, "room") == 0) {
        return 2U;
    }
    if (strcmp(role, "sensor") == 0) {
        return 3U;
    }
    return 4U;
}

static void build_node_view(const d1l_node_entry_t *node, d1l_node_view_t *view)
{
    if (!node || !view) {
        return;
    }
    memset(view, 0, sizeof(*view));
    view->node = *node;

    d1l_contact_entry_t contact = {0};
    const bool has_contact =
        d1l_contact_store_find_by_fingerprint(node->fingerprint, &contact);
    view->favorite = has_contact && contact.favorite;
    view->muted = has_contact && contact.muted;
    view->keyed = node_has_key(node, has_contact ? &contact : NULL);
    view->reachable = node->last_heard_ms != 0;
    sanitize_ascii(view->role, sizeof(view->role), node_role_name(node));
    if (has_contact && contact.alias[0] != '\0') {
        sanitize_ascii(view->display_name, sizeof(view->display_name), contact.alias);
    } else if (node->name[0] != '\0') {
        sanitize_ascii(view->display_name, sizeof(view->display_name), node->name);
    } else {
        sanitize_ascii(view->display_name, sizeof(view->display_name), node->fingerprint);
    }
}

static bool node_view_matches_filter(const d1l_node_view_t *view, d1l_node_filter_t filter)
{
    if (!view) {
        return false;
    }
    switch (filter) {
    case D1L_NODE_FILTER_ALL:
        return true;
    case D1L_NODE_FILTER_COMPANION:
        return strcmp(view->role, "companion") == 0;
    case D1L_NODE_FILTER_REPEATER:
        return strcmp(view->role, "repeater") == 0;
    case D1L_NODE_FILTER_ROOM:
        return strcmp(view->role, "room") == 0;
    case D1L_NODE_FILTER_SENSOR:
        return strcmp(view->role, "sensor") == 0;
    case D1L_NODE_FILTER_FAVORITE:
        return view->favorite;
    default:
        return true;
    }
}

static bool node_view_matches_query(const d1l_node_view_t *view, const d1l_node_query_t *query)
{
    if (!view) {
        return false;
    }
    if (query) {
        if (!node_view_matches_filter(view, query->filter)) {
            return false;
        }
        if (query->keyed_only && !view->keyed) {
            return false;
        }
        if (query->reachable_only && !view->reachable) {
            return false;
        }
        if (query->text && query->text[0] != '\0' &&
            !contains_casefold(view->display_name, query->text) &&
            !contains_casefold(view->role, query->text) &&
            !contains_casefold(view->node.fingerprint, query->text) &&
            !contains_casefold(view->node.type, query->text) &&
            !contains_casefold(view->node.public_key_hex, query->text)) {
            return false;
        }
    }
    return true;
}

static bool node_view_better(const d1l_node_view_t *candidate, const d1l_node_view_t *best,
                             d1l_node_sort_t sort)
{
    if (!best) {
        return true;
    }
    switch (sort) {
    case D1L_NODE_SORT_SIGNAL:
        if (candidate->node.rssi_dbm != best->node.rssi_dbm) {
            return candidate->node.rssi_dbm > best->node.rssi_dbm;
        }
        if (candidate->node.snr_tenths != best->node.snr_tenths) {
            return candidate->node.snr_tenths > best->node.snr_tenths;
        }
        break;
    case D1L_NODE_SORT_NAME: {
        int cmp = ascii_casecmp(candidate->display_name, best->display_name);
        if (cmp != 0) {
            return cmp < 0;
        }
        break;
    }
    case D1L_NODE_SORT_ROLE: {
        uint8_t candidate_role = node_role_order(candidate->role);
        uint8_t best_role = node_role_order(best->role);
        if (candidate_role != best_role) {
            return candidate_role < best_role;
        }
        int cmp = ascii_casecmp(candidate->display_name, best->display_name);
        if (cmp != 0) {
            return cmp < 0;
        }
        break;
    }
    case D1L_NODE_SORT_FAVORITE:
        if (candidate->favorite != best->favorite) {
            return candidate->favorite;
        }
        break;
    case D1L_NODE_SORT_LAST_HEARD:
    default:
        break;
    }
    if (candidate->node.last_heard_ms != best->node.last_heard_ms) {
        return candidate->node.last_heard_ms > best->node.last_heard_ms;
    }
    return candidate->node.seq > best->node.seq;
}

esp_err_t d1l_node_store_init(void)
{
    s_marker_generation = 1U;
    clear_ram();
    bool migrated = false;

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(D1L_NODE_STORE_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        s_loaded = false;
        return ret;
    }

    size_t len = sizeof(s_blob_scratch);
    ret = nvs_get_blob(handle, D1L_NODE_STORE_KEY, &s_blob_scratch, &len);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ret = ESP_OK;
    } else if (ret == ESP_OK && blob_is_valid(&s_blob_scratch, len)) {
        memcpy(s_entries, s_blob_scratch.entries,
               s_blob_scratch.count * sizeof(s_entries[0]));
        s_count = s_blob_scratch.count;
        s_next_seq = s_blob_scratch.next_seq;
        s_total_written = s_blob_scratch.total_written;
        s_dropped_oldest = s_blob_scratch.dropped_oldest;
    } else if (ret == ESP_OK &&
               blob_v3_is_valid((const d1l_node_store_blob_v3_t *)&s_blob_scratch, len)) {
        migrate_v3_blob((const d1l_node_store_blob_v3_t *)&s_blob_scratch);
        migrated = true;
    } else if (ret == ESP_OK &&
               blob_v2_is_valid((const d1l_node_store_blob_v2_t *)&s_blob_scratch, len)) {
        migrate_v2_blob((const d1l_node_store_blob_v2_t *)&s_blob_scratch);
        migrated = true;
    } else if (ret == ESP_OK &&
               blob_v1_is_valid((const d1l_node_store_blob_v1_t *)&s_blob_scratch, len)) {
        migrate_v1_blob((const d1l_node_store_blob_v1_t *)&s_blob_scratch);
        migrated = true;
    } else if (ret == ESP_OK) {
        clear_ram();
        ret = nvs_erase_key(handle, D1L_NODE_STORE_KEY);
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

esp_err_t d1l_node_store_clear(void)
{
    d1l_store_lock_take(&s_store_lock);
    bool had_markers = false;
    for (size_t i = 0; i < s_count; ++i) {
        had_markers = had_markers || s_entries[i].location_valid;
    }
    clear_ram();
    if (had_markers) {
        bump_marker_generation();
    }
    s_loaded = true;

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(D1L_NODE_STORE_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        d1l_store_lock_give(&s_store_lock);
        return ret;
    }
    ret = nvs_erase_key(handle, D1L_NODE_STORE_KEY);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ret = ESP_OK;
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    d1l_store_lock_give(&s_store_lock);
    return ret;
}

esp_err_t d1l_node_store_upsert_advert(const char *fingerprint, const char *public_key_hex,
                                       const char *name, char type_code, int rssi_dbm,
                                       int snr_tenths, uint8_t path_hash_bytes,
                                       uint8_t path_hops, uint32_t advert_timestamp,
                                       bool location_valid, int32_t lat_e6, int32_t lon_e6,
                                       bool *out_stale)
{
    if (!out_stale || !fingerprint || fingerprint[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    *out_stale = false;
    if (location_valid && !location_in_bounds(lat_e6, lon_e6)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_loaded) {
        esp_err_t ret = d1l_node_store_init();
        if (ret != ESP_OK) {
            return ret;
        }
    }

    d1l_store_lock_take(&s_store_lock);
    const size_t count_before = s_count;
    const uint32_t next_seq_before = s_next_seq;
    const uint32_t total_written_before = s_total_written;
    const uint32_t dropped_oldest_before = s_dropped_oldest;
    const uint32_t marker_generation_before = s_marker_generation;
    int existing = find_by_fingerprint(fingerprint);
    if (existing >= 0 && public_key_hex && public_key_hex[0] != '\0' &&
        s_entries[existing].public_key_hex[0] != '\0' &&
        !public_keys_equal(s_entries[existing].public_key_hex, public_key_hex)) {
        d1l_store_lock_give(&s_store_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (existing >= 0 && advert_timestamp <= s_entries[existing].advert_timestamp) {
        d1l_store_lock_give(&s_store_lock);
        *out_stale = true;
        return ESP_OK;
    }
    size_t index;
    bool is_new = existing < 0;
    bool replacing_oldest = false;
    if (!is_new) {
        index = (size_t)existing;
    } else if (s_count < D1L_NODE_STORE_CAPACITY) {
        index = s_count++;
    } else {
        index = oldest_index();
        replacing_oldest = true;
        s_dropped_oldest++;
    }

    d1l_node_entry_t *entry = &s_entries[index];
    const d1l_node_entry_t entry_before = *entry;
    const d1l_node_entry_t marker_before =
        (!is_new || replacing_oldest) ? entry_before : (d1l_node_entry_t){0};
    const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    if (is_new) {
        memset(entry, 0, sizeof(*entry));
        entry->first_heard_ms = now_ms;
        entry->heard_count = 0;
    }
    entry->seq = s_next_seq++;
    entry->last_heard_ms = now_ms;
    entry->advert_timestamp = advert_timestamp;
    entry->heard_count++;
    entry->rssi_dbm = rssi_dbm;
    entry->snr_tenths = snr_tenths;
    entry->path_hash_bytes = path_hash_bytes;
    entry->path_hops = path_hops;
    sanitize_ascii(entry->fingerprint, sizeof(entry->fingerprint), fingerprint);
    if (public_key_hex && public_key_hex[0] != '\0') {
        sanitize_ascii(entry->public_key_hex, sizeof(entry->public_key_hex), public_key_hex);
    }
    if (name && name[0] != '\0') {
        sanitize_ascii(entry->name, sizeof(entry->name), name);
    }
    sanitize_ascii(entry->type, sizeof(entry->type), type_name(type_code));
    if (entry->name[0] == '\0') {
        sanitize_ascii(entry->name, sizeof(entry->name), entry->fingerprint);
    }
    if (location_valid) {
        entry->location_valid = true;
        entry->lat_e6 = lat_e6;
        entry->lon_e6 = lon_e6;
        entry->location_advert_timestamp = advert_timestamp;
        entry->location_seq = entry->seq;
    }
    if (marker_material_changed(&marker_before, entry)) {
        bump_marker_generation();
    }
    s_total_written++;
    esp_err_t ret = persist_store();
    if (ret != ESP_OK) {
        *entry = entry_before;
        s_count = count_before;
        s_next_seq = next_seq_before;
        s_total_written = total_written_before;
        s_dropped_oldest = dropped_oldest_before;
        s_marker_generation = marker_generation_before;
    }
    d1l_store_lock_give(&s_store_lock);
    return ret;
}

d1l_node_store_stats_t d1l_node_store_stats(void)
{
    d1l_store_lock_take(&s_store_lock);
    d1l_node_store_stats_t stats = {
        .next_seq = s_next_seq,
        .total_written = s_total_written,
        .dropped_oldest = s_dropped_oldest,
        .count = s_count,
        .capacity = D1L_NODE_STORE_CAPACITY,
    };
    d1l_store_lock_give(&s_store_lock);
    return stats;
}

bool d1l_node_store_find_by_fingerprint(const char *fingerprint, d1l_node_entry_t *out_entry)
{
    if (!s_loaded && d1l_node_store_init() != ESP_OK) {
        return false;
    }
    d1l_store_lock_take(&s_store_lock);
    int index = find_by_fingerprint(fingerprint);
    if (index < 0) {
        d1l_store_lock_give(&s_store_lock);
        return false;
    }
    if (out_entry) {
        *out_entry = s_entries[index];
    }
    d1l_store_lock_give(&s_store_lock);
    return true;
}

size_t d1l_node_store_copy_recent(d1l_node_entry_t *out_entries, size_t max_entries)
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
    bool used[D1L_NODE_STORE_CAPACITY] = {0};
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

size_t d1l_node_store_query(const d1l_node_query_t *query, d1l_node_view_t *out_entries,
                            size_t max_entries)
{
    if (!out_entries || max_entries == 0) {
        return 0;
    }
    if (!s_loaded && d1l_node_store_init() != ESP_OK) {
        return 0;
    }
    d1l_store_lock_take(&s_store_lock);
    for (size_t i = 0; i < s_count; ++i) {
        build_node_view(&s_entries[i], &s_query_scratch[i]);
    }

    const d1l_node_sort_t sort = query ? query->sort : D1L_NODE_SORT_LAST_HEARD;
    bool used[D1L_NODE_STORE_CAPACITY] = {0};
    size_t copied = 0;
    while (copied < max_entries) {
        size_t best = 0;
        bool best_set = false;
        for (size_t i = 0; i < s_count; ++i) {
            if (used[i] || !node_view_matches_query(&s_query_scratch[i], query)) {
                continue;
            }
            if (!best_set || node_view_better(&s_query_scratch[i], &s_query_scratch[best], sort)) {
                best = i;
                best_set = true;
            }
        }
        if (!best_set) {
            break;
        }
        used[best] = true;
        out_entries[copied++] = s_query_scratch[best];
    }
    d1l_store_lock_give(&s_store_lock);
    return copied;
}

uint32_t d1l_node_store_marker_generation(void)
{
    if (!s_loaded && d1l_node_store_init() != ESP_OK) {
        return 0U;
    }
    d1l_store_lock_take(&s_store_lock);
    const uint32_t generation = s_marker_generation;
    d1l_store_lock_give(&s_store_lock);
    return generation;
}

size_t d1l_node_store_copy_markers(d1l_node_marker_t *out_markers, size_t max_markers)
{
    if (!out_markers || max_markers == 0U) {
        return 0U;
    }
    if (!s_loaded && d1l_node_store_init() != ESP_OK) {
        return 0U;
    }

    d1l_store_lock_take(&s_store_lock);
    bool used[D1L_NODE_STORE_CAPACITY] = {0};
    size_t copied = 0U;
    while (copied < max_markers) {
        size_t best = 0U;
        bool best_set = false;
        for (size_t i = 0; i < s_count; ++i) {
            if (used[i] || !s_entries[i].location_valid) {
                continue;
            }
            if (!best_set || s_entries[i].location_seq >
                                 s_entries[best].location_seq ||
                (s_entries[i].location_seq == s_entries[best].location_seq &&
                 s_entries[i].seq > s_entries[best].seq)) {
                best = i;
                best_set = true;
            }
        }
        if (!best_set) {
            break;
        }
        used[best] = true;
        d1l_node_marker_t *marker = &out_markers[copied++];
        memset(marker, 0, sizeof(*marker));
        sanitize_ascii(marker->fingerprint, sizeof(marker->fingerprint),
                       s_entries[best].fingerprint);
        sanitize_ascii(marker->name, sizeof(marker->name), s_entries[best].name);
        sanitize_ascii(marker->type, sizeof(marker->type), s_entries[best].type);
        marker->lat_e6 = s_entries[best].lat_e6;
        marker->lon_e6 = s_entries[best].lon_e6;
        marker->location_advert_timestamp = s_entries[best].location_advert_timestamp;
        marker->location_seq = s_entries[best].location_seq;
        marker->location_provenance =
            D1L_NODE_LOCATION_PROVENANCE_SIGNED_ADVERT;
    }
    d1l_store_lock_give(&s_store_lock);
    return copied;
}
