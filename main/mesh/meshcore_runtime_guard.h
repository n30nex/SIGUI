#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    D1L_MESH_REQUEST_FREE = 0,
    D1L_MESH_REQUEST_CLAIMED,
    D1L_MESH_REQUEST_PENDING,
    D1L_MESH_REQUEST_ADMITTED,
    D1L_MESH_REQUEST_CALLER_CANCELLED,
    D1L_MESH_REQUEST_OWNER_EXPIRED,
    D1L_MESH_REQUEST_COMPLETED,
    D1L_MESH_REQUEST_RELEASING,
} d1l_mesh_request_state_t;

#define D1L_MESH_REQUEST_STATE_BITS 3U
#define D1L_MESH_REQUEST_STATE_MASK \
    ((1U << D1L_MESH_REQUEST_STATE_BITS) - 1U)
#define D1L_MESH_REQUEST_ID_MAX \
    (UINT32_MAX >> D1L_MESH_REQUEST_STATE_BITS)

typedef struct {
    /* Request identity and lifecycle state share one CAS word.  Keeping the
     * tuple atomic prevents an old request from observing its ID, being
     * preempted across slot reuse, and then changing the new request's state
     * through a state-only ABA. */
    volatile uint32_t word;
} d1l_mesh_request_guard_t;

static inline uint32_t d1l_mesh_request_guard_word(
    uint32_t request_id, d1l_mesh_request_state_t state)
{
    return (request_id << D1L_MESH_REQUEST_STATE_BITS) |
           ((uint32_t)state & D1L_MESH_REQUEST_STATE_MASK);
}

static inline d1l_mesh_request_state_t d1l_mesh_request_guard_state(
    const d1l_mesh_request_guard_t *guard)
{
    return guard ? (d1l_mesh_request_state_t)(
                       __atomic_load_n(&guard->word, __ATOMIC_ACQUIRE) &
                       D1L_MESH_REQUEST_STATE_MASK) :
                   D1L_MESH_REQUEST_FREE;
}

static inline bool d1l_mesh_request_guard_claim(
    d1l_mesh_request_guard_t *guard, uint32_t request_id)
{
    if (!guard || request_id == 0U || request_id > D1L_MESH_REQUEST_ID_MAX) {
        return false;
    }
    uint32_t expected = 0U;
    const uint32_t claimed = d1l_mesh_request_guard_word(
        request_id, D1L_MESH_REQUEST_CLAIMED);
    return __atomic_compare_exchange_n(
        &guard->word, &expected, claimed, false, __ATOMIC_ACQ_REL,
        __ATOMIC_ACQUIRE);
}

static inline bool d1l_mesh_request_guard_publish(
    d1l_mesh_request_guard_t *guard, uint32_t request_id)
{
    uint32_t expected = d1l_mesh_request_guard_word(
        request_id, D1L_MESH_REQUEST_CLAIMED);
    const uint32_t pending = d1l_mesh_request_guard_word(
        request_id, D1L_MESH_REQUEST_PENDING);
    return guard && request_id != 0U &&
           __atomic_compare_exchange_n(
               &guard->word, &expected, pending, false, __ATOMIC_ACQ_REL,
               __ATOMIC_ACQUIRE);
}

static inline bool d1l_mesh_request_guard_matches(
    const d1l_mesh_request_guard_t *guard, uint32_t request_id)
{
    return guard && request_id != 0U &&
           (__atomic_load_n(&guard->word, __ATOMIC_ACQUIRE) >>
            D1L_MESH_REQUEST_STATE_BITS) == request_id;
}

static inline bool d1l_mesh_request_guard_transition(
    d1l_mesh_request_guard_t *guard, uint32_t request_id,
    d1l_mesh_request_state_t expected_state,
    d1l_mesh_request_state_t next_state)
{
    if (!guard || request_id == 0U) {
        return false;
    }
    uint32_t expected = d1l_mesh_request_guard_word(
        request_id, expected_state);
    const uint32_t next = d1l_mesh_request_guard_word(
        request_id, next_state);
    return __atomic_compare_exchange_n(
        &guard->word, &expected, next, false, __ATOMIC_ACQ_REL,
        __ATOMIC_ACQUIRE);
}

static inline bool d1l_mesh_request_guard_release(
    d1l_mesh_request_guard_t *guard, uint32_t request_id,
    d1l_mesh_request_state_t expected_state)
{
    if (!guard || request_id == 0U) {
        return false;
    }
    uint32_t expected = d1l_mesh_request_guard_word(
        request_id, expected_state);
    return __atomic_compare_exchange_n(
        &guard->word, &expected, 0U, false, __ATOMIC_ACQ_REL,
        __ATOMIC_ACQUIRE);
}

typedef enum {
    D1L_MESH_TX_OPERATION_NONE = 0,
    D1L_MESH_TX_OPERATION_GENERIC,
    D1L_MESH_TX_OPERATION_PUBLIC,
    D1L_MESH_TX_OPERATION_ADVERT,
    D1L_MESH_TX_OPERATION_ACK_RESPONSE,
    D1L_MESH_TX_OPERATION_DM,
} d1l_mesh_tx_operation_kind_t;

typedef struct {
    uint64_t operation_id;
    d1l_mesh_tx_operation_kind_t kind;
    uint64_t dm_session_id;
    uint32_t dm_revision;
} d1l_mesh_tx_operation_identity_t;

static inline bool d1l_mesh_tx_operation_identity_valid(
    const d1l_mesh_tx_operation_identity_t *identity)
{
    if (!identity || identity->operation_id == 0U ||
        identity->kind == D1L_MESH_TX_OPERATION_NONE) {
        return false;
    }
    if (identity->kind == D1L_MESH_TX_OPERATION_DM) {
        return identity->dm_session_id != 0U && identity->dm_revision != 0U;
    }
    return identity->dm_session_id == 0U && identity->dm_revision == 0U;
}

static inline bool d1l_mesh_tx_operation_identity_equal(
    const d1l_mesh_tx_operation_identity_t *left,
    const d1l_mesh_tx_operation_identity_t *right)
{
    return d1l_mesh_tx_operation_identity_valid(left) &&
           d1l_mesh_tx_operation_identity_valid(right) &&
           left->operation_id == right->operation_id &&
           left->kind == right->kind &&
           left->dm_session_id == right->dm_session_id &&
           left->dm_revision == right->dm_revision;
}

typedef struct {
    bool armed;
    uint64_t deadline_us;
    d1l_mesh_tx_operation_identity_t operation;
} d1l_mesh_tx_watchdog_t;

static inline void d1l_mesh_tx_watchdog_reset(
    d1l_mesh_tx_watchdog_t *watchdog)
{
    if (watchdog) {
        *watchdog = (d1l_mesh_tx_watchdog_t){0};
    }
}

static inline bool d1l_mesh_tx_watchdog_arm(
    d1l_mesh_tx_watchdog_t *watchdog,
    const d1l_mesh_tx_operation_identity_t *operation,
    uint64_t now_us, uint64_t timeout_us)
{
    if (!watchdog || watchdog->armed || timeout_us == 0U ||
        !d1l_mesh_tx_operation_identity_valid(operation) ||
        now_us > UINT64_MAX - timeout_us) {
        return false;
    }
    watchdog->armed = true;
    watchdog->deadline_us = now_us + timeout_us;
    watchdog->operation = *operation;
    return true;
}

static inline bool d1l_mesh_tx_watchdog_take_due(
    d1l_mesh_tx_watchdog_t *watchdog, uint64_t now_us,
    d1l_mesh_tx_operation_identity_t *out_operation)
{
    if (!watchdog || !out_operation || !watchdog->armed ||
        now_us < watchdog->deadline_us ||
        !d1l_mesh_tx_operation_identity_valid(&watchdog->operation)) {
        return false;
    }
    *out_operation = watchdog->operation;
    d1l_mesh_tx_watchdog_reset(watchdog);
    return true;
}
