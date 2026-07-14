#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "platform/time_service_core.h"

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

static void test_cold_boot_is_honestly_monotonic_only(void)
{
    d1l_time_service_core_t core;
    d1l_time_core_init(&core, UINT64_C(1000000));
    d1l_time_core_snapshot_t snapshot;
    d1l_time_core_snapshot(&core, UINT64_C(1500000), &snapshot);
    assert(snapshot.boot_monotonic_us == UINT64_C(500000));
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
}

static void test_protocol_ranges_reduce_writes_and_survive_reboot(void)
{
    d1l_time_service_core_t first;
    d1l_time_core_init(&first, 0U);
    assert(d1l_time_core_seed_protocol(&first, false, 0U) == ESP_OK);
    reserve_fixture_t reserve = {.result = ESP_OK};
    uint32_t last = 0U;
    for (uint32_t i = 0U; i < D1L_TIME_PROTOCOL_RESERVATION_SIZE; ++i) {
        assert(d1l_time_core_next_protocol_timestamp(
                   &first, i, fake_reserve, &reserve, &last) == ESP_OK);
        assert(last == D1L_TIME_PROTOCOL_TIMESTAMP_BASE + 1U + i);
    }
    assert(reserve.calls == 1U);
    assert(reserve.persisted_through ==
           D1L_TIME_PROTOCOL_TIMESTAMP_BASE +
               D1L_TIME_PROTOCOL_RESERVATION_SIZE);

    assert(d1l_time_core_next_protocol_timestamp(
               &first, 100U, fake_reserve, &reserve, &last) == ESP_OK);
    assert(reserve.calls == 2U);
    const uint32_t pre_reboot_last = last;
    const uint32_t persisted = reserve.persisted_through;

    d1l_time_service_core_t rebooted;
    d1l_time_core_init(&rebooted, 0U);
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
    d1l_time_core_init(&core, 0U);
    reserve_fixture_t reserve = {.result = ESP_FAIL};
    uint32_t timestamp = 0xA5A5A5A5U;
    assert(d1l_time_core_next_protocol_timestamp(
               &core, 0U, fake_reserve, &reserve, &timestamp) == ESP_FAIL);
    assert(timestamp == 0xA5A5A5A5U);
    assert(core.protocol_next == D1L_TIME_PROTOCOL_TIMESTAMP_BASE + 1U);
    assert(!core.protocol_started);

    reserve.result = ESP_OK;
    assert(d1l_time_core_next_protocol_timestamp(
               &core, 1U, fake_reserve, &reserve, &timestamp) == ESP_OK);
    assert(timestamp == D1L_TIME_PROTOCOL_TIMESTAMP_BASE + 1U);
}

static void test_wall_jumps_never_rewind_protocol_uniqueness(void)
{
    d1l_time_service_core_t core;
    d1l_time_core_init(&core, 0U);
    reserve_fixture_t reserve = {.result = ESP_OK};
    assert(d1l_time_core_set_wall(
               &core, INT64_C(1800000000), 10U,
               D1L_TIME_VALIDITY_NETWORK_VALIDATED,
               D1L_TIME_SOURCE_SNTP) == ESP_OK);
    d1l_time_core_snapshot_t snapshot;
    d1l_time_core_snapshot(&core, UINT64_C(5000010), &snapshot);
    assert(snapshot.wall_epoch_sec == INT64_C(1800000005));
    assert(snapshot.certificate_time_valid);

    uint32_t first = 0U;
    assert(d1l_time_core_next_protocol_timestamp(
               &core, UINT64_C(5000010), fake_reserve, &reserve, &first) ==
           ESP_OK);
    assert(first == UINT32_C(1800000005));

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
    assert(after_forward_jump > after_backward_jump);
}

static void test_stale_sntp_generation_cannot_overwrite_companion_time(void)
{
    d1l_time_service_core_t core;
    d1l_time_core_init(&core, 0U);
    const uint32_t before_companion = core.wall_generation;
    assert(d1l_time_core_set_wall(
               &core, INT64_C(1900000000), 0U,
               D1L_TIME_VALIDITY_COMPANION_VALIDATED,
               D1L_TIME_SOURCE_COMPANION_AUTHENTICATED) == ESP_OK);
    assert(d1l_time_core_set_wall_if_generation(
               &core, before_companion, INT64_C(1800000000), 0U,
               D1L_TIME_VALIDITY_NETWORK_VALIDATED,
               D1L_TIME_SOURCE_SNTP) == ESP_ERR_INVALID_STATE);

    d1l_time_core_snapshot_t snapshot;
    d1l_time_core_snapshot(&core, 0U, &snapshot);
    assert(snapshot.wall_epoch_sec == INT64_C(1900000000));
    assert(snapshot.wall_validity == D1L_TIME_VALIDITY_COMPANION_VALIDATED);
    assert(snapshot.wall_source == D1L_TIME_SOURCE_COMPANION_AUTHENTICATED);

    assert(d1l_time_core_set_wall_if_generation(
               &core, snapshot.wall_generation, INT64_C(2000000000), 0U,
               D1L_TIME_VALIDITY_NETWORK_VALIDATED,
               D1L_TIME_SOURCE_SNTP) == ESP_OK);
    d1l_time_core_snapshot(&core, 0U, &snapshot);
    assert(snapshot.wall_epoch_sec == INT64_C(2000000000));
    assert(snapshot.wall_source == D1L_TIME_SOURCE_SNTP);
}

static void test_approximate_time_never_unlocks_certificate_validation(void)
{
    d1l_time_service_core_t core;
    d1l_time_core_init(&core, 0U);
    assert(d1l_time_core_set_wall(
               &core, D1L_TIME_WALL_MIN_EPOCH, 0U,
               D1L_TIME_VALIDITY_APPROXIMATE,
               D1L_TIME_SOURCE_RETAINED_AUTHENTICATED) == ESP_OK);
    d1l_time_core_snapshot_t snapshot;
    d1l_time_core_snapshot(&core, 0U, &snapshot);
    assert(snapshot.wall_valid);
    assert(!snapshot.certificate_time_valid);
    assert(strcmp(d1l_time_validity_name(snapshot.wall_validity),
                  "approximate") == 0);
    assert(strcmp(d1l_time_source_name(snapshot.wall_source),
                  "retained_authenticated") == 0);
}

static void test_invalid_wall_source_pairs_fail_closed(void)
{
    d1l_time_service_core_t core;
    d1l_time_core_init(&core, 0U);
    assert(d1l_time_core_set_wall(
               &core, D1L_TIME_WALL_MIN_EPOCH - 1, 0U,
               D1L_TIME_VALIDITY_NETWORK_VALIDATED,
               D1L_TIME_SOURCE_SNTP) == ESP_ERR_INVALID_ARG);
    assert(d1l_time_core_set_wall(
               &core, D1L_TIME_WALL_MIN_EPOCH, 0U,
               D1L_TIME_VALIDITY_NETWORK_VALIDATED,
               D1L_TIME_SOURCE_COMPANION_AUTHENTICATED) ==
           ESP_ERR_INVALID_ARG);
    assert(d1l_time_core_set_wall(
               &core, D1L_TIME_WALL_MIN_EPOCH, 0U,
               D1L_TIME_VALIDITY_MONOTONIC_ONLY,
               D1L_TIME_SOURCE_BOOT_MONOTONIC) == ESP_ERR_INVALID_ARG);
    assert(core.wall_generation == 0U);
}

static void test_lower_bound_never_rewinds_or_downgrades_validated_time(void)
{
    d1l_time_service_core_t approximate;
    d1l_time_core_init(&approximate, 0U);
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

    assert(d1l_time_core_note_authenticated_lower_bound(
               &approximate, INT64_C(1900000000), UINT64_C(2000000)) ==
           ESP_OK);
    d1l_time_core_snapshot(&approximate, UINT64_C(2000000), &snapshot);
    assert(snapshot.wall_epoch_sec == INT64_C(1900000000));
    assert(snapshot.wall_generation == first_generation + 1U);

    d1l_time_service_core_t validated;
    d1l_time_core_init(&validated, 0U);
    assert(d1l_time_core_set_wall(
               &validated, INT64_C(1800000000), 0U,
               D1L_TIME_VALIDITY_NETWORK_VALIDATED,
               D1L_TIME_SOURCE_SNTP) == ESP_OK);
    assert(d1l_time_core_note_authenticated_lower_bound(
               &validated, INT64_C(1900000000), 0U) == ESP_OK);
    d1l_time_core_snapshot(&validated, 0U, &snapshot);
    assert(snapshot.wall_epoch_sec == INT64_C(1800000000));
    assert(snapshot.wall_validity == D1L_TIME_VALIDITY_NETWORK_VALIDATED);
    assert(snapshot.wall_source == D1L_TIME_SOURCE_SNTP);
    assert(snapshot.wall_generation == 1U);
}

static void test_protocol_exhaustion_is_fail_closed(void)
{
    d1l_time_service_core_t exhausted;
    d1l_time_core_init(&exhausted, 0U);
    assert(d1l_time_core_seed_protocol(&exhausted, true, UINT32_MAX) ==
           ESP_OK);
    uint32_t timestamp = 0U;
    reserve_fixture_t reserve = {.result = ESP_OK};
    assert(d1l_time_core_next_protocol_timestamp(
               &exhausted, 0U, fake_reserve, &reserve, &timestamp) ==
           ESP_ERR_INVALID_STATE);
    assert(reserve.calls == 0U);

    d1l_time_service_core_t final_value;
    d1l_time_core_init(&final_value, 0U);
    assert(d1l_time_core_seed_protocol(&final_value, true, UINT32_MAX - 1U) ==
           ESP_OK);
    assert(d1l_time_core_next_protocol_timestamp(
               &final_value, 0U, fake_reserve, &reserve, &timestamp) == ESP_OK);
    assert(timestamp == UINT32_MAX);
    assert(final_value.protocol_exhausted);
    assert(d1l_time_core_next_protocol_timestamp(
               &final_value, 0U, fake_reserve, &reserve, &timestamp) ==
           ESP_ERR_INVALID_STATE);
}

static void test_wall_values_above_protocol_width_do_not_wrap(void)
{
    d1l_time_service_core_t core;
    d1l_time_core_init(&core, 0U);
    assert(d1l_time_core_set_wall(
               &core, INT64_MAX, 0U,
               D1L_TIME_VALIDITY_NETWORK_VALIDATED,
               D1L_TIME_SOURCE_SNTP) == ESP_OK);
    d1l_time_core_snapshot_t snapshot;
    d1l_time_core_snapshot(&core, UINT64_MAX, &snapshot);
    assert(snapshot.wall_epoch_sec == INT64_MAX);

    reserve_fixture_t reserve = {.result = ESP_OK};
    uint32_t timestamp = 0U;
    assert(d1l_time_core_next_protocol_timestamp(
               &core, UINT64_MAX, fake_reserve, &reserve, &timestamp) == ESP_OK);
    assert(timestamp == D1L_TIME_PROTOCOL_TIMESTAMP_BASE + 1U);
}

static void test_protocol_seed_cannot_rewind_after_allocation(void)
{
    d1l_time_service_core_t core;
    d1l_time_core_init(&core, 0U);
    reserve_fixture_t reserve = {.result = ESP_OK};
    uint32_t timestamp = 0U;
    assert(d1l_time_core_next_protocol_timestamp(
               &core, 0U, fake_reserve, &reserve, &timestamp) == ESP_OK);
    assert(d1l_time_core_seed_protocol(
               &core, true, D1L_TIME_PROTOCOL_TIMESTAMP_BASE) ==
           ESP_ERR_INVALID_STATE);
}

static void test_wall_generation_saturation_fails_closed_without_aba(void)
{
    d1l_time_service_core_t core;
    d1l_time_core_init(&core, 0U);
    core.wall_generation = UINT32_MAX;
    core.wall_anchor_epoch_sec = INT64_C(1900000000);
    core.wall_anchor_monotonic_us = 0U;
    core.wall_validity = D1L_TIME_VALIDITY_COMPANION_VALIDATED;
    core.wall_source = D1L_TIME_SOURCE_COMPANION_AUTHENTICATED;
    core.wall_set = true;

    assert(d1l_time_core_set_wall_if_generation(
               &core, UINT32_MAX, INT64_C(1800000000), 1U,
               D1L_TIME_VALIDITY_NETWORK_VALIDATED,
               D1L_TIME_SOURCE_SNTP) == ESP_ERR_INVALID_STATE);
    d1l_time_core_snapshot_t snapshot;
    d1l_time_core_snapshot(&core, 1U, &snapshot);
    assert(snapshot.wall_generation == UINT32_MAX);
    assert(snapshot.wall_epoch_sec == INT64_C(1900000000));
    assert(snapshot.wall_source == D1L_TIME_SOURCE_COMPANION_AUTHENTICATED);
}

int main(void)
{
    test_cold_boot_is_honestly_monotonic_only();
    test_protocol_seed_classification_fails_closed_on_legacy_state();
    test_protocol_ranges_reduce_writes_and_survive_reboot();
    test_failed_reservation_consumes_no_timestamp();
    test_wall_jumps_never_rewind_protocol_uniqueness();
    test_stale_sntp_generation_cannot_overwrite_companion_time();
    test_approximate_time_never_unlocks_certificate_validation();
    test_invalid_wall_source_pairs_fail_closed();
    test_lower_bound_never_rewinds_or_downgrades_validated_time();
    test_protocol_exhaustion_is_fail_closed();
    test_wall_values_above_protocol_width_do_not_wrap();
    test_protocol_seed_cannot_rewind_after_allocation();
    test_wall_generation_saturation_fails_closed_without_aba();
    puts("native truthful-time core: ok");
    return 0;
}
