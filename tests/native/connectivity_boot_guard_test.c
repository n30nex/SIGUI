#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "comms/connectivity_boot_guard.h"

static void test_checksum_and_scoped_recovery(void)
{
    d1l_connectivity_boot_guard_record_t record;
    d1l_connectivity_boot_guard_init(&record);
    assert(d1l_connectivity_boot_guard_valid(&record));
    assert(record.generation == 1U);

    record.checksum ^= 1U;
    assert(!d1l_connectivity_boot_guard_valid(&record));
    const d1l_connectivity_boot_guard_decision_t recovered =
        d1l_connectivity_boot_guard_note_boot(&record, true);
    assert(!recovered.previous_record_valid);
    assert(!recovered.crash_attributed);
    assert(!recovered.crash_loop_detected);
    assert(d1l_connectivity_boot_guard_valid(&record));
    assert(record.consecutive_crash_boots == 0U);
}

static void test_repeated_wifi_crashes_are_attributed_and_bounded(void)
{
    d1l_connectivity_boot_guard_record_t record;
    d1l_connectivity_boot_guard_init(&record);
    d1l_connectivity_boot_guard_mark_active(
        &record, D1L_CONNECTIVITY_SUBSYSTEM_WIFI);

    d1l_connectivity_boot_guard_decision_t decision =
        d1l_connectivity_boot_guard_note_boot(&record, true);
    assert(decision.previous_record_valid);
    assert(decision.crash_attributed);
    assert(!decision.crash_loop_detected);
    assert(decision.consecutive_crash_boots == 1U);
    assert(decision.previous_active_subsystem ==
           D1L_CONNECTIVITY_SUBSYSTEM_WIFI);
    assert(record.last_active_subsystem ==
           D1L_CONNECTIVITY_SUBSYSTEM_NONE);

    /* A crash while Wi-Fi has not re-armed must neither be attributed nor
     * preserve a sequence that is no longer consecutive. */
    decision = d1l_connectivity_boot_guard_note_boot(&record, true);
    assert(!decision.crash_attributed);
    assert(!decision.crash_loop_detected);
    assert(decision.consecutive_crash_boots == 0U);

    d1l_connectivity_boot_guard_mark_active(
        &record, D1L_CONNECTIVITY_SUBSYSTEM_WIFI);
    decision = d1l_connectivity_boot_guard_note_boot(&record, true);
    assert(decision.crash_attributed);
    assert(!decision.crash_loop_detected);
    assert(decision.consecutive_crash_boots == 1U);
    d1l_connectivity_boot_guard_mark_active(
        &record, D1L_CONNECTIVITY_SUBSYSTEM_WIFI);
    decision = d1l_connectivity_boot_guard_note_boot(&record, true);
    assert(decision.crash_loop_detected);
    assert(decision.consecutive_crash_boots ==
           D1L_CONNECTIVITY_BOOT_GUARD_FAILURE_LIMIT);

    d1l_connectivity_boot_guard_clear(&record);
    for (uint16_t count = 0U; count < UINT8_MAX; ++count) {
        d1l_connectivity_boot_guard_mark_active(
            &record, D1L_CONNECTIVITY_SUBSYSTEM_WIFI);
        decision = d1l_connectivity_boot_guard_note_boot(&record, true);
    }
    d1l_connectivity_boot_guard_mark_active(
        &record, D1L_CONNECTIVITY_SUBSYSTEM_WIFI);
    decision = d1l_connectivity_boot_guard_note_boot(&record, true);
    assert(decision.consecutive_crash_boots == UINT8_MAX);
    assert(decision.crash_loop_detected);
}

static void test_stable_acknowledgement_resets_count_without_disarming(void)
{
    d1l_connectivity_boot_guard_record_t record;
    d1l_connectivity_boot_guard_init(&record);
    d1l_connectivity_boot_guard_mark_active(
        &record, D1L_CONNECTIVITY_SUBSYSTEM_WIFI);
    d1l_connectivity_boot_guard_decision_t decision =
        d1l_connectivity_boot_guard_note_boot(&record, true);
    assert(decision.consecutive_crash_boots == 1U);

    d1l_connectivity_boot_guard_mark_active(
        &record, D1L_CONNECTIVITY_SUBSYSTEM_WIFI);
    assert(d1l_connectivity_boot_guard_acknowledge_stable(
        &record, D1L_CONNECTIVITY_SUBSYSTEM_WIFI));
    assert(record.consecutive_crash_boots == 0U);
    assert(record.last_active_subsystem == D1L_CONNECTIVITY_SUBSYSTEM_WIFI);
    assert(d1l_connectivity_boot_guard_valid(&record));
    assert(!d1l_connectivity_boot_guard_acknowledge_stable(
        &record, D1L_CONNECTIVITY_SUBSYSTEM_WIFI));

    d1l_connectivity_boot_guard_clear(&record);
    assert(!d1l_connectivity_boot_guard_acknowledge_stable(
        &record, D1L_CONNECTIVITY_SUBSYSTEM_WIFI));
    assert(record.last_active_subsystem == D1L_CONNECTIVITY_SUBSYSTEM_NONE);
}

static void test_clean_boot_and_explicit_disable_recover(void)
{
    d1l_connectivity_boot_guard_record_t record;
    d1l_connectivity_boot_guard_init(&record);
    d1l_connectivity_boot_guard_mark_active(
        &record, D1L_CONNECTIVITY_SUBSYSTEM_WIFI);
    d1l_connectivity_boot_guard_decision_t decision =
        d1l_connectivity_boot_guard_note_boot(&record, true);
    assert(decision.crash_attributed);
    assert(decision.previous_active_subsystem ==
           D1L_CONNECTIVITY_SUBSYSTEM_WIFI);

    decision = d1l_connectivity_boot_guard_note_boot(&record, false);
    assert(!decision.crash_attributed);
    assert(!decision.crash_loop_detected);
    assert(decision.consecutive_crash_boots == 0U);

    d1l_connectivity_boot_guard_clear(&record);
    assert(d1l_connectivity_boot_guard_valid(&record));
    assert(record.last_active_subsystem == D1L_CONNECTIVITY_SUBSYSTEM_NONE);
    assert(record.consecutive_crash_boots == 0U);
    decision = d1l_connectivity_boot_guard_note_boot(&record, true);
    assert(!decision.crash_attributed);
    assert(!decision.crash_loop_detected);
}

static void test_generation_wrap_and_names(void)
{
    d1l_connectivity_boot_guard_record_t record;
    d1l_connectivity_boot_guard_init(&record);
    record.generation = UINT32_MAX;
    d1l_connectivity_boot_guard_mark_active(
        &record, D1L_CONNECTIVITY_SUBSYSTEM_WIFI);
    /* Direct generation mutation invalidates the checksum, so recovery starts
     * a fresh valid record and the following mutation advances it once. */
    assert(record.generation == 2U);
    assert(d1l_connectivity_boot_guard_valid(&record));

    const d1l_connectivity_boot_guard_record_t unchanged = record;
    d1l_connectivity_boot_guard_mark_active(
        &record, D1L_CONNECTIVITY_SUBSYSTEM_NONE);
    assert(memcmp(&record, &unchanged, sizeof(record)) == 0);
    d1l_connectivity_boot_guard_mark_active(
        &record, (d1l_connectivity_subsystem_t)-1);
    assert(memcmp(&record, &unchanged, sizeof(record)) == 0);
    d1l_connectivity_boot_guard_mark_active(
        &record, (d1l_connectivity_subsystem_t)99);
    assert(memcmp(&record, &unchanged, sizeof(record)) == 0);

    assert(strcmp(d1l_connectivity_subsystem_name(
                      D1L_CONNECTIVITY_SUBSYSTEM_NONE), "none") == 0);
    assert(strcmp(d1l_connectivity_subsystem_name(
                      D1L_CONNECTIVITY_SUBSYSTEM_WIFI), "wifi") == 0);
}

int main(void)
{
    test_checksum_and_scoped_recovery();
    test_repeated_wifi_crashes_are_attributed_and_bounded();
    test_stable_acknowledgement_resets_count_without_disarming();
    test_clean_boot_and_explicit_disable_recover();
    test_generation_wrap_and_names();
    puts("native connectivity boot guard: ok");
    return 0;
}
