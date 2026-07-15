#include "connectivity_boot_guard.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(d1l_connectivity_boot_guard_record_t) == 16U,
               "connectivity boot guard NVS record layout changed");
_Static_assert(D1L_CONNECTIVITY_BOOT_GUARD_FAILURE_LIMIT > 1U,
               "crash-loop detection requires repeated failures");

static uint32_t crc32_byte(uint32_t crc, uint8_t value)
{
    crc ^= value;
    for (uint8_t bit = 0U; bit < 8U; ++bit) {
        const uint32_t mask = 0U - (crc & 1U);
        crc = (crc >> 1U) ^ (0xEDB88320U & mask);
    }
    return crc;
}

static uint32_t crc32_u32_le(uint32_t crc, uint32_t value)
{
    for (uint8_t shift = 0U; shift < 32U; shift += 8U) {
        crc = crc32_byte(crc, (uint8_t)(value >> shift));
    }
    return crc;
}

static uint32_t record_checksum(
    const d1l_connectivity_boot_guard_record_t *record)
{
    if (!record) {
        return 0U;
    }
    uint32_t crc = UINT32_MAX;
    crc = crc32_u32_le(crc, record->schema);
    crc = crc32_u32_le(crc, record->generation);
    crc = crc32_byte(crc, record->consecutive_crash_boots);
    crc = crc32_byte(crc, record->last_active_subsystem);
    crc = crc32_byte(crc, record->reserved[0]);
    crc = crc32_byte(crc, record->reserved[1]);
    return crc ^ UINT32_MAX;
}

static uint32_t next_generation(uint32_t generation)
{
    return generation == UINT32_MAX ? 1U : generation + 1U;
}

static void seal_record(d1l_connectivity_boot_guard_record_t *record)
{
    record->reserved[0] = 0U;
    record->reserved[1] = 0U;
    record->checksum = record_checksum(record);
}

void d1l_connectivity_boot_guard_init(
    d1l_connectivity_boot_guard_record_t *record)
{
    if (!record) {
        return;
    }
    memset(record, 0, sizeof(*record));
    record->schema = D1L_CONNECTIVITY_BOOT_GUARD_SCHEMA;
    record->generation = 1U;
    seal_record(record);
}

bool d1l_connectivity_boot_guard_valid(
    const d1l_connectivity_boot_guard_record_t *record)
{
    return record &&
           record->schema == D1L_CONNECTIVITY_BOOT_GUARD_SCHEMA &&
           record->generation > 0U &&
           record->last_active_subsystem < D1L_CONNECTIVITY_SUBSYSTEM_COUNT &&
           record->reserved[0] == 0U && record->reserved[1] == 0U &&
           record->checksum == record_checksum(record);
}

d1l_connectivity_boot_guard_decision_t d1l_connectivity_boot_guard_note_boot(
    d1l_connectivity_boot_guard_record_t *record,
    bool crash_like_reset)
{
    d1l_connectivity_boot_guard_decision_t decision = {0};
    if (!record) {
        return decision;
    }

    decision.previous_record_valid =
        d1l_connectivity_boot_guard_valid(record);
    if (!decision.previous_record_valid) {
        d1l_connectivity_boot_guard_init(record);
    }
    decision.previous_active_subsystem =
        (d1l_connectivity_subsystem_t)record->last_active_subsystem;
    decision.crash_attributed = crash_like_reset &&
        decision.previous_active_subsystem != D1L_CONNECTIVITY_SUBSYSTEM_NONE;

    if (decision.crash_attributed) {
        if (record->consecutive_crash_boots < UINT8_MAX) {
            record->consecutive_crash_boots++;
        }
    } else {
        record->consecutive_crash_boots = 0U;
    }
    /* Consume the previous boot's marker. A subsystem must re-arm itself only
     * when it actually starts during this boot, so an unrelated safe-mode
     * reset cannot be misattributed to stale Wi-Fi activity. */
    record->last_active_subsystem = D1L_CONNECTIVITY_SUBSYSTEM_NONE;
    record->generation = next_generation(record->generation);
    seal_record(record);

    decision.consecutive_crash_boots = record->consecutive_crash_boots;
    decision.crash_loop_detected = decision.crash_attributed &&
        record->consecutive_crash_boots >=
            D1L_CONNECTIVITY_BOOT_GUARD_FAILURE_LIMIT;
    return decision;
}

void d1l_connectivity_boot_guard_mark_active(
    d1l_connectivity_boot_guard_record_t *record,
    d1l_connectivity_subsystem_t subsystem)
{
    if (!record || subsystem <= D1L_CONNECTIVITY_SUBSYSTEM_NONE ||
        subsystem >= D1L_CONNECTIVITY_SUBSYSTEM_COUNT) {
        return;
    }
    if (!d1l_connectivity_boot_guard_valid(record)) {
        d1l_connectivity_boot_guard_init(record);
    }
    record->last_active_subsystem = (uint8_t)subsystem;
    record->generation = next_generation(record->generation);
    seal_record(record);
}

bool d1l_connectivity_boot_guard_acknowledge_stable(
    d1l_connectivity_boot_guard_record_t *record,
    d1l_connectivity_subsystem_t subsystem)
{
    if (!d1l_connectivity_boot_guard_valid(record) ||
        subsystem <= D1L_CONNECTIVITY_SUBSYSTEM_NONE ||
        subsystem >= D1L_CONNECTIVITY_SUBSYSTEM_COUNT ||
        record->last_active_subsystem != (uint8_t)subsystem ||
        record->consecutive_crash_boots == 0U) {
        return false;
    }
    record->consecutive_crash_boots = 0U;
    record->generation = next_generation(record->generation);
    seal_record(record);
    return true;
}

void d1l_connectivity_boot_guard_clear(
    d1l_connectivity_boot_guard_record_t *record)
{
    if (!record) {
        return;
    }
    if (!d1l_connectivity_boot_guard_valid(record)) {
        d1l_connectivity_boot_guard_init(record);
    }
    record->consecutive_crash_boots = 0U;
    record->last_active_subsystem = D1L_CONNECTIVITY_SUBSYSTEM_NONE;
    record->generation = next_generation(record->generation);
    seal_record(record);
}

const char *d1l_connectivity_subsystem_name(
    d1l_connectivity_subsystem_t subsystem)
{
    switch (subsystem) {
    case D1L_CONNECTIVITY_SUBSYSTEM_WIFI:
        return "wifi";
    case D1L_CONNECTIVITY_SUBSYSTEM_NONE:
    default:
        return "none";
    }
}
