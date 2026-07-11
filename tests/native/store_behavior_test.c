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
#define LEGACY_TYPE_LEN 8U

static const char KEY_HEX[] =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

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
    char alias[D1L_CONTACT_ALIAS_LEN];
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
    uint32_t schema;
    uint32_t next_seq;
    uint32_t total_written;
    uint32_t dropped_oldest;
    uint32_t count;
    d1l_contact_entry_t entries[D1L_CONTACT_STORE_CAPACITY];
} contact_blob_v4_t;

_Static_assert(sizeof(node_entry_v3_t) == 148U,
               "native node v3 fixture no longer matches production migration layout");
_Static_assert(sizeof(contact_entry_v3_t) == 236U,
               "native contact v3 fixture no longer matches production migration layout");
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

static void test_contact_v3_to_v4_migration(void)
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

    contact_blob_v4_t current = {0};
    assert(mock_nvs_copy_blob(CONTACT_NAMESPACE, CONTACT_KEY, &current,
                              sizeof(current)) == sizeof(current));
    assert(current.schema == 4U);
    assert(current.count == 2U);
    assert(d1l_contact_store_init() == ESP_OK);
    assert(d1l_contact_store_find_by_fingerprint("4444444444444444", &contact));
    assert(strcmp(contact.type, "repeater") == 0);
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
        copy_field(contact.alias, sizeof(contact.alias), "North Room");
        copy_field(contact.type, sizeof(contact.type), types[i]);
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
    copy_field(unknown.alias, sizeof(unknown.alias), "Unknown");
    copy_field(unknown.type, sizeof(unknown.type), "unknown");
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
    test_contact_v3_to_v4_migration();
    test_stale_advert_and_location_preservation();
    test_node_nvs_failure_rolls_back_and_retry_succeeds();
    test_contact_nvs_failure_rolls_back_and_retry_succeeds();
    test_contact_export_role_mapping();
    puts("native node/contact store behavior: ok");
    return 0;
}
