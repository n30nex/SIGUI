#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "platform/time_service_core.h"

#define TEST_BUILD_EPOCH UINT32_C(1784068276)

typedef struct {
    esp_err_t result;
    uint32_t persisted_through;
    uint32_t calls;
} reserve_fixture_t;

static esp_err_t fake_reserve(void *context, uint32_t reserved_through)
{
    reserve_fixture_t *fixture = context;
    fixture->calls++;
    if (fixture->result == ESP_OK) {
        fixture->persisted_through = reserved_through;
    }
    return fixture->result;
}

static void init_core(d1l_time_service_core_t *core, uint64_t now_us)
{
    assert(d1l_time_core_init(core, now_us, TEST_BUILD_EPOCH) == ESP_OK);
}

static void test_cold_boot_is_build_anchored_and_honest(void)
{
    assert(d1l_time_core_init(NULL, 0U, TEST_BUILD_EPOCH) ==
           ESP_ERR_INVALID_ARG);
    d1l_time_service_core_t invalid;
    assert(d1l_time_core_init(
               &invalid, 0U, D1L_TIME_PROTOCOL_TIMESTAMP_BASE - 1U) ==
           ESP_ERR_INVALID_ARG);
    assert(d1l_time_core_init(
               &invalid, 0U,
               (uint32_t)D1L_TIME_SNTP_MAX_PROTOCOL_EPOCH + 1U) ==
           ESP_ERR_INVALID_ARG);

    d1l_time_service_core_t core;
    init_core(&core, UINT64_C(1000000));
    d1l_time_core_snapshot_t snapshot;
    d1l_time_core_snapshot(&core, UINT64_C(1500000), &snapshot);
    assert(snapshot.boot_monotonic_us == UINT64_C(500000));
    assert(snapshot.build_epoch_sec == TEST_BUILD_EPOCH);
    assert(snapshot.protocol_next == TEST_BUILD_EPOCH);
    assert(snapshot.protocol_wall_admission ==
           D1L_TIME_PROTOCOL_WALL_SEQUENCE_ONLY);
    assert(snapshot.protocol_tx_ready);
    assert(!snapshot.wall_valid);
    assert(!snapshot.certificate_time_valid);
    assert(snapshot.wall_epoch_sec == 0);
    assert(snapshot.wall_validity == D1L_TIME_VALIDITY_MONOTONIC_ONLY);
    assert(snapshot.wall_source == D1L_TIME_SOURCE_BOOT_MONOTONIC);

    d1l_time_core_snapshot(&core, UINT64_C(1250000), &snapshot);
    assert(snapshot.boot_monotonic_us == UINT64_C(500000));
}

static void test_protocol_seed_classification_fails_closed_on_legacy_state(void)
{
    assert(d1l_time_core_classify_protocol_seed(false, false, 0U) ==
           D1L_TIME_PROTOCOL_PERSISTENCE_FRESH);
    assert(d1l_time_core_classify_protocol_seed(
               false, true, D1L_TIME_PROTOCOL_TIMESTAMP_BASE) ==
           D1L_TIME_PROTOCOL_PERSISTENCE_READY);
    assert(d1l_time_core_classify_protocol_seed(
               false, true, D1L_TIME_PROTOCOL_TIMESTAMP_BASE - 1U) ==
           D1L_TIME_PROTOCOL_PERSISTENCE_CORRUPT);
    assert(d1l_time_core_classify_protocol_seed(
               true, false, D1L_TIME_PROTOCOL_TIMESTAMP_BASE) ==
           D1L_TIME_PROTOCOL_PERSISTENCE_MIGRATION_REQUIRED);
    assert(d1l_time_core_classify_protocol_seed(
               true, true, D1L_TIME_PROTOCOL_TIMESTAMP_BASE + 64U) ==
           D1L_TIME_PROTOCOL_PERSISTENCE_MIGRATION_REQUIRED);
    assert(strcmp(d1l_time_protocol_persistence_state_name(
                      D1L_TIME_PROTOCOL_PERSISTENCE_UNINITIALIZED),
                  "uninitialized") == 0);
    assert(strcmp(d1l_time_protocol_persistence_state_name(
                      D1L_TIME_PROTOCOL_PERSISTENCE_FRESH),
                  "fresh") == 0);
    assert(strcmp(d1l_time_protocol_persistence_state_name(
                      D1L_TIME_PROTOCOL_PERSISTENCE_READY),
                  "ready") == 0);
    assert(strcmp(d1l_time_protocol_persistence_state_name(
                      D1L_TIME_PROTOCOL_PERSISTENCE_MIGRATION_REQUIRED),
                  "migration_required") == 0);
    assert(strcmp(d1l_time_protocol_persistence_state_name(
                      D1L_TIME_PROTOCOL_PERSISTENCE_CORRUPT),
                  "corrupt") == 0);
    assert(strcmp(d1l_time_protocol_persistence_state_name(
                      D1L_TIME_PROTOCOL_PERSISTENCE_STORAGE_ERROR),
                  "storage_error") == 0);
}

static void test_protocol_ranges_reduce_writes_and_survive_reboot(void)
{
    d1l_time_service_core_t first;
    init_core(&first, 0U);
    assert(d1l_time_core_seed_protocol(&first, false, 0U) == ESP_OK);
    reserve_fixture_t reserve = {.result = ESP_OK};
    uint32_t last = 0U;
    for (uint32_t i = 0U; i < D1L_TIME_PROTOCOL_RESERVATION_SIZE; ++i) {
        assert(d1l_time_core_next_protocol_timestamp(
                   &first, i, fake_reserve, &reserve, &last) == ESP_OK);
        assert(last == TEST_BUILD_EPOCH + i);
    }
    assert(reserve.calls == 1U);
    assert(reserve.persisted_through ==
           TEST_BUILD_EPOCH + D1L_TIME_PROTOCOL_RESERVATION_SIZE - 1U);

    assert(d1l_time_core_next_protocol_timestamp(
               &first, 100U, fake_reserve, &reserve, &last) == ESP_OK);
    assert(reserve.calls == 2U);
    const uint32_t pre_reboot_last = last;
    const uint32_t persisted = reserve.persisted_through;

    d1l_time_service_core_t rebooted;
    init_core(&rebooted, 0U);
    assert(d1l_time_core_seed_protocol(&rebooted, true, persisted) == ESP_OK);
    uint32_t after_reboot = 0U;
    assert(d1l_time_core_next_protocol_timestamp(
               &rebooted, 0U, fake_reserve, &reserve, &after_reboot) == ESP_OK);
    assert(after_reboot > pre_reboot_last);
    assert(after_reboot == persisted + 1U);
    assert(reserve.calls == 3U);
}

static void test_failed_reservation_consumes_no_timestamp(void)
{
    d1l_time_service_core_t core;
    init_core(&core, 0U);
    reserve_fixture_t reserve = {.result = ESP_FAIL};
    uint32_t timestamp = UINT32_C(0xA5A5A5A5);
    assert(d1l_time_core_next_protocol_timestamp(
               &core, 0U, fake_reserve, &reserve, &timestamp) == ESP_FAIL);
    assert(timestamp == UINT32_C(0xA5A5A5A5));
    assert(core.protocol_next == TEST_BUILD_EPOCH);
    assert(!core.protocol_started);

    reserve.result = ESP_OK;
    assert(d1l_time_core_next_protocol_timestamp(
               &core, 1U, fake_reserve, &reserve, &timestamp) == ESP_OK);
    assert(timestamp == TEST_BUILD_EPOCH);
}

static void test_protocol_preflight_is_pure_and_matches_allocator_blocks(void)
{
    d1l_time_service_core_t admitted;
    init_core(&admitted, 0U);
    assert(d1l_time_core_set_wall(
               &admitted, (int64_t)TEST_BUILD_EPOCH + 100, 0U,
               D1L_TIME_VALIDITY_NETWORK_VALIDATED,
               D1L_TIME_SOURCE_SNTP) == ESP_OK);
    const d1l_time_service_core_t admitted_before = admitted;
    assert(d1l_time_core_preflight_protocol_timestamp(
               &admitted, UINT64_C(5000000)) == ESP_OK);
    assert(memcmp(&admitted, &admitted_before, sizeof(admitted)) == 0);

    d1l_time_service_core_t blocked;
    init_core(&blocked, 0U);
    assert(d1l_time_core_set_wall(
               &blocked,
               (int64_t)TEST_BUILD_EPOCH +
                   D1L_TIME_SNTP_MAX_FORWARD_SEC + 1,
               0U, D1L_TIME_VALIDITY_NETWORK_VALIDATED,
               D1L_TIME_SOURCE_SNTP) == ESP_OK);
    const d1l_time_service_core_t blocked_before = blocked;
    assert(d1l_time_core_preflight_protocol_timestamp(&blocked, 0U) ==
           ESP_ERR_INVALID_STATE);
    assert(memcmp(&blocked, &blocked_before, sizeof(blocked)) == 0);

    d1l_time_service_core_t exhausted;
    init_core(&exhausted, 0U);
    assert(d1l_time_core_seed_protocol(&exhausted, true, UINT32_MAX) ==
           ESP_OK);
    const d1l_time_service_core_t exhausted_before = exhausted;
    assert(d1l_time_core_preflight_protocol_timestamp(&exhausted, 0U) ==
           ESP_ERR_INVALID_STATE);
    assert(memcmp(&exhausted, &exhausted_before, sizeof(exhausted)) == 0);
}

static void test_validated_wall_aligns_unix_time_without_rewind(void)
{
    d1l_time_service_core_t core;
    init_core(&core, 0U);
    reserve_fixture_t reserve = {.result = ESP_OK};
    assert(d1l_time_core_set_wall(
               &core, (int64_t)TEST_BUILD_EPOCH + 100, 10U,
               D1L_TIME_VALIDITY_NETWORK_VALIDATED,
               D1L_TIME_SOURCE_SNTP) == ESP_OK);
    d1l_time_core_snapshot_t snapshot;
    d1l_time_core_snapshot(&core, UINT64_C(5000010), &snapshot);
    assert(snapshot.wall_epoch_sec == (int64_t)TEST_BUILD_EPOCH + 105);
    assert(snapshot.protocol_wall_admission ==
           D1L_TIME_PROTOCOL_WALL_SNTP_ADMITTED);
    assert(snapshot.certificate_time_valid);

    uint32_t first = 0U;
    assert(d1l_time_core_next_protocol_timestamp(
               &core, UINT64_C(5000010), fake_reserve, &reserve, &first) ==
           ESP_OK);
    assert(first == TEST_BUILD_EPOCH + 105U);

    assert(d1l_time_core_set_wall(
               &core, D1L_TIME_WALL_MIN_EPOCH, UINT64_C(6000000),
               D1L_TIME_VALIDITY_NETWORK_VALIDATED,
               D1L_TIME_SOURCE_SNTP) == ESP_OK);
    uint32_t after_backward_jump = 0U;
    assert(d1l_time_core_next_protocol_timestamp(
               &core, UINT64_C(6000000), fake_reserve, &reserve,
               &after_backward_jump) == ESP_OK);
    assert(after_backward_jump == first + 1U);

    assert(d1l_time_core_set_wall(
               &core, INT64_C(1900000000), UINT64_C(7000000),
               D1L_TIME_VALIDITY_COMPANION_VALIDATED,
               D1L_TIME_SOURCE_COMPANION_AUTHENTICATED) == ESP_OK);
    uint32_t after_forward_jump = 0U;
    assert(d1l_time_core_next_protocol_timestamp(
               &core, UINT64_C(7000000), fake_reserve, &reserve,
               &after_forward_jump) == ESP_OK);
    assert(after_forward_jump == UINT32_C(1900000000));
}

static void test_approximate_wall_never_advances_protocol(void)
{
    d1l_time_service_core_t core;
    init_core(&core, 0U);
    assert(d1l_time_core_note_authenticated_lower_bound(
               &core, (int64_t)UINT32_MAX, 0U) == ESP_OK);
    d1l_time_core_snapshot_t snapshot;
    d1l_time_core_snapshot(&core, 0U, &snapshot);
    assert(snapshot.protocol_wall_admission ==
           D1L_TIME_PROTOCOL_WALL_APPROXIMATE_IGNORED);
    assert(!snapshot.certificate_time_valid);

    reserve_fixture_t reserve = {.result = ESP_OK};
    uint32_t timestamp = 0U;
    assert(d1l_time_core_next_protocol_timestamp(
               &core, 0U, fake_reserve, &reserve, &timestamp) == ESP_OK);
    assert(timestamp == TEST_BUILD_EPOCH);
    assert(!core.protocol_exhausted);
}

static void test_sntp_forward_quarantine_has_zero_reservation_side_effects(void)
{
    const int64_t ceiling =
        (int64_t)TEST_BUILD_EPOCH + D1L_TIME_SNTP_MAX_FORWARD_SEC;
    d1l_time_service_core_t admitted;
    init_core(&admitted, 0U);
    assert(d1l_time_core_set_wall(
               &admitted, ceiling, 0U,
               D1L_TIME_VALIDITY_NETWORK_VALIDATED,
               D1L_TIME_SOURCE_SNTP) == ESP_OK);
    reserve_fixture_t reserve = {.result = ESP_OK};
    uint32_t timestamp = 0U;
    assert(d1l_time_core_next_protocol_timestamp(
               &admitted, 0U, fake_reserve, &reserve, &timestamp) == ESP_OK);
    assert(timestamp == (uint32_t)ceiling);

    d1l_time_service_core_t blocked;
    init_core(&blocked, 0U);
    assert(d1l_time_core_set_wall(
               &blocked, ceiling + 1, 0U,
               D1L_TIME_VALIDITY_NETWORK_VALIDATED,
               D1L_TIME_SOURCE_SNTP) == ESP_OK);
    d1l_time_core_snapshot_t snapshot;
    d1l_time_core_snapshot(&blocked, 0U, &snapshot);
    assert(snapshot.certificate_time_valid);
    assert(snapshot.protocol_wall_admission ==
           D1L_TIME_PROTOCOL_WALL_SNTP_FORWARD_BLOCKED);
    assert(!snapshot.protocol_tx_ready);
    reserve = (reserve_fixture_t) {.result = ESP_OK};
    timestamp = UINT32_C(0xA5A5A5A5);
    assert(d1l_time_core_next_protocol_timestamp(
               &blocked, 0U, fake_reserve, &reserve, &timestamp) ==
           ESP_ERR_INVALID_STATE);
    assert(timestamp == UINT32_C(0xA5A5A5A5));
    assert(reserve.calls == 0U);
    assert(blocked.protocol_reserved_through ==
           D1L_TIME_PROTOCOL_TIMESTAMP_BASE);

    assert(d1l_time_core_set_wall(
               &blocked, (int64_t)TEST_BUILD_EPOCH + 1000, 0U,
               D1L_TIME_VALIDITY_NETWORK_VALIDATED,
               D1L_TIME_SOURCE_SNTP) == ESP_OK);
    assert(d1l_time_core_next_protocol_timestamp(
               &blocked, 0U, fake_reserve, &reserve, &timestamp) == ESP_OK);
    assert(timestamp == TEST_BUILD_EPOCH + 1000U);
}

static void test_authenticated_companion_recovers_blocked_sntp(void)
{
    d1l_time_service_core_t core;
    init_core(&core, 0U);
    assert(d1l_time_core_set_wall(
               &core,
               (int64_t)TEST_BUILD_EPOCH +
                   D1L_TIME_SNTP_MAX_FORWARD_SEC + 1,
               0U, D1L_TIME_VALIDITY_NETWORK_VALIDATED,
               D1L_TIME_SOURCE_SNTP) == ESP_OK);
    assert(d1l_time_core_set_wall(
               &core, INT64_C(2000000000), 0U,
               D1L_TIME_VALIDITY_COMPANION_VALIDATED,
               D1L_TIME_SOURCE_COMPANION_AUTHENTICATED) == ESP_OK);
    reserve_fixture_t reserve = {.result = ESP_OK};
    uint32_t timestamp = 0U;
    assert(d1l_time_core_next_protocol_timestamp(
               &core, 0U, fake_reserve, &reserve, &timestamp) == ESP_OK);
    assert(timestamp == UINT32_C(2000000000));
}

static void test_admitted_wall_has_no_fixed_elapsed_horizon(void)
{
    d1l_time_service_core_t core;
    init_core(&core, 0U);
    const int64_t initial_ceiling =
        (int64_t)TEST_BUILD_EPOCH + D1L_TIME_SNTP_MAX_FORWARD_SEC;
    assert(d1l_time_core_set_wall(
               &core, initial_ceiling, 0U,
               D1L_TIME_VALIDITY_NETWORK_VALIDATED,
               D1L_TIME_SOURCE_SNTP) == ESP_OK);
    const uint64_t ten_days_us = UINT64_C(10) * 24U * 60U * 60U * 1000000U;
    reserve_fixture_t reserve = {.result = ESP_OK};
    uint32_t timestamp = 0U;
    assert(d1l_time_core_next_protocol_timestamp(
               &core, ten_days_us, fake_reserve, &reserve, &timestamp) ==
           ESP_OK);
    assert(timestamp == (uint32_t)(initial_ceiling + 10 * 24 * 60 * 60));

    d1l_time_core_snapshot_t snapshot;
    d1l_time_core_snapshot(&core, ten_days_us, &snapshot);
    assert(snapshot.protocol_trust_anchor >= timestamp);
    assert(snapshot.protocol_sntp_ceiling > (uint32_t)initial_ceiling);
    assert(d1l_time_core_set_wall(
               &core, (int64_t)snapshot.protocol_sntp_ceiling, ten_days_us,
               D1L_TIME_VALIDITY_NETWORK_VALIDATED,
               D1L_TIME_SOURCE_SNTP) == ESP_OK);
    assert(core.protocol_wall_admission ==
           D1L_TIME_PROTOCOL_WALL_SNTP_ADMITTED);

    d1l_time_service_core_t never_anchor_blocked;
    init_core(&never_anchor_blocked, 0U);
    assert(d1l_time_core_set_wall(
               &never_anchor_blocked, initial_ceiling + 1, 0U,
               D1L_TIME_VALIDITY_NETWORK_VALIDATED,
               D1L_TIME_SOURCE_SNTP) == ESP_OK);
    assert(d1l_time_core_set_wall(
               &never_anchor_blocked,
               initial_ceiling + D1L_TIME_SNTP_MAX_FORWARD_SEC, 0U,
               D1L_TIME_VALIDITY_NETWORK_VALIDATED,
               D1L_TIME_SOURCE_SNTP) == ESP_OK);
    d1l_time_core_snapshot(&never_anchor_blocked, 0U, &snapshot);
    assert(snapshot.protocol_wall_admission ==
           D1L_TIME_PROTOCOL_WALL_SNTP_FORWARD_BLOCKED);
    assert(snapshot.protocol_trust_anchor == TEST_BUILD_EPOCH);
}

static void test_sntp_headroom_boundary_is_exact_and_nonwrapping(void)
{
    const uint32_t high_water =
        (uint32_t)D1L_TIME_SNTP_MAX_PROTOCOL_EPOCH - 100U;
    d1l_time_service_core_t exact;
    init_core(&exact, 0U);
    assert(d1l_time_core_seed_protocol(&exact, true, high_water) == ESP_OK);
    assert(d1l_time_core_set_wall(
               &exact, D1L_TIME_SNTP_MAX_PROTOCOL_EPOCH, 0U,
               D1L_TIME_VALIDITY_NETWORK_VALIDATED,
               D1L_TIME_SOURCE_SNTP) == ESP_OK);
    reserve_fixture_t reserve = {.result = ESP_OK};
    uint32_t timestamp = 0U;
    assert(d1l_time_core_next_protocol_timestamp(
               &exact, 0U, fake_reserve, &reserve, &timestamp) == ESP_OK);
    assert(timestamp == (uint32_t)D1L_TIME_SNTP_MAX_PROTOCOL_EPOCH);
    assert(reserve.persisted_through == timestamp +
           D1L_TIME_PROTOCOL_RESERVATION_SIZE - 1U);

    d1l_time_service_core_t over;
    init_core(&over, 0U);
    assert(d1l_time_core_seed_protocol(&over, true, high_water) == ESP_OK);
    assert(d1l_time_core_set_wall(
               &over, D1L_TIME_SNTP_MAX_PROTOCOL_EPOCH + 1, 0U,
               D1L_TIME_VALIDITY_NETWORK_VALIDATED,
               D1L_TIME_SOURCE_SNTP) == ESP_OK);
    assert(over.protocol_wall_admission ==
           D1L_TIME_PROTOCOL_WALL_SNTP_FORWARD_BLOCKED);
}

static void test_stale_sntp_generation_cannot_overwrite_companion_time(void)
{
    d1l_time_service_core_t core;
    init_core(&core, 0U);
    const uint32_t before_companion = core.wall_generation;
    assert(d1l_time_core_set_wall(
               &core, (int64_t)TEST_BUILD_EPOCH + 200, 0U,
               D1L_TIME_VALIDITY_COMPANION_VALIDATED,
               D1L_TIME_SOURCE_COMPANION_AUTHENTICATED) == ESP_OK);
    assert(d1l_time_core_set_wall_if_generation(
               &core, before_companion, (int64_t)TEST_BUILD_EPOCH + 100, 0U,
               D1L_TIME_VALIDITY_NETWORK_VALIDATED,
               D1L_TIME_SOURCE_SNTP) == ESP_ERR_INVALID_STATE);

    d1l_time_core_snapshot_t snapshot;
    d1l_time_core_snapshot(&core, 0U, &snapshot);
    assert(snapshot.wall_epoch_sec == (int64_t)TEST_BUILD_EPOCH + 200);
    assert(snapshot.wall_source == D1L_TIME_SOURCE_COMPANION_AUTHENTICATED);

    assert(d1l_time_core_set_wall_if_generation(
               &core, snapshot.wall_generation,
               (int64_t)TEST_BUILD_EPOCH + 300, 0U,
               D1L_TIME_VALIDITY_NETWORK_VALIDATED,
               D1L_TIME_SOURCE_SNTP) == ESP_OK);
    d1l_time_core_snapshot(&core, 0U, &snapshot);
    assert(snapshot.wall_source == D1L_TIME_SOURCE_SNTP);
    assert(snapshot.protocol_wall_admission ==
           D1L_TIME_PROTOCOL_WALL_SNTP_ADMITTED);
}

static void test_invalid_wall_pairs_and_generation_fail_preflight(void)
{
    d1l_time_service_core_t core;
    init_core(&core, 0U);
    assert(d1l_time_core_preflight_wall(
               &core, D1L_TIME_WALL_MIN_EPOCH - 1,
               D1L_TIME_VALIDITY_NETWORK_VALIDATED,
               D1L_TIME_SOURCE_SNTP) == ESP_ERR_INVALID_ARG);
    assert(d1l_time_core_preflight_wall(
               &core, D1L_TIME_WALL_MIN_EPOCH,
               D1L_TIME_VALIDITY_NETWORK_VALIDATED,
               D1L_TIME_SOURCE_COMPANION_AUTHENTICATED) ==
           ESP_ERR_INVALID_ARG);
    core.wall_generation = UINT32_MAX;
    assert(d1l_time_core_preflight_wall(
               &core, D1L_TIME_WALL_MIN_EPOCH,
               D1L_TIME_VALIDITY_NETWORK_VALIDATED,
               D1L_TIME_SOURCE_SNTP) == ESP_ERR_INVALID_STATE);
    assert(!core.wall_set);
}

static void test_lower_bound_never_rewinds_or_downgrades_validated_time(void)
{
    d1l_time_service_core_t approximate;
    init_core(&approximate, 0U);
    assert(d1l_time_core_note_authenticated_lower_bound(
               &approximate, INT64_C(1800000000), 0U) == ESP_OK);
    d1l_time_core_snapshot_t snapshot;
    d1l_time_core_snapshot(&approximate, 0U, &snapshot);
    assert(snapshot.wall_epoch_sec == INT64_C(1800000000));
    assert(snapshot.wall_validity == D1L_TIME_VALIDITY_APPROXIMATE);
    const uint32_t first_generation = snapshot.wall_generation;

    assert(d1l_time_core_note_authenticated_lower_bound(
               &approximate, INT64_C(1705000000), UINT64_C(1000000)) ==
           ESP_OK);
    d1l_time_core_snapshot(&approximate, UINT64_C(1000000), &snapshot);
    assert(snapshot.wall_epoch_sec == INT64_C(1800000001));
    assert(snapshot.wall_generation == first_generation);

    d1l_time_service_core_t validated;
    init_core(&validated, 0U);
    assert(d1l_time_core_set_wall(
               &validated, (int64_t)TEST_BUILD_EPOCH + 100, 0U,
               D1L_TIME_VALIDITY_NETWORK_VALIDATED,
               D1L_TIME_SOURCE_SNTP) == ESP_OK);
    assert(d1l_time_core_note_authenticated_lower_bound(
               &validated, INT64_C(1900000000), 0U) == ESP_OK);
    d1l_time_core_snapshot(&validated, 0U, &snapshot);
    assert(snapshot.wall_epoch_sec == (int64_t)TEST_BUILD_EPOCH + 100);
    assert(snapshot.wall_validity == D1L_TIME_VALIDITY_NETWORK_VALIDATED);
}

static void test_companion_unrepresentable_and_protocol_exhaustion_fail_closed(void)
{
    d1l_time_service_core_t unrepresentable;
    init_core(&unrepresentable, 0U);
    assert(d1l_time_core_set_wall(
               &unrepresentable, INT64_C(4294967296), 0U,
               D1L_TIME_VALIDITY_COMPANION_VALIDATED,
               D1L_TIME_SOURCE_COMPANION_AUTHENTICATED) == ESP_OK);
    d1l_time_core_snapshot_t snapshot;
    d1l_time_core_snapshot(&unrepresentable, 0U, &snapshot);
    assert(snapshot.wall_valid && snapshot.certificate_time_valid);
    assert(snapshot.protocol_wall_admission ==
           D1L_TIME_PROTOCOL_WALL_UNREPRESENTABLE);
    assert(!snapshot.protocol_tx_ready);
    reserve_fixture_t reserve = {.result = ESP_OK};
    uint32_t timestamp = UINT32_C(0xA5A5A5A5);
    assert(d1l_time_core_next_protocol_timestamp(
               &unrepresentable, 0U, fake_reserve, &reserve, &timestamp) ==
           ESP_ERR_INVALID_STATE);
    assert(reserve.calls == 0U);

    d1l_time_service_core_t final_value;
    init_core(&final_value, 0U);
    assert(d1l_time_core_set_wall(
               &final_value, (int64_t)UINT32_MAX, 0U,
               D1L_TIME_VALIDITY_COMPANION_VALIDATED,
               D1L_TIME_SOURCE_COMPANION_AUTHENTICATED) == ESP_OK);
    assert(d1l_time_core_next_protocol_timestamp(
               &final_value, 0U, fake_reserve, &reserve, &timestamp) == ESP_OK);
    assert(timestamp == UINT32_MAX);
    assert(final_value.protocol_exhausted);
    assert(d1l_time_core_next_protocol_timestamp(
               &final_value, 0U, fake_reserve, &reserve, &timestamp) ==
           ESP_ERR_INVALID_STATE);

    d1l_time_service_core_t exhausted;
    init_core(&exhausted, 0U);
    assert(d1l_time_core_seed_protocol(&exhausted, true, UINT32_MAX) ==
           ESP_OK);
    assert(d1l_time_core_next_protocol_timestamp(
               &exhausted, 0U, fake_reserve, &reserve, &timestamp) ==
           ESP_ERR_INVALID_STATE);
}

static void test_protocol_seed_cannot_rewind_after_allocation(void)
{
    d1l_time_service_core_t core;
    init_core(&core, 0U);
    reserve_fixture_t reserve = {.result = ESP_OK};
    uint32_t timestamp = 0U;
    assert(d1l_time_core_next_protocol_timestamp(
               &core, 0U, fake_reserve, &reserve, &timestamp) == ESP_OK);
    assert(d1l_time_core_seed_protocol(
               &core, true, D1L_TIME_PROTOCOL_TIMESTAMP_BASE) ==
           ESP_ERR_INVALID_STATE);
}

int main(void)
{
    test_cold_boot_is_build_anchored_and_honest();
    test_protocol_seed_classification_fails_closed_on_legacy_state();
    test_protocol_ranges_reduce_writes_and_survive_reboot();
    test_failed_reservation_consumes_no_timestamp();
    test_protocol_preflight_is_pure_and_matches_allocator_blocks();
    test_validated_wall_aligns_unix_time_without_rewind();
    test_approximate_wall_never_advances_protocol();
    test_sntp_forward_quarantine_has_zero_reservation_side_effects();
    test_authenticated_companion_recovers_blocked_sntp();
    test_admitted_wall_has_no_fixed_elapsed_horizon();
    test_sntp_headroom_boundary_is_exact_and_nonwrapping();
    test_stale_sntp_generation_cannot_overwrite_companion_time();
    test_invalid_wall_pairs_and_generation_fail_preflight();
    test_lower_bound_never_rewinds_or_downgrades_validated_time();
    test_companion_unrepresentable_and_protocol_exhaustion_fail_closed();
    test_protocol_seed_cannot_rewind_after_allocation();
    puts("native truthful-time core: ok");
    return 0;
}
