#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Exact-owner ACK completion coordinator.
 *
 * The retained delivery row is authoritative.  A received ACK may suppress
 * further RF retries as soon as the exact row/revision CAS is admitted, but
 * deadline release, direct-route credit, and terminal publication are a
 * take-once effect that is allowed only after that same revision is durable
 * and the runtime owner has published it.
 */
typedef struct {
    uint64_t session_id;
    uint32_t base_revision;
    uint32_t admitted_revision;
    bool active;
    bool cas_admitted;
    bool durable;
    bool terminal_effects_taken;
} d1l_meshcore_ack_completion_t;

typedef enum {
    D1L_MESHCORE_ACK_CAS_REJECTED = 0,
    D1L_MESHCORE_ACK_CAS_STALE,
    D1L_MESHCORE_ACK_CAS_ACCEPTED_PENDING,
    D1L_MESHCORE_ACK_CAS_ACCEPTED_DURABLE,
    D1L_MESHCORE_ACK_CAS_ALREADY_PENDING,
    D1L_MESHCORE_ACK_CAS_ALREADY_DURABLE,
} d1l_meshcore_ack_cas_result_t;

static inline bool d1l_meshcore_ack_completion_begin(
    d1l_meshcore_ack_completion_t *completion, uint64_t session_id,
    uint32_t base_revision)
{
    if (!completion || completion->active || session_id == 0U ||
        base_revision == 0U || base_revision == UINT32_MAX) {
        return false;
    }
    *completion = (d1l_meshcore_ack_completion_t){
        .session_id = session_id,
        .base_revision = base_revision,
        .active = true,
    };
    return true;
}

/*
 * Observe the store result only after the caller has checked the ACK hash
 * against the active owner.  rejected/stale observations never mutate the
 * coordinator.  Re-observing the same admitted revision is the bounded
 * persistence-retry path and cannot create a second terminal effect.
 */
static inline d1l_meshcore_ack_cas_result_t
d1l_meshcore_ack_completion_observe_cas(
    d1l_meshcore_ack_completion_t *completion, uint64_t session_id,
    uint32_t expected_revision, uint32_t resulting_revision,
    bool cas_admitted, bool durable)
{
    if (!cas_admitted) {
        return D1L_MESHCORE_ACK_CAS_REJECTED;
    }
    if (!completion || !completion->active ||
        completion->session_id != session_id ||
        completion->base_revision != expected_revision ||
        expected_revision == UINT32_MAX ||
        resulting_revision != expected_revision + 1U) {
        return D1L_MESHCORE_ACK_CAS_STALE;
    }
    if (completion->cas_admitted) {
        if (completion->admitted_revision != resulting_revision) {
            return D1L_MESHCORE_ACK_CAS_STALE;
        }
        if (durable) {
            completion->durable = true;
        }
        return completion->durable ?
            D1L_MESHCORE_ACK_CAS_ALREADY_DURABLE :
            D1L_MESHCORE_ACK_CAS_ALREADY_PENDING;
    }
    completion->cas_admitted = true;
    completion->admitted_revision = resulting_revision;
    completion->durable = durable;
    return durable ? D1L_MESHCORE_ACK_CAS_ACCEPTED_DURABLE :
                     D1L_MESHCORE_ACK_CAS_ACCEPTED_PENDING;
}

static inline bool d1l_meshcore_ack_completion_mark_reconciled(
    d1l_meshcore_ack_completion_t *completion, uint64_t session_id,
    uint32_t durable_revision)
{
    if (!completion || !completion->active || !completion->cas_admitted ||
        completion->session_id != session_id ||
        completion->admitted_revision != durable_revision ||
        completion->terminal_effects_taken) {
        return false;
    }
    completion->durable = true;
    return true;
}

/* True while a retained CAS/storage retry owns completion; RF must not retry. */
static inline bool d1l_meshcore_ack_completion_suppresses_rf_retry(
    const d1l_meshcore_ack_completion_t *completion)
{
    return completion && completion->active && completion->cas_admitted &&
           !completion->terminal_effects_taken;
}

/*
 * Called only after d1l_dm_delivery_owner_apply() publishes the durable
 * revision.  A true result authorizes exactly one deadline clear, one direct
 * route credit, one durable ACK log, and owner release.
 */
static inline bool d1l_meshcore_ack_completion_take_terminal_effects(
    d1l_meshcore_ack_completion_t *completion, uint64_t session_id,
    uint32_t published_revision)
{
    if (!completion || !completion->active || !completion->cas_admitted ||
        !completion->durable || completion->terminal_effects_taken ||
        completion->session_id != session_id ||
        completion->admitted_revision != published_revision) {
        return false;
    }
    completion->terminal_effects_taken = true;
    return true;
}
