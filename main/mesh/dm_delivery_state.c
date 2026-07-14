#include "dm_delivery_state.h"

bool d1l_dm_delivery_state_valid(d1l_dm_delivery_state_t state)
{
    return state >= D1L_DM_DELIVERY_NOT_APPLICABLE &&
           state < D1L_DM_DELIVERY_STATE_COUNT;
}

bool d1l_dm_delivery_reason_valid(d1l_dm_delivery_reason_t reason)
{
    return reason >= D1L_DM_DELIVERY_REASON_NONE &&
           reason < D1L_DM_DELIVERY_REASON_COUNT;
}

bool d1l_dm_delivery_transition_allowed(d1l_dm_delivery_state_t from,
                                        d1l_dm_delivery_state_t to)
{
    if (!d1l_dm_delivery_state_valid(from) ||
        !d1l_dm_delivery_state_valid(to) || from == to ||
        from == D1L_DM_DELIVERY_NOT_APPLICABLE ||
        to == D1L_DM_DELIVERY_NOT_APPLICABLE) {
        return false;
    }

    /* A matching authenticated ACK is stronger evidence than a local timeout,
     * radio error, or reboot interruption.  It may therefore close any state
     * in which bytes could already have reached the peer. */
    if (to == D1L_DM_DELIVERY_ACKNOWLEDGED) {
        return from != D1L_DM_DELIVERY_ACKNOWLEDGED &&
               from != D1L_DM_DELIVERY_FAILED_QUEUE &&
               from != D1L_DM_DELIVERY_CANCELLED;
    }

    switch (from) {
    case D1L_DM_DELIVERY_QUEUED:
        return to == D1L_DM_DELIVERY_WAITING_RADIO ||
               to == D1L_DM_DELIVERY_FAILED_QUEUE ||
               to == D1L_DM_DELIVERY_CANCELLED;
    case D1L_DM_DELIVERY_WAITING_RADIO:
        return to == D1L_DM_DELIVERY_TX_ACTIVE ||
               to == D1L_DM_DELIVERY_FAILED_QUEUE ||
               to == D1L_DM_DELIVERY_RETRY_WAIT ||
               to == D1L_DM_DELIVERY_CANCELLED;
    case D1L_DM_DELIVERY_TX_ACTIVE:
        return to == D1L_DM_DELIVERY_TX_DONE ||
               to == D1L_DM_DELIVERY_FAILED_RADIO ||
               to == D1L_DM_DELIVERY_RETRY_WAIT ||
               to == D1L_DM_DELIVERY_CANCELLED;
    case D1L_DM_DELIVERY_TX_DONE:
        return to == D1L_DM_DELIVERY_AWAITING_ACK ||
               to == D1L_DM_DELIVERY_RETRY_WAIT ||
               to == D1L_DM_DELIVERY_FAILED_TIMEOUT ||
               to == D1L_DM_DELIVERY_CANCELLED;
    case D1L_DM_DELIVERY_AWAITING_ACK:
        return to == D1L_DM_DELIVERY_RETRY_WAIT ||
               to == D1L_DM_DELIVERY_FAILED_TIMEOUT ||
               to == D1L_DM_DELIVERY_CANCELLED;
    case D1L_DM_DELIVERY_RETRY_WAIT:
        return to == D1L_DM_DELIVERY_RETRY_TX ||
               to == D1L_DM_DELIVERY_FAILED_TIMEOUT ||
               to == D1L_DM_DELIVERY_CANCELLED;
    case D1L_DM_DELIVERY_RETRY_TX:
        return to == D1L_DM_DELIVERY_WAITING_RADIO ||
               to == D1L_DM_DELIVERY_TX_ACTIVE ||
               to == D1L_DM_DELIVERY_FAILED_QUEUE ||
               to == D1L_DM_DELIVERY_FAILED_RADIO ||
               to == D1L_DM_DELIVERY_RETRY_WAIT ||
               to == D1L_DM_DELIVERY_CANCELLED;
    case D1L_DM_DELIVERY_FAILED_RADIO:
    case D1L_DM_DELIVERY_FAILED_TIMEOUT:
    case D1L_DM_DELIVERY_FAILED_QUEUE:
        return to == D1L_DM_DELIVERY_QUEUED ||
               to == D1L_DM_DELIVERY_RETRY_WAIT ||
               to == D1L_DM_DELIVERY_CANCELLED;
    case D1L_DM_DELIVERY_INTERRUPTED_BY_REBOOT:
        return to == D1L_DM_DELIVERY_QUEUED ||
               to == D1L_DM_DELIVERY_RETRY_WAIT ||
               to == D1L_DM_DELIVERY_FAILED_TIMEOUT ||
               to == D1L_DM_DELIVERY_CANCELLED;
    default:
        return false;
    }
}

bool d1l_dm_delivery_interrupted_by_reboot(d1l_dm_delivery_state_t state)
{
    return state == D1L_DM_DELIVERY_WAITING_RADIO ||
           state == D1L_DM_DELIVERY_TX_ACTIVE ||
           state == D1L_DM_DELIVERY_TX_DONE ||
           state == D1L_DM_DELIVERY_AWAITING_ACK ||
           state == D1L_DM_DELIVERY_RETRY_TX;
}

const char *d1l_dm_delivery_state_name(d1l_dm_delivery_state_t state)
{
    static const char *const names[D1L_DM_DELIVERY_STATE_COUNT] = {
        "not_applicable", "queued", "waiting_radio", "tx_active",
        "tx_done", "awaiting_ack", "acknowledged", "retry_wait",
        "retry_tx", "failed_radio", "failed_timeout", "failed_queue",
        "interrupted_by_reboot", "cancelled",
    };
    return d1l_dm_delivery_state_valid(state) ? names[state] : "invalid";
}

const char *d1l_dm_delivery_reason_name(d1l_dm_delivery_reason_t reason)
{
    static const char *const names[D1L_DM_DELIVERY_REASON_COUNT] = {
        "none", "enqueued", "radio_reserved", "radio_started",
        "radio_completed", "ack_expected", "ack_received",
        "retry_scheduled", "retry_started", "queue_rejected",
        "radio_error", "ack_timeout", "reboot_recovery",
        "user_cancelled", "user_retry", "legacy_import",
    };
    return d1l_dm_delivery_reason_valid(reason) ? names[reason] : "invalid";
}
