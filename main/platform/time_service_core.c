#include "time_service_core.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

static bool wall_pair_valid(d1l_time_validity_t validity,
                            d1l_time_source_t source)
{
    switch (validity) {
        case D1L_TIME_VALIDITY_APPROXIMATE:
            return source == D1L_TIME_SOURCE_RETAINED_AUTHENTICATED;
        case D1L_TIME_VALIDITY_NETWORK_VALIDATED:
            return source == D1L_TIME_SOURCE_SNTP;
        case D1L_TIME_VALIDITY_COMPANION_VALIDATED:
            return source == D1L_TIME_SOURCE_COMPANION_AUTHENTICATED;
        default:
            return false;
    }
}

static int64_t wall_at(const d1l_time_service_core_t *core, uint64_t now_us)
{
    const uint64_t elapsed_us = now_us - core->wall_anchor_monotonic_us;
    const uint64_t elapsed_sec = elapsed_us / UINT64_C(1000000);
    if (elapsed_sec > (uint64_t)INT64_MAX ||
        core->wall_anchor_epoch_sec > INT64_MAX - (int64_t)elapsed_sec) {
        return INT64_MAX;
    }
    return core->wall_anchor_epoch_sec + (int64_t)elapsed_sec;
}

static uint32_t reservation_through(uint32_t first)
{
    const uint32_t extra = D1L_TIME_PROTOCOL_RESERVATION_SIZE - 1U;
    return UINT32_MAX - first < extra ? UINT32_MAX : first + extra;
}

void d1l_time_core_init(d1l_time_service_core_t *core, uint64_t now_us)
{
    if (!core) {
        return;
    }
    memset(core, 0, sizeof(*core));
    core->boot_monotonic_us = now_us;
    core->last_monotonic_us = now_us;
    core->wall_validity = D1L_TIME_VALIDITY_MONOTONIC_ONLY;
    core->wall_source = D1L_TIME_SOURCE_BOOT_MONOTONIC;
    core->protocol_next = D1L_TIME_PROTOCOL_TIMESTAMP_BASE + 1U;
    core->protocol_reserved_through = D1L_TIME_PROTOCOL_TIMESTAMP_BASE;
}

d1l_time_protocol_persistence_state_t d1l_time_core_classify_protocol_seed(
    bool legacy_present,
    bool high_water_present,
    uint32_t high_water)
{
    if (legacy_present) {
        return D1L_TIME_PROTOCOL_PERSISTENCE_MIGRATION_REQUIRED;
    }
    if (!high_water_present) {
        return D1L_TIME_PROTOCOL_PERSISTENCE_FRESH;
    }
    return high_water >= D1L_TIME_PROTOCOL_TIMESTAMP_BASE ?
               D1L_TIME_PROTOCOL_PERSISTENCE_READY :
               D1L_TIME_PROTOCOL_PERSISTENCE_CORRUPT;
}

esp_err_t d1l_time_core_seed_protocol(d1l_time_service_core_t *core,
                                      bool persisted,
                                      uint32_t reserved_through)
{
    if (!core || core->protocol_started) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!persisted || reserved_through < D1L_TIME_PROTOCOL_TIMESTAMP_BASE) {
        core->protocol_next = D1L_TIME_PROTOCOL_TIMESTAMP_BASE + 1U;
        core->protocol_reserved_through = D1L_TIME_PROTOCOL_TIMESTAMP_BASE;
        core->protocol_exhausted = false;
        return ESP_OK;
    }
    core->protocol_reserved_through = reserved_through;
    if (reserved_through == UINT32_MAX) {
        core->protocol_next = UINT32_MAX;
        core->protocol_exhausted = true;
    } else {
        core->protocol_next = reserved_through + 1U;
        core->protocol_exhausted = false;
    }
    return ESP_OK;
}

uint64_t d1l_time_core_observe_monotonic(d1l_time_service_core_t *core,
                                         uint64_t observed_now_us)
{
    if (!core) {
        return 0U;
    }
    if (observed_now_us > core->last_monotonic_us) {
        core->last_monotonic_us = observed_now_us;
    }
    return core->last_monotonic_us - core->boot_monotonic_us;
}

esp_err_t d1l_time_core_set_wall(d1l_time_service_core_t *core,
                                 int64_t epoch_sec,
                                 uint64_t observed_now_us,
                                 d1l_time_validity_t validity,
                                 d1l_time_source_t source)
{
    if (!core || epoch_sec < D1L_TIME_WALL_MIN_EPOCH ||
        !wall_pair_valid(validity, source)) {
        return ESP_ERR_INVALID_ARG;
    }
    /* Generation is the compare-and-set token for asynchronous wall-clock
     * sources.  Saturation must fail closed; reusing UINT32_MAX would let a
     * stale SNTP waiter overwrite a later authenticated update (ABA). */
    if (core->wall_generation == UINT32_MAX) {
        return ESP_ERR_INVALID_STATE;
    }
    (void)d1l_time_core_observe_monotonic(core, observed_now_us);
    core->wall_anchor_monotonic_us = core->last_monotonic_us;
    core->wall_anchor_epoch_sec = epoch_sec;
    core->wall_validity = validity;
    core->wall_source = source;
    core->wall_set = true;
    core->wall_generation++;
    return ESP_OK;
}

esp_err_t d1l_time_core_set_wall_if_generation(
    d1l_time_service_core_t *core,
    uint32_t expected_generation,
    int64_t epoch_sec,
    uint64_t observed_now_us,
    d1l_time_validity_t validity,
    d1l_time_source_t source)
{
    if (!core) {
        return ESP_ERR_INVALID_ARG;
    }
    if (core->wall_generation != expected_generation) {
        return ESP_ERR_INVALID_STATE;
    }
    return d1l_time_core_set_wall(core, epoch_sec, observed_now_us, validity,
                                  source);
}

esp_err_t d1l_time_core_note_authenticated_lower_bound(
    d1l_time_service_core_t *core,
    int64_t epoch_sec,
    uint64_t observed_now_us)
{
    if (!core || epoch_sec < D1L_TIME_WALL_MIN_EPOCH) {
        return ESP_ERR_INVALID_ARG;
    }
    (void)d1l_time_core_observe_monotonic(core, observed_now_us);
    if (core->wall_set) {
        const int64_t current_wall = wall_at(core, core->last_monotonic_us);
        if (d1l_time_core_certificate_validity(core->wall_validity) ||
            current_wall >= epoch_sec) {
            return ESP_OK;
        }
    }
    return d1l_time_core_set_wall(
        core, epoch_sec, core->last_monotonic_us,
        D1L_TIME_VALIDITY_APPROXIMATE,
        D1L_TIME_SOURCE_RETAINED_AUTHENTICATED);
}

esp_err_t d1l_time_core_next_protocol_timestamp(
    d1l_time_service_core_t *core,
    uint64_t observed_now_us,
    d1l_time_protocol_reserve_cb_t reserve,
    void *reserve_context,
    uint32_t *out_timestamp)
{
    if (!core || !out_timestamp) {
        return ESP_ERR_INVALID_ARG;
    }
    if (core->protocol_exhausted) {
        return ESP_ERR_INVALID_STATE;
    }

    (void)d1l_time_core_observe_monotonic(core, observed_now_us);
    uint32_t candidate = core->protocol_next;
    if (core->wall_set) {
        const int64_t current_wall = wall_at(core, core->last_monotonic_us);
        if (current_wall > (int64_t)candidate && current_wall <= UINT32_MAX) {
            candidate = (uint32_t)current_wall;
        }
    }

    if (candidate > core->protocol_reserved_through) {
        if (!reserve) {
            return ESP_ERR_INVALID_ARG;
        }
        const uint32_t new_reserved_through = reservation_through(candidate);
        const esp_err_t ret = reserve(reserve_context, new_reserved_through);
        if (ret != ESP_OK) {
            return ret;
        }
        core->protocol_reserved_through = new_reserved_through;
    }

    *out_timestamp = candidate;
    core->protocol_started = true;
    if (candidate == UINT32_MAX) {
        core->protocol_exhausted = true;
    } else {
        core->protocol_next = candidate + 1U;
    }
    return ESP_OK;
}

void d1l_time_core_snapshot(d1l_time_service_core_t *core,
                            uint64_t observed_now_us,
                            d1l_time_core_snapshot_t *out_snapshot)
{
    if (!core || !out_snapshot) {
        return;
    }
    memset(out_snapshot, 0, sizeof(*out_snapshot));
    out_snapshot->boot_monotonic_us =
        d1l_time_core_observe_monotonic(core, observed_now_us);
    out_snapshot->wall_generation = core->wall_generation;
    out_snapshot->protocol_next = core->protocol_next;
    out_snapshot->protocol_reserved_through = core->protocol_reserved_through;
    out_snapshot->protocol_exhausted = core->protocol_exhausted;
    out_snapshot->wall_validity = core->wall_validity;
    out_snapshot->wall_source = core->wall_source;
    out_snapshot->wall_valid = core->wall_set;
    out_snapshot->certificate_time_valid =
        core->wall_set && d1l_time_core_certificate_validity(core->wall_validity);
    if (core->wall_set) {
        out_snapshot->wall_epoch_sec = wall_at(core, core->last_monotonic_us);
    }
}

bool d1l_time_core_certificate_validity(d1l_time_validity_t validity)
{
    return validity == D1L_TIME_VALIDITY_NETWORK_VALIDATED ||
           validity == D1L_TIME_VALIDITY_COMPANION_VALIDATED;
}

const char *d1l_time_validity_name(d1l_time_validity_t validity)
{
    switch (validity) {
        case D1L_TIME_VALIDITY_UNSET:
            return "unset";
        case D1L_TIME_VALIDITY_MONOTONIC_ONLY:
            return "monotonic_only";
        case D1L_TIME_VALIDITY_APPROXIMATE:
            return "approximate";
        case D1L_TIME_VALIDITY_NETWORK_VALIDATED:
            return "network_validated";
        case D1L_TIME_VALIDITY_COMPANION_VALIDATED:
            return "companion_validated";
        default:
            return "unknown";
    }
}

const char *d1l_time_source_name(d1l_time_source_t source)
{
    switch (source) {
        case D1L_TIME_SOURCE_NONE:
            return "none";
        case D1L_TIME_SOURCE_BOOT_MONOTONIC:
            return "boot_monotonic";
        case D1L_TIME_SOURCE_RETAINED_AUTHENTICATED:
            return "retained_authenticated";
        case D1L_TIME_SOURCE_SNTP:
            return "sntp";
        case D1L_TIME_SOURCE_COMPANION_AUTHENTICATED:
            return "companion_authenticated";
        default:
            return "unknown";
    }
}
