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

/* Runtime counters are diagnostic truth, not wrapping sequence numbers. A
 * wrapped drop/saturation/recovery count would look healthier than reality,
 * so every owner/callback telemetry increment clamps at UINT32_MAX. */
static inline uint32_t d1l_mesh_runtime_counter_increment_saturating(
    volatile uint32_t *counter)
{
    if (!counter) {
        return 0U;
    }
    uint32_t observed = __atomic_load_n(counter, __ATOMIC_RELAXED);
    while (observed != UINT32_MAX) {
        const uint32_t next = observed + 1U;
        if (__atomic_compare_exchange_n(
                counter, &observed, next, false, __ATOMIC_RELAXED,
                __ATOMIC_RELAXED)) {
            return next;
        }
    }
    return UINT32_MAX;
}

/* Every requested RX restart passes through this gate before any radio
 * profile/configure/Rx call. While an exact TX is active, requests coalesce
 * into one pending bit. The sole owner consumes that bit only after the exact
 * terminal path has cleared tx_busy and is actually restarting RX. */
static inline bool d1l_mesh_rx_restart_begin(
    volatile uint32_t *pending_restart, bool tx_busy, bool radio_ready)
{
    if (!pending_restart) {
        return false;
    }
    if (tx_busy) {
        __atomic_store_n(pending_restart, 1U, __ATOMIC_RELEASE);
        return false;
    }
    if (!radio_ready) {
        return false;
    }
    (void)__atomic_exchange_n(pending_restart, 0U, __ATOMIC_ACQ_REL);
    return true;
}

/* A latched exact terminal owns the RX restart that releases its TX. Leave
 * the coalesced bit in shared state for that terminal handler to consume,
 * rather than copying it into a task-local request that can replay after the
 * terminal has already restarted RX. A stale terminal leaves the bit intact
 * for a later exact terminal or idle owner iteration. */
static inline bool d1l_mesh_rx_recovery_take(
    volatile uint32_t *pending_restart, bool terminal_recovery)
{
    if (!pending_restart || terminal_recovery) {
        return false;
    }
    return __atomic_exchange_n(
               pending_restart, 0U, __ATOMIC_ACQ_REL) != 0U;
}

/* Exact terminals and ordinary owner work linearize through one 32-bit lane
 * word. Bits 0..30 are immutable callback-history slots; bit 31 is the sole
 * owner's ordinary-work reservation. A callback performs one nonblocking OR
 * before writing its slot. The owner can reserve only an empty lane, then
 * validates the unchanged reservation after dequeue and before execution.
 * A failed validation means the dequeued command must be retained locally and
 * the terminal lane runs first. */
#define D1L_MESH_TERMINAL_LANE_OWNER_RESERVED (UINT32_C(1) << 31U)
#define D1L_MESH_TERMINAL_LANE_PENDING_MASK \
    (D1L_MESH_TERMINAL_LANE_OWNER_RESERVED - 1U)

static inline bool d1l_mesh_terminal_lane_publish_slot(
    volatile uint32_t *lane, uint8_t slot)
{
    if (!lane || slot >= 31U) {
        return false;
    }
    (void)__atomic_fetch_or(
        lane, 1UL << slot, __ATOMIC_RELEASE);
    return true;
}

static inline uint32_t d1l_mesh_terminal_lane_take_pending(
    volatile uint32_t *lane)
{
    if (!lane) {
        return 0U;
    }
    const uint32_t before = __atomic_fetch_and(
        lane, D1L_MESH_TERMINAL_LANE_OWNER_RESERVED, __ATOMIC_ACQ_REL);
    return before & D1L_MESH_TERMINAL_LANE_PENDING_MASK;
}

static inline bool d1l_mesh_terminal_lane_try_reserve_owner(
    volatile uint32_t *lane)
{
    uint32_t expected = 0U;
    return lane && __atomic_compare_exchange_n(
        lane, &expected, D1L_MESH_TERMINAL_LANE_OWNER_RESERVED, false,
        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

static inline bool d1l_mesh_terminal_lane_owner_still_reserved(
    volatile uint32_t *lane)
{
    uint32_t expected = D1L_MESH_TERMINAL_LANE_OWNER_RESERVED;
    return lane && __atomic_compare_exchange_n(
        lane, &expected, D1L_MESH_TERMINAL_LANE_OWNER_RESERVED, false,
        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

static inline void d1l_mesh_terminal_lane_release_owner(
    volatile uint32_t *lane)
{
    if (lane) {
        (void)__atomic_fetch_and(
            lane, ~D1L_MESH_TERMINAL_LANE_OWNER_RESERVED,
            __ATOMIC_RELEASE);
    }
}

static inline bool d1l_mesh_terminal_lane_has_pending(
    const volatile uint32_t *lane)
{
    return lane &&
           (__atomic_load_n(lane, __ATOMIC_ACQUIRE) &
            D1L_MESH_TERMINAL_LANE_PENDING_MASK) != 0U;
}

/* The owner has three independently bounded work lanes. Radio callbacks may
 * run ahead briefly to keep the transceiver responsive, and internally
 * generated responses may run ahead briefly to meet protocol timing, but an
 * accepted normal command must not be starved by either sustained source.
 * Exact terminal recovery always wins before any queued work. */
#define D1L_MESH_OWNER_RADIO_EVENT_BURST_MAX 4U
#define D1L_MESH_OWNER_PRIORITY_COMMAND_BURST_MAX 2U

typedef enum {
    D1L_MESH_OWNER_WORK_IDLE = 0,
    D1L_MESH_OWNER_WORK_TERMINAL_RECOVERY,
    D1L_MESH_OWNER_WORK_RADIO_EVENT,
    D1L_MESH_OWNER_WORK_PRIORITY_COMMAND,
    D1L_MESH_OWNER_WORK_NORMAL_COMMAND,
} d1l_mesh_owner_work_t;

typedef struct {
    uint8_t radio_event_burst;
    uint8_t priority_command_burst;
} d1l_mesh_owner_scheduler_t;

static inline d1l_mesh_owner_work_t d1l_mesh_owner_scheduler_choose(
    d1l_mesh_owner_scheduler_t *scheduler,
    bool terminal_recovery_pending,
    bool radio_event_available,
    bool priority_command_available,
    bool normal_command_available,
    bool *out_forced_normal)
{
    if (out_forced_normal) {
        *out_forced_normal = false;
    }
    if (!scheduler) {
        return D1L_MESH_OWNER_WORK_IDLE;
    }

    if (terminal_recovery_pending) {
        return D1L_MESH_OWNER_WORK_TERMINAL_RECOVERY;
    }

    if (radio_event_available &&
        scheduler->radio_event_burst <
            D1L_MESH_OWNER_RADIO_EVENT_BURST_MAX) {
        scheduler->radio_event_burst++;
        return D1L_MESH_OWNER_WORK_RADIO_EVENT;
    }

    if (normal_command_available && priority_command_available &&
        scheduler->priority_command_burst >=
            D1L_MESH_OWNER_PRIORITY_COMMAND_BURST_MAX) {
        scheduler->radio_event_burst = 0U;
        scheduler->priority_command_burst = 0U;
        if (out_forced_normal) {
            *out_forced_normal = true;
        }
        return D1L_MESH_OWNER_WORK_NORMAL_COMMAND;
    }

    if (priority_command_available) {
        scheduler->radio_event_burst = 0U;
        if (scheduler->priority_command_burst <
            D1L_MESH_OWNER_PRIORITY_COMMAND_BURST_MAX) {
            scheduler->priority_command_burst++;
        }
        return D1L_MESH_OWNER_WORK_PRIORITY_COMMAND;
    }

    if (normal_command_available) {
        scheduler->radio_event_burst = 0U;
        scheduler->priority_command_burst = 0U;
        return D1L_MESH_OWNER_WORK_NORMAL_COMMAND;
    }

    if (radio_event_available) {
        /* There is no queued command to protect. Keep draining radio events,
         * but leave the burst saturated so a newly arriving command wins the
         * next decision. */
        return D1L_MESH_OWNER_WORK_RADIO_EVENT;
    }

    scheduler->radio_event_burst = 0U;
    scheduler->priority_command_burst = 0U;
    return D1L_MESH_OWNER_WORK_IDLE;
}

typedef enum {
    D1L_MESH_TX_OPERATION_NONE = 0,
    D1L_MESH_TX_OPERATION_GENERIC,
    /* Legacy name for the encrypted group-channel operation class. Exact
     * Public/private identity is carried separately by the pending channel
     * ID and must never be inferred from this enum value. */
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
