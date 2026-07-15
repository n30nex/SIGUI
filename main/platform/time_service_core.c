#include "time_service_core.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

_Static_assert(D1L_TIME_PROTOCOL_RESERVATION_SIZE > 0U,
               "protocol reservations must contain at least one value");
_Static_assert(D1L_TIME_SNTP_MAX_PROTOCOL_EPOCH <=
                   (int64_t)UINT32_MAX -
                       (D1L_TIME_PROTOCOL_RESERVATION_SIZE - 1U),
               "SNTP admission must preserve one complete reservation");

static bool wall_pair_valid(d1l_time_validity_t validity,
                            d1l_time_source_t source)
{
    switch (validity) {
        case D1L_TIME_VALIDITY_APPROXIMATE:
            return source == D1L_TIME_SOURCE_RETAINED_AUTHENTICATED ||
                   source ==
                       D1L_TIME_SOURCE_RETAINED_VALIDATED_CHECKPOINT;
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

static uint32_t max_u32(uint32_t left, uint32_t right)
{
    return left > right ? left : right;
}

static uint32_t sntp_ceiling(uint32_t anchor)
{
    int64_t ceiling = (int64_t)anchor + D1L_TIME_SNTP_MAX_FORWARD_SEC;
    if (ceiling > D1L_TIME_SNTP_MAX_PROTOCOL_EPOCH) {
        ceiling = D1L_TIME_SNTP_MAX_PROTOCOL_EPOCH;
    }
    return (uint32_t)ceiling;
}

static bool admission_has_continuous_anchor(
    d1l_time_protocol_wall_admission_t admission)
{
    return admission == D1L_TIME_PROTOCOL_WALL_SNTP_ADMITTED ||
           admission == D1L_TIME_PROTOCOL_WALL_COMPANION_AUTHORIZED;
}

static uint32_t protocol_trust_anchor_at(
    const d1l_time_service_core_t *core,
    uint64_t now_us)
{
    uint32_t anchor = max_u32(core->build_epoch_sec,
                              core->protocol_reserved_through);
    if (core->wall_set &&
        admission_has_continuous_anchor(core->protocol_wall_admission)) {
        const int64_t current_wall = wall_at(core, now_us);
        if (current_wall > (int64_t)anchor) {
            anchor = current_wall > (int64_t)UINT32_MAX ?
                         UINT32_MAX : (uint32_t)current_wall;
        }
    }
    return anchor;
}

static esp_err_t protocol_candidate_at(
    const d1l_time_service_core_t *core,
    uint64_t observed_now_us,
    uint32_t *out_candidate)
{
    if (!core || !out_candidate) {
        return ESP_ERR_INVALID_ARG;
    }
    if (core->protocol_exhausted) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint64_t now_us = observed_now_us > core->last_monotonic_us ?
        observed_now_us : core->last_monotonic_us;
    uint32_t candidate = core->protocol_next;
    if (core->wall_set &&
        d1l_time_core_certificate_validity(core->wall_validity)) {
        const int64_t current_wall = wall_at(core, now_us);
        if (current_wall > (int64_t)candidate) {
            if (current_wall > (int64_t)UINT32_MAX ||
                core->protocol_wall_admission ==
                    D1L_TIME_PROTOCOL_WALL_SNTP_FORWARD_BLOCKED ||
                core->protocol_wall_admission ==
                    D1L_TIME_PROTOCOL_WALL_UNREPRESENTABLE) {
                return ESP_ERR_INVALID_STATE;
            }
            if (core->protocol_wall_admission ==
                    D1L_TIME_PROTOCOL_WALL_SNTP_ADMITTED ||
                core->protocol_wall_admission ==
                    D1L_TIME_PROTOCOL_WALL_COMPANION_AUTHORIZED) {
                candidate = (uint32_t)current_wall;
            }
        }
    }
    *out_candidate = candidate;
    return ESP_OK;
}

esp_err_t d1l_time_core_init(d1l_time_service_core_t *core,
                             uint64_t now_us,
                             uint32_t build_epoch_sec)
{
    if (!core || build_epoch_sec < D1L_TIME_PROTOCOL_TIMESTAMP_BASE ||
        (int64_t)build_epoch_sec > D1L_TIME_SNTP_MAX_PROTOCOL_EPOCH) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(core, 0, sizeof(*core));
    core->boot_monotonic_us = now_us;
    core->last_monotonic_us = now_us;
    core->build_epoch_sec = build_epoch_sec;
    core->wall_validity = D1L_TIME_VALIDITY_MONOTONIC_ONLY;
    core->wall_source = D1L_TIME_SOURCE_BOOT_MONOTONIC;
    core->protocol_wall_admission =
        D1L_TIME_PROTOCOL_WALL_SEQUENCE_ONLY;
    core->protocol_next = max_u32(
        D1L_TIME_PROTOCOL_TIMESTAMP_BASE + 1U, build_epoch_sec);
    core->protocol_reserved_through = D1L_TIME_PROTOCOL_TIMESTAMP_BASE;
    core->protocol_trust_anchor = build_epoch_sec;
    core->protocol_sntp_ceiling = sntp_ceiling(build_epoch_sec);
    return ESP_OK;
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
        core->protocol_next = max_u32(
            D1L_TIME_PROTOCOL_TIMESTAMP_BASE + 1U,
            core->build_epoch_sec);
        core->protocol_reserved_through = D1L_TIME_PROTOCOL_TIMESTAMP_BASE;
        core->protocol_exhausted = false;
    } else {
        core->protocol_reserved_through = reserved_through;
        if (reserved_through == UINT32_MAX) {
            core->protocol_next = UINT32_MAX;
            core->protocol_exhausted = true;
        } else {
            core->protocol_next = max_u32(reserved_through + 1U,
                                          core->build_epoch_sec);
            core->protocol_exhausted = false;
        }
    }
    core->protocol_trust_anchor = max_u32(
        core->build_epoch_sec, core->protocol_reserved_through);
    core->protocol_sntp_ceiling = sntp_ceiling(
        core->protocol_trust_anchor);
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

esp_err_t d1l_time_core_preflight_wall(
    const d1l_time_service_core_t *core,
    int64_t epoch_sec,
    d1l_time_validity_t validity,
    d1l_time_source_t source)
{
    if (!core || epoch_sec < D1L_TIME_WALL_MIN_EPOCH ||
        !wall_pair_valid(validity, source)) {
        return ESP_ERR_INVALID_ARG;
    }
    return core->wall_generation == UINT32_MAX ?
               ESP_ERR_INVALID_STATE : ESP_OK;
}

esp_err_t d1l_time_core_set_wall(d1l_time_service_core_t *core,
                                 int64_t epoch_sec,
                                 uint64_t observed_now_us,
                                 d1l_time_validity_t validity,
                                 d1l_time_source_t source)
{
    const esp_err_t preflight = d1l_time_core_preflight_wall(
        core, epoch_sec, validity, source);
    if (preflight != ESP_OK) {
        return preflight;
    }
    (void)d1l_time_core_observe_monotonic(core, observed_now_us);
    const uint32_t trust_anchor = protocol_trust_anchor_at(
        core, core->last_monotonic_us);
    const uint32_t ceiling = sntp_ceiling(trust_anchor);
    d1l_time_protocol_wall_admission_t admission =
        D1L_TIME_PROTOCOL_WALL_APPROXIMATE_IGNORED;
    if (source == D1L_TIME_SOURCE_SNTP) {
        admission = epoch_sec <= (int64_t)ceiling ?
            D1L_TIME_PROTOCOL_WALL_SNTP_ADMITTED :
            D1L_TIME_PROTOCOL_WALL_SNTP_FORWARD_BLOCKED;
    } else if (source == D1L_TIME_SOURCE_COMPANION_AUTHENTICATED) {
        admission = epoch_sec <= (int64_t)UINT32_MAX ?
            D1L_TIME_PROTOCOL_WALL_COMPANION_AUTHORIZED :
            D1L_TIME_PROTOCOL_WALL_UNREPRESENTABLE;
    }
    core->wall_anchor_monotonic_us = core->last_monotonic_us;
    core->wall_anchor_epoch_sec = epoch_sec;
    core->protocol_trust_anchor = trust_anchor;
    core->protocol_sntp_ceiling = ceiling;
    core->protocol_wall_admission = admission;
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

esp_err_t d1l_time_core_recover_retained_checkpoint(
    d1l_time_service_core_t *core,
    int64_t epoch_sec,
    uint32_t protocol_reserved_through_at_commit,
    uint64_t observed_now_us)
{
    if (!core || epoch_sec < D1L_TIME_WALL_MIN_EPOCH ||
        protocol_reserved_through_at_commit <
            D1L_TIME_PROTOCOL_TIMESTAMP_BASE ||
        protocol_reserved_through_at_commit >
            core->protocol_reserved_through) {
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
        D1L_TIME_SOURCE_RETAINED_VALIDATED_CHECKPOINT);
}

esp_err_t d1l_time_core_preflight_protocol_timestamp(
    const d1l_time_service_core_t *core,
    uint64_t observed_now_us)
{
    uint32_t candidate = 0U;
    return protocol_candidate_at(core, observed_now_us, &candidate);
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
    (void)d1l_time_core_observe_monotonic(core, observed_now_us);
    uint32_t candidate = 0U;
    const esp_err_t preflight = protocol_candidate_at(
        core, core->last_monotonic_us, &candidate);
    if (preflight != ESP_OK) {
        return preflight;
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
        core->protocol_trust_anchor = max_u32(
            core->build_epoch_sec, core->protocol_reserved_through);
        core->protocol_sntp_ceiling = sntp_ceiling(
            core->protocol_trust_anchor);
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
    out_snapshot->build_epoch_sec = core->build_epoch_sec;
    out_snapshot->wall_generation = core->wall_generation;
    out_snapshot->protocol_next = core->protocol_next;
    out_snapshot->protocol_reserved_through = core->protocol_reserved_through;
    out_snapshot->protocol_exhausted = core->protocol_exhausted;
    out_snapshot->protocol_wall_admission =
        core->protocol_wall_admission;
    out_snapshot->wall_validity = core->wall_validity;
    out_snapshot->wall_source = core->wall_source;
    out_snapshot->wall_valid = core->wall_set;
    out_snapshot->certificate_time_valid =
        core->wall_set && d1l_time_core_certificate_validity(core->wall_validity);
    if (core->wall_set) {
        out_snapshot->wall_epoch_sec = wall_at(core, core->last_monotonic_us);
        if ((int64_t)core->protocol_next > out_snapshot->wall_epoch_sec) {
            out_snapshot->protocol_ahead_of_wall_sec =
                (int64_t)core->protocol_next - out_snapshot->wall_epoch_sec;
        }
    }
    out_snapshot->protocol_trust_anchor = protocol_trust_anchor_at(
        core, core->last_monotonic_us);
    out_snapshot->protocol_sntp_ceiling = sntp_ceiling(
        out_snapshot->protocol_trust_anchor);
    out_snapshot->protocol_tx_ready = !core->protocol_exhausted;
    out_snapshot->protocol_tx_error = core->protocol_exhausted ?
        ESP_ERR_INVALID_STATE : ESP_OK;
    if (out_snapshot->protocol_tx_ready &&
        out_snapshot->certificate_time_valid &&
        out_snapshot->wall_epoch_sec > (int64_t)core->protocol_next &&
        (out_snapshot->wall_epoch_sec > (int64_t)UINT32_MAX ||
         core->protocol_wall_admission ==
             D1L_TIME_PROTOCOL_WALL_SNTP_FORWARD_BLOCKED ||
         core->protocol_wall_admission ==
             D1L_TIME_PROTOCOL_WALL_UNREPRESENTABLE)) {
        out_snapshot->protocol_tx_ready = false;
        out_snapshot->protocol_tx_error = ESP_ERR_INVALID_STATE;
    }
}

bool d1l_time_core_certificate_validity(d1l_time_validity_t validity)
{
    return validity == D1L_TIME_VALIDITY_NETWORK_VALIDATED ||
           validity == D1L_TIME_VALIDITY_COMPANION_VALIDATED;
}

bool d1l_time_core_wall_checkpoint_eligible(
    const d1l_time_service_core_t *core)
{
    if (!core || !core->wall_set) {
        return false;
    }
    return (core->wall_validity == D1L_TIME_VALIDITY_NETWORK_VALIDATED &&
            core->wall_source == D1L_TIME_SOURCE_SNTP &&
            core->protocol_wall_admission ==
                D1L_TIME_PROTOCOL_WALL_SNTP_ADMITTED) ||
           (core->wall_validity == D1L_TIME_VALIDITY_COMPANION_VALIDATED &&
            core->wall_source ==
                D1L_TIME_SOURCE_COMPANION_AUTHENTICATED &&
            core->protocol_wall_admission ==
                D1L_TIME_PROTOCOL_WALL_COMPANION_AUTHORIZED);
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
        case D1L_TIME_SOURCE_RETAINED_VALIDATED_CHECKPOINT:
            return "retained_validated_checkpoint";
        default:
            return "unknown";
    }
}

const char *d1l_time_protocol_wall_admission_name(
    d1l_time_protocol_wall_admission_t admission)
{
    switch (admission) {
        case D1L_TIME_PROTOCOL_WALL_SEQUENCE_ONLY:
            return "sequence_only";
        case D1L_TIME_PROTOCOL_WALL_APPROXIMATE_IGNORED:
            return "approximate_ignored";
        case D1L_TIME_PROTOCOL_WALL_SNTP_ADMITTED:
            return "sntp_admitted";
        case D1L_TIME_PROTOCOL_WALL_SNTP_FORWARD_BLOCKED:
            return "sntp_forward_blocked";
        case D1L_TIME_PROTOCOL_WALL_COMPANION_AUTHORIZED:
            return "companion_authorized";
        case D1L_TIME_PROTOCOL_WALL_UNREPRESENTABLE:
            return "wall_unrepresentable";
        default:
            return "unknown";
    }
}

const char *d1l_time_protocol_persistence_state_name(
    d1l_time_protocol_persistence_state_t state)
{
    switch (state) {
        case D1L_TIME_PROTOCOL_PERSISTENCE_UNINITIALIZED:
            return "uninitialized";
        case D1L_TIME_PROTOCOL_PERSISTENCE_FRESH:
            return "fresh";
        case D1L_TIME_PROTOCOL_PERSISTENCE_READY:
            return "ready";
        case D1L_TIME_PROTOCOL_PERSISTENCE_MIGRATION_REQUIRED:
            return "migration_required";
        case D1L_TIME_PROTOCOL_PERSISTENCE_CORRUPT:
            return "corrupt";
        case D1L_TIME_PROTOCOL_PERSISTENCE_STORAGE_ERROR:
            return "storage_error";
        default:
            return "unknown";
    }
}
