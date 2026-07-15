#pragma once

#include <stdbool.h>
#include <stdint.h>

#define D1L_CONNECTIVITY_BOOT_GUARD_SCHEMA 1U
#define D1L_CONNECTIVITY_BOOT_GUARD_FAILURE_LIMIT 2U

typedef enum {
    D1L_CONNECTIVITY_SUBSYSTEM_NONE = 0,
    D1L_CONNECTIVITY_SUBSYSTEM_WIFI,
    D1L_CONNECTIVITY_SUBSYSTEM_COUNT,
} d1l_connectivity_subsystem_t;

typedef struct {
    uint32_t schema;
    uint32_t generation;
    uint32_t checksum;
    uint8_t consecutive_crash_boots;
    uint8_t last_active_subsystem;
    uint8_t reserved[2];
} d1l_connectivity_boot_guard_record_t;

typedef struct {
    bool previous_record_valid;
    bool crash_attributed;
    bool crash_loop_detected;
    uint8_t consecutive_crash_boots;
    d1l_connectivity_subsystem_t previous_active_subsystem;
} d1l_connectivity_boot_guard_decision_t;

void d1l_connectivity_boot_guard_init(
    d1l_connectivity_boot_guard_record_t *record);
bool d1l_connectivity_boot_guard_valid(
    const d1l_connectivity_boot_guard_record_t *record);
d1l_connectivity_boot_guard_decision_t d1l_connectivity_boot_guard_note_boot(
    d1l_connectivity_boot_guard_record_t *record,
    bool crash_like_reset);
void d1l_connectivity_boot_guard_mark_active(
    d1l_connectivity_boot_guard_record_t *record,
    d1l_connectivity_subsystem_t subsystem);
bool d1l_connectivity_boot_guard_acknowledge_stable(
    d1l_connectivity_boot_guard_record_t *record,
    d1l_connectivity_subsystem_t subsystem);
void d1l_connectivity_boot_guard_clear(
    d1l_connectivity_boot_guard_record_t *record);
const char *d1l_connectivity_subsystem_name(
    d1l_connectivity_subsystem_t subsystem);
