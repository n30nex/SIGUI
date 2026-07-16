#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define D1L_TIME_PROTOCOL_NVS_NAMESPACE "d1l_settings"
#define D1L_TIME_PROTOCOL_LEGACY_KEY "mesh_ts"
#define D1L_TIME_PROTOCOL_HIGH_WATER_KEY "mesh_hi_v2"
#define D1L_TIME_PROTOCOL_MIGRATION_NVS_NAMESPACE "d1l_time_mig"
#define D1L_TIME_PROTOCOL_MIGRATION_NVS_KEY "mesh_mig_v1"
#define D1L_TIME_PROTOCOL_MIGRATION_SCHEMA_VERSION 1U
#define D1L_TIME_PROTOCOL_MIGRATION_DOMAIN_MAGIC 0x31474D54UL
#define D1L_TIME_PROTOCOL_MIGRATION_PHASE_INTENT 1U
#define D1L_TIME_PROTOCOL_MIGRATION_PHASE_COMPLETE 2U
#define D1L_TIME_PROTOCOL_MIGRATION_INTENT_REVISION 1U
#define D1L_TIME_PROTOCOL_MIGRATION_COMPLETE_REVISION 2U
#define D1L_TIME_PROTOCOL_MIGRATION_MAX_RECEIPT_SIZE 256U
#define D1L_TIME_PROTOCOL_MIGRATION_CONFIRMATION \
    "CONFIRM-EXACT-DEVICE-PROTOCOL-UPPER-BOUND"

typedef enum {
    D1L_TIME_PROTOCOL_MIGRATION_UNINITIALIZED = 0,
    D1L_TIME_PROTOCOL_MIGRATION_ABSENT,
    D1L_TIME_PROTOCOL_MIGRATION_REQUIRED,
    D1L_TIME_PROTOCOL_MIGRATION_PENDING,
    D1L_TIME_PROTOCOL_MIGRATION_COMPLETE,
    D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_NEWER_SCHEMA,
    D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_MALFORMED,
    D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_CHECKSUM,
    D1L_TIME_PROTOCOL_MIGRATION_QUARANTINED_DOWNGRADE,
    D1L_TIME_PROTOCOL_MIGRATION_STORAGE_ERROR,
    D1L_TIME_PROTOCOL_MIGRATION_REVISION_SATURATED,
} d1l_time_protocol_migration_state_t;

typedef struct {
    d1l_time_protocol_migration_state_t state;
    esp_err_t error;
    uint32_t receipt_schema_version;
    uint32_t receipt_phase;
    uint32_t revision;
    uint32_t legacy_value;
    uint32_t observed_legacy_value;
    uint32_t confirmed_upper_bound;
    uint32_t target_high_water;
    uint32_t observed_high_water;
    bool receipt_found;
    bool legacy_present;
    bool high_water_present;
    bool confirmation_required;
    bool resume_required;
    bool write_blocked;
    bool intent_committed;
    bool completion_committed;
} d1l_time_protocol_migration_status_t;

/* Inspection never mutates NVS. A legacy mesh_ts value is deliberately only
 * reported as a lower bound; it is never promoted to a high-water mark or
 * interpreted as wall time. */
esp_err_t d1l_time_protocol_migration_inspect(
    d1l_time_protocol_migration_status_t *out_status);

/* The caller must attest an exact-device upper bound that is >= every
 * protocol timestamp the predecessor could have issued, including RAM-only
 * fallback values. The confirmation string prevents accidental or automatic
 * migration. Each durable stage is idempotent and retryable after power loss. */
esp_err_t d1l_time_protocol_migration_run(
    uint32_t expected_legacy_value,
    uint32_t confirmed_upper_bound,
    const char *confirmation,
    bool *out_written,
    d1l_time_protocol_migration_status_t *out_status);

const char *d1l_time_protocol_migration_state_name(
    d1l_time_protocol_migration_state_t state);
