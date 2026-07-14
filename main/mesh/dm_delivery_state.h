#pragma once

#include <stdbool.h>

typedef enum {
    D1L_DM_DELIVERY_NOT_APPLICABLE = 0,
    D1L_DM_DELIVERY_QUEUED,
    D1L_DM_DELIVERY_WAITING_RADIO,
    D1L_DM_DELIVERY_TX_ACTIVE,
    D1L_DM_DELIVERY_TX_DONE,
    D1L_DM_DELIVERY_AWAITING_ACK,
    D1L_DM_DELIVERY_ACKNOWLEDGED,
    D1L_DM_DELIVERY_RETRY_WAIT,
    D1L_DM_DELIVERY_RETRY_TX,
    D1L_DM_DELIVERY_FAILED_RADIO,
    D1L_DM_DELIVERY_FAILED_TIMEOUT,
    D1L_DM_DELIVERY_FAILED_QUEUE,
    D1L_DM_DELIVERY_INTERRUPTED_BY_REBOOT,
    D1L_DM_DELIVERY_CANCELLED,
    D1L_DM_DELIVERY_STATE_COUNT,
} d1l_dm_delivery_state_t;

typedef enum {
    D1L_DM_DELIVERY_REASON_NONE = 0,
    D1L_DM_DELIVERY_REASON_ENQUEUED,
    D1L_DM_DELIVERY_REASON_RADIO_RESERVED,
    D1L_DM_DELIVERY_REASON_RADIO_STARTED,
    D1L_DM_DELIVERY_REASON_RADIO_COMPLETED,
    D1L_DM_DELIVERY_REASON_ACK_EXPECTED,
    D1L_DM_DELIVERY_REASON_ACK_RECEIVED,
    D1L_DM_DELIVERY_REASON_RETRY_SCHEDULED,
    D1L_DM_DELIVERY_REASON_RETRY_STARTED,
    D1L_DM_DELIVERY_REASON_QUEUE_REJECTED,
    D1L_DM_DELIVERY_REASON_RADIO_ERROR,
    D1L_DM_DELIVERY_REASON_ACK_TIMEOUT,
    D1L_DM_DELIVERY_REASON_REBOOT_RECOVERY,
    D1L_DM_DELIVERY_REASON_USER_CANCELLED,
    D1L_DM_DELIVERY_REASON_USER_RETRY,
    D1L_DM_DELIVERY_REASON_LEGACY_IMPORT,
    D1L_DM_DELIVERY_REASON_COUNT,
} d1l_dm_delivery_reason_t;

bool d1l_dm_delivery_state_valid(d1l_dm_delivery_state_t state);
bool d1l_dm_delivery_reason_valid(d1l_dm_delivery_reason_t reason);
bool d1l_dm_delivery_transition_allowed(d1l_dm_delivery_state_t from,
                                        d1l_dm_delivery_state_t to);
bool d1l_dm_delivery_interrupted_by_reboot(d1l_dm_delivery_state_t state);
const char *d1l_dm_delivery_state_name(d1l_dm_delivery_state_t state);
const char *d1l_dm_delivery_reason_name(d1l_dm_delivery_reason_t reason);
