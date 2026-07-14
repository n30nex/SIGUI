#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "mesh/dm_delivery_state.h"

static void test_names_cover_every_persisted_value(void)
{
    for (int value = 0; value < D1L_DM_DELIVERY_STATE_COUNT; ++value) {
        const d1l_dm_delivery_state_t state =
            (d1l_dm_delivery_state_t)value;
        assert(d1l_dm_delivery_state_valid(state));
        assert(strcmp(d1l_dm_delivery_state_name(state), "invalid") != 0);
    }
    for (int value = 0; value < D1L_DM_DELIVERY_REASON_COUNT; ++value) {
        const d1l_dm_delivery_reason_t reason =
            (d1l_dm_delivery_reason_t)value;
        assert(d1l_dm_delivery_reason_valid(reason));
        assert(strcmp(d1l_dm_delivery_reason_name(reason), "invalid") != 0);
    }
    assert(!d1l_dm_delivery_state_valid(D1L_DM_DELIVERY_STATE_COUNT));
    assert(!d1l_dm_delivery_reason_valid(D1L_DM_DELIVERY_REASON_COUNT));
}

static void test_happy_path_and_retry_edges_are_explicit(void)
{
    assert(d1l_dm_delivery_transition_allowed(
        D1L_DM_DELIVERY_QUEUED, D1L_DM_DELIVERY_WAITING_RADIO));
    assert(d1l_dm_delivery_transition_allowed(
        D1L_DM_DELIVERY_WAITING_RADIO, D1L_DM_DELIVERY_TX_ACTIVE));
    assert(d1l_dm_delivery_transition_allowed(
        D1L_DM_DELIVERY_TX_ACTIVE, D1L_DM_DELIVERY_TX_DONE));
    assert(d1l_dm_delivery_transition_allowed(
        D1L_DM_DELIVERY_TX_DONE, D1L_DM_DELIVERY_AWAITING_ACK));
    assert(d1l_dm_delivery_transition_allowed(
        D1L_DM_DELIVERY_AWAITING_ACK, D1L_DM_DELIVERY_ACKNOWLEDGED));
    assert(d1l_dm_delivery_transition_allowed(
        D1L_DM_DELIVERY_AWAITING_ACK, D1L_DM_DELIVERY_RETRY_WAIT));
    assert(d1l_dm_delivery_transition_allowed(
        D1L_DM_DELIVERY_RETRY_WAIT, D1L_DM_DELIVERY_RETRY_TX));
    assert(d1l_dm_delivery_transition_allowed(
        D1L_DM_DELIVERY_RETRY_TX, D1L_DM_DELIVERY_TX_ACTIVE));

    assert(!d1l_dm_delivery_transition_allowed(
        D1L_DM_DELIVERY_QUEUED, D1L_DM_DELIVERY_TX_DONE));
    assert(!d1l_dm_delivery_transition_allowed(
        D1L_DM_DELIVERY_TX_ACTIVE, D1L_DM_DELIVERY_AWAITING_ACK));
    assert(!d1l_dm_delivery_transition_allowed(
        D1L_DM_DELIVERY_ACKNOWLEDGED, D1L_DM_DELIVERY_RETRY_WAIT));
    assert(!d1l_dm_delivery_transition_allowed(
        D1L_DM_DELIVERY_CANCELLED, D1L_DM_DELIVERY_QUEUED));
    assert(!d1l_dm_delivery_transition_allowed(
        D1L_DM_DELIVERY_QUEUED, D1L_DM_DELIVERY_QUEUED));
}

static void test_ack_and_reboot_rules_preserve_truth(void)
{
    assert(d1l_dm_delivery_transition_allowed(
        D1L_DM_DELIVERY_INTERRUPTED_BY_REBOOT,
        D1L_DM_DELIVERY_ACKNOWLEDGED));
    assert(d1l_dm_delivery_transition_allowed(
        D1L_DM_DELIVERY_FAILED_TIMEOUT,
        D1L_DM_DELIVERY_ACKNOWLEDGED));
    assert(!d1l_dm_delivery_transition_allowed(
        D1L_DM_DELIVERY_FAILED_QUEUE,
        D1L_DM_DELIVERY_ACKNOWLEDGED));
    assert(!d1l_dm_delivery_transition_allowed(
        D1L_DM_DELIVERY_CANCELLED,
        D1L_DM_DELIVERY_ACKNOWLEDGED));

    for (int value = 0; value < D1L_DM_DELIVERY_STATE_COUNT; ++value) {
        const d1l_dm_delivery_state_t state =
            (d1l_dm_delivery_state_t)value;
        const bool expected =
            state == D1L_DM_DELIVERY_WAITING_RADIO ||
            state == D1L_DM_DELIVERY_TX_ACTIVE ||
            state == D1L_DM_DELIVERY_TX_DONE ||
            state == D1L_DM_DELIVERY_AWAITING_ACK ||
            state == D1L_DM_DELIVERY_RETRY_TX;
        assert(d1l_dm_delivery_interrupted_by_reboot(state) == expected);
    }
}

int main(void)
{
    test_names_cover_every_persisted_value();
    test_happy_path_and_retry_edges_are_explicit();
    test_ack_and_reboot_rules_preserve_truth();
    puts("native DM delivery state: ok");
    return 0;
}
