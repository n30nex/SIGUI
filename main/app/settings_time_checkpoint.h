#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define D1L_SETTINGS_TIME_CHECKPOINT_SCHEMA_VERSION 1U
/* At most four routine checkpoint commits per day.  A stronger companion
 * validation may still replace an SNTP checkpoint immediately. */
#define D1L_SETTINGS_TIME_CHECKPOINT_MIN_ADVANCE_SEC INT64_C(21600)

typedef enum {
    D1L_SETTINGS_TIME_CHECKPOINT_SOURCE_NONE = 0,
    D1L_SETTINGS_TIME_CHECKPOINT_SOURCE_SNTP,
    D1L_SETTINGS_TIME_CHECKPOINT_SOURCE_COMPANION_AUTHENTICATED,
} d1l_settings_time_checkpoint_source_t;

typedef enum {
    D1L_SETTINGS_TIME_CHECKPOINT_UNINITIALIZED = 0,
    D1L_SETTINGS_TIME_CHECKPOINT_ABSENT,
    D1L_SETTINGS_TIME_CHECKPOINT_READY,
    D1L_SETTINGS_TIME_CHECKPOINT_QUARANTINED_NEWER_SCHEMA,
    D1L_SETTINGS_TIME_CHECKPOINT_QUARANTINED_MALFORMED,
    D1L_SETTINGS_TIME_CHECKPOINT_QUARANTINED_CHECKSUM,
    D1L_SETTINGS_TIME_CHECKPOINT_STORAGE_ERROR,
    D1L_SETTINGS_TIME_CHECKPOINT_REVISION_SATURATED,
} d1l_settings_time_checkpoint_state_t;

typedef struct {
    int64_t epoch_sec;
    uint32_t protocol_reserved_through;
    d1l_settings_time_checkpoint_source_t source;
} d1l_settings_time_checkpoint_t;

typedef struct {
    d1l_settings_time_checkpoint_state_t state;
    esp_err_t error;
    uint32_t revision;
    int64_t epoch_sec;
    uint32_t protocol_reserved_through;
    d1l_settings_time_checkpoint_source_t source;
    bool found;
} d1l_settings_time_checkpoint_status_t;

/* This is a narrow persistence boundary for wall time only.  The checkpoint
 * never becomes certificate-valid or protocol-authoritative merely because it
 * survived reboot; the time service recovers it as an approximate lower bound.
 */
esp_err_t d1l_settings_time_checkpoint_load(
    d1l_settings_time_checkpoint_t *out_checkpoint,
    d1l_settings_time_checkpoint_status_t *out_status);

/* Saves only validated sources. Material forward progress and backward
 * corrections are coalesced; corrupt or future data is preserved and blocks
 * replacement. Protocol monotonicity is owned by the separate allocator. */
esp_err_t d1l_settings_time_checkpoint_save(
    const d1l_settings_time_checkpoint_t *checkpoint,
    bool *out_written,
    d1l_settings_time_checkpoint_status_t *out_status);

const char *d1l_settings_time_checkpoint_state_name(
    d1l_settings_time_checkpoint_state_t state);
const char *d1l_settings_time_checkpoint_source_name(
    d1l_settings_time_checkpoint_source_t source);
