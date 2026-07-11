#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "mesh/route_store.h"
#include "storage/retained_blob_store.h"
#include "mock_esp_nvs.h"

#define LEGACY_KEY "routes"
#define V2_KEY "routes_v2"
#define LEGACY_SCHEMA 1U
#define FULL_SCHEMA 2U
#define FALLBACK_SCHEMA 3U
#define MOCK_BLOB_MAX 8192U

typedef struct {
    bool valid;
    uint8_t data[MOCK_BLOB_MAX];
    size_t len;
} blob_slot_t;

typedef struct {
    uint32_t schema;
    uint32_t next_seq;
    uint32_t total_written;
    uint32_t dropped_oldest;
    uint32_t count;
    d1l_route_entry_t entries[D1L_ROUTE_STORE_CAPACITY];
} legacy_blob_t;

typedef struct {
    uint32_t schema;
    uint32_t epoch;
    uint32_t next_seq;
    uint32_t total_written;
    uint32_t dropped_oldest;
    uint32_t count;
    d1l_route_entry_t entries[D1L_ROUTE_STORE_CAPACITY];
} full_blob_t;

typedef struct {
    uint32_t schema;
    uint32_t epoch;
    uint32_t next_seq;
    uint32_t total_written;
    uint32_t dropped_oldest;
    uint32_t count;
    d1l_route_entry_t entries[D1L_ROUTE_STORE_NVS_FALLBACK_CAPACITY];
} fallback_blob_t;

static bool s_use_sd;
static uint32_t s_backend_generation;
static blob_slot_t s_sd_v2;
static blob_slot_t s_sd_legacy;
static blob_slot_t s_nvs_v2;
static blob_slot_t s_nvs_legacy;
static uint32_t s_sd_write_count;
static uint32_t s_nvs_write_count;
static uint32_t s_sd_read_count;
static uint32_t s_nvs_read_count;
static uint32_t s_sd_erase_count;
static uint32_t s_nvs_erase_count;
static esp_err_t s_fail_next_sd_write;
static esp_err_t s_fail_next_nvs_write;
static esp_err_t s_fail_next_sd_read;
static esp_err_t s_fail_sd_v2_read;
static esp_err_t s_fail_sd_legacy_read;
static esp_err_t s_fail_next_sd_erase;
static esp_err_t s_fail_next_nvs_erase;
static void (*s_io_hook)(void);

static void mock_set_sd(bool enabled)
{
    if (s_use_sd != enabled) {
        s_use_sd = enabled;
        s_backend_generation++;
    }
}

static blob_slot_t *sd_slot(const char *key)
{
    return strcmp(key, V2_KEY) == 0 ? &s_sd_v2 :
           strcmp(key, LEGACY_KEY) == 0 ? &s_sd_legacy : NULL;
}

static blob_slot_t *nvs_slot(const char *key)
{
    return strcmp(key, V2_KEY) == 0 ? &s_nvs_v2 :
           strcmp(key, LEGACY_KEY) == 0 ? &s_nvs_legacy : NULL;
}

static void mock_retained_reset(void)
{
    mock_set_sd(false);
    s_backend_generation = 0U;
    mock_set_sd(true);
    memset(&s_sd_v2, 0, sizeof(s_sd_v2));
    memset(&s_sd_legacy, 0, sizeof(s_sd_legacy));
    memset(&s_nvs_v2, 0, sizeof(s_nvs_v2));
    memset(&s_nvs_legacy, 0, sizeof(s_nvs_legacy));
    s_sd_write_count = 0;
    s_nvs_write_count = 0;
    s_sd_read_count = 0;
    s_nvs_read_count = 0;
    s_sd_erase_count = 0;
    s_nvs_erase_count = 0;
    s_fail_next_sd_write = ESP_OK;
    s_fail_next_nvs_write = ESP_OK;
    s_fail_next_sd_read = ESP_OK;
    s_fail_sd_v2_read = ESP_OK;
    s_fail_sd_legacy_read = ESP_OK;
    s_fail_next_sd_erase = ESP_OK;
    s_fail_next_nvs_erase = ESP_OK;
    s_io_hook = NULL;
    mock_timer_set_us(0);
}

static void seed_slot(blob_slot_t *slot, const void *src, size_t len)
{
    assert(slot && src && len <= sizeof(slot->data));
    memcpy(slot->data, src, len);
    slot->len = len;
    slot->valid = true;
}

static esp_err_t copy_slot_out(const blob_slot_t *slot, void *dst, size_t *len_inout)
{
    if (!slot || !slot->valid) {
        return ESP_ERR_NOT_FOUND;
    }
    if (!dst || !len_inout) {
        return ESP_ERR_INVALID_ARG;
    }
    if (*len_inout < slot->len) {
        *len_inout = slot->len;
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(dst, slot->data, slot->len);
    *len_inout = slot->len;
    return ESP_OK;
}

static void run_io_hook_once(void)
{
    void (*hook)(void) = s_io_hook;
    s_io_hook = NULL;
    if (hook) {
        hook();
    }
}

bool d1l_retained_blob_store_uses_sd(d1l_retained_blob_store_id_t store_id)
{
    assert(store_id == D1L_RETAINED_BLOB_STORE_ROUTES);
    return s_use_sd;
}

bool d1l_retained_blob_store_backend_state(
    d1l_retained_blob_store_id_t store_id,
    d1l_retained_blob_store_backend_state_t *out_state)
{
    assert(store_id == D1L_RETAINED_BLOB_STORE_ROUTES);
    assert(out_state);
    out_state->enabled = s_use_sd;
    out_state->generation = s_backend_generation;
    return true;
}

esp_err_t d1l_retained_blob_store_read_sd_primary(
    d1l_retained_blob_store_id_t store_id, const char *key,
    void *dst, size_t *len_inout)
{
    assert(store_id == D1L_RETAINED_BLOB_STORE_ROUTES);
    assert(s_use_sd);
    s_sd_read_count++;
    if (s_fail_next_sd_read != ESP_OK) {
        const esp_err_t ret = s_fail_next_sd_read;
        s_fail_next_sd_read = ESP_OK;
        return ret;
    }
    if (strcmp(key, V2_KEY) == 0 && s_fail_sd_v2_read != ESP_OK) {
        return s_fail_sd_v2_read;
    }
    if (strcmp(key, LEGACY_KEY) == 0 && s_fail_sd_legacy_read != ESP_OK) {
        return s_fail_sd_legacy_read;
    }
    return copy_slot_out(sd_slot(key), dst, len_inout);
}

esp_err_t d1l_retained_blob_store_read_nvs_fallback(
    d1l_retained_blob_store_id_t store_id, const char *key,
    void *dst, size_t *len_inout)
{
    assert(store_id == D1L_RETAINED_BLOB_STORE_ROUTES);
    s_nvs_read_count++;
    return copy_slot_out(nvs_slot(key), dst, len_inout);
}

esp_err_t d1l_retained_blob_store_write_sd_primary(
    d1l_retained_blob_store_id_t store_id, const char *key,
    const void *src, size_t len)
{
    assert(store_id == D1L_RETAINED_BLOB_STORE_ROUTES);
    blob_slot_t *slot = sd_slot(key);
    assert(s_use_sd && slot && src && len <= sizeof(slot->data));
    s_sd_write_count++;
    if (s_fail_next_sd_write != ESP_OK) {
        const esp_err_t ret = s_fail_next_sd_write;
        s_fail_next_sd_write = ESP_OK;
        run_io_hook_once();
        return ret;
    }
    seed_slot(slot, src, len);
    run_io_hook_once();
    return ESP_OK;
}

esp_err_t d1l_retained_blob_store_write_sd_primary_guarded(
    d1l_retained_blob_store_id_t store_id, const char *key,
    const void *src, size_t len, uint32_t expected_generation)
{
    assert(store_id == D1L_RETAINED_BLOB_STORE_ROUTES);
    blob_slot_t *slot = sd_slot(key);
    assert(s_use_sd && slot && src && len <= sizeof(slot->data));
    if (s_backend_generation != expected_generation) {
        return ESP_ERR_INVALID_STATE;
    }
    s_sd_write_count++;
    if (s_fail_next_sd_write != ESP_OK) {
        const esp_err_t ret = s_fail_next_sd_write;
        s_fail_next_sd_write = ESP_OK;
        run_io_hook_once();
        return ret;
    }
    /* The hook models work performed while a temp blob is being written. The
     * guarded production path rechecks generation before replace-rename. */
    run_io_hook_once();
    if (!s_use_sd || s_backend_generation != expected_generation) {
        return ESP_ERR_INVALID_STATE;
    }
    seed_slot(slot, src, len);
    return ESP_OK;
}

esp_err_t d1l_retained_blob_store_write_nvs_fallback(
    d1l_retained_blob_store_id_t store_id, const char *key,
    const void *src, size_t len)
{
    assert(store_id == D1L_RETAINED_BLOB_STORE_ROUTES);
    blob_slot_t *slot = nvs_slot(key);
    assert(slot && src && len <= sizeof(slot->data));
    s_nvs_write_count++;
    if (s_fail_next_nvs_write != ESP_OK) {
        const esp_err_t ret = s_fail_next_nvs_write;
        s_fail_next_nvs_write = ESP_OK;
        run_io_hook_once();
        return ret;
    }
    seed_slot(slot, src, len);
    run_io_hook_once();
    return ESP_OK;
}

esp_err_t d1l_retained_blob_store_erase_sd_primary(
    d1l_retained_blob_store_id_t store_id, const char *key)
{
    assert(store_id == D1L_RETAINED_BLOB_STORE_ROUTES);
    blob_slot_t *slot = sd_slot(key);
    assert(s_use_sd && slot);
    s_sd_erase_count++;
    if (s_fail_next_sd_erase != ESP_OK) {
        const esp_err_t ret = s_fail_next_sd_erase;
        s_fail_next_sd_erase = ESP_OK;
        return ret;
    }
    slot->valid = false;
    return ESP_OK;
}

esp_err_t d1l_retained_blob_store_erase_sd_primary_guarded(
    d1l_retained_blob_store_id_t store_id, const char *key,
    uint32_t expected_generation)
{
    if (!s_use_sd || s_backend_generation != expected_generation) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t ret = d1l_retained_blob_store_erase_sd_primary(
        store_id, key);
    if (!s_use_sd || s_backend_generation != expected_generation) {
        return ESP_ERR_INVALID_STATE;
    }
    return ret;
}

esp_err_t d1l_retained_blob_store_erase_nvs_fallback(
    d1l_retained_blob_store_id_t store_id, const char *key)
{
    assert(store_id == D1L_RETAINED_BLOB_STORE_ROUTES);
    blob_slot_t *slot = nvs_slot(key);
    assert(slot);
    s_nvs_erase_count++;
    if (s_fail_next_nvs_erase != ESP_OK) {
        const esp_err_t ret = s_fail_next_nvs_erase;
        s_fail_next_nvs_erase = ESP_OK;
        return ret;
    }
    slot->valid = false;
    return ESP_OK;
}

static esp_err_t observe(const char *target, const char *route,
                         uint8_t path_hash_bytes, uint8_t path_hops)
{
    return d1l_route_store_upsert_observation(target, target, "advert", route, "rx",
                                              -70, 80, path_hash_bytes, path_hops, 42);
}

static void fill_route_entry(d1l_route_entry_t *entry, uint32_t seq,
                             const char *target)
{
    memset(entry, 0, sizeof(*entry));
    entry->seq = seq;
    entry->seen_count = 1U;
    entry->confidence = 80U;
    entry->path_hash_bytes = 1U;
    snprintf(entry->target, sizeof(entry->target), "%s", target);
    snprintf(entry->label, sizeof(entry->label), "%s", target);
    strcpy(entry->kind, "advert");
    strcpy(entry->route, "flood");
    strcpy(entry->direction, "rx");
}

static full_blob_t s_replacement_during_write;

static void replace_card_during_temp_write(void)
{
    mock_set_sd(false);
    seed_slot(&s_sd_v2, &s_replacement_during_write,
              sizeof(s_replacement_during_write));
    mock_set_sd(true);
}

static void seed_synced_v2_history(uint32_t epoch, uint32_t total_written,
                                   uint32_t full_count)
{
    assert(full_count <= D1L_ROUTE_STORE_CAPACITY);
    full_blob_t full = {
        .schema = FULL_SCHEMA,
        .epoch = epoch,
        .next_seq = total_written + 1U,
        .total_written = total_written,
        .count = full_count,
    };
    for (uint32_t i = 0; i < full_count; ++i) {
        char target[16];
        snprintf(target, sizeof(target), "route-%02lu", (unsigned long)i);
        fill_route_entry(&full.entries[i], total_written - full_count + i + 1U,
                         target);
    }
    seed_slot(&s_sd_v2, &full, sizeof(full));

    fallback_blob_t compact = {
        .schema = FALLBACK_SCHEMA,
        .epoch = epoch,
        .next_seq = total_written + 1U,
        .total_written = total_written,
    };
    const uint32_t compact_count =
        full_count < D1L_ROUTE_STORE_NVS_FALLBACK_CAPACITY ?
        full_count : D1L_ROUTE_STORE_NVS_FALLBACK_CAPACITY;
    compact.count = compact_count;
    for (uint32_t i = 0; i < compact_count; ++i) {
        compact.entries[i] = full.entries[full_count - compact_count + i];
    }
    seed_slot(&s_nvs_v2, &compact, sizeof(compact));
}

static void concurrent_observation(void)
{
    assert(observe("during_io", "direct", 2, 0) == ESP_OK);
}

static void test_partial_nvs_failure_retries_without_rewriting_sd(void)
{
    mock_retained_reset();
    assert(d1l_route_store_init() == ESP_OK);
    s_fail_next_nvs_write = ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    assert(observe("a", "flood", 1, 1) == ESP_OK);
    assert(s_sd_write_count == 0U);
    assert(s_nvs_write_count == 0U);
    mock_timer_set_us(5000000LL);
    assert(d1l_route_store_flush_if_due() == ESP_ERR_NVS_NOT_ENOUGH_SPACE);
    assert(s_sd_write_count == 1U);
    assert(s_nvs_write_count == 1U);

    d1l_route_store_stats_t stats = d1l_route_store_stats();
    assert(!stats.sd_primary_dirty);
    assert(stats.nvs_fallback_dirty);
    assert(stats.persistence_dirty);
    assert(stats.sd_primary_commit_count == 1U);
    assert(stats.nvs_fallback_fail_count == 1U);

    mock_timer_set_us(10000000LL);
    assert(d1l_route_store_flush_if_due() == ESP_OK);
    assert(s_sd_write_count == 1U);
    assert(s_nvs_write_count == 2U);
    stats = d1l_route_store_stats();
    assert(!stats.persistence_dirty);
    assert(stats.nvs_fallback_commit_count == 1U);
    assert(s_nvs_v2.valid && s_nvs_v2.len == sizeof(fallback_blob_t));
    assert(((const fallback_blob_t *)s_nvs_v2.data)->schema == FALLBACK_SCHEMA);
}

static void test_concurrent_callback_keeps_new_revision_dirty(void)
{
    mock_retained_reset();
    assert(d1l_route_store_init() == ESP_OK);
    assert(observe("a", "flood", 1, 1) == ESP_OK);
    assert(s_sd_write_count == 0U);
    assert(s_nvs_write_count == 0U);
    mock_timer_set_us(5000000LL);
    assert(d1l_route_store_flush_if_due() == ESP_OK);
    mock_timer_set_us(6000000LL);
    assert(observe("a", "direct", 2, 0) == ESP_OK);
    s_io_hook = concurrent_observation;
    mock_timer_set_us(11000000LL);
    assert(d1l_route_store_flush_if_due() == ESP_OK);

    d1l_route_store_stats_t stats = d1l_route_store_stats();
    assert(stats.persistence_stale_snapshot_count == 1U);
    assert(stats.persistence_dirty);
    const uint64_t concurrent_revision = stats.persistence_revision;

    mock_timer_set_us(16000000LL);
    assert(d1l_route_store_flush_if_due() == ESP_OK);
    stats = d1l_route_store_stats();
    assert(stats.persistence_revision == concurrent_revision);
    assert(!stats.persistence_dirty);
    assert(((const full_blob_t *)s_sd_v2.data)->total_written == 3U);
    assert(((const fallback_blob_t *)s_nvs_v2.data)->total_written == 3U);
}

static void test_legacy_full_blob_migrates_without_recovery_erase(void)
{
    mock_retained_reset();
    mock_set_sd(false);
    legacy_blob_t legacy = {
        .schema = LEGACY_SCHEMA,
        .next_seq = 3U,
        .total_written = 2U,
        .count = 2U,
    };
    fill_route_entry(&legacy.entries[0], 1U, "old-a");
    fill_route_entry(&legacy.entries[1], 2U, "old-b");
    seed_slot(&s_nvs_legacy, &legacy, sizeof(legacy));

    assert(d1l_route_store_init() == ESP_OK);
    d1l_route_store_stats_t stats = d1l_route_store_stats();
    assert(stats.count == 2U);
    assert(stats.nvs_fallback_dirty);
    assert(d1l_route_store_flush() == ESP_OK);
    assert(s_nvs_v2.valid);
    assert(s_nvs_legacy.valid);
    assert(s_sd_erase_count == 0U);
    assert(s_nvs_erase_count == 0U);

    assert(d1l_route_store_init() == ESP_OK);
    stats = d1l_route_store_stats();
    assert(stats.count == 2U);
    assert(!stats.persistence_dirty);
}

static void test_nvs_full_legacy_migration_reclaims_only_route_cache(void)
{
    mock_retained_reset();
    mock_set_sd(false);
    legacy_blob_t legacy = {
        .schema = LEGACY_SCHEMA,
        .next_seq = 3U,
        .total_written = 2U,
        .count = 2U,
    };
    fill_route_entry(&legacy.entries[0], 1U, "legacy-a");
    fill_route_entry(&legacy.entries[1], 2U, "legacy-b");
    seed_slot(&s_nvs_legacy, &legacy, sizeof(legacy));

    assert(d1l_route_store_init() == ESP_OK);
    s_fail_next_nvs_write = ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    assert(d1l_route_store_flush() == ESP_OK);
    assert(s_nvs_write_count == 2U);
    assert(s_nvs_erase_count == 1U);
    assert(!s_nvs_legacy.valid);
    assert(s_nvs_v2.valid);
    assert(((const fallback_blob_t *)s_nvs_v2.data)->count == 2U);
    assert(!d1l_route_store_stats().persistence_dirty);
}

static void test_nvs_full_legacy_reclaim_failure_stays_fail_closed(void)
{
    mock_retained_reset();
    mock_set_sd(false);
    legacy_blob_t legacy = {
        .schema = LEGACY_SCHEMA,
        .next_seq = 2U,
        .total_written = 1U,
        .count = 1U,
    };
    fill_route_entry(&legacy.entries[0], 1U, "legacy-a");
    seed_slot(&s_nvs_legacy, &legacy, sizeof(legacy));

    assert(d1l_route_store_init() == ESP_OK);
    s_fail_next_nvs_write = ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    s_fail_next_nvs_erase = ESP_FAIL;
    assert(d1l_route_store_flush() == ESP_FAIL);
    assert(s_nvs_legacy.valid);
    assert(!s_nvs_v2.valid);
    assert(d1l_route_store_stats().persistence_dirty);
}

static void test_corrupt_compact_fails_closed_without_erase(void)
{
    mock_retained_reset();
    mock_set_sd(false);
    fallback_blob_t corrupt = {
        .schema = 99U,
        .epoch = 7U,
        .next_seq = 1U,
    };
    seed_slot(&s_nvs_v2, &corrupt, sizeof(corrupt));
    assert(d1l_route_store_init() == ESP_ERR_INVALID_STATE);
    assert(s_nvs_v2.valid);
    assert(s_sd_erase_count == 0U);
    assert(s_nvs_erase_count == 0U);
    assert(s_sd_write_count == 0U);
    assert(s_nvs_write_count == 0U);
}

static void test_unterminated_nvs_route_entry_fails_closed(void)
{
    mock_retained_reset();
    mock_set_sd(false);
    fallback_blob_t corrupt = {
        .schema = FALLBACK_SCHEMA,
        .epoch = 2U,
        .next_seq = 2U,
        .total_written = 1U,
        .count = 1U,
    };
    fill_route_entry(&corrupt.entries[0], 1U, "compact");
    memset(corrupt.entries[0].target, 'X',
           sizeof(corrupt.entries[0].target));
    seed_slot(&s_nvs_v2, &corrupt, sizeof(corrupt));

    assert(d1l_route_store_init() == ESP_ERR_INVALID_STATE);
    assert(s_nvs_v2.valid);
    assert(s_nvs_erase_count == 0U);
    assert(s_nvs_write_count == 0U);
}

static void test_unterminated_sd_route_entry_fails_closed(void)
{
    mock_retained_reset();
    full_blob_t corrupt = {
        .schema = FULL_SCHEMA,
        .epoch = 2U,
        .next_seq = 2U,
        .total_written = 1U,
        .count = 1U,
    };
    fill_route_entry(&corrupt.entries[0], 1U, "primary");
    memset(corrupt.entries[0].direction, 'X',
           sizeof(corrupt.entries[0].direction));
    seed_slot(&s_sd_v2, &corrupt, sizeof(corrupt));

    assert(d1l_route_store_init() == ESP_ERR_INVALID_STATE);
    assert(s_sd_v2.valid);
    assert(s_sd_erase_count == 0U);
    assert(s_sd_write_count == 0U);
}

static void test_invalid_persisted_route_sequence_fails_closed(void)
{
    mock_retained_reset();
    mock_set_sd(false);
    fallback_blob_t corrupt = {
        .schema = FALLBACK_SCHEMA,
        .epoch = 2U,
        .next_seq = 1U,
        .total_written = 1U,
        .count = 1U,
    };
    fill_route_entry(&corrupt.entries[0], 1U, "bad-seq");
    seed_slot(&s_nvs_v2, &corrupt, sizeof(corrupt));

    assert(d1l_route_store_init() == ESP_ERR_INVALID_STATE);
    assert(s_nvs_v2.valid);
    assert(s_nvs_erase_count == 0U);
    assert(s_nvs_write_count == 0U);
}

static void test_clear_failure_latches_and_cannot_reboot_stale_routes(void)
{
    mock_retained_reset();
    assert(d1l_route_store_init() == ESP_OK);
    assert(observe("keep", "flood", 1, 1) == ESP_OK);
    s_fail_next_nvs_erase = ESP_FAIL;
    assert(d1l_route_store_clear() == ESP_FAIL);

    d1l_route_store_stats_t stats = d1l_route_store_stats();
    assert(stats.count == 1U);
    assert(stats.clear_failure_latched);
    assert(stats.persistence_dirty);
    assert(d1l_route_store_flush() == ESP_FAIL);
    const uint32_t sd_writes_after_failure = s_sd_write_count;
    const uint32_t nvs_writes_after_failure = s_nvs_write_count;
    for (uint32_t retry = 1U; retry <= 3U; ++retry) {
        mock_timer_set_us((int64_t)retry * 5000000LL);
        assert(d1l_route_store_flush_if_due() == ESP_FAIL);
        assert(s_sd_write_count == sd_writes_after_failure);
        assert(s_nvs_write_count == nvs_writes_after_failure);
    }

    assert(d1l_route_store_clear() == ESP_OK);
    stats = d1l_route_store_stats();
    assert(stats.count == 0U);
    assert(!stats.clear_failure_latched);
    assert(!stats.persistence_dirty);
    assert(s_nvs_v2.valid);
    assert(((const fallback_blob_t *)s_nvs_v2.data)->count == 0U);
    assert(d1l_route_store_init() == ESP_OK);
    assert(d1l_route_store_stats().count == 0U);
}

static void test_first_observation_honors_five_second_coalescing_floor(void)
{
    mock_retained_reset();
    assert(d1l_route_store_init() == ESP_OK);
    mock_timer_set_us(10000000LL);
    assert(observe("first", "direct", 2, 0) == ESP_OK);
    assert(d1l_route_store_flush_if_due() == ESP_OK);
    assert(s_sd_write_count == 0U && s_nvs_write_count == 0U);

    mock_timer_set_us(14999000LL);
    assert(d1l_route_store_flush_if_due() == ESP_OK);
    assert(s_sd_write_count == 0U && s_nvs_write_count == 0U);

    mock_timer_set_us(15000000LL);
    assert(d1l_route_store_flush_if_due() == ESP_OK);
    assert(s_sd_write_count == 1U && s_nvs_write_count == 1U);
}

static void test_nonmaterial_update_waits_for_thirty_second_deadline(void)
{
    mock_retained_reset();
    assert(d1l_route_store_init() == ESP_OK);
    assert(observe("steady", "direct", 2, 0) == ESP_OK);
    mock_timer_set_us(5000000LL);
    assert(d1l_route_store_flush_if_due() == ESP_OK);
    assert(s_sd_write_count == 1U && s_nvs_write_count == 1U);

    mock_timer_set_us(6000000LL);
    assert(observe("steady", "direct", 2, 0) == ESP_OK);
    mock_timer_set_us(35000000LL);
    assert(d1l_route_store_flush_if_due() == ESP_OK);
    assert(s_sd_write_count == 1U && s_nvs_write_count == 1U);

    mock_timer_set_us(36000000LL);
    assert(d1l_route_store_flush_if_due() == ESP_OK);
    assert(s_sd_write_count == 2U && s_nvs_write_count == 2U);
}

static void test_late_sd_reconciles_full_primary_without_truncation(void)
{
    mock_retained_reset();
    seed_synced_v2_history(1U, 20U, D1L_ROUTE_STORE_CAPACITY);
    mock_set_sd(false);
    assert(d1l_route_store_init() == ESP_OK);
    assert(d1l_route_store_stats().count == D1L_ROUTE_STORE_NVS_FALLBACK_CAPACITY);

    mock_set_sd(true);
    assert(d1l_route_store_flush_if_due() == ESP_OK);
    d1l_route_store_stats_t stats = d1l_route_store_stats();
    assert(stats.count == D1L_ROUTE_STORE_CAPACITY);
    assert(!stats.sd_primary_reconcile_pending);
    assert(!stats.persistence_dirty);
    assert(s_sd_write_count == 0U);
    assert(((const full_blob_t *)s_sd_v2.data)->count == D1L_ROUTE_STORE_CAPACITY);
}

static void test_late_sd_merges_live_observation_before_write(void)
{
    mock_retained_reset();
    seed_synced_v2_history(1U, 20U, D1L_ROUTE_STORE_CAPACITY);
    mock_set_sd(false);
    assert(d1l_route_store_init() == ESP_OK);
    mock_timer_set_us(1000000LL);
    assert(observe("live-overlay", "direct", 2, 0) == ESP_OK);

    mock_set_sd(true);
    assert(d1l_route_store_flush_if_due() == ESP_OK);
    d1l_route_store_stats_t stats = d1l_route_store_stats();
    assert(stats.count == D1L_ROUTE_STORE_CAPACITY);
    assert(!stats.sd_primary_reconcile_pending);
    assert(!stats.persistence_dirty);
    assert(s_sd_write_count == 1U);
    const full_blob_t *persisted = (const full_blob_t *)s_sd_v2.data;
    assert(persisted->count == D1L_ROUTE_STORE_CAPACITY);
    assert(persisted->total_written == 21U);
    bool found_overlay = false;
    for (uint32_t i = 0; i < persisted->count; ++i) {
        found_overlay = found_overlay ||
            strcmp(persisted->entries[i].target, "live-overlay") == 0;
    }
    assert(found_overlay);
}

static void test_overlay_replay_preserves_chronological_newest_four(void)
{
    mock_retained_reset();
    full_blob_t primary = {
        .schema = FULL_SCHEMA,
        .epoch = 1U,
        .next_seq = 201U,
        .total_written = 200U,
        .count = D1L_ROUTE_STORE_CAPACITY,
    };
    for (uint32_t i = 0; i < primary.count; ++i) {
        char target[32];
        snprintf(target, sizeof(target), "primary-%02lu", (unsigned long)i);
        fill_route_entry(&primary.entries[i], 185U + i, target);
    }
    seed_slot(&s_sd_v2, &primary, sizeof(primary));

    fallback_blob_t compact = {
        .schema = FALLBACK_SCHEMA,
        .epoch = 1U,
        .next_seq = 101U,
        .total_written = 100U,
        .count = D1L_ROUTE_STORE_NVS_FALLBACK_CAPACITY,
    };
    for (uint32_t i = 0; i < compact.count; ++i) {
        char target[32];
        snprintf(target, sizeof(target), "base-%02lu", (unsigned long)i);
        fill_route_entry(&compact.entries[i], 97U + i, target);
    }
    seed_slot(&s_nvs_v2, &compact, sizeof(compact));

    mock_set_sd(false);
    assert(d1l_route_store_init() == ESP_OK);
    for (uint32_t i = 0; i < D1L_ROUTE_STORE_CAPACITY; ++i) {
        char target[32];
        snprintf(target, sizeof(target), "overlay-%02lu", (unsigned long)i);
        assert(observe(target, "direct", 2, 0) == ESP_OK);
    }
    /* overlay-12 occupies a low ring index after the full-ring replacements;
     * update it last so raw array order differs from chronology. */
    assert(observe("overlay-12", "updated", 2, 0) == ESP_OK);

    mock_set_sd(true);
    assert(d1l_route_store_flush_if_due() == ESP_OK);
    const full_blob_t *persisted = (const full_blob_t *)s_sd_v2.data;
    const fallback_blob_t *fallback = (const fallback_blob_t *)s_nvs_v2.data;
    assert(persisted->count == D1L_ROUTE_STORE_CAPACITY);
    assert(persisted->total_written == 217U);
    assert(fallback->count == D1L_ROUTE_STORE_NVS_FALLBACK_CAPACITY);
    assert(fallback->total_written == 217U);

    bool newest_12 = false;
    bool newest_13 = false;
    bool newest_14 = false;
    bool newest_15 = false;
    for (uint32_t i = 0; i < fallback->count; ++i) {
        newest_12 = newest_12 || strcmp(fallback->entries[i].target, "overlay-12") == 0;
        newest_13 = newest_13 || strcmp(fallback->entries[i].target, "overlay-13") == 0;
        newest_14 = newest_14 || strcmp(fallback->entries[i].target, "overlay-14") == 0;
        newest_15 = newest_15 || strcmp(fallback->entries[i].target, "overlay-15") == 0;
    }
    assert(newest_12 && newest_13 && newest_14 && newest_15);
}

static void test_newer_compact_merges_readable_full_sd_before_write(void)
{
    mock_retained_reset();
    seed_synced_v2_history(1U, 20U, D1L_ROUTE_STORE_CAPACITY);
    fallback_blob_t *compact = (fallback_blob_t *)s_nvs_v2.data;
    compact->total_written = 21U;
    compact->next_seq = 22U;
    compact->count = 1U;
    fill_route_entry(&compact->entries[0], 21U, "compact-new");

    assert(d1l_route_store_init() == ESP_OK);
    d1l_route_store_stats_t stats = d1l_route_store_stats();
    assert(stats.count == 1U);
    assert(stats.sd_primary_reconcile_pending);
    assert(s_sd_write_count == 0U);
    assert(s_nvs_write_count == 0U);

    assert(d1l_route_store_flush_if_due() == ESP_OK);
    stats = d1l_route_store_stats();
    assert(stats.count == D1L_ROUTE_STORE_CAPACITY);
    assert(!stats.sd_primary_reconcile_pending);
    assert(!stats.persistence_dirty);
    assert(s_sd_write_count == 1U);
    assert(s_nvs_write_count == 1U);
    const full_blob_t *persisted = (const full_blob_t *)s_sd_v2.data;
    assert(persisted->count == D1L_ROUTE_STORE_CAPACITY);
    assert(persisted->total_written == 21U);
    const fallback_blob_t *refilled = (const fallback_blob_t *)s_nvs_v2.data;
    assert(refilled->count == D1L_ROUTE_STORE_NVS_FALLBACK_CAPACITY);
    bool retained_newest = false;
    for (uint32_t i = 0; i < refilled->count; ++i) {
        retained_newest = retained_newest ||
                          strcmp(refilled->entries[i].target, "compact-new") == 0;
    }
    assert(retained_newest);
}

static void test_transient_sd_read_error_retries_before_any_overwrite(void)
{
    mock_retained_reset();
    seed_synced_v2_history(1U, 20U, D1L_ROUTE_STORE_CAPACITY);
    s_fail_next_sd_read = ESP_FAIL;
    assert(d1l_route_store_init() == ESP_OK);
    d1l_route_store_stats_t stats = d1l_route_store_stats();
    assert(stats.count == D1L_ROUTE_STORE_NVS_FALLBACK_CAPACITY);
    assert(stats.sd_primary_reconcile_pending);
    assert(s_sd_write_count == 0U);

    assert(d1l_route_store_flush_if_due() == ESP_OK);
    stats = d1l_route_store_stats();
    assert(stats.count == D1L_ROUTE_STORE_CAPACITY);
    assert(!stats.sd_primary_reconcile_pending);
    assert(!stats.persistence_dirty);
    assert(s_sd_write_count == 0U);
}

static void test_persistent_sd_read_failure_still_advances_compact_nvs(void)
{
    mock_retained_reset();
    seed_synced_v2_history(1U, 20U, D1L_ROUTE_STORE_CAPACITY);
    mock_set_sd(false);
    assert(d1l_route_store_init() == ESP_OK);
    mock_timer_set_us(1000000LL);
    assert(observe("nvs-survivor", "direct", 2, 0) == ESP_OK);

    mock_set_sd(true);
    s_fail_sd_v2_read = ESP_FAIL;
    assert(d1l_route_store_flush_if_due() == ESP_FAIL);
    d1l_route_store_stats_t stats = d1l_route_store_stats();
    assert(stats.sd_primary_reconcile_pending);
    assert(stats.persistence_dirty);
    assert(!stats.nvs_fallback_dirty);
    assert(s_sd_write_count == 0U);
    assert(s_nvs_write_count == 1U);
    const fallback_blob_t *fallback = (const fallback_blob_t *)s_nvs_v2.data;
    assert(fallback->total_written == 21U);
    bool found_survivor = false;
    for (uint32_t i = 0; i < fallback->count; ++i) {
        found_survivor = found_survivor ||
            strcmp(fallback->entries[i].target, "nvs-survivor") == 0;
    }
    assert(found_survivor);
    assert(((const full_blob_t *)s_sd_v2.data)->total_written == 20U);

    assert(d1l_route_store_flush() == ESP_FAIL);
    assert(s_sd_write_count == 0U);
    assert(s_nvs_write_count == 1U);
}

static void test_volatile_canary_never_evicts_durable_history(void)
{
    mock_retained_reset();
    seed_synced_v2_history(1U, 20U, D1L_ROUTE_STORE_CAPACITY);
    assert(d1l_route_store_init() == ESP_OK);
    const d1l_route_store_stats_t before = d1l_route_store_stats();

    assert(d1l_route_store_upsert_observation_volatile(
               "ui-canary", "UI Canary", "ui_canary", "local", "rx",
               0, 0, 1, 0, 12U) == ESP_OK);
    const d1l_route_store_stats_t after_volatile = d1l_route_store_stats();
    assert(after_volatile.next_seq == before.next_seq);
    assert(after_volatile.total_written == before.total_written);
    assert(after_volatile.dropped_oldest == before.dropped_oldest);
    assert(after_volatile.persistence_revision == before.persistence_revision);
    assert(after_volatile.persistence_dirty == before.persistence_dirty);
    assert(after_volatile.count == D1L_ROUTE_STORE_CAPACITY);
    assert(s_sd_write_count == 0U && s_nvs_write_count == 0U);

    d1l_route_entry_t preview[D1L_ROUTE_STORE_CAPACITY];
    assert(d1l_route_store_copy_recent(preview, D1L_ROUTE_STORE_CAPACITY) ==
           D1L_ROUTE_STORE_CAPACITY);
    assert(strcmp(preview[0].target, "ui-canary") == 0);
    assert(preview[0].seq == before.next_seq);

    assert(observe("real-new", "direct", 2, 0) == ESP_OK);
    const d1l_route_store_stats_t after_real = d1l_route_store_stats();
    assert(after_real.count == D1L_ROUTE_STORE_CAPACITY);
    assert(after_real.next_seq == before.next_seq + 1U);
    assert(after_real.total_written == before.total_written + 1U);
    assert(after_real.dropped_oldest == before.dropped_oldest + 1U);
    assert(after_real.persistence_revision == before.persistence_revision + 1U);
    assert(d1l_route_store_flush() == ESP_OK);

    const full_blob_t *persisted = (const full_blob_t *)s_sd_v2.data;
    assert(persisted->count == D1L_ROUTE_STORE_CAPACITY);
    bool found_real = false;
    bool found_canary = false;
    bool found_oldest = false;
    uint32_t retained_originals = 0U;
    for (uint32_t i = 0; i < persisted->count; ++i) {
        found_real = found_real || strcmp(persisted->entries[i].target, "real-new") == 0;
        found_canary = found_canary ||
            strcmp(persisted->entries[i].target, "ui-canary") == 0;
        found_oldest = found_oldest ||
            strcmp(persisted->entries[i].target, "route-00") == 0;
        if (strncmp(persisted->entries[i].target, "route-", 6U) == 0) {
            retained_originals++;
        }
    }
    assert(found_real);
    assert(!found_canary);
    assert(!found_oldest);
    assert(retained_originals == D1L_ROUTE_STORE_CAPACITY - 1U);
}

static void test_sentinel_shaped_durable_route_survives_persistence(void)
{
    mock_retained_reset();
    assert(d1l_route_store_init() == ESP_OK);
    assert(d1l_route_store_upsert_observation(
               "sentinel-real", "UI Canary", "ui_canary", "local", "rx",
               -60, 40, 1U, 0U, 12U) == ESP_OK);
    assert(d1l_route_store_flush() == ESP_OK);

    const full_blob_t *primary = (const full_blob_t *)s_sd_v2.data;
    const fallback_blob_t *fallback = (const fallback_blob_t *)s_nvs_v2.data;
    assert(primary->count == 1U);
    assert(fallback->count == 1U);
    assert(strcmp(primary->entries[0].target, "sentinel-real") == 0);
    assert(strcmp(primary->entries[0].label, "UI Canary") == 0);
    assert(strcmp(primary->entries[0].kind, "ui_canary") == 0);

    assert(d1l_route_store_init() == ESP_OK);
    d1l_route_entry_t restored = {0};
    assert(d1l_route_store_copy_recent(&restored, 1U) == 1U);
    assert(strcmp(restored.target, "sentinel-real") == 0);
    assert(strcmp(restored.label, "UI Canary") == 0);
    assert(strcmp(restored.kind, "ui_canary") == 0);
}

static void test_hot_upsert_never_retries_failed_storage_init(void)
{
    mock_retained_reset();
    mock_set_sd(false);
    fallback_blob_t corrupt = {
        .schema = 99U,
        .epoch = 1U,
        .next_seq = 1U,
    };
    seed_slot(&s_nvs_v2, &corrupt, sizeof(corrupt));
    assert(d1l_route_store_init() == ESP_ERR_INVALID_STATE);
    const uint32_t reads_before = s_sd_read_count + s_nvs_read_count;
    assert(observe("must-not-load", "direct", 1, 0) == ESP_ERR_INVALID_STATE);
    assert(s_sd_read_count + s_nvs_read_count == reads_before);
}

static void test_fresh_no_card_uses_nvs_and_later_populates_sd(void)
{
    mock_retained_reset();
    mock_set_sd(false);
    assert(d1l_route_store_init() == ESP_OK);
    assert(observe("nvs-first", "direct", 1, 0) == ESP_OK);
    mock_timer_set_us(5000000LL);
    assert(d1l_route_store_flush_if_due() == ESP_OK);
    assert(s_nvs_write_count == 1U);
    assert(s_sd_write_count == 0U);
    assert(s_nvs_v2.valid);
    assert(((const fallback_blob_t *)s_nvs_v2.data)->count == 1U);

    mock_set_sd(true);
    assert(d1l_route_store_flush_if_due() == ESP_OK);
    assert(s_sd_write_count == 1U);
    assert(s_sd_v2.valid);
    assert(((const full_blob_t *)s_sd_v2.data)->count == 1U);
    assert(strcmp(((const full_blob_t *)s_sd_v2.data)->entries[0].target,
                  "nvs-first") == 0);
    assert(!d1l_route_store_stats().persistence_dirty);
}

static void test_newer_legacy_rollback_data_wins_without_erase(void)
{
    mock_retained_reset();
    mock_set_sd(false);
    fallback_blob_t compact = {
        .schema = FALLBACK_SCHEMA,
        .epoch = 1U,
        .next_seq = 3U,
        .total_written = 2U,
        .count = 2U,
    };
    fill_route_entry(&compact.entries[0], 1U, "v2-a");
    fill_route_entry(&compact.entries[1], 2U, "v2-b");
    seed_slot(&s_nvs_v2, &compact, sizeof(compact));

    legacy_blob_t rollback = {
        .schema = LEGACY_SCHEMA,
        .next_seq = 6U,
        .total_written = 5U,
        .count = 5U,
    };
    for (uint32_t i = 0; i < rollback.count; ++i) {
        char target[32];
        snprintf(target, sizeof(target), "legacy-%lu", (unsigned long)i);
        fill_route_entry(&rollback.entries[i], i + 1U, target);
    }
    seed_slot(&s_nvs_legacy, &rollback, sizeof(rollback));

    assert(d1l_route_store_init() == ESP_OK);
    assert(d1l_route_store_stats().count == rollback.count);
    assert(s_sd_erase_count == 0U && s_nvs_erase_count == 0U);
    assert(d1l_route_store_flush() == ESP_OK);
    assert(s_nvs_legacy.valid);
    assert(((const fallback_blob_t *)s_nvs_v2.data)->total_written == 5U);
}

static void test_newer_v2_epoch_blocks_legacy_resurrection(void)
{
    mock_retained_reset();
    mock_set_sd(false);
    fallback_blob_t tombstone = {
        .schema = FALLBACK_SCHEMA,
        .epoch = 2U,
        .next_seq = 1U,
    };
    seed_slot(&s_nvs_v2, &tombstone, sizeof(tombstone));
    legacy_blob_t stale = {
        .schema = LEGACY_SCHEMA,
        .next_seq = 9U,
        .total_written = 8U,
        .count = 1U,
    };
    fill_route_entry(&stale.entries[0], 8U, "stale");
    seed_slot(&s_nvs_legacy, &stale, sizeof(stale));

    assert(d1l_route_store_init() == ESP_OK);
    assert(d1l_route_store_stats().count == 0U);
    assert(s_nvs_legacy.valid);
}

static void test_higher_epoch_v2_ignores_irrelevant_legacy_read_failure(void)
{
    mock_retained_reset();
    full_blob_t primary = {
        .schema = FULL_SCHEMA,
        .epoch = 2U,
        .next_seq = 1U,
    };
    fallback_blob_t compact = {
        .schema = FALLBACK_SCHEMA,
        .epoch = 2U,
        .next_seq = 1U,
    };
    seed_slot(&s_sd_v2, &primary, sizeof(primary));
    seed_slot(&s_nvs_v2, &compact, sizeof(compact));
    s_fail_sd_legacy_read = ESP_FAIL;

    assert(d1l_route_store_init() == ESP_OK);
    assert(d1l_route_store_stats().sd_primary_reconcile_pending);
    assert(d1l_route_store_flush_if_due() == ESP_OK);
    assert(!d1l_route_store_stats().sd_primary_reconcile_pending);
    assert(!d1l_route_store_stats().persistence_dirty);
}

static void test_unsampled_remove_reinsert_generation_reconciles_before_write(void)
{
    mock_retained_reset();
    seed_synced_v2_history(1U, 1U, 1U);
    assert(d1l_route_store_init() == ESP_OK);

    /* The worker never samples the disabled level. A replacement card is
     * mounted before its next flush, so only the monotonic generation exposes
     * the remove/reinsert cycle. */
    mock_set_sd(false);
    full_blob_t replacement = {
        .schema = FULL_SCHEMA,
        .epoch = 1U,
        .next_seq = 11U,
        .total_written = 10U,
        .count = 1U,
    };
    fill_route_entry(&replacement.entries[0], 10U, "replacement");
    seed_slot(&s_sd_v2, &replacement, sizeof(replacement));
    mock_set_sd(true);

    s_sd_read_count = 0U;
    s_sd_write_count = 0U;
    assert(observe("local-live", "direct", 1U, 0U) == ESP_OK);
    assert(d1l_route_store_flush_if_due() == ESP_OK);
    assert(s_sd_read_count >= 2U);
    assert(s_sd_write_count == 1U);

    const full_blob_t *committed = (const full_blob_t *)s_sd_v2.data;
    bool found_replacement = false;
    bool found_local = false;
    for (uint32_t i = 0; i < committed->count; ++i) {
        found_replacement = found_replacement ||
            strcmp(committed->entries[i].target, "replacement") == 0;
        found_local = found_local ||
            strcmp(committed->entries[i].target, "local-live") == 0;
    }
    assert(found_replacement);
    assert(found_local);
    assert(committed->total_written == 11U);
    assert(!d1l_route_store_stats().persistence_dirty);
}

static void test_generation_edge_during_temp_write_never_replace_renames(void)
{
    mock_retained_reset();
    seed_synced_v2_history(1U, 1U, 1U);
    assert(d1l_route_store_init() == ESP_OK);
    assert(observe("local-live", "direct", 1U, 0U) == ESP_OK);
    mock_timer_set_us(5000000LL);

    memset(&s_replacement_during_write, 0,
           sizeof(s_replacement_during_write));
    s_replacement_during_write.schema = FULL_SCHEMA;
    s_replacement_during_write.epoch = 1U;
    s_replacement_during_write.next_seq = 11U;
    s_replacement_during_write.total_written = 10U;
    s_replacement_during_write.count = 1U;
    fill_route_entry(&s_replacement_during_write.entries[0], 10U,
                     "swap-card");
    s_io_hook = replace_card_during_temp_write;

    assert(d1l_route_store_flush_if_due() == ESP_ERR_INVALID_STATE);
    const full_blob_t *preserved = (const full_blob_t *)s_sd_v2.data;
    assert(preserved->count == 1U);
    assert(strcmp(preserved->entries[0].target, "swap-card") == 0);
    d1l_route_store_stats_t stats = d1l_route_store_stats();
    assert(stats.persistence_dirty);
    assert(stats.sd_primary_reconcile_pending);

    assert(d1l_route_store_flush() == ESP_OK);
    const full_blob_t *committed = (const full_blob_t *)s_sd_v2.data;
    bool found_replacement = false;
    bool found_local = false;
    for (uint32_t i = 0; i < committed->count; ++i) {
        found_replacement = found_replacement ||
            strcmp(committed->entries[i].target, "swap-card") == 0;
        found_local = found_local ||
            strcmp(committed->entries[i].target, "local-live") == 0;
    }
    assert(found_replacement);
    assert(found_local);
    assert(!d1l_route_store_stats().persistence_dirty);
}

int main(void)
{
    test_partial_nvs_failure_retries_without_rewriting_sd();
    test_concurrent_callback_keeps_new_revision_dirty();
    test_legacy_full_blob_migrates_without_recovery_erase();
    test_nvs_full_legacy_migration_reclaims_only_route_cache();
    test_nvs_full_legacy_reclaim_failure_stays_fail_closed();
    test_corrupt_compact_fails_closed_without_erase();
    test_unterminated_nvs_route_entry_fails_closed();
    test_unterminated_sd_route_entry_fails_closed();
    test_invalid_persisted_route_sequence_fails_closed();
    test_clear_failure_latches_and_cannot_reboot_stale_routes();
    test_first_observation_honors_five_second_coalescing_floor();
    test_nonmaterial_update_waits_for_thirty_second_deadline();
    test_late_sd_reconciles_full_primary_without_truncation();
    test_late_sd_merges_live_observation_before_write();
    test_overlay_replay_preserves_chronological_newest_four();
    test_newer_compact_merges_readable_full_sd_before_write();
    test_transient_sd_read_error_retries_before_any_overwrite();
    test_persistent_sd_read_failure_still_advances_compact_nvs();
    test_volatile_canary_never_evicts_durable_history();
    test_sentinel_shaped_durable_route_survives_persistence();
    test_hot_upsert_never_retries_failed_storage_init();
    test_fresh_no_card_uses_nvs_and_later_populates_sd();
    test_newer_legacy_rollback_data_wins_without_erase();
    test_newer_v2_epoch_blocks_legacy_resurrection();
    test_higher_epoch_v2_ignores_irrelevant_legacy_read_failure();
    test_unsampled_remove_reinsert_generation_reconciles_before_write();
    test_generation_edge_during_temp_write_never_replace_renames();
    puts("native route-store durability behavior: ok");
    return 0;
}
