#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define D1L_TIME_PROTOCOL_TIMESTAMP_BASE 1767225600UL
#define D1L_TIME_PROTOCOL_RESERVATION_SIZE 64U
#define D1L_TIME_WALL_MIN_EPOCH 1704067200LL
/* SNTP is sufficient to bootstrap TLS but is not authenticated protocol-time
 * authority.  Bound each adoption and preserve ten years of the intrinsic
 * 32-bit MeshCore timestamp space.  Authenticated companion time is tracked
 * separately and may intentionally cross the SNTP-only ceiling. */
#define D1L_TIME_SNTP_MAX_FORWARD_SEC INT64_C(2592000)
#define D1L_TIME_SNTP_MIN_PROTOCOL_HEADROOM_SEC INT64_C(316224000)
#define D1L_TIME_SNTP_MAX_PROTOCOL_EPOCH \
    ((int64_t)UINT32_MAX - D1L_TIME_SNTP_MIN_PROTOCOL_HEADROOM_SEC)

typedef enum {
    D1L_TIME_VALIDITY_UNSET = 0,
    D1L_TIME_VALIDITY_MONOTONIC_ONLY,
    D1L_TIME_VALIDITY_APPROXIMATE,
    D1L_TIME_VALIDITY_NETWORK_VALIDATED,
    D1L_TIME_VALIDITY_COMPANION_VALIDATED,
} d1l_time_validity_t;

typedef enum {
    D1L_TIME_SOURCE_NONE = 0,
    D1L_TIME_SOURCE_BOOT_MONOTONIC,
    D1L_TIME_SOURCE_RETAINED_AUTHENTICATED,
    D1L_TIME_SOURCE_SNTP,
    D1L_TIME_SOURCE_COMPANION_AUTHENTICATED,
    D1L_TIME_SOURCE_RETAINED_VALIDATED_CHECKPOINT,
} d1l_time_source_t;

typedef enum {
    D1L_TIME_PROTOCOL_PERSISTENCE_UNINITIALIZED = 0,
    D1L_TIME_PROTOCOL_PERSISTENCE_FRESH,
    D1L_TIME_PROTOCOL_PERSISTENCE_READY,
    D1L_TIME_PROTOCOL_PERSISTENCE_MIGRATION_REQUIRED,
    D1L_TIME_PROTOCOL_PERSISTENCE_CORRUPT,
    D1L_TIME_PROTOCOL_PERSISTENCE_STORAGE_ERROR,
} d1l_time_protocol_persistence_state_t;

typedef enum {
    D1L_TIME_PROTOCOL_WALL_SEQUENCE_ONLY = 0,
    D1L_TIME_PROTOCOL_WALL_APPROXIMATE_IGNORED,
    D1L_TIME_PROTOCOL_WALL_SNTP_ADMITTED,
    D1L_TIME_PROTOCOL_WALL_SNTP_FORWARD_BLOCKED,
    D1L_TIME_PROTOCOL_WALL_COMPANION_AUTHORIZED,
    D1L_TIME_PROTOCOL_WALL_UNREPRESENTABLE,
} d1l_time_protocol_wall_admission_t;

typedef struct {
    uint64_t boot_monotonic_us;
    uint64_t last_monotonic_us;
    uint64_t wall_anchor_monotonic_us;
    int64_t wall_anchor_epoch_sec;
    uint32_t build_epoch_sec;
    uint32_t wall_generation;
    uint32_t protocol_next;
    uint32_t protocol_reserved_through;
    uint32_t protocol_trust_anchor;
    uint32_t protocol_sntp_ceiling;
    d1l_time_validity_t wall_validity;
    d1l_time_source_t wall_source;
    d1l_time_protocol_wall_admission_t protocol_wall_admission;
    bool wall_set;
    bool protocol_started;
    bool protocol_exhausted;
} d1l_time_service_core_t;

typedef struct {
    uint64_t boot_monotonic_us;
    int64_t wall_epoch_sec;
    int64_t protocol_ahead_of_wall_sec;
    uint32_t build_epoch_sec;
    uint32_t wall_generation;
    uint32_t protocol_next;
    uint32_t protocol_reserved_through;
    uint32_t protocol_trust_anchor;
    uint32_t protocol_sntp_ceiling;
    d1l_time_validity_t wall_validity;
    d1l_time_source_t wall_source;
    d1l_time_protocol_wall_admission_t protocol_wall_admission;
    esp_err_t protocol_tx_error;
    bool wall_valid;
    bool certificate_time_valid;
    bool protocol_tx_ready;
    bool protocol_exhausted;
} d1l_time_core_snapshot_t;

typedef esp_err_t (*d1l_time_protocol_reserve_cb_t)(void *context,
                                                    uint32_t reserved_through);

esp_err_t d1l_time_core_init(d1l_time_service_core_t *core,
                             uint64_t now_us,
                             uint32_t build_epoch_sec);
d1l_time_protocol_persistence_state_t d1l_time_core_classify_protocol_seed(
    bool legacy_present,
    bool high_water_present,
    uint32_t high_water);
esp_err_t d1l_time_core_seed_protocol(d1l_time_service_core_t *core,
                                      bool persisted,
                                      uint32_t reserved_through);
uint64_t d1l_time_core_observe_monotonic(d1l_time_service_core_t *core,
                                         uint64_t observed_now_us);
esp_err_t d1l_time_core_preflight_wall(
    const d1l_time_service_core_t *core,
    int64_t epoch_sec,
    d1l_time_validity_t validity,
    d1l_time_source_t source);
esp_err_t d1l_time_core_set_wall(d1l_time_service_core_t *core,
                                 int64_t epoch_sec,
                                 uint64_t observed_now_us,
                                 d1l_time_validity_t validity,
                                 d1l_time_source_t source);
esp_err_t d1l_time_core_set_wall_if_generation(
    d1l_time_service_core_t *core,
    uint32_t expected_generation,
    int64_t epoch_sec,
    uint64_t observed_now_us,
    d1l_time_validity_t validity,
    d1l_time_source_t source);
esp_err_t d1l_time_core_note_authenticated_lower_bound(
    d1l_time_service_core_t *core,
    int64_t epoch_sec,
    uint64_t observed_now_us);
esp_err_t d1l_time_core_recover_retained_checkpoint(
    d1l_time_service_core_t *core,
    int64_t epoch_sec,
    uint32_t protocol_reserved_through_at_commit,
    uint64_t observed_now_us);
/* Pure readiness check: no reservation, allocation, clock, or persistence
 * state is changed.  The allocator repeats this check under the same owner
 * lock before it commits a timestamp. */
esp_err_t d1l_time_core_preflight_protocol_timestamp(
    const d1l_time_service_core_t *core,
    uint64_t observed_now_us);
esp_err_t d1l_time_core_next_protocol_timestamp(
    d1l_time_service_core_t *core,
    uint64_t observed_now_us,
    d1l_time_protocol_reserve_cb_t reserve,
    void *reserve_context,
    uint32_t *out_timestamp);
void d1l_time_core_snapshot(d1l_time_service_core_t *core,
                            uint64_t observed_now_us,
                            d1l_time_core_snapshot_t *out_snapshot);
bool d1l_time_core_certificate_validity(d1l_time_validity_t validity);
bool d1l_time_core_wall_checkpoint_eligible(
    const d1l_time_service_core_t *core);
const char *d1l_time_validity_name(d1l_time_validity_t validity);
const char *d1l_time_source_name(d1l_time_source_t source);
const char *d1l_time_protocol_wall_admission_name(
    d1l_time_protocol_wall_admission_t admission);
const char *d1l_time_protocol_persistence_state_name(
    d1l_time_protocol_persistence_state_t state);
