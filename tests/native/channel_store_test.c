#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mesh/channel_store.h"
#include "mock_esp_nvs.h"

#define CHANNEL_NAMESPACE "d1l_channels"
#define CHANNEL_KEY "channels"

static const uint8_t PUBLIC_SECRET[D1L_CHANNEL_SECRET_MAX_LEN] = {
    0x8b, 0x33, 0x87, 0xe9, 0xc5, 0xcd, 0xea, 0x6a,
    0xc9, 0xe5, 0xed, 0xba, 0xa1, 0x15, 0xcd, 0x72,
};

typedef struct {
    uint64_t channel_id;
    uint32_t created_ms;
    uint32_t updated_ms;
    uint32_t newest_message_seq;
    uint32_t read_through_seq;
    uint32_t unread_count;
    char name[D1L_CHANNEL_NAME_LEN];
    uint8_t secret[D1L_CHANNEL_SECRET_MAX_LEN];
    uint8_t secret_len;
    uint8_t channel_hash;
    uint8_t enabled;
    uint8_t is_default;
    uint8_t reserved[7];
} channel_entry_v1_t;

typedef struct {
    uint32_t schema;
    uint32_t count;
    uint64_t next_channel_id;
    channel_entry_v1_t entries[D1L_CHANNEL_STORE_CAPACITY];
} channel_blob_v1_t;

_Static_assert(sizeof(channel_entry_v1_t) == 104U,
               "native v1 fixture entry layout changed");
_Static_assert(sizeof(channel_blob_v1_t) == 848U,
               "native v1 fixture blob layout changed");

static void fill_secret(uint8_t secret[D1L_CHANNEL_SECRET_MAX_LEN],
                        uint8_t first, uint8_t salt)
{
    memset(secret, 0, D1L_CHANNEL_SECRET_MAX_LEN);
    secret[0] = first;
    for (size_t i = 1U; i < D1L_CHANNEL_SECRET_128_LEN; ++i) {
        secret[i] = (uint8_t)(salt + (uint8_t)i);
    }
}

static d1l_channel_info_t find_channel(uint64_t channel_id)
{
    d1l_channel_info_t info;
    memset(&info, 0, sizeof(info));
    assert(d1l_channel_store_find(channel_id, &info));
    return info;
}

static void put_u32_le(uint8_t *data, size_t offset, uint32_t value)
{
    data[offset] = (uint8_t)value;
    data[offset + 1U] = (uint8_t)(value >> 8U);
    data[offset + 2U] = (uint8_t)(value >> 16U);
    data[offset + 3U] = (uint8_t)(value >> 24U);
}

static void assert_blob_rejected_and_preserved(const uint8_t *blob,
                                               size_t blob_len)
{
    uint8_t preserved[1024];
    memset(preserved, 0, sizeof(preserved));
    assert(blob_len <= sizeof(preserved));
    assert(mock_nvs_seed_blob(CHANNEL_NAMESPACE, CHANNEL_KEY, blob, blob_len));
    assert(d1l_channel_store_init() == ESP_ERR_INVALID_STATE);
    assert(mock_nvs_copy_blob(CHANNEL_NAMESPACE, CHANNEL_KEY, preserved,
                              sizeof(preserved)) == blob_len);
    assert(memcmp(blob, preserved, blob_len) == 0);
}

static void assert_public_default(void)
{
    d1l_channel_info_t info = find_channel(D1L_CHANNEL_PUBLIC_ID);
    assert(strcmp(info.name, "Public") == 0);
    assert(info.channel_id == D1L_CHANNEL_PUBLIC_ID);
    assert(info.history_key != 0U);
    assert(info.source == D1L_CHANNEL_SOURCE_BUILTIN);
    assert(info.enabled);
    assert(info.is_default);
}

static void test_default_add_persist_and_unread(void)
{
    mock_nvs_reset();
    mock_timer_set_us(1000000);
    assert(d1l_channel_store_init() == ESP_OK);
    d1l_channel_store_stats_t stats = d1l_channel_store_stats();
    assert(stats.loaded);
    assert(stats.load_status == ESP_OK);
    assert(stats.count == 1U);
    assert(stats.capacity == D1L_CHANNEL_STORE_CAPACITY);
    assert_public_default();

    uint8_t public_hash_16 = 0U;
    uint8_t public_hash_32 = 0U;
    assert(d1l_channel_store_protocol_hash(
               PUBLIC_SECRET, D1L_CHANNEL_SECRET_128_LEN, &public_hash_16) ==
           ESP_OK);
    assert(d1l_channel_store_protocol_hash(
               PUBLIC_SECRET, D1L_CHANNEL_SECRET_256_LEN, &public_hash_32) ==
           ESP_OK);
    assert(public_hash_16 == public_hash_32);

    char public_uri[D1L_CHANNEL_SHARE_URI_LEN] = {0};
    assert(d1l_channel_store_export_share_uri(
               D1L_CHANNEL_PUBLIC_ID, public_uri, sizeof(public_uri)) == ESP_OK);
    assert(strcmp(public_uri,
                  "meshcore://channel/add?name=Public&secret="
                  "8b3387e9c5cdea6ac9e5edbaa115cd72") == 0);

    uint8_t alpha_secret[D1L_CHANNEL_SECRET_MAX_LEN];
    fill_secret(alpha_secret, 0x11U, 0x20U);
    d1l_channel_mutation_result_t result = D1L_CHANNEL_MUTATION_NONE;
    d1l_channel_info_t alpha;
    memset(&alpha, 0, sizeof(alpha));
    mock_timer_set_us(2000000);
    assert(d1l_channel_store_add("Alpha", alpha_secret,
                                 D1L_CHANNEL_SECRET_128_LEN, true, true,
                                 &result, &alpha) == ESP_OK);
    assert(result == D1L_CHANNEL_MUTATION_CREATED);
    assert(alpha.channel_id > D1L_CHANNEL_PUBLIC_ID);
    assert(alpha.history_key != 0U);
    assert(alpha.history_key != alpha.channel_id);
    assert(alpha.source == D1L_CHANNEL_SOURCE_MANUAL);
    assert(alpha.is_default);
    assert(!find_channel(D1L_CHANNEL_PUBLIC_ID).is_default);

    assert(d1l_channel_store_note_message(alpha.channel_id, 10U, true) ==
           ESP_OK);
    assert(d1l_channel_store_note_message(alpha.channel_id, 10U, true) ==
           ESP_OK);
    assert(d1l_channel_store_note_message(alpha.channel_id, 9U, true) ==
           ESP_OK);
    alpha = find_channel(alpha.channel_id);
    assert(alpha.newest_message_seq == 10U);
    assert(alpha.read_through_seq == 0U);
    assert(alpha.unread_count == 1U);
    assert(d1l_channel_store_note_message(alpha.channel_id, 11U, false) ==
           ESP_OK);
    alpha = find_channel(alpha.channel_id);
    assert(alpha.newest_message_seq == 11U);
    assert(alpha.unread_count == 1U);

    const uint64_t alpha_id = alpha.channel_id;
    const uint64_t alpha_history_key = alpha.history_key;
    assert(d1l_channel_store_init() == ESP_OK);
    alpha = find_channel(alpha_id);
    assert(strcmp(alpha.name, "Alpha") == 0);
    assert(alpha.history_key == alpha_history_key);
    assert(alpha.is_default);
    assert(alpha.newest_message_seq == 11U);
    assert(alpha.unread_count == 1U);
    assert(d1l_channel_store_mark_all_read(alpha.channel_id) == ESP_OK);
    alpha = find_channel(alpha.channel_id);
    assert(alpha.read_through_seq == 11U);
    assert(alpha.unread_count == 0U);
}

static void test_collisions_capacity_defaults_and_rollback(void)
{
    mock_nvs_reset();
    assert(d1l_channel_store_init() == ESP_OK);

    uint8_t secret_a[D1L_CHANNEL_SECRET_MAX_LEN];
    uint8_t secret_b[D1L_CHANNEL_SECRET_MAX_LEN];
    uint8_t secret_c[D1L_CHANNEL_SECRET_MAX_LEN];
    fill_secret(secret_a, 0x55U, 0x10U);
    fill_secret(secret_b, 0x55U, 0x80U);
    fill_secret(secret_c, 0x66U, 0x40U);

    d1l_channel_mutation_result_t result = D1L_CHANNEL_MUTATION_NONE;
    d1l_channel_info_t info;
    memset(&info, 0, sizeof(info));
    assert(d1l_channel_store_add("One", secret_a,
                                 D1L_CHANNEL_SECRET_128_LEN, true, false,
                                 &result, &info) == ESP_OK);
    const uint64_t one_id = info.channel_id;

    assert(d1l_channel_store_add("one", secret_c,
                                 D1L_CHANNEL_SECRET_128_LEN, true, false,
                                 &result, &info) == ESP_ERR_INVALID_STATE);
    assert(result == D1L_CHANNEL_MUTATION_NAME_COLLISION);
    assert(d1l_channel_store_add("Other", secret_a,
                                 D1L_CHANNEL_SECRET_128_LEN, true, false,
                                 &result, &info) == ESP_ERR_INVALID_STATE);
    assert(result == D1L_CHANNEL_MUTATION_SECRET_COLLISION);

    assert(d1l_channel_store_add("Two", secret_b,
                                 D1L_CHANNEL_SECRET_128_LEN, true, false,
                                 &result, &info) == ESP_OK);
    d1l_channel_protocol_key_t one_key;
    memset(&one_key, 0, sizeof(one_key));
    assert(d1l_channel_store_copy_protocol_key(one_id, &one_key) == ESP_OK);
    d1l_channel_protocol_key_t matches[D1L_CHANNEL_STORE_CAPACITY];
    memset(matches, 0, sizeof(matches));
    assert(d1l_channel_store_copy_hash_matches(
               one_key.channel_hash, matches, D1L_CHANNEL_STORE_CAPACITY) ==
           2U);
    d1l_channel_protocol_key_t unique;
    memset(&unique, 0, sizeof(unique));
    assert(d1l_channel_store_find_unique_hash(one_key.channel_hash, &unique) ==
           ESP_ERR_INVALID_STATE);

    assert(d1l_channel_store_update(one_id, "One renamed", true, true,
                                    &result, &info) == ESP_OK);
    assert(info.is_default);
    assert(d1l_channel_store_update(one_id, "One renamed", false, false,
                                    &result, &info) == ESP_OK);
    assert(!info.enabled);
    assert_public_default();
    memset(&unique, 0, sizeof(unique));
    assert(d1l_channel_store_copy_protocol_key(one_id, &unique) ==
           ESP_ERR_INVALID_STATE);

    const d1l_channel_store_stats_t before = d1l_channel_store_stats();
    mock_nvs_fail_next_set(ESP_FAIL);
    assert(d1l_channel_store_add("Rollback", secret_c,
                                 D1L_CHANNEL_SECRET_128_LEN, true, false,
                                 &result, &info) == ESP_FAIL);
    const d1l_channel_store_stats_t after = d1l_channel_store_stats();
    assert(after.count == before.count);
    assert(after.next_channel_id == before.next_channel_id);
    assert(after.revision == before.revision);

    for (uint8_t i = 0U;
         d1l_channel_store_stats().count < D1L_CHANNEL_STORE_CAPACITY; ++i) {
        uint8_t secret[D1L_CHANNEL_SECRET_MAX_LEN];
        fill_secret(secret, (uint8_t)(0x80U + i), i);
        char name[D1L_CHANNEL_NAME_LEN];
        (void)snprintf(name, sizeof(name), "Fill %u", (unsigned)i);
        assert(d1l_channel_store_add(name, secret,
                                     D1L_CHANNEL_SECRET_128_LEN, true, false,
                                     &result, &info) == ESP_OK);
    }
    uint8_t overflow[D1L_CHANNEL_SECRET_MAX_LEN];
    fill_secret(overflow, 0xfeU, 0xefU);
    assert(d1l_channel_store_add("Overflow", overflow,
                                 D1L_CHANNEL_SECRET_128_LEN, true, false,
                                 &result, &info) == ESP_ERR_NO_MEM);
    assert(result == D1L_CHANNEL_MUTATION_FULL);
    assert(d1l_channel_store_stats().count == D1L_CHANNEL_STORE_CAPACITY);

    assert(d1l_channel_store_remove(D1L_CHANNEL_PUBLIC_ID, &result, &info) ==
           ESP_ERR_NOT_SUPPORTED);
    assert(result == D1L_CHANNEL_MUTATION_PROTECTED);
    assert(d1l_channel_store_remove(one_id, &result, &info) == ESP_OK);
    assert(result == D1L_CHANNEL_MUTATION_REMOVED);
    assert(strcmp(info.name, "One renamed") == 0);
    assert(!d1l_channel_store_find(one_id, &info));
}

static void test_uri_import_and_explicit_secret_export(void)
{
    mock_nvs_reset();
    assert(d1l_channel_store_init() == ESP_OK);
    static const char URI[] =
        "meshcore://channel/add?secret="
        "00112233445566778899aabbccddeeff"
        "&name=Field+Ops";
    d1l_channel_mutation_result_t result = D1L_CHANNEL_MUTATION_NONE;
    d1l_channel_info_t info;
    memset(&info, 0, sizeof(info));
    mock_timer_set_us(4000000);
    assert(d1l_channel_store_import_uri(URI, sizeof(URI) - 1U, &result,
                                        &info) == ESP_OK);
    assert(result == D1L_CHANNEL_MUTATION_CREATED);
    assert(strcmp(info.name, "Field Ops") == 0);
    assert(info.source == D1L_CHANNEL_SOURCE_URI_IMPORT);
    assert(info.imported_at_ms == 4000U);
    const uint64_t id = info.channel_id;

    char share[D1L_CHANNEL_SHARE_URI_LEN] = {0};
    assert(d1l_channel_store_export_share_uri(id, share, sizeof(share)) ==
           ESP_OK);
    assert(strstr(share, "name=Field+Ops") != NULL);
    assert(strstr(share,
                  "secret=00112233445566778899aabbccddeeff") != NULL);

    static const char RENAME_URI[] =
        "meshcore://channel/add?name=Ops%20Renamed&secret="
        "00112233445566778899aabbccddeeff";
    assert(d1l_channel_store_import_uri(RENAME_URI,
                                        sizeof(RENAME_URI) - 1U, &result,
                                        &info) == ESP_OK);
    assert(result == D1L_CHANNEL_MUTATION_UPDATED);
    assert(info.channel_id == id);
    assert(info.history_key != 0U);
    assert(strcmp(info.name, "Ops Renamed") == 0);

    static const char BAD_URI[] =
        "meshcore://channel/add?name=A&name=B&secret="
        "00112233445566778899aabbccddeeff";
    assert(d1l_channel_store_import_uri(BAD_URI, sizeof(BAD_URI) - 1U,
                                        &result, &info) ==
           ESP_ERR_INVALID_ARG);

    static const char SECRET_FIRST_MALFORMED[] =
        "meshcore://channel/add?secret="
        "00112233445566778899aabbccddeeff&broken";
    static const char SECRET_FIRST_DUPLICATE_NAME[] =
        "meshcore://channel/add?secret="
        "00112233445566778899aabbccddeeff&name=A&name=B";
    static const char SECRET_FIRST_TRAILING_EMPTY[] =
        "meshcore://channel/add?secret="
        "00112233445566778899aabbccddeeff&name=A&";
    assert(d1l_channel_store_import_uri(
               SECRET_FIRST_MALFORMED,
               sizeof(SECRET_FIRST_MALFORMED) - 1U, &result, &info) ==
           ESP_ERR_INVALID_ARG);
    assert(d1l_channel_store_import_uri(
               SECRET_FIRST_DUPLICATE_NAME,
               sizeof(SECRET_FIRST_DUPLICATE_NAME) - 1U, &result, &info) ==
           ESP_ERR_INVALID_ARG);
    assert(d1l_channel_store_import_uri(
               SECRET_FIRST_TRAILING_EMPTY,
               sizeof(SECRET_FIRST_TRAILING_EMPTY) - 1U, &result, &info) ==
           ESP_ERR_INVALID_ARG);

    static const char NON_OFFICIAL_32_BYTE_URI[] =
        "meshcore://channel/add?name=TooLong&secret="
        "00112233445566778899aabbccddeeff"
        "102132435465768798a9bacbdcedfe0f";
    assert(d1l_channel_store_import_uri(
               NON_OFFICIAL_32_BYTE_URI,
               sizeof(NON_OFFICIAL_32_BYTE_URI) - 1U, &result, &info) ==
           ESP_ERR_INVALID_ARG);

    uint8_t internal_secret[D1L_CHANNEL_SECRET_MAX_LEN];
    for (size_t i = 0U; i < sizeof(internal_secret); ++i) {
        internal_secret[i] = (uint8_t)(i + 1U);
    }
    assert(d1l_channel_store_add(
               "Internal 256", internal_secret, D1L_CHANNEL_SECRET_256_LEN,
               true, false, &result, &info) == ESP_OK);
    const uint64_t internal_id = info.channel_id;
    d1l_channel_protocol_key_t internal_key;
    memset(&internal_key, 0, sizeof(internal_key));
    assert(d1l_channel_store_copy_protocol_key(internal_id, &internal_key) ==
           ESP_OK);
    assert(internal_key.secret_len == D1L_CHANNEL_SECRET_256_LEN);
    share[0] = 'x';
    assert(d1l_channel_store_export_share_uri(internal_id, share,
                                               sizeof(share)) ==
           ESP_ERR_NOT_SUPPORTED);
    assert(share[0] == '\0');

    static const char PUBLIC_RENAME[] =
        "meshcore://channel/add?name=NotPublic&secret="
        "8b3387e9c5cdea6ac9e5edbaa115cd72";
    assert(d1l_channel_store_import_uri(
               PUBLIC_RENAME, sizeof(PUBLIC_RENAME) - 1U, &result, &info) ==
           ESP_ERR_NOT_SUPPORTED);
    assert(result == D1L_CHANNEL_MUTATION_PROTECTED);
}

static void test_reset_changes_lineage_and_never_reuses_ids(void)
{
    mock_nvs_reset();
    assert(d1l_channel_store_init() == ESP_OK);
    const d1l_channel_info_t public_before =
        find_channel(D1L_CHANNEL_PUBLIC_ID);

    uint8_t secret[D1L_CHANNEL_SECRET_MAX_LEN];
    fill_secret(secret, 0x42U, 0x31U);
    d1l_channel_mutation_result_t result = D1L_CHANNEL_MUTATION_NONE;
    d1l_channel_info_t first;
    memset(&first, 0, sizeof(first));
    assert(d1l_channel_store_add("Reset target", secret,
                                 D1L_CHANNEL_SECRET_128_LEN, true, false,
                                 &result, &first) == ESP_OK);
    const uint64_t first_id = first.channel_id;
    const uint64_t first_history = first.history_key;
    const d1l_channel_store_stats_t before = d1l_channel_store_stats();
    assert(before.lineage != 0U);
    assert(before.generation != 0U);

    assert(d1l_channel_store_reset() == ESP_OK);
    const d1l_channel_store_stats_t after_reset = d1l_channel_store_stats();
    const d1l_channel_info_t public_after =
        find_channel(D1L_CHANNEL_PUBLIC_ID);
    assert(after_reset.lineage != before.lineage);
    assert(after_reset.generation != before.generation);
    assert(after_reset.next_channel_id == before.next_channel_id);
    assert(public_after.history_key != public_before.history_key);
    assert(!d1l_channel_store_find(first_id, &first));

    d1l_channel_info_t second;
    memset(&second, 0, sizeof(second));
    assert(d1l_channel_store_add("Reset target", secret,
                                 D1L_CHANNEL_SECRET_128_LEN, true, false,
                                 &result, &second) == ESP_OK);
    assert(second.channel_id != first_id);
    assert(second.history_key != first_history);
    const uint64_t second_id = second.channel_id;
    const uint64_t second_history = second.history_key;
    const d1l_channel_store_stats_t before_reload =
        d1l_channel_store_stats();
    assert(d1l_channel_store_init() == ESP_OK);
    second = find_channel(second_id);
    assert(second.history_key == second_history);
    const d1l_channel_store_stats_t after_reload =
        d1l_channel_store_stats();
    assert(after_reload.lineage == before_reload.lineage);
    assert(after_reload.generation == before_reload.generation);
}

static void build_v1_fixture(channel_blob_v1_t *legacy)
{
    assert(legacy != NULL);
    memset(legacy, 0, sizeof(*legacy));
    legacy->schema = 1U;
    legacy->count = 2U;
    legacy->next_channel_id = 3U;

    channel_entry_v1_t *public_entry = &legacy->entries[0];
    public_entry->channel_id = D1L_CHANNEL_PUBLIC_ID;
    (void)snprintf(public_entry->name, sizeof(public_entry->name), "Public");
    memcpy(public_entry->secret, PUBLIC_SECRET, sizeof(public_entry->secret));
    public_entry->secret_len = D1L_CHANNEL_SECRET_128_LEN;
    assert(d1l_channel_store_protocol_hash(
               public_entry->secret, public_entry->secret_len,
               &public_entry->channel_hash) == ESP_OK);
    public_entry->enabled = 1U;
    public_entry->is_default = 1U;

    channel_entry_v1_t *legacy_user = &legacy->entries[1];
    legacy_user->channel_id = 2U;
    legacy_user->created_ms = 123U;
    legacy_user->updated_ms = 456U;
    legacy_user->newest_message_seq = 7U;
    legacy_user->read_through_seq = 6U;
    legacy_user->unread_count = 1U;
    (void)snprintf(legacy_user->name, sizeof(legacy_user->name), "Legacy");
    fill_secret(legacy_user->secret, 0x77U, 0x12U);
    legacy_user->secret_len = D1L_CHANNEL_SECRET_128_LEN;
    assert(d1l_channel_store_protocol_hash(
               legacy_user->secret, legacy_user->secret_len,
               &legacy_user->channel_hash) == ESP_OK);
    legacy_user->enabled = 1U;
}

static void test_v1_migration_and_corrupt_fail_closed(void)
{
    mock_nvs_reset();
    channel_blob_v1_t legacy;
    build_v1_fixture(&legacy);
    assert(mock_nvs_seed_blob(CHANNEL_NAMESPACE, CHANNEL_KEY, &legacy,
                              sizeof(legacy)));

    assert(d1l_channel_store_init() == ESP_OK);
    d1l_channel_info_t migrated = find_channel(2U);
    assert(strcmp(migrated.name, "Legacy") == 0);
    assert(migrated.history_key != migrated.channel_id);
    assert(migrated.source == D1L_CHANNEL_SOURCE_MIGRATED);
    assert(migrated.newest_message_seq == 7U);
    assert(migrated.read_through_seq == 6U);
    assert(migrated.unread_count == 1U);
    assert(mock_nvs_copy_blob(CHANNEL_NAMESPACE, CHANNEL_KEY, NULL, 0U) ==
           960U);

    uint8_t valid_envelope[960];
    memset(valid_envelope, 0, sizeof(valid_envelope));
    assert(mock_nvs_copy_blob(CHANNEL_NAMESPACE, CHANNEL_KEY, valid_envelope,
                              sizeof(valid_envelope)) ==
           sizeof(valid_envelope));
    assert(valid_envelope[0] == 0x4cU && valid_envelope[1] == 0x4eU &&
           valid_envelope[2] == 0x48U && valid_envelope[3] == 0x43U);

    uint8_t corrupt[960];
    memcpy(corrupt, valid_envelope, sizeof(corrupt));
    corrupt[sizeof(corrupt) - 1U] ^= 0x80U;
    assert_blob_rejected_and_preserved(corrupt, sizeof(corrupt));

    memcpy(corrupt, valid_envelope, sizeof(corrupt));
    put_u32_le(corrupt, 0U, UINT32_C(0x43484e00));
    assert_blob_rejected_and_preserved(corrupt, sizeof(corrupt));

    memcpy(corrupt, valid_envelope, sizeof(corrupt));
    put_u32_le(corrupt, 8U, (uint32_t)sizeof(corrupt) - 1U);
    assert_blob_rejected_and_preserved(corrupt, sizeof(corrupt));

    memcpy(corrupt, valid_envelope, sizeof(corrupt));
    put_u32_le(corrupt, 12U, (uint32_t)sizeof(corrupt) - 25U);
    assert_blob_rejected_and_preserved(corrupt, sizeof(corrupt));

    memcpy(corrupt, valid_envelope, sizeof(corrupt));
    put_u32_le(corrupt, 4U, 3U);
    assert_blob_rejected_and_preserved(corrupt, sizeof(corrupt));

    mock_nvs_reset();
    const uint32_t unknown_schema = 99U;
    assert(mock_nvs_seed_blob(CHANNEL_NAMESPACE, CHANNEL_KEY, &unknown_schema,
                              sizeof(unknown_schema)));
    assert(d1l_channel_store_init() == ESP_ERR_INVALID_STATE);
    d1l_channel_store_stats_t stats = d1l_channel_store_stats();
    assert(!stats.loaded);
    assert(stats.load_status == ESP_ERR_INVALID_STATE);
    assert(mock_nvs_copy_blob(CHANNEL_NAMESPACE, CHANNEL_KEY, NULL, 0U) ==
           sizeof(unknown_schema));
    assert(d1l_channel_store_reset() == ESP_OK);
    assert_public_default();
    assert(mock_nvs_copy_blob(CHANNEL_NAMESPACE, CHANNEL_KEY, NULL, 0U) ==
           960U);
}

static void assert_v1_migration_failure_preserves_nvs(bool fail_persist_open)
{
    mock_nvs_reset();
    channel_blob_v1_t legacy;
    build_v1_fixture(&legacy);
    assert(mock_nvs_seed_blob(CHANNEL_NAMESPACE, CHANNEL_KEY, &legacy,
                              sizeof(legacy)));
    if (fail_persist_open) {
        mock_nvs_fail_open_after(1U, ESP_FAIL);
    } else {
        mock_nvs_fail_next_set(ESP_FAIL);
    }

    assert(d1l_channel_store_init() == ESP_FAIL);
    const d1l_channel_store_stats_t failed = d1l_channel_store_stats();
    assert(!failed.loaded);
    assert(failed.load_status == ESP_FAIL);
    assert(failed.count == 0U);
    assert(failed.lineage == 0U);
    assert(failed.generation == 0U);
    assert(failed.next_channel_id == 0U);

    channel_blob_v1_t preserved;
    memset(&preserved, 0, sizeof(preserved));
    assert(mock_nvs_copy_blob(CHANNEL_NAMESPACE, CHANNEL_KEY, &preserved,
                              sizeof(preserved)) == sizeof(preserved));
    assert(memcmp(&preserved, &legacy, sizeof(legacy)) == 0);

    /* The one-shot fault is consumed and the untouched v1 blob remains
     * independently recoverable on the next explicit initialization. */
    assert(d1l_channel_store_init() == ESP_OK);
    const d1l_channel_info_t migrated = find_channel(2U);
    assert(strcmp(migrated.name, "Legacy") == 0);
}

static void test_v1_migration_failures_clear_unloaded_ram(void)
{
    assert_v1_migration_failure_preserves_nvs(true);
    assert_v1_migration_failure_preserves_nvs(false);
}

int main(void)
{
    test_default_add_persist_and_unread();
    test_collisions_capacity_defaults_and_rollback();
    test_uri_import_and_explicit_secret_export();
    test_reset_changes_lineage_and_never_reuses_ids();
    test_v1_migration_and_corrupt_fail_closed();
    test_v1_migration_failures_clear_unloaded_ram();
    puts("native channel store behavior: ok");
    return 0;
}
