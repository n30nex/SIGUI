#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mesh/contact_store.h"
#include "mesh/node_store.h"
#include "mock_esp_nvs.h"

#define NODE_NAMESPACE "d1l_nodes"
#define NODE_KEY "heard"
#define CONTACT_NAMESPACE "d1l_contacts"
#define CONTACT_KEY "contacts"
#define LEGACY_ALIAS_LEN 24U
#define LEGACY_TYPE_LEN 8U

static const char KEY_HEX[] =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

static void make_public_key(char key[D1L_NODE_PUBLIC_KEY_HEX_LEN], uint32_t id);
static void fingerprint_from_key(
    char fingerprint[D1L_NODE_FINGERPRINT_LEN],
    const char key[D1L_NODE_PUBLIC_KEY_HEX_LEN]);

typedef struct {
    uint32_t seq;
    uint32_t first_heard_ms;
    uint32_t last_heard_ms;
    uint32_t advert_timestamp;
    uint32_t heard_count;
    char fingerprint[D1L_NODE_FINGERPRINT_LEN];
    char public_key_hex[D1L_NODE_PUBLIC_KEY_HEX_LEN];
    char name[D1L_HEARD_NODE_NAME_LEN];
    char type[LEGACY_TYPE_LEN];
    int rssi_dbm;
    int snr_tenths;
    uint8_t path_hash_bytes;
    uint8_t path_hops;
} node_entry_v3_t;

typedef struct {
    uint32_t schema;
    uint32_t next_seq;
    uint32_t total_written;
    uint32_t dropped_oldest;
    uint32_t count;
    node_entry_v3_t entries[D1L_NODE_NVS_FALLBACK_CAPACITY];
} node_blob_v3_t;

typedef struct {
    uint32_t schema;
    uint32_t next_seq;
    uint32_t total_written;
    uint32_t dropped_oldest;
    uint32_t count;
    d1l_node_entry_t entries[D1L_NODE_NVS_FALLBACK_CAPACITY];
} node_blob_v4_t;

typedef struct {
    uint32_t seq;
    uint32_t created_ms;
    uint32_t updated_ms;
    uint32_t out_path_updated_ms;
    char fingerprint[D1L_NODE_FINGERPRINT_LEN];
    char public_key_hex[D1L_NODE_PUBLIC_KEY_HEX_LEN];
    char alias[LEGACY_ALIAS_LEN];
    char heard_name[D1L_HEARD_NODE_NAME_LEN];
    char type[LEGACY_TYPE_LEN];
    int last_rssi_dbm;
    int last_snr_tenths;
    uint8_t path_hash_bytes;
    uint8_t path_hops;
    bool out_path_valid;
    uint8_t out_path_len;
    uint8_t out_path[D1L_CONTACT_OUT_PATH_MAX];
    bool favorite;
    bool muted;
} contact_entry_v3_t;

typedef struct {
    uint32_t schema;
    uint32_t next_seq;
    uint32_t total_written;
    uint32_t dropped_oldest;
    uint32_t count;
    contact_entry_v3_t entries[D1L_CONTACT_STORE_CAPACITY];
} contact_blob_v3_t;

typedef struct {
    uint32_t seq;
    uint32_t created_ms;
    uint32_t updated_ms;
    uint32_t out_path_updated_ms;
    char fingerprint[D1L_NODE_FINGERPRINT_LEN];
    char public_key_hex[D1L_NODE_PUBLIC_KEY_HEX_LEN];
    char alias[LEGACY_ALIAS_LEN];
    char heard_name[D1L_HEARD_NODE_NAME_LEN];
    char type[D1L_NODE_TYPE_LEN];
    int last_rssi_dbm;
    int last_snr_tenths;
    uint8_t path_hash_bytes;
    uint8_t path_hops;
    bool out_path_valid;
    uint8_t out_path_len;
    uint8_t out_path[D1L_CONTACT_OUT_PATH_MAX];
    bool favorite;
    bool muted;
} contact_entry_v4_t;

typedef struct {
    uint32_t schema;
    uint32_t next_seq;
    uint32_t total_written;
    uint32_t dropped_oldest;
    uint32_t count;
    contact_entry_v4_t entries[D1L_CONTACT_STORE_CAPACITY];
} contact_blob_v4_t;

typedef struct {
    uint32_t seq;
    uint32_t created_ms;
    uint32_t updated_ms;
    uint32_t out_path_updated_ms;
    char fingerprint[D1L_NODE_FINGERPRINT_LEN];
    char public_key_hex[D1L_NODE_PUBLIC_KEY_HEX_LEN];
    char alias[LEGACY_ALIAS_LEN];
    char heard_name[D1L_HEARD_NODE_NAME_LEN];
    char type[D1L_NODE_TYPE_LEN];
    int last_rssi_dbm;
    int last_snr_tenths;
    uint8_t path_hash_bytes;
    uint8_t path_hops;
    bool out_path_valid;
    uint8_t out_path_len;
    uint8_t out_path[D1L_CONTACT_OUT_PATH_MAX];
    bool favorite;
    bool muted;
    uint8_t verification_source;
    uint32_t verified_at_ms;
    uint32_t signed_advert_timestamp;
    uint32_t last_heard_ms;
} contact_entry_v5_t;

typedef struct {
    uint32_t schema;
    uint32_t next_seq;
    uint32_t total_written;
    uint32_t dropped_oldest;
    uint32_t count;
    contact_entry_v5_t entries[D1L_CONTACT_STORE_CAPACITY];
} contact_blob_v5_t;

typedef struct {
    uint32_t schema;
    uint32_t next_seq;
    uint32_t total_written;
    uint32_t dropped_oldest;
    uint32_t count;
    d1l_contact_entry_t entries[D1L_CONTACT_STORE_CAPACITY];
} contact_blob_v6_t;

_Static_assert(sizeof(node_entry_v3_t) == 148U,
               "native node v3 fixture no longer matches production migration layout");
_Static_assert(sizeof(contact_entry_v3_t) == 236U,
               "native contact v3 fixture no longer matches production migration layout");
_Static_assert(sizeof(contact_entry_v4_t) == 236U,
               "native contact v4 fixture no longer matches production migration layout");
_Static_assert(sizeof(contact_entry_v5_t) == 248U,
               "native contact v5 fixture no longer matches migration layout");
_Static_assert(sizeof(d1l_contact_entry_t) == 256U,
               "native contact v6 fixture no longer matches production layout");
_Static_assert(sizeof(KEY_HEX) == D1L_NODE_PUBLIC_KEY_HEX_LEN,
               "test public key must be exactly 64 hex characters");

static void copy_field(char *dest, size_t dest_size, const char *src)
{
    assert(dest != NULL);
    assert(dest_size > 0U);
    memset(dest, 0, dest_size);
    if (!src) {
        return;
    }
    size_t length = strlen(src);
    if (length > dest_size) {
        length = dest_size;
    }
    memcpy(dest, src, length);
}

static void assert_node_stats_equal(d1l_node_store_stats_t left,
                                    d1l_node_store_stats_t right)
{
    assert(left.next_seq == right.next_seq);
    assert(left.total_written == right.total_written);
    assert(left.dropped_oldest == right.dropped_oldest);
    assert(left.count == right.count);
    assert(left.capacity == right.capacity);
}

static void assert_contact_stats_equal(d1l_contact_store_stats_t left,
                                       d1l_contact_store_stats_t right)
{
    assert(left.next_seq == right.next_seq);
    assert(left.total_written == right.total_written);
    assert(left.dropped_oldest == right.dropped_oldest);
    assert(left.count == right.count);
    assert(left.capacity == right.capacity);
}

static void fill_node_v3(node_entry_v3_t *entry, uint32_t seq,
                         const char *fingerprint, const char *name,
                         const char *legacy_type)
{
    memset(entry, 0, sizeof(*entry));
    entry->seq = seq;
    entry->first_heard_ms = seq * 100U;
    entry->last_heard_ms = seq * 200U;
    entry->advert_timestamp = seq * 10U;
    entry->heard_count = seq;
    copy_field(entry->fingerprint, sizeof(entry->fingerprint), fingerprint);
    copy_field(entry->public_key_hex, sizeof(entry->public_key_hex), KEY_HEX);
    copy_field(entry->name, sizeof(entry->name), name);
    copy_field(entry->type, sizeof(entry->type), legacy_type);
    entry->rssi_dbm = -40 - (int)seq;
    entry->snr_tenths = 100 + (int)seq;
    entry->path_hash_bytes = 1U;
    entry->path_hops = 2U;
}

static void fill_contact_v3(contact_entry_v3_t *entry, uint32_t seq,
                            const char *fingerprint, const char *alias,
                            const char *legacy_type)
{
    memset(entry, 0, sizeof(*entry));
    entry->seq = seq;
    entry->created_ms = seq * 100U;
    entry->updated_ms = seq * 200U;
    entry->out_path_updated_ms = seq * 300U;
    copy_field(entry->fingerprint, sizeof(entry->fingerprint), fingerprint);
    copy_field(entry->public_key_hex, sizeof(entry->public_key_hex), KEY_HEX);
    copy_field(entry->alias, sizeof(entry->alias), alias);
    copy_field(entry->heard_name, sizeof(entry->heard_name), alias);
    copy_field(entry->type, sizeof(entry->type), legacy_type);
    entry->last_rssi_dbm = -50 - (int)seq;
    entry->last_snr_tenths = 80 + (int)seq;
    entry->path_hash_bytes = 2U;
    entry->path_hops = 3U;
    entry->out_path_valid = true;
    entry->out_path_len = 2U;
    entry->out_path[0] = 0xa5U;
    entry->out_path[1] = 0x5aU;
    entry->favorite = true;
    entry->muted = (seq & 1U) == 0U;
}

static void fill_contact_v4(contact_entry_v4_t *entry, uint32_t seq,
                            const char *fingerprint, const char *public_key,
                            const char *alias, const char *type)
{
    memset(entry, 0, sizeof(*entry));
    entry->seq = seq;
    entry->created_ms = seq * 100U;
    entry->updated_ms = seq * 200U;
    entry->out_path_updated_ms = seq * 300U;
    copy_field(entry->fingerprint, sizeof(entry->fingerprint), fingerprint);
    copy_field(entry->public_key_hex, sizeof(entry->public_key_hex), public_key);
    copy_field(entry->alias, sizeof(entry->alias), alias);
    copy_field(entry->heard_name, sizeof(entry->heard_name), alias);
    copy_field(entry->type, sizeof(entry->type), type);
    entry->last_rssi_dbm = -50 - (int)seq;
    entry->last_snr_tenths = 80 + (int)seq;
    entry->path_hash_bytes = 2U;
    entry->path_hops = 3U;
    entry->out_path_valid = true;
    entry->out_path_len = 2U;
    entry->out_path[0] = 0xa5U;
    entry->out_path[1] = 0x5aU;
    entry->favorite = true;
    entry->muted = (seq & 1U) == 0U;
}

static void test_node_v3_to_v4_migration(void)
{
    mock_nvs_reset();
    node_blob_v3_t legacy = {0};
    legacy.schema = 3U;
    legacy.next_seq = 10U;
    legacy.total_written = 21U;
    legacy.dropped_oldest = 2U;
    legacy.count = 3U;
    fill_node_v3(&legacy.entries[0], 1U, "1111111111111111", "Alice", "chat");
    fill_node_v3(&legacy.entries[1], 2U, "2222222222222222", "Hill", "room");
    fill_node_v3(&legacy.entries[2], 3U, "3333333333333333", "Weather", "sensor");
    assert(mock_nvs_seed_blob(NODE_NAMESPACE, NODE_KEY, &legacy, sizeof(legacy)));

    assert(d1l_node_store_init() == ESP_OK);
    d1l_node_store_stats_t stats = d1l_node_store_stats();
    assert(stats.next_seq == 10U);
    assert(stats.total_written == 21U);
    assert(stats.dropped_oldest == 2U);
    assert(stats.count == 3U);

    d1l_node_entry_t node = {0};
    assert(d1l_node_store_find_by_fingerprint("1111111111111111", &node));
    assert(strcmp(node.type, "chat") == 0);
    assert(!node.location_valid);
    assert(node.location_advert_timestamp == 0U);
    assert(node.location_seq == 0U);
    assert(d1l_node_store_find_by_fingerprint("2222222222222222", &node));
    assert(strcmp(node.type, "repeater") == 0);
    assert(!node.location_valid);
    assert(d1l_node_store_find_by_fingerprint("3333333333333333", &node));
    assert(strcmp(node.type, "unknown") == 0);
    assert(!node.location_valid);

    node_blob_v4_t current = {0};
    assert(mock_nvs_copy_blob(NODE_NAMESPACE, NODE_KEY, &current,
                              sizeof(current)) == sizeof(current));
    assert(current.schema == 4U);
    assert(current.count == 3U);
    assert(d1l_node_store_init() == ESP_OK);
    assert(d1l_node_store_find_by_fingerprint("3333333333333333", &node));
    assert(strcmp(node.type, "unknown") == 0);
}

static void test_contact_v3_to_v6_migration(void)
{
    mock_nvs_reset();
    contact_blob_v3_t legacy = {0};
    legacy.schema = 3U;
    legacy.next_seq = 9U;
    legacy.total_written = 14U;
    legacy.dropped_oldest = 1U;
    legacy.count = 2U;
    fill_contact_v3(&legacy.entries[0], 4U, "4444444444444444", "Legacy Room", "room");
    fill_contact_v3(&legacy.entries[1], 5U, "5555555555555555", "Ambiguous", "sensor");
    assert(mock_nvs_seed_blob(CONTACT_NAMESPACE, CONTACT_KEY, &legacy, sizeof(legacy)));

    assert(d1l_contact_store_init() == ESP_OK);
    d1l_contact_store_stats_t stats = d1l_contact_store_stats();
    assert(stats.next_seq == 9U);
    assert(stats.total_written == 14U);
    assert(stats.dropped_oldest == 1U);
    assert(stats.count == 2U);

    d1l_contact_entry_t contact = {0};
    assert(d1l_contact_store_find_by_fingerprint("4444444444444444", &contact));
    assert(strcmp(contact.type, "repeater") == 0);
    assert(contact.verification_source == D1L_CONTACT_VERIFICATION_NONE);
    assert(contact.verified_at_ms == 0U);
    assert(!d1l_contact_store_is_canonical(&contact));
    assert(contact.out_path_valid);
    assert(contact.out_path_len == 2U);
    assert(contact.out_path[0] == 0xa5U && contact.out_path[1] == 0x5aU);
    assert(contact.favorite);
    assert(contact.out_path_updated_ms == 1200U);

    assert(d1l_contact_store_find_by_fingerprint("5555555555555555", &contact));
    assert(strcmp(contact.type, "unknown") == 0);
    char uri[D1L_CONTACT_EXPORT_URI_LEN] = "must-be-cleared";
    assert(d1l_contact_store_export_uri(&contact, uri, sizeof(uri)) ==
           ESP_ERR_INVALID_STATE);
    assert(uri[0] == '\0');

    contact_blob_v6_t current = {0};
    assert(mock_nvs_copy_blob(CONTACT_NAMESPACE, CONTACT_KEY, &current,
                              sizeof(current)) == sizeof(current));
    assert(current.schema == 6U);
    assert(current.count == 2U);
    assert(d1l_contact_store_init() == ESP_OK);
    assert(d1l_contact_store_find_by_fingerprint("4444444444444444", &contact));
    assert(strcmp(contact.type, "repeater") == 0);
}

static void test_contact_v4_to_v6_migration_is_truthful(void)
{
    mock_nvs_reset();
    contact_blob_v4_t legacy = {0};
    legacy.schema = 4U;
    legacy.next_seq = 12U;
    legacy.total_written = 9U;
    legacy.count = 2U;

    char valid_key[D1L_NODE_PUBLIC_KEY_HEX_LEN] = {0};
    char valid_fingerprint[D1L_NODE_FINGERPRINT_LEN] = {0};
    make_public_key(valid_key, 0x51U);
    fingerprint_from_key(valid_fingerprint, valid_key);
    fill_contact_v4(&legacy.entries[0], 7U, valid_fingerprint, valid_key,
                    "Migrated", "chat");
    fill_contact_v4(&legacy.entries[1], 8U, "ffffffffffffffff", "",
                    "Placeholder", "unknown");
    assert(mock_nvs_seed_blob(CONTACT_NAMESPACE, CONTACT_KEY, &legacy,
                              sizeof(legacy)));

    assert(d1l_contact_store_init() == ESP_OK);
    d1l_contact_entry_t contact = {0};
    assert(d1l_contact_store_find_by_fingerprint(valid_fingerprint, &contact));
    assert(contact.verification_source ==
           D1L_CONTACT_VERIFICATION_MIGRATED_SIGNED_ADVERT);
    assert(contact.verified_at_ms == 1400U);
    assert(contact.signed_advert_timestamp == 0U);
    assert(contact.last_heard_ms == 0U);
    assert(d1l_contact_store_is_canonical(&contact));
    assert(d1l_contact_store_can_dm(&contact));
    assert(!d1l_contact_store_can_admin(&contact));

    assert(d1l_contact_store_find_by_fingerprint("ffffffffffffffff", &contact));
    assert(contact.verification_source == D1L_CONTACT_VERIFICATION_NONE);
    assert(!d1l_contact_store_is_canonical(&contact));
    assert(!d1l_contact_store_can_dm(&contact));

    contact_blob_v6_t current = {0};
    assert(mock_nvs_copy_blob(CONTACT_NAMESPACE, CONTACT_KEY, &current,
                              sizeof(current)) == sizeof(current));
    assert(current.schema == 6U);
    assert(current.count == 2U);
}

static void test_contact_v5_to_v6_migration_preserves_provenance(void)
{
    mock_nvs_reset();
    contact_blob_v5_t legacy = {0};
    legacy.schema = 5U;
    legacy.next_seq = 18U;
    legacy.total_written = 13U;
    legacy.dropped_oldest = 2U;
    legacy.count = 1U;

    char key[D1L_NODE_PUBLIC_KEY_HEX_LEN] = {0};
    char fingerprint[D1L_NODE_FINGERPRINT_LEN] = {0};
    make_public_key(key, 0x59U);
    fingerprint_from_key(fingerprint, key);
    contact_entry_v5_t *old = &legacy.entries[0];
    old->seq = 17U;
    old->created_ms = 1000U;
    old->updated_ms = 2000U;
    old->out_path_updated_ms = 1500U;
    copy_field(old->fingerprint, sizeof(old->fingerprint), fingerprint);
    copy_field(old->public_key_hex, sizeof(old->public_key_hex), key);
    copy_field(old->alias, sizeof(old->alias), "12345678901234567890123");
    copy_field(old->heard_name, sizeof(old->heard_name), "Heard");
    copy_field(old->type, sizeof(old->type), "chat");
    old->last_rssi_dbm = -67;
    old->last_snr_tenths = 42;
    old->path_hash_bytes = 1U;
    old->path_hops = 2U;
    old->out_path_valid = true;
    old->out_path_len = 2U;
    old->out_path[0] = 0x12U;
    old->out_path[1] = 0x34U;
    old->favorite = true;
    old->muted = true;
    old->verification_source = D1L_CONTACT_VERIFICATION_URI_IMPORT;
    old->verified_at_ms = 1900U;
    old->signed_advert_timestamp = 0U;
    old->last_heard_ms = 1700U;
    assert(mock_nvs_seed_blob(CONTACT_NAMESPACE, CONTACT_KEY, &legacy,
                              sizeof(legacy)));

    assert(d1l_contact_store_init() == ESP_OK);
    d1l_contact_entry_t contact = {0};
    assert(d1l_contact_store_find_by_fingerprint(fingerprint, &contact));
    assert(strcmp(contact.alias, "12345678901234567890123") == 0);
    assert(strcmp(contact.public_key_hex, key) == 0);
    assert(contact.verification_source == D1L_CONTACT_VERIFICATION_URI_IMPORT);
    assert(contact.verified_at_ms == 1900U);
    assert(contact.last_heard_ms == 1700U);
    assert(contact.favorite && contact.muted);
    assert(contact.out_path_valid && contact.out_path_len == 2U);
    assert(contact.out_path[0] == 0x12U && contact.out_path[1] == 0x34U);
    assert(d1l_contact_store_can_dm(&contact));

    contact_blob_v6_t current = {0};
    assert(mock_nvs_copy_blob(CONTACT_NAMESPACE, CONTACT_KEY, &current,
                              sizeof(current)) == sizeof(current));
    assert(current.schema == 6U);
    assert(current.next_seq == 18U);
    assert(current.total_written == 13U);
    assert(current.dropped_oldest == 2U);
}

static esp_err_t upsert_node(uint32_t advert_timestamp, const char *name,
                             bool location_valid, int32_t lat_e6, int32_t lon_e6,
                             bool *out_stale)
{
    return d1l_node_store_upsert_advert(
        "abcdef0123456789", KEY_HEX, name, 'C', -72, 45, 1U, 2U,
        advert_timestamp, location_valid, lat_e6, lon_e6, out_stale);
}

static void test_stale_advert_and_location_preservation(void)
{
    mock_nvs_reset();
    assert(d1l_node_store_init() == ESP_OK);
    mock_timer_set_us(1000000);
    bool stale = true;
    assert(upsert_node(100U, "Cambridge", true, 43427977, -80316478,
                       &stale) == ESP_OK);
    assert(!stale);

    d1l_node_entry_t original = {0};
    assert(d1l_node_store_find_by_fingerprint("abcdef0123456789", &original));
    const d1l_node_store_stats_t original_stats = d1l_node_store_stats();
    const uint32_t original_generation = d1l_node_store_marker_generation();
    assert(original.location_valid);
    assert(original.location_seq == original.seq);

    mock_timer_set_us(2000000);
    stale = false;
    assert(upsert_node(100U, "Replay", true, 43000000, -80000000,
                       &stale) == ESP_OK);
    assert(stale);
    d1l_node_entry_t after = {0};
    assert(d1l_node_store_find_by_fingerprint("abcdef0123456789", &after));
    assert(memcmp(&after, &original, sizeof(after)) == 0);
    assert_node_stats_equal(d1l_node_store_stats(), original_stats);
    assert(d1l_node_store_marker_generation() == original_generation);

    stale = false;
    assert(upsert_node(99U, "Older", false, 0, 0, &stale) == ESP_OK);
    assert(stale);
    assert(d1l_node_store_find_by_fingerprint("abcdef0123456789", &after));
    assert(memcmp(&after, &original, sizeof(after)) == 0);

    stale = true;
    assert(upsert_node(101U, "Cambridge", false, 0, 0, &stale) == ESP_OK);
    assert(!stale);
    assert(d1l_node_store_find_by_fingerprint("abcdef0123456789", &after));
    assert(after.advert_timestamp == 101U);
    assert(after.seq > original.seq);
    assert(after.location_valid);
    assert(after.lat_e6 == original.lat_e6);
    assert(after.lon_e6 == original.lon_e6);
    assert(after.location_advert_timestamp == original.location_advert_timestamp);
    assert(after.location_seq == original.location_seq);
    assert(d1l_node_store_marker_generation() == original_generation);

    stale = true;
    assert(upsert_node(102U, "Cambridge", true, 43675000, -79440000,
                       &stale) == ESP_OK);
    assert(!stale);
    assert(d1l_node_store_find_by_fingerprint("abcdef0123456789", &after));
    assert(after.lat_e6 == 43675000);
    assert(after.lon_e6 == -79440000);
    assert(after.location_advert_timestamp == 102U);
    assert(after.location_seq == after.seq);
    assert(d1l_node_store_marker_generation() == original_generation + 1U);

    assert(upsert_node(103U, "Cambridge", false, 0, 0, NULL) ==
           ESP_ERR_INVALID_ARG);
}

static void test_node_nvs_failure_rolls_back_and_retry_succeeds(void)
{
    mock_nvs_reset();
    assert(d1l_node_store_init() == ESP_OK);
    const d1l_node_store_stats_t empty_stats = d1l_node_store_stats();
    const uint32_t empty_generation = d1l_node_store_marker_generation();

    bool stale = true;
    mock_nvs_fail_next_set(ESP_FAIL);
    assert(upsert_node(200U, "Retry Node", true, 43427977, -80316478,
                       &stale) == ESP_FAIL);
    assert(!stale);
    assert(!d1l_node_store_find_by_fingerprint("abcdef0123456789", NULL));
    assert_node_stats_equal(d1l_node_store_stats(), empty_stats);
    assert(d1l_node_store_marker_generation() == empty_generation);

    stale = true;
    assert(upsert_node(200U, "Retry Node", true, 43427977, -80316478,
                       &stale) == ESP_OK);
    assert(!stale);
    d1l_node_entry_t baseline = {0};
    assert(d1l_node_store_find_by_fingerprint("abcdef0123456789", &baseline));
    const d1l_node_store_stats_t baseline_stats = d1l_node_store_stats();
    const uint32_t baseline_generation = d1l_node_store_marker_generation();

    mock_timer_set_us(3000000);
    mock_nvs_fail_next_set(ESP_FAIL);
    stale = true;
    assert(upsert_node(201U, "Changed", true, 44000000, -81000000,
                       &stale) == ESP_FAIL);
    assert(!stale);
    d1l_node_entry_t after = {0};
    assert(d1l_node_store_find_by_fingerprint("abcdef0123456789", &after));
    assert(memcmp(&after, &baseline, sizeof(after)) == 0);
    assert_node_stats_equal(d1l_node_store_stats(), baseline_stats);
    assert(d1l_node_store_marker_generation() == baseline_generation);

    assert(d1l_node_store_init() == ESP_OK);
    assert(d1l_node_store_find_by_fingerprint("abcdef0123456789", &after));
    assert(memcmp(&after, &baseline, sizeof(after)) == 0);
    stale = true;
    assert(upsert_node(201U, "Changed", true, 44000000, -81000000,
                       &stale) == ESP_OK);
    assert(!stale);
    assert(d1l_node_store_find_by_fingerprint("abcdef0123456789", &after));
    assert(after.advert_timestamp == 201U);
    assert(strcmp(after.name, "Changed") == 0);
}

static void test_verified_advert_retry_and_prefix_collision_preserve_node(void)
{
    mock_nvs_reset();
    assert(d1l_node_store_init() == ESP_OK);
    assert(d1l_contact_store_init() == ESP_OK);

    char key[D1L_NODE_PUBLIC_KEY_HEX_LEN] = {0};
    char fingerprint[D1L_NODE_FINGERPRINT_LEN] = {0};
    make_public_key(key, 0x31U);
    fingerprint_from_key(fingerprint, key);
    bool stale = true;
    assert(d1l_node_store_upsert_advert(
               fingerprint, key, "Retry Peer", 'C', -55, 80, 1U, 2U,
               300U, false, 0, 0, &stale) == ESP_OK);
    assert(!stale);

    d1l_node_entry_t retained = {0};
    assert(d1l_node_store_find_by_fingerprint(fingerprint, &retained));
    d1l_contact_verified_advert_result_t result =
        D1L_CONTACT_VERIFIED_ADVERT_NONE;
    mock_nvs_fail_next_set(ESP_FAIL);
    assert(d1l_contact_store_upsert_verified_advert(
               fingerprint, &retained, &result, NULL) == ESP_FAIL);
    assert(result == D1L_CONTACT_VERIFIED_ADVERT_NONE);
    assert(!d1l_contact_store_find_by_public_key(key, NULL));

    /* The node replay watermark is retained, but the exact equal advert can
     * recover contact promotion from the retained full-key node. */
    stale = false;
    assert(d1l_node_store_upsert_advert(
               fingerprint, key, "Retry Peer", 'C', -55, 80, 1U, 2U,
               300U, false, 0, 0, &stale) == ESP_OK);
    assert(stale);
    assert(d1l_node_store_find_by_fingerprint(fingerprint, &retained));
    result = D1L_CONTACT_VERIFIED_ADVERT_NONE;
    assert(d1l_contact_store_upsert_verified_advert(
               fingerprint, &retained, &result, NULL) == ESP_OK);
    assert(result == D1L_CONTACT_VERIFIED_ADVERT_CREATED);
    assert(d1l_contact_store_find_by_public_key(key, NULL));

    const d1l_node_entry_t baseline = retained;
    const d1l_node_store_stats_t baseline_stats = d1l_node_store_stats();
    char colliding_key[D1L_NODE_PUBLIC_KEY_HEX_LEN] = {0};
    copy_field(colliding_key, sizeof(colliding_key), key);
    colliding_key[D1L_NODE_PUBLIC_KEY_HEX_LEN - 2U] =
        colliding_key[D1L_NODE_PUBLIC_KEY_HEX_LEN - 2U] == 'f' ? 'e' : 'f';
    stale = true;
    assert(d1l_node_store_upsert_advert(
               fingerprint, colliding_key, "Collision", 'R', -20, 100,
               2U, 3U, 301U, true, 45000000, -80000000, &stale) ==
           ESP_ERR_INVALID_STATE);
    assert(!stale);
    memset(&retained, 0, sizeof(retained));
    assert(d1l_node_store_find_by_fingerprint(fingerprint, &retained));
    assert(memcmp(&retained, &baseline, sizeof(retained)) == 0);
    assert_node_stats_equal(d1l_node_store_stats(), baseline_stats);
}

static d1l_node_entry_t make_heard_node(const char *name, const char *type)
{
    d1l_node_entry_t node = {0};
    copy_field(node.public_key_hex, sizeof(node.public_key_hex), KEY_HEX);
    copy_field(node.name, sizeof(node.name), name);
    copy_field(node.type, sizeof(node.type), type);
    node.rssi_dbm = -61;
    node.snr_tenths = 72;
    node.path_hash_bytes = 1U;
    node.path_hops = 2U;
    return node;
}

static void make_public_key(char key[D1L_NODE_PUBLIC_KEY_HEX_LEN], uint32_t id)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < D1L_NODE_PUBLIC_KEY_HEX_LEN - 1U; ++i) {
        key[i] = hex[(i + id) & 0x0fU];
    }
    for (size_t i = 0; i < 8U; ++i) {
        key[15U - i] = hex[(id >> (i * 4U)) & 0x0fU];
    }
    key[D1L_NODE_PUBLIC_KEY_HEX_LEN - 1U] = '\0';
}

static void fingerprint_from_key(
    char fingerprint[D1L_NODE_FINGERPRINT_LEN],
    const char key[D1L_NODE_PUBLIC_KEY_HEX_LEN])
{
    memcpy(fingerprint, key, D1L_NODE_FINGERPRINT_LEN - 1U);
    fingerprint[D1L_NODE_FINGERPRINT_LEN - 1U] = '\0';
}

static void uppercase_hex(char *dest, size_t dest_size, const char *src)
{
    size_t i = 0U;
    for (; i + 1U < dest_size && src[i] != '\0'; ++i) {
        dest[i] = (src[i] >= 'a' && src[i] <= 'f')
                      ? (char)(src[i] - ('a' - 'A'))
                      : src[i];
    }
    dest[i] = '\0';
}

static d1l_node_entry_t make_verified_node(const char *public_key_hex,
                                           const char *name,
                                           const char *type)
{
    d1l_node_entry_t node = make_heard_node(name, type);
    copy_field(node.public_key_hex, sizeof(node.public_key_hex), public_key_hex);
    node.advert_timestamp = 123U;
    node.last_heard_ms = 456U;
    return node;
}

static void test_verified_contact_create_reload_update_preserves_preferences(void)
{
    mock_nvs_reset();
    assert(d1l_contact_store_init() == ESP_OK);
    char key[D1L_NODE_PUBLIC_KEY_HEX_LEN] = {0};
    char fingerprint[D1L_NODE_FINGERPRINT_LEN] = {0};
    make_public_key(key, 1U);
    fingerprint_from_key(fingerprint, key);
    d1l_node_entry_t node = make_verified_node(key, "Verified One", "chat");
    d1l_contact_verified_advert_result_t result =
        D1L_CONTACT_VERIFIED_ADVERT_NONE;
    d1l_contact_entry_t contact = {0};
    assert(d1l_contact_store_upsert_verified_advert(
               fingerprint, &node, &result, &contact) == ESP_OK);
    assert(result == D1L_CONTACT_VERIFIED_ADVERT_CREATED);
    assert(strcmp(contact.public_key_hex, key) == 0);
    assert(strcmp(contact.alias, "Verified One") == 0);
    assert(contact.verification_source ==
           D1L_CONTACT_VERIFICATION_SIGNED_ADVERT);
    assert(contact.signed_advert_timestamp == 123U);
    assert(contact.last_heard_ms == 456U);
    assert(d1l_contact_store_is_canonical(&contact));
    assert(d1l_contact_store_can_dm(&contact));

    assert(d1l_contact_store_rename(fingerprint, "Pinned Alias", NULL) == ESP_OK);
    assert(d1l_contact_store_set_flags(fingerprint, true, true, NULL) == ESP_OK);
    const uint8_t path[] = {0xa5U, 0x5aU};
    assert(d1l_contact_store_update_path(fingerprint, path, 2U) == ESP_OK);
    d1l_contact_entry_t preferred = {0};
    assert(d1l_contact_store_find_by_fingerprint(fingerprint, &preferred));

    assert(d1l_contact_store_init() == ESP_OK);
    char upper_key[D1L_NODE_PUBLIC_KEY_HEX_LEN] = {0};
    char upper_fingerprint[D1L_NODE_FINGERPRINT_LEN] = {0};
    uppercase_hex(upper_key, sizeof(upper_key), key);
    uppercase_hex(upper_fingerprint, sizeof(upper_fingerprint), fingerprint);
    node = make_verified_node(upper_key, "Fresh Heard Name", "repeater");
    node.rssi_dbm = -44;
    result = D1L_CONTACT_VERIFIED_ADVERT_NONE;
    memset(&contact, 0, sizeof(contact));
    assert(d1l_contact_store_upsert_verified_advert(
               upper_fingerprint, &node, &result, &contact) == ESP_OK);
    assert(result == D1L_CONTACT_VERIFIED_ADVERT_UPDATED);
    assert(strcmp(contact.public_key_hex, key) == 0);
    assert(strcmp(contact.fingerprint, fingerprint) == 0);
    assert(strcmp(contact.alias, preferred.alias) == 0);
    assert(contact.favorite == preferred.favorite);
    assert(contact.muted == preferred.muted);
    assert(contact.out_path_valid == preferred.out_path_valid);
    assert(contact.out_path_len == preferred.out_path_len);
    assert(memcmp(contact.out_path, preferred.out_path,
                  sizeof(contact.out_path)) == 0);
    assert(strcmp(contact.heard_name, "Fresh Heard Name") == 0);
    assert(strcmp(contact.type, "repeater") == 0);
    assert(contact.last_rssi_dbm == -44);
    assert(d1l_contact_store_find_by_public_key(upper_key, &contact));
    assert(strcmp(contact.alias, "Pinned Alias") == 0);
    assert(d1l_contact_store_stats().count == 1U);
}

static void test_verified_contact_promotes_placeholder_without_losing_preferences(void)
{
    mock_nvs_reset();
    assert(d1l_contact_store_init() == ESP_OK);
    char key[D1L_NODE_PUBLIC_KEY_HEX_LEN] = {0};
    char fingerprint[D1L_NODE_FINGERPRINT_LEN] = {0};
    make_public_key(key, 2U);
    fingerprint_from_key(fingerprint, key);
    assert(d1l_contact_store_upsert_from_node(
               fingerprint, "Manual Alias", NULL) == ESP_OK);
    assert(d1l_contact_store_set_flags(fingerprint, true, true, NULL) == ESP_OK);
    const uint8_t path[] = {0x11U, 0x22U, 0x33U};
    assert(d1l_contact_store_update_path(fingerprint, path, 3U) == ESP_OK);

    d1l_node_entry_t node = make_verified_node(key, "Signed Name", "room");
    d1l_contact_verified_advert_result_t result =
        D1L_CONTACT_VERIFIED_ADVERT_NONE;
    d1l_contact_entry_t contact = {0};
    assert(d1l_contact_store_upsert_verified_advert(
               fingerprint, &node, &result, &contact) == ESP_OK);
    assert(result == D1L_CONTACT_VERIFIED_ADVERT_PROMOTED_PLACEHOLDER);
    assert(strcmp(contact.public_key_hex, key) == 0);
    assert(strcmp(contact.alias, "Manual Alias") == 0);
    assert(contact.favorite && contact.muted);
    assert(contact.out_path_valid && contact.out_path_len == 3U);
    assert(memcmp(contact.out_path, path, sizeof(path)) == 0);
    assert(strcmp(contact.heard_name, "Signed Name") == 0);
    assert(d1l_contact_store_stats().count == 1U);
}

static void test_verified_contact_refuses_collision_and_rolls_back_nvs_failure(void)
{
    mock_nvs_reset();
    assert(d1l_contact_store_init() == ESP_OK);
    char key[D1L_NODE_PUBLIC_KEY_HEX_LEN] = {0};
    char fingerprint[D1L_NODE_FINGERPRINT_LEN] = {0};
    make_public_key(key, 3U);
    fingerprint_from_key(fingerprint, key);
    d1l_node_entry_t node = make_verified_node(key, "Original", "chat");
    d1l_contact_verified_advert_result_t result =
        D1L_CONTACT_VERIFIED_ADVERT_NONE;
    assert(d1l_contact_store_upsert_verified_advert(
               fingerprint, &node, &result, NULL) == ESP_OK);

    d1l_contact_entry_t baseline = {0};
    assert(d1l_contact_store_find_by_fingerprint(fingerprint, &baseline));
    const d1l_contact_store_stats_t baseline_stats = d1l_contact_store_stats();
    node = make_verified_node(key, "Failed Update", "repeater");
    mock_nvs_fail_next_set(ESP_FAIL);
    result = D1L_CONTACT_VERIFIED_ADVERT_CREATED;
    assert(d1l_contact_store_upsert_verified_advert(
               fingerprint, &node, &result, NULL) == ESP_FAIL);
    assert(result == D1L_CONTACT_VERIFIED_ADVERT_NONE);
    d1l_contact_entry_t after = {0};
    assert(d1l_contact_store_find_by_fingerprint(fingerprint, &after));
    assert(memcmp(&after, &baseline, sizeof(after)) == 0);
    assert_contact_stats_equal(d1l_contact_store_stats(), baseline_stats);
    assert(d1l_contact_store_init() == ESP_OK);
    assert(d1l_contact_store_find_by_fingerprint(fingerprint, &after));
    assert(memcmp(&after, &baseline, sizeof(after)) == 0);

    char colliding_key[D1L_NODE_PUBLIC_KEY_HEX_LEN] = {0};
    copy_field(colliding_key, sizeof(colliding_key), key);
    colliding_key[D1L_NODE_PUBLIC_KEY_HEX_LEN - 2U] =
        colliding_key[D1L_NODE_PUBLIC_KEY_HEX_LEN - 2U] == 'f' ? 'e' : 'f';
    node = make_verified_node(colliding_key, "Collision", "room");
    d1l_contact_entry_t rejected = baseline;
    result = D1L_CONTACT_VERIFIED_ADVERT_NONE;
    assert(d1l_contact_store_upsert_verified_advert(
               fingerprint, &node, &result, &rejected) == ESP_ERR_INVALID_STATE);
    assert(result == D1L_CONTACT_VERIFIED_ADVERT_COLLISION);
    memset(&after, 0, sizeof(after));
    assert(d1l_contact_store_find_by_fingerprint(fingerprint, &after));
    assert(memcmp(&after, &baseline, sizeof(after)) == 0);
    assert_contact_stats_equal(d1l_contact_store_stats(), baseline_stats);
    d1l_contact_entry_t empty = {0};
    assert(memcmp(&rejected, &empty, sizeof(rejected)) == 0);

    char retry_key[D1L_NODE_PUBLIC_KEY_HEX_LEN] = {0};
    char retry_fingerprint[D1L_NODE_FINGERPRINT_LEN] = {0};
    make_public_key(retry_key, 4U);
    fingerprint_from_key(retry_fingerprint, retry_key);
    node = make_verified_node(retry_key, "Retry", "sensor");
    mock_nvs_fail_next_set(ESP_FAIL);
    result = D1L_CONTACT_VERIFIED_ADVERT_UPDATED;
    assert(d1l_contact_store_upsert_verified_advert(
               retry_fingerprint, &node, &result, NULL) == ESP_FAIL);
    assert(result == D1L_CONTACT_VERIFIED_ADVERT_NONE);
    assert(!d1l_contact_store_find_by_public_key(retry_key, NULL));
    assert_contact_stats_equal(d1l_contact_store_stats(), baseline_stats);
    assert(d1l_contact_store_init() == ESP_OK);
    result = D1L_CONTACT_VERIFIED_ADVERT_NONE;
    assert(d1l_contact_store_upsert_verified_advert(
               retry_fingerprint, &node, &result, NULL) == ESP_OK);
    assert(result == D1L_CONTACT_VERIFIED_ADVERT_CREATED);
}

static void test_verified_contact_full_store_never_evicts(void)
{
    mock_nvs_reset();
    assert(d1l_contact_store_init() == ESP_OK);
    char first_key[D1L_NODE_PUBLIC_KEY_HEX_LEN] = {0};
    char first_fingerprint[D1L_NODE_FINGERPRINT_LEN] = {0};
    for (uint32_t id = 20U;
         id < 20U + D1L_CONTACT_STORE_CAPACITY; ++id) {
        char key[D1L_NODE_PUBLIC_KEY_HEX_LEN] = {0};
        char fingerprint[D1L_NODE_FINGERPRINT_LEN] = {0};
        make_public_key(key, id);
        fingerprint_from_key(fingerprint, key);
        if (id == 20U) {
            copy_field(first_key, sizeof(first_key), key);
            copy_field(first_fingerprint, sizeof(first_fingerprint), fingerprint);
        }
        d1l_node_entry_t node = make_verified_node(key, "Capacity", "chat");
        d1l_contact_verified_advert_result_t result =
            D1L_CONTACT_VERIFIED_ADVERT_NONE;
        assert(d1l_contact_store_upsert_verified_advert(
                   fingerprint, &node, &result, NULL) == ESP_OK);
        assert(result == D1L_CONTACT_VERIFIED_ADVERT_CREATED);
    }
    const d1l_contact_store_stats_t full_stats = d1l_contact_store_stats();
    assert(full_stats.count == D1L_CONTACT_STORE_CAPACITY);
    assert(full_stats.dropped_oldest == 0U);

    char overflow_key[D1L_NODE_PUBLIC_KEY_HEX_LEN] = {0};
    char overflow_fingerprint[D1L_NODE_FINGERPRINT_LEN] = {0};
    make_public_key(overflow_key, 99U);
    fingerprint_from_key(overflow_fingerprint, overflow_key);
    d1l_node_entry_t overflow = make_verified_node(
        overflow_key, "Overflow", "chat");
    d1l_contact_verified_advert_result_t result =
        D1L_CONTACT_VERIFIED_ADVERT_NONE;
    assert(d1l_contact_store_upsert_verified_advert(
               overflow_fingerprint, &overflow, &result, NULL) == ESP_ERR_NO_MEM);
    assert(result == D1L_CONTACT_VERIFIED_ADVERT_FULL);
    assert_contact_stats_equal(d1l_contact_store_stats(), full_stats);
    assert(d1l_contact_store_find_by_public_key(first_key, NULL));
    assert(d1l_contact_store_find_by_fingerprint(first_fingerprint, NULL));
    assert(!d1l_contact_store_find_by_public_key(overflow_key, NULL));
    d1l_node_entry_t heard = make_heard_node("Bypass", "chat");
    assert(d1l_contact_store_upsert_from_node(
               "ffffffffffffffff", "Bypass", &heard) == ESP_ERR_NO_MEM);
    assert_contact_stats_equal(d1l_contact_store_stats(), full_stats);
    assert(d1l_contact_store_find_by_public_key(first_key, NULL));
}

static void test_favorite_placeholders_are_never_evicted(void)
{
    mock_nvs_reset();
    assert(d1l_contact_store_init() == ESP_OK);
    char first_fingerprint[D1L_NODE_FINGERPRINT_LEN] = {0};
    for (uint32_t i = 0U; i < D1L_CONTACT_STORE_CAPACITY; ++i) {
        char fingerprint[D1L_NODE_FINGERPRINT_LEN] = {0};
        (void)snprintf(fingerprint, sizeof(fingerprint), "%016llx",
                       (unsigned long long)(0x1000ULL + i));
        if (i == 0U) {
            copy_field(first_fingerprint, sizeof(first_fingerprint), fingerprint);
        }
        assert(d1l_contact_store_upsert_from_node(
                   fingerprint, "Favorite", NULL) == ESP_OK);
        assert(d1l_contact_store_set_flags(fingerprint, true, false, NULL) ==
               ESP_OK);
    }
    const d1l_contact_store_stats_t full = d1l_contact_store_stats();
    assert(full.count == D1L_CONTACT_STORE_CAPACITY);
    assert(d1l_contact_store_upsert_from_node(
               "eeeeeeeeeeeeeeee", "Overflow", NULL) == ESP_ERR_NO_MEM);
    assert_contact_stats_equal(d1l_contact_store_stats(), full);
    assert(d1l_contact_store_find_by_fingerprint(first_fingerprint, NULL));
}

static void test_contact_nvs_failure_rolls_back_and_retry_succeeds(void)
{
    mock_nvs_reset();
    assert(d1l_contact_store_init() == ESP_OK);
    d1l_node_entry_t heard = make_heard_node("Original", "chat");
    assert(d1l_contact_store_upsert_from_node("aaaaaaaaaaaaaaaa", "Original",
                                               &heard) == ESP_OK);
    d1l_contact_entry_t baseline = {0};
    assert(d1l_contact_store_find_by_fingerprint("aaaaaaaaaaaaaaaa", &baseline));
    const d1l_contact_store_stats_t baseline_stats = d1l_contact_store_stats();

    mock_timer_set_us(4000000);
    mock_nvs_fail_next_set(ESP_FAIL);
    assert(d1l_contact_store_rename("aaaaaaaaaaaaaaaa", "Changed", NULL) ==
           ESP_FAIL);
    d1l_contact_entry_t after = {0};
    assert(d1l_contact_store_find_by_fingerprint("aaaaaaaaaaaaaaaa", &after));
    assert(memcmp(&after, &baseline, sizeof(after)) == 0);
    assert_contact_stats_equal(d1l_contact_store_stats(), baseline_stats);

    assert(d1l_contact_store_init() == ESP_OK);
    assert(d1l_contact_store_find_by_fingerprint("aaaaaaaaaaaaaaaa", &after));
    assert(memcmp(&after, &baseline, sizeof(after)) == 0);
    assert(d1l_contact_store_rename("aaaaaaaaaaaaaaaa", "Changed", &after) ==
           ESP_OK);
    assert(strcmp(after.alias, "Changed") == 0);

    const d1l_contact_store_stats_t before_new = d1l_contact_store_stats();
    d1l_node_entry_t second = make_heard_node("Second", "room");
    mock_nvs_fail_next_set(ESP_FAIL);
    assert(d1l_contact_store_upsert_from_node("bbbbbbbbbbbbbbbb", "Second",
                                               &second) == ESP_FAIL);
    assert(!d1l_contact_store_find_by_fingerprint("bbbbbbbbbbbbbbbb", NULL));
    assert_contact_stats_equal(d1l_contact_store_stats(), before_new);
    assert(d1l_contact_store_upsert_from_node("bbbbbbbbbbbbbbbb", "Second",
                                               &second) == ESP_OK);
    assert(d1l_contact_store_find_by_fingerprint("bbbbbbbbbbbbbbbb", &after));
    assert(strcmp(after.type, "room") == 0);
}

static void make_contact_uri(char out[D1L_CONTACT_EXPORT_URI_LEN],
                             const char *name, const char *key,
                             unsigned type_id)
{
    const int written = snprintf(
        out, D1L_CONTACT_EXPORT_URI_LEN,
        "meshcore://contact/add?name=%s&public_key=%s&type=%u", name, key,
        type_id);
    assert(written > 0 && written < (int)D1L_CONTACT_EXPORT_URI_LEN);
}

static void test_uri_import_promotes_placeholder_and_preserves_preferences(void)
{
    mock_nvs_reset();
    assert(d1l_contact_store_init() == ESP_OK);
    char key[D1L_NODE_PUBLIC_KEY_HEX_LEN] = {0};
    char fingerprint[D1L_NODE_FINGERPRINT_LEN] = {0};
    make_public_key(key, 0x61U);
    fingerprint_from_key(fingerprint, key);

    d1l_node_entry_t heard = make_verified_node(key, "Observed Name", "chat");
    assert(d1l_contact_store_upsert_from_node(
               fingerprint, "Pinned Alias", &heard) == ESP_OK);
    assert(d1l_contact_store_set_flags(fingerprint, true, true, NULL) == ESP_OK);
    const uint8_t path[] = {0x11U, 0x22U};
    assert(d1l_contact_store_update_path(fingerprint, path, 2U) == ESP_OK);

    d1l_contact_entry_t contact = {0};
    assert(d1l_contact_store_find_by_fingerprint(fingerprint, &contact));
    assert(contact.public_key_hex[0] == '\0');
    assert(contact.verification_source == D1L_CONTACT_VERIFICATION_NONE);
    assert(!d1l_contact_store_is_canonical(&contact));
    assert(!d1l_contact_store_can_dm(&contact));
    char exported[D1L_CONTACT_EXPORT_URI_LEN] = "dirty";
    assert(d1l_contact_store_export_uri(&contact, exported,
                                        sizeof(exported)) ==
           ESP_ERR_INVALID_STATE);
    assert(exported[0] == '\0');

    char uri[D1L_CONTACT_EXPORT_URI_LEN] = {0};
    make_contact_uri(uri, "Imported+Name", key, 1U);
    d1l_contact_import_result_t result = D1L_CONTACT_IMPORT_NONE;
    assert(d1l_contact_store_import_uri(uri, strlen(uri), &result, &contact) ==
           ESP_OK);
    assert(result == D1L_CONTACT_IMPORT_PROMOTED_PLACEHOLDER);
    assert(strcmp(contact.public_key_hex, key) == 0);
    assert(strcmp(contact.alias, "Pinned Alias") == 0);
    assert(strcmp(contact.type, "chat") == 0);
    assert(contact.favorite && contact.muted);
    assert(contact.out_path_valid && contact.out_path_len == 2U);
    assert(memcmp(contact.out_path, path, sizeof(path)) == 0);
    assert(contact.verification_source ==
           D1L_CONTACT_VERIFICATION_URI_IMPORT);
    assert(contact.signed_advert_timestamp == 0U);
    assert(contact.last_heard_ms == 0U);
    assert(d1l_contact_store_is_canonical(&contact));
    assert(d1l_contact_store_can_dm(&contact));
    assert(!d1l_contact_store_can_admin(&contact));

    assert(d1l_contact_store_init() == ESP_OK);
    assert(d1l_contact_store_find_by_public_key(key, &contact));
    assert(contact.verification_source ==
           D1L_CONTACT_VERIFICATION_URI_IMPORT);
    assert(strcmp(contact.alias, "Pinned Alias") == 0);

    const d1l_contact_store_stats_t before_duplicate =
        d1l_contact_store_stats();
    result = D1L_CONTACT_IMPORT_NONE;
    assert(d1l_contact_store_import_uri(uri, strlen(uri), &result, &contact) ==
           ESP_OK);
    assert(result == D1L_CONTACT_IMPORT_UPDATED);
    assert(d1l_contact_store_stats().count == 1U);
    assert_contact_stats_equal(d1l_contact_store_stats(), before_duplicate);
    assert(strcmp(contact.alias, "Pinned Alias") == 0);
}

static void test_uri_import_conflicts_and_nvs_failure_are_non_mutating(void)
{
    mock_nvs_reset();
    assert(d1l_contact_store_init() == ESP_OK);
    char key[D1L_NODE_PUBLIC_KEY_HEX_LEN] = {0};
    char fingerprint[D1L_NODE_FINGERPRINT_LEN] = {0};
    make_public_key(key, 0x71U);
    fingerprint_from_key(fingerprint, key);
    char uri[D1L_CONTACT_EXPORT_URI_LEN] = {0};
    make_contact_uri(uri, "Canonical", key, 2U);

    d1l_contact_import_result_t result = D1L_CONTACT_IMPORT_NONE;
    d1l_contact_entry_t baseline = {0};
    assert(d1l_contact_store_import_uri(uri, strlen(uri), &result, &baseline) ==
           ESP_OK);
    assert(result == D1L_CONTACT_IMPORT_CREATED);
    assert(d1l_contact_store_can_admin(&baseline));
    const d1l_contact_store_stats_t baseline_stats = d1l_contact_store_stats();

    make_contact_uri(uri, "Conflicting", key, 1U);
    result = D1L_CONTACT_IMPORT_NONE;
    assert(d1l_contact_store_import_uri(uri, strlen(uri), &result, NULL) ==
           ESP_ERR_INVALID_STATE);
    assert(result == D1L_CONTACT_IMPORT_ROLE_CONFLICT);
    d1l_contact_entry_t after = {0};
    assert(d1l_contact_store_find_by_fingerprint(fingerprint, &after));
    assert(memcmp(&after, &baseline, sizeof(after)) == 0);
    assert_contact_stats_equal(d1l_contact_store_stats(), baseline_stats);

    char colliding_key[D1L_NODE_PUBLIC_KEY_HEX_LEN] = {0};
    copy_field(colliding_key, sizeof(colliding_key), key);
    colliding_key[D1L_NODE_PUBLIC_KEY_HEX_LEN - 2U] =
        colliding_key[D1L_NODE_PUBLIC_KEY_HEX_LEN - 2U] == 'f' ? 'e' : 'f';
    make_contact_uri(uri, "Collision", colliding_key, 2U);
    result = D1L_CONTACT_IMPORT_NONE;
    assert(d1l_contact_store_import_uri(uri, strlen(uri), &result, NULL) ==
           ESP_ERR_INVALID_STATE);
    assert(result == D1L_CONTACT_IMPORT_COLLISION);
    assert_contact_stats_equal(d1l_contact_store_stats(), baseline_stats);

    char retry_key[D1L_NODE_PUBLIC_KEY_HEX_LEN] = {0};
    make_public_key(retry_key, 0x72U);
    make_contact_uri(uri, "Retry", retry_key, 4U);
    mock_nvs_fail_next_set(ESP_FAIL);
    result = D1L_CONTACT_IMPORT_CREATED;
    assert(d1l_contact_store_import_uri(uri, strlen(uri), &result, NULL) ==
           ESP_FAIL);
    assert(result == D1L_CONTACT_IMPORT_NONE);
    assert(!d1l_contact_store_find_by_public_key(retry_key, NULL));
    assert_contact_stats_equal(d1l_contact_store_stats(), baseline_stats);
    assert(d1l_contact_store_init() == ESP_OK);
    result = D1L_CONTACT_IMPORT_NONE;
    assert(d1l_contact_store_import_uri(uri, strlen(uri), &result, &after) ==
           ESP_OK);
    assert(result == D1L_CONTACT_IMPORT_CREATED);
    assert(strcmp(after.type, "sensor") == 0);
}

static void test_uri_import_preserves_official_names(void)
{
    mock_nvs_reset();
    assert(d1l_contact_store_init() == ESP_OK);

    char key[D1L_NODE_PUBLIC_KEY_HEX_LEN] = {0};
    make_public_key(key, 0x81U);
    char uri[D1L_CONTACT_EXPORT_URI_LEN] = {0};
    make_contact_uri(uri, "1234567890123456789012345678901", key, 1U);
    d1l_contact_import_result_t result = D1L_CONTACT_IMPORT_NONE;
    d1l_contact_entry_t contact = {0};
    assert(d1l_contact_store_import_uri(uri, strlen(uri), &result, &contact) ==
           ESP_OK);
    assert(result == D1L_CONTACT_IMPORT_CREATED);
    assert(strcmp(contact.alias, "1234567890123456789012345678901") == 0);
    char exported[D1L_CONTACT_EXPORT_URI_LEN] = {0};
    assert(d1l_contact_store_export_uri(&contact, exported,
                                        sizeof(exported)) == ESP_OK);
    assert(strcmp(exported, uri) == 0);

    make_public_key(key, 0x82U);
    make_contact_uri(uri, "Caf%C3%A9+%E6%9D%B1%E4%BA%AC", key, 1U);
    result = D1L_CONTACT_IMPORT_NONE;
    memset(&contact, 0, sizeof(contact));
    assert(d1l_contact_store_import_uri(uri, strlen(uri), &result, &contact) ==
           ESP_OK);
    assert(strcmp(contact.alias,
                  "Caf\xc3\xa9 \xe6\x9d\xb1\xe4\xba\xac") == 0);
    memset(exported, 0, sizeof(exported));
    assert(d1l_contact_store_export_uri(&contact, exported,
                                        sizeof(exported)) == ESP_OK);
    assert(strcmp(exported, uri) == 0);

    make_public_key(key, 0x83U);
    make_contact_uri(uri,
                     "A%22%2C%22x%22%3Atrue%2C%22s%22%3A%22%5CZ",
                     key, 1U);
    result = D1L_CONTACT_IMPORT_NONE;
    memset(&contact, 0, sizeof(contact));
    assert(d1l_contact_store_import_uri(uri, strlen(uri), &result, &contact) ==
           ESP_OK);
    assert(strcmp(contact.alias, "A\",\"x\":true,\"s\":\"\\Z") == 0);
    memset(exported, 0, sizeof(exported));
    assert(d1l_contact_store_export_uri(&contact, exported,
                                        sizeof(exported)) == ESP_OK);
    assert(strcmp(exported, uri) == 0);
}

static void test_full_key_chat_without_provenance_is_not_dm_capable(void)
{
    d1l_contact_entry_t contact = {0};
    copy_field(contact.fingerprint, sizeof(contact.fingerprint),
               "0123456789abcdef");
    copy_field(contact.public_key_hex, sizeof(contact.public_key_hex), KEY_HEX);
    copy_field(contact.alias, sizeof(contact.alias), "Unverified");
    copy_field(contact.type, sizeof(contact.type), "chat");
    contact.verification_source = D1L_CONTACT_VERIFICATION_NONE;

    assert(d1l_contact_store_has_export_key(&contact));
    assert(!d1l_contact_store_is_canonical(&contact));
    assert(!d1l_contact_store_can_dm(&contact));
    assert(!d1l_contact_store_can_admin(&contact));
    char uri[D1L_CONTACT_EXPORT_URI_LEN] = "dirty";
    assert(d1l_contact_store_export_uri(&contact, uri, sizeof(uri)) ==
           ESP_ERR_INVALID_STATE);
    assert(uri[0] == '\0');
}

static void test_contact_export_role_mapping(void)
{
    assert(d1l_contact_store_meshcore_type_id("chat") == 1U);
    assert(d1l_contact_store_meshcore_type_id("repeater") == 2U);
    assert(d1l_contact_store_meshcore_type_id("room") == 3U);
    assert(d1l_contact_store_meshcore_type_id("sensor") == 4U);
    assert(d1l_contact_store_meshcore_type_id("unknown") == 0U);
    assert(d1l_contact_store_meshcore_type_id("node") == 0U);
    assert(d1l_contact_store_meshcore_type_id(NULL) == 0U);

    static const char *types[] = {"chat", "repeater", "room", "sensor"};
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); ++i) {
        d1l_contact_entry_t contact = {0};
        copy_field(contact.public_key_hex, sizeof(contact.public_key_hex), KEY_HEX);
        copy_field(contact.fingerprint, sizeof(contact.fingerprint),
                   "0123456789abcdef");
        copy_field(contact.alias, sizeof(contact.alias), "North Room");
        copy_field(contact.type, sizeof(contact.type), types[i]);
        contact.verification_source = D1L_CONTACT_VERIFICATION_URI_IMPORT;
        char uri[D1L_CONTACT_EXPORT_URI_LEN] = {0};
        assert(d1l_contact_store_export_uri(&contact, uri, sizeof(uri)) == ESP_OK);
        assert(strstr(uri, "meshcore://contact/add?name=North+Room&public_key=") == uri);
        char expected_type[16] = {0};
        (void)snprintf(expected_type, sizeof(expected_type), "&type=%u",
                       (unsigned)(i + 1U));
        assert(strstr(uri, expected_type) != NULL);
    }

    d1l_contact_entry_t unknown = {0};
    copy_field(unknown.public_key_hex, sizeof(unknown.public_key_hex), KEY_HEX);
    copy_field(unknown.fingerprint, sizeof(unknown.fingerprint),
               "0123456789abcdef");
    copy_field(unknown.alias, sizeof(unknown.alias), "Unknown");
    copy_field(unknown.type, sizeof(unknown.type), "unknown");
    unknown.verification_source = D1L_CONTACT_VERIFICATION_URI_IMPORT;
    char uri[D1L_CONTACT_EXPORT_URI_LEN] = "not-empty";
    assert(d1l_contact_store_export_uri(&unknown, uri, sizeof(uri)) ==
           ESP_ERR_INVALID_STATE);
    assert(uri[0] == '\0');

    unknown.public_key_hex[0] = 'z';
    copy_field(unknown.type, sizeof(unknown.type), "chat");
    assert(d1l_contact_store_export_uri(&unknown, uri, sizeof(uri)) ==
           ESP_ERR_INVALID_STATE);
    assert(uri[0] == '\0');
}

int main(void)
{
    test_node_v3_to_v4_migration();
    test_contact_v3_to_v6_migration();
    test_contact_v4_to_v6_migration_is_truthful();
    test_contact_v5_to_v6_migration_preserves_provenance();
    test_stale_advert_and_location_preservation();
    test_node_nvs_failure_rolls_back_and_retry_succeeds();
    test_verified_advert_retry_and_prefix_collision_preserve_node();
    test_contact_nvs_failure_rolls_back_and_retry_succeeds();
    test_verified_contact_create_reload_update_preserves_preferences();
    test_verified_contact_promotes_placeholder_without_losing_preferences();
    test_verified_contact_refuses_collision_and_rolls_back_nvs_failure();
    test_verified_contact_full_store_never_evicts();
    test_favorite_placeholders_are_never_evicted();
    test_uri_import_promotes_placeholder_and_preserves_preferences();
    test_uri_import_conflicts_and_nvs_failure_are_non_mutating();
    test_uri_import_preserves_official_names();
    test_full_key_chat_without_provenance_is_not_dm_capable();
    test_contact_export_role_mapping();
    puts("native node/contact store behavior: ok");
    return 0;
}
