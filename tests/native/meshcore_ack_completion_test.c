#include <assert.h>
#include <limits.h>
#include <stdio.h>

#include "mesh/dm_delivery_state.h"
#include "mesh/meshcore_ack_completion.h"
#include "mesh/meshcore_dm_retry.h"

static void advance_owner_to_awaiting_ack(d1l_dm_delivery_owner_t *owner)
{
    static const d1l_dm_delivery_state_t states[] = {
        D1L_DM_DELIVERY_WAITING_RADIO,
        D1L_DM_DELIVERY_TX_ACTIVE,
        D1L_DM_DELIVERY_TX_DONE,
        D1L_DM_DELIVERY_AWAITING_ACK,
    };
    for (size_t index = 0U; index < sizeof(states) / sizeof(states[0]); ++index) {
        const uint32_t revision = owner->revision;
        assert(d1l_dm_delivery_owner_apply(
            owner, owner->session_id, revision, states[index], revision + 1U));
    }
}

static void test_unmatched_and_stale_ack_are_non_mutating(void)
{
    d1l_meshcore_ack_completion_t completion = {0};
    assert(d1l_meshcore_ack_completion_begin(&completion, 0x1001U, 7U));
    assert(d1l_meshcore_ack_completion_observe_cas(
               &completion, 0x1001U, 7U, 8U, false, false) ==
           D1L_MESHCORE_ACK_CAS_REJECTED);
    assert(!completion.cas_admitted);
    assert(d1l_meshcore_ack_completion_observe_cas(
               &completion, 0x1002U, 7U, 8U, true, true) ==
           D1L_MESHCORE_ACK_CAS_STALE);
    assert(d1l_meshcore_ack_completion_observe_cas(
               &completion, 0x1001U, 6U, 7U, true, true) ==
           D1L_MESHCORE_ACK_CAS_STALE);
    assert(d1l_meshcore_ack_completion_observe_cas(
               &completion, 0x1001U, 7U, 9U, true, true) ==
           D1L_MESHCORE_ACK_CAS_STALE);
    assert(!completion.cas_admitted);
    assert(!d1l_meshcore_ack_completion_suppresses_rf_retry(&completion));
}

static void test_persistence_pending_retry_and_reconcile_are_take_once(void)
{
    d1l_meshcore_ack_completion_t completion = {0};
    d1l_dm_delivery_owner_t owner = {0};
    assert(d1l_dm_delivery_owner_begin(&owner, 0x2002U, 1U, 0xaabbccddU));
    advance_owner_to_awaiting_ack(&owner);
    assert(owner.revision == 5U);
    assert(d1l_dm_delivery_owner_ack_matches(&owner, 0xaabbccddU));
    assert(!d1l_dm_delivery_owner_ack_matches(&owner, 0xaabbccdeU));
    assert(d1l_meshcore_ack_completion_begin(
        &completion, owner.session_id, owner.revision));
    assert(d1l_meshcore_ack_completion_observe_cas(
               &completion, 0x2002U, 5U, 6U, true, false) ==
           D1L_MESHCORE_ACK_CAS_ACCEPTED_PENDING);
    assert(d1l_meshcore_ack_completion_suppresses_rf_retry(&completion));
    assert(!d1l_meshcore_ack_completion_take_terminal_effects(
        &completion, 0x2002U, 6U));
    assert(owner.state == D1L_DM_DELIVERY_AWAITING_ACK);

    /* The identical RF ACK may trigger only a retained persistence retry. */
    assert(d1l_meshcore_ack_completion_observe_cas(
               &completion, 0x2002U, 5U, 6U, true, false) ==
           D1L_MESHCORE_ACK_CAS_ALREADY_PENDING);
    assert(d1l_meshcore_ack_completion_mark_reconciled(
        &completion, 0x2002U, 6U));
    assert(d1l_dm_delivery_owner_apply(
        &owner, 0x2002U, 5U, D1L_DM_DELIVERY_ACKNOWLEDGED, 6U));
    assert(d1l_meshcore_ack_completion_take_terminal_effects(
        &completion, owner.session_id, owner.revision));
    assert(!d1l_meshcore_ack_completion_take_terminal_effects(
        &completion, owner.session_id, owner.revision));
    assert(!d1l_meshcore_ack_completion_mark_reconciled(
        &completion, owner.session_id, owner.revision));
    assert(!d1l_meshcore_ack_completion_suppresses_rf_retry(&completion));
}

static void test_exact_owner_durable_cas_and_revision_overflow(void)
{
    d1l_meshcore_ack_completion_t completion = {0};
    assert(d1l_meshcore_ack_completion_begin(&completion, 0x3003U, 99U));
    assert(d1l_meshcore_ack_completion_observe_cas(
               &completion, 0x3003U, 99U, 100U, true, true) ==
           D1L_MESHCORE_ACK_CAS_ACCEPTED_DURABLE);
    assert(d1l_meshcore_ack_completion_take_terminal_effects(
        &completion, 0x3003U, 100U));
    assert(!d1l_meshcore_ack_completion_take_terminal_effects(
        &completion, 0x3003U, 100U));

    d1l_meshcore_ack_completion_t overflow = {0};
    assert(!d1l_meshcore_ack_completion_begin(
        &overflow, 0x4004U, UINT32_MAX));
}

static void test_deadline_clear_is_after_durable_publish_and_take_once(void)
{
    d1l_meshcore_ack_completion_t completion = {0};
    d1l_meshcore_dm_ack_deadline_t deadline = {0};
    assert(d1l_meshcore_ack_completion_begin(&completion, 0x5005U, 4U));
    assert(d1l_meshcore_dm_ack_deadline_arm(
        &deadline, UINT64_MAX - 1U, D1L_MESHCORE_ROUTE_DIRECT));
    assert(deadline.armed && deadline.deadline_us == UINT64_MAX);
    assert(d1l_meshcore_ack_completion_observe_cas(
               &completion, 0x5005U, 4U, 5U, true, false) ==
           D1L_MESHCORE_ACK_CAS_ACCEPTED_PENDING);
    assert(deadline.armed);

    unsigned deadline_clears = 0U;
    unsigned route_credits = 0U;
    assert(d1l_meshcore_ack_completion_mark_reconciled(
        &completion, 0x5005U, 5U));
    if (d1l_meshcore_ack_completion_take_terminal_effects(
            &completion, 0x5005U, 5U)) {
        d1l_meshcore_dm_ack_deadline_clear(&deadline);
        deadline_clears++;
        route_credits++;
    }
    if (d1l_meshcore_ack_completion_take_terminal_effects(
            &completion, 0x5005U, 5U)) {
        d1l_meshcore_dm_ack_deadline_clear(&deadline);
        deadline_clears++;
        route_credits++;
    }
    assert(!deadline.armed);
    assert(deadline_clears == 1U);
    assert(route_credits == 1U);
}

int main(void)
{
    test_unmatched_and_stale_ack_are_non_mutating();
    test_persistence_pending_retry_and_reconcile_are_take_once();
    test_exact_owner_durable_cas_and_revision_overflow();
    test_deadline_clear_is_after_durable_publish_and_take_once();
    puts("native MeshCore ACK completion: ok");
    return 0;
}
