#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "mesh/meshcore_runtime_guard.h"

static void test_caller_timeout_cancels_only_pending(void)
{
    d1l_mesh_request_guard_t guard = {0};
    assert(d1l_mesh_request_guard_claim(&guard, 11U));
    assert(d1l_mesh_request_guard_publish(&guard, 11U));
    assert(d1l_mesh_request_guard_transition(
        &guard, 11U, D1L_MESH_REQUEST_PENDING,
        D1L_MESH_REQUEST_CALLER_CANCELLED));

    /* Once cancellation wins, owner admission and completion both lose. */
    assert(!d1l_mesh_request_guard_transition(
        &guard, 11U, D1L_MESH_REQUEST_PENDING,
        D1L_MESH_REQUEST_ADMITTED));
    assert(!d1l_mesh_request_guard_transition(
        &guard, 11U, D1L_MESH_REQUEST_ADMITTED,
        D1L_MESH_REQUEST_COMPLETED));
    assert(d1l_mesh_request_guard_release(
        &guard, 11U, D1L_MESH_REQUEST_CALLER_CANCELLED));
    assert(d1l_mesh_request_guard_state(&guard) == D1L_MESH_REQUEST_FREE);
}

static void test_admission_wins_timeout_and_requires_exact_completion(void)
{
    d1l_mesh_request_guard_t guard = {0};
    assert(d1l_mesh_request_guard_claim(&guard, 12U));
    assert(d1l_mesh_request_guard_publish(&guard, 12U));
    assert(d1l_mesh_request_guard_transition(
        &guard, 12U, D1L_MESH_REQUEST_PENDING,
        D1L_MESH_REQUEST_ADMITTED));

    /* The caller can no longer cancel after the owner admits side effects. */
    assert(!d1l_mesh_request_guard_transition(
        &guard, 12U, D1L_MESH_REQUEST_PENDING,
        D1L_MESH_REQUEST_CALLER_CANCELLED));
    assert(!d1l_mesh_request_guard_transition(
        &guard, 13U, D1L_MESH_REQUEST_ADMITTED,
        D1L_MESH_REQUEST_COMPLETED));
    assert(d1l_mesh_request_guard_transition(
        &guard, 12U, D1L_MESH_REQUEST_ADMITTED,
        D1L_MESH_REQUEST_COMPLETED));
    assert(!d1l_mesh_request_guard_transition(
        &guard, 12U, D1L_MESH_REQUEST_ADMITTED,
        D1L_MESH_REQUEST_COMPLETED));
    assert(d1l_mesh_request_guard_release(
        &guard, 12U, D1L_MESH_REQUEST_COMPLETED));
}

static void test_expiry_and_stale_reply_cannot_cross_slot_generations(void)
{
    d1l_mesh_request_guard_t guard = {0};
    assert(d1l_mesh_request_guard_claim(&guard, 21U));
    assert(d1l_mesh_request_guard_publish(&guard, 21U));
    assert(d1l_mesh_request_guard_transition(
        &guard, 21U, D1L_MESH_REQUEST_PENDING,
        D1L_MESH_REQUEST_OWNER_EXPIRED));
    assert(!d1l_mesh_request_guard_transition(
        &guard, 21U, D1L_MESH_REQUEST_PENDING,
        D1L_MESH_REQUEST_ADMITTED));
    assert(d1l_mesh_request_guard_release(
        &guard, 21U, D1L_MESH_REQUEST_OWNER_EXPIRED));

    assert(d1l_mesh_request_guard_claim(&guard, 22U));
    assert(d1l_mesh_request_guard_publish(&guard, 22U));
    assert(!d1l_mesh_request_guard_transition(
        &guard, 21U, D1L_MESH_REQUEST_PENDING,
        D1L_MESH_REQUEST_ADMITTED));
    assert(!d1l_mesh_request_guard_transition(
        &guard, 21U, D1L_MESH_REQUEST_ADMITTED,
        D1L_MESH_REQUEST_COMPLETED));
    assert(!d1l_mesh_request_guard_release(
        &guard, 21U, D1L_MESH_REQUEST_PENDING));
    assert(d1l_mesh_request_guard_state(&guard) == D1L_MESH_REQUEST_PENDING);
    assert(d1l_mesh_request_guard_matches(&guard, 22U));
    assert(d1l_mesh_request_guard_transition(
        &guard, 22U, D1L_MESH_REQUEST_PENDING,
        D1L_MESH_REQUEST_CALLER_CANCELLED));
    assert(d1l_mesh_request_guard_release(
        &guard, 22U, D1L_MESH_REQUEST_CALLER_CANCELLED));
}

static void test_atomic_identity_state_tuple_blocks_slot_reuse_aba(void)
{
    d1l_mesh_request_guard_t guard = {0};
    assert(sizeof(guard) == sizeof(uint32_t));
    assert(d1l_mesh_request_guard_claim(&guard, 31U));
    assert(d1l_mesh_request_guard_publish(&guard, 31U));

    const uint32_t stale_pending = d1l_mesh_request_guard_word(
        31U, D1L_MESH_REQUEST_PENDING);
    assert(d1l_mesh_request_guard_transition(
        &guard, 31U, D1L_MESH_REQUEST_PENDING,
        D1L_MESH_REQUEST_CALLER_CANCELLED));
    assert(d1l_mesh_request_guard_release(
        &guard, 31U, D1L_MESH_REQUEST_CALLER_CANCELLED));
    assert(d1l_mesh_request_guard_claim(&guard, 32U));
    assert(d1l_mesh_request_guard_publish(&guard, 32U));

    /* Model an old actor resuming with the exact CAS expectation it captured
     * before release/reuse. The new request has the same lifecycle state but
     * a different identity in the same atomic word, so the CAS must fail. */
    uint32_t expected = stale_pending;
    const uint32_t stale_admitted = d1l_mesh_request_guard_word(
        31U, D1L_MESH_REQUEST_ADMITTED);
    assert(!__atomic_compare_exchange_n(
        &guard.word, &expected, stale_admitted, false,
        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));
    assert(d1l_mesh_request_guard_matches(&guard, 32U));
    assert(d1l_mesh_request_guard_state(&guard) == D1L_MESH_REQUEST_PENDING);
    assert(!d1l_mesh_request_guard_transition(
        &guard, 31U, D1L_MESH_REQUEST_PENDING,
        D1L_MESH_REQUEST_ADMITTED));
    assert(d1l_mesh_request_guard_transition(
        &guard, 32U, D1L_MESH_REQUEST_PENDING,
        D1L_MESH_REQUEST_CALLER_CANCELLED));
    assert(d1l_mesh_request_guard_release(
        &guard, 32U, D1L_MESH_REQUEST_CALLER_CANCELLED));
}

static bool consume_terminal_if_current(
    d1l_mesh_tx_operation_identity_t *active,
    const d1l_mesh_tx_operation_identity_t *terminal)
{
    if (!d1l_mesh_tx_operation_identity_equal(active, terminal)) {
        return false;
    }
    *active = (d1l_mesh_tx_operation_identity_t){0};
    return true;
}

typedef struct {
    uint32_t active_origin;
    bool irq_recovery_claimed;
    bool mode_tx;
    bool dio_asserted;
    unsigned standby_count;
    unsigned irq_clear_count;
    unsigned expander_latch_read_count;
    unsigned callback_count;
} mock_origin_radio_t;

static bool mock_origin_radio_begin(mock_origin_radio_t *radio,
                                    uint32_t origin)
{
    if (!radio || origin == 0U || radio->active_origin != 0U ||
        radio->irq_recovery_claimed) {
        return false;
    }
    radio->active_origin = origin;
    radio->mode_tx = true;
    radio->dio_asserted = false;
    return true;
}

static bool mock_origin_radio_recover(mock_origin_radio_t *radio,
                                      uint32_t origin)
{
    if (!radio || origin == 0U || radio->irq_recovery_claimed) {
        return false;
    }
    radio->irq_recovery_claimed = true;
    if (radio->active_origin != origin) {
        radio->irq_recovery_claimed = false;
        return false;
    }
    radio->standby_count++;
    radio->irq_clear_count++;
    radio->expander_latch_read_count++;
    radio->mode_tx = false;
    radio->dio_asserted = false;
    radio->active_origin = 0U;
    radio->irq_recovery_claimed = false;
    return true;
}

static bool mock_origin_radio_terminal(mock_origin_radio_t *radio,
                                       uint32_t origin)
{
    if (!radio || radio->irq_recovery_claimed) {
        return false;
    }
    radio->irq_recovery_claimed = true;
    if (origin == 0U || radio->active_origin != origin) {
        radio->irq_recovery_claimed = false;
        return false;
    }
    radio->irq_clear_count++;
    radio->standby_count++;
    radio->mode_tx = false;
    radio->dio_asserted = false;
    radio->active_origin = 0U;
    radio->callback_count++;
    radio->irq_recovery_claimed = false;
    return true;
}

static void mock_origin_radio_finish_claimed_terminal(
    mock_origin_radio_t *radio, uint32_t origin)
{
    assert(radio && radio->irq_recovery_claimed);
    assert(radio->active_origin == origin);
    radio->irq_clear_count++;
    radio->standby_count++;
    radio->mode_tx = false;
    radio->dio_asserted = false;
    radio->active_origin = 0U;
    radio->callback_count++;
    radio->irq_recovery_claimed = false;
}

static void test_late_a_terminal_cannot_mutate_active_b(void)
{
    const d1l_mesh_tx_operation_identity_t operation_a = {
        .operation_id = 41U,
        .kind = D1L_MESH_TX_OPERATION_DM,
        .dm_session_id = 0x0102030405060708ULL,
        .dm_revision = 3U,
    };
    const d1l_mesh_tx_operation_identity_t operation_a_done = operation_a;
    const d1l_mesh_tx_operation_identity_t operation_a_late = operation_a;
    const d1l_mesh_tx_operation_identity_t operation_b = {
        .operation_id = 42U,
        .kind = D1L_MESH_TX_OPERATION_DM,
        .dm_session_id = 0x1112131415161718ULL,
        .dm_revision = 1U,
    };
    d1l_mesh_tx_operation_identity_t active = operation_a;

    /* A completes normally, and only then may the owner begin B. */
    assert(consume_terminal_if_current(&active, &operation_a_done));
    assert(!d1l_mesh_tx_operation_identity_valid(&active));
    active = operation_b;
    const d1l_mesh_tx_operation_identity_t b_before_late_a = active;

    /* Model either a delayed TxDone or delayed TxTimeout for A. Both carry
     * A's immutable origin tuple, fail the exact matcher, and leave every
     * field of B untouched. */
    assert(!consume_terminal_if_current(&active, &operation_a_late));
    assert(memcmp(&active, &b_before_late_a, sizeof(active)) == 0);
    assert(!consume_terminal_if_current(&active, &operation_a_late));
    assert(memcmp(&active, &b_before_late_a, sizeof(active)) == 0);

    assert(consume_terminal_if_current(&active, &operation_b));
    assert(!consume_terminal_if_current(&active, &operation_b));
}

static void test_watchdog_recovers_when_full_irq_queue_drops_terminal(void)
{
    const d1l_mesh_tx_operation_identity_t operation_a = {
        .operation_id = 71U,
        .kind = D1L_MESH_TX_OPERATION_DM,
        .dm_session_id = 0x7172737475767778ULL,
        .dm_revision = 3U,
    };
    const d1l_mesh_tx_operation_identity_t operation_b = {
        .operation_id = 72U,
        .kind = D1L_MESH_TX_OPERATION_PUBLIC,
    };
    d1l_mesh_tx_operation_identity_t active = operation_a;
    d1l_mesh_tx_watchdog_t watchdog_a = {0};
    mock_origin_radio_t radio = {0};
    const uint64_t started_us = 1000000ULL;
    const uint64_t timeout_us = 5250000ULL;
    assert(mock_origin_radio_begin(
        &radio, (uint32_t)operation_a.operation_id));
    assert(d1l_mesh_tx_watchdog_arm(
        &watchdog_a, &operation_a, started_us, timeout_us));

    /* Fill all ten vendor DIO queue slots and model the sole terminal IRQ
     * being dropped. No callback reaches the owner, so only the independent
     * immutable-origin watchdog can release this operation. */
    for (unsigned queued = 0U; queued < 10U; ++queued) {
        assert(watchdog_a.armed);
        assert(d1l_mesh_tx_operation_identity_equal(&active, &operation_a));
    }
    d1l_mesh_tx_operation_identity_t terminal = {0};
    assert(!d1l_mesh_tx_watchdog_take_due(
        &watchdog_a, started_us + timeout_us - 1U, &terminal));
    assert(d1l_mesh_tx_watchdog_take_due(
        &watchdog_a, started_us + timeout_us, &terminal));
    assert(d1l_mesh_tx_operation_identity_equal(&terminal, &operation_a));
    assert(mock_origin_radio_recover(
        &radio, (uint32_t)terminal.operation_id));
    assert(radio.standby_count == 1U);
    assert(radio.irq_clear_count == 1U);
    assert(radio.expander_latch_read_count == 1U);
    assert(consume_terminal_if_current(&active, &terminal));
    assert(!d1l_mesh_tx_operation_identity_valid(&active));
    assert(!watchdog_a.armed);
    assert(!d1l_mesh_tx_watchdog_take_due(
        &watchdog_a, UINT64_MAX, &terminal));

    /* B becomes eligible only after A's physical IRQ/latch cleanup. A queued
     * vendor callback delivered after B starts must return before clearing
     * B's IRQ, entering standby, or changing B's watchdog tuple. */
    active = operation_b;
    assert(mock_origin_radio_begin(
        &radio, (uint32_t)operation_b.operation_id));
    d1l_mesh_tx_watchdog_t watchdog_b = {0};
    assert(d1l_mesh_tx_watchdog_arm(
        &watchdog_b, &operation_b, started_us + timeout_us, timeout_us));
    const uint64_t b_deadline = watchdog_b.deadline_us;
    const unsigned standby_before_late_a = radio.standby_count;
    const unsigned irq_clear_before_late_a = radio.irq_clear_count;
    assert(!mock_origin_radio_terminal(
        &radio, (uint32_t)operation_a.operation_id));
    assert(radio.active_origin == (uint32_t)operation_b.operation_id);
    assert(radio.mode_tx);
    assert(radio.standby_count == standby_before_late_a);
    assert(radio.irq_clear_count == irq_clear_before_late_a);
    assert(watchdog_b.armed && watchdog_b.deadline_us == b_deadline);
    assert(d1l_mesh_tx_operation_identity_equal(
        &watchdog_b.operation, &operation_b));
    assert(d1l_mesh_tx_operation_identity_equal(&active, &operation_b));

    assert(mock_origin_radio_terminal(
        &radio, (uint32_t)operation_b.operation_id));
    assert(radio.callback_count == 1U);
    assert(consume_terminal_if_current(&active, &operation_b));
    d1l_mesh_tx_watchdog_reset(&watchdog_b);
}

static void test_vendor_terminal_claim_defers_competing_watchdog_recovery(void)
{
    const d1l_mesh_tx_operation_identity_t operation_a = {
        .operation_id = 91U,
        .kind = D1L_MESH_TX_OPERATION_GENERIC,
    };
    const d1l_mesh_tx_operation_identity_t operation_b = {
        .operation_id = 92U,
        .kind = D1L_MESH_TX_OPERATION_PUBLIC,
    };
    mock_origin_radio_t radio = {0};
    d1l_mesh_tx_operation_identity_t active = operation_a;
    d1l_mesh_tx_watchdog_t watchdog = {0};
    assert(mock_origin_radio_begin(
        &radio, (uint32_t)operation_a.operation_id));
    assert(d1l_mesh_tx_watchdog_arm(&watchdog, &operation_a, 10U, 20U));

    /* The vendor terminal path wins the physical cleanup claim just before
     * owner maintenance. Watchdog recovery must defer and keep A active, so B
     * cannot arm until the vendor path publishes A's exact callback. */
    radio.irq_recovery_claimed = true;
    d1l_mesh_tx_operation_identity_t terminal = {0};
    assert(d1l_mesh_tx_watchdog_take_due(&watchdog, 30U, &terminal));
    assert(!mock_origin_radio_recover(
        &radio, (uint32_t)terminal.operation_id));
    assert(d1l_mesh_tx_watchdog_arm(&watchdog, &terminal, 30U, 10U));
    assert(d1l_mesh_tx_operation_identity_equal(&active, &operation_a));
    assert(!mock_origin_radio_begin(
        &radio, (uint32_t)operation_b.operation_id));

    mock_origin_radio_finish_claimed_terminal(
        &radio, (uint32_t)operation_a.operation_id);
    assert(consume_terminal_if_current(&active, &operation_a));
    d1l_mesh_tx_watchdog_reset(&watchdog);
    active = operation_b;
    assert(mock_origin_radio_begin(
        &radio, (uint32_t)operation_b.operation_id));
    assert(radio.active_origin == (uint32_t)operation_b.operation_id);
    assert(radio.mode_tx);
}

static void test_watchdog_origin_cannot_cross_into_newer_operation(void)
{
    const d1l_mesh_tx_operation_identity_t operation_a = {
        .operation_id = 81U,
        .kind = D1L_MESH_TX_OPERATION_GENERIC,
    };
    const d1l_mesh_tx_operation_identity_t operation_b = {
        .operation_id = 82U,
        .kind = D1L_MESH_TX_OPERATION_PUBLIC,
    };
    d1l_mesh_tx_watchdog_t watchdog = {0};
    assert(d1l_mesh_tx_watchdog_arm(&watchdog, &operation_a, 10U, 20U));
    d1l_mesh_tx_watchdog_reset(&watchdog);
    assert(d1l_mesh_tx_watchdog_arm(&watchdog, &operation_b, 30U, 20U));

    d1l_mesh_tx_operation_identity_t active = operation_b;
    assert(!consume_terminal_if_current(&active, &operation_a));
    assert(d1l_mesh_tx_operation_identity_equal(&active, &operation_b));
    d1l_mesh_tx_operation_identity_t terminal = {0};
    assert(d1l_mesh_tx_watchdog_take_due(&watchdog, 50U, &terminal));
    assert(d1l_mesh_tx_operation_identity_equal(&terminal, &operation_b));
    assert(consume_terminal_if_current(&active, &terminal));
}

int main(void)
{
    test_caller_timeout_cancels_only_pending();
    test_admission_wins_timeout_and_requires_exact_completion();
    test_expiry_and_stale_reply_cannot_cross_slot_generations();
    test_atomic_identity_state_tuple_blocks_slot_reuse_aba();
    test_late_a_terminal_cannot_mutate_active_b();
    test_watchdog_recovers_when_full_irq_queue_drops_terminal();
    test_vendor_terminal_claim_defers_competing_watchdog_recovery();
    test_watchdog_origin_cannot_cross_into_newer_operation();
    puts("native mesh runtime guards: ok");
    return 0;
}
