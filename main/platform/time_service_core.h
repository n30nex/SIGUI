#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define D1L_TIME_PROTOCOL_TIMESTAMP_BASE 1767225600UL
#define D1L_TIME_PROTOCOL_RESERVATION_SIZE 64U
#define D1L_TIME_WALL_MIN_EPOCH 1704067200LL

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
} d1l_time_source_t;

typedef enum {
    D1L_TIME_PROTOCOL_PERSISTENCE_UNINITIALIZED = 0,
    D1L_TIME_PROTOCOL_PERSISTENCE_FRESH,
    D1L_TIME_PROTOCOL_PERSISTENCE_READY,
    D1L_TIME_PROTOCOL_PERSISTENCE_MIGRATION_REQUIRED,
    D1L_TIME_PROTOCOL_PERSISTENCE_CORRUPT,
    D1L_TIME_PROTOCOL_PERSISTENCE_STORAGE_ERROR,
} d1l_time_protocol_persistence_state_t;

typedef struct {
    uint64_t boot_monotonic_us;
    uint64_t last_monotonic_us;
    uint64_t wall_anchor_monotonic_us;
    int64_t wall_anchor_epoch_sec;
    uint32_t wall_generation;
    uint32_t protocol_next;
    uint32_t protocol_reserved_through;
    d1l_time_validity_t wall_validity;
    d1l_time_source_t wall_source;
    bool wall_set;
    bool protocol_started;
    bool protocol_exhausted;
} d1l_time_service_core_t;

typedef struct {
    uint64_t boot_monotonic_us;
    int64_t wall_epoch_sec;
    uint32_t wall_generation;
    uint32_t protocol_next;
    uint32_t protocol_reserved_through;
    d1l_time_validity_t wall_validity;
    d1l_time_source_t wall_source;
    bool wall_valid;
    bool certificate_time_valid;
    bool protocol_exhausted;
} d1l_time_core_snapshot_t;

typedef esp_err_t (*d1l_time_protocol_reserve_cb_t)(void *context,
                                                    uint32_t reserved_through);

void d1l_time_core_init(d1l_time_service_core_t *core, uint64_t now_us);
d1l_time_protocol_persistence_state_t d1l_time_core_classify_protocol_seed(
    bool legacy_present,
    bool high_water_present,
    uint32_t high_water);
esp_err_t d1l_time_core_seed_protocol(d1l_time_service_core_t *core,
                                      bool persisted,
                                      uint32_t reserved_through);
uint64_t d1l_time_core_observe_monotonic(d1l_time_service_core_t *core,
                                         uint64_t observed_now_us);
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
const char *d1l_time_validity_name(d1l_time_validity_t validity);
const char *d1l_time_source_name(d1l_time_source_t source);
