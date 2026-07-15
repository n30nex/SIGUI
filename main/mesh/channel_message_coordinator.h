#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define D1L_CHANNEL_MESSAGE_RECONCILE_RETRY_MS 5000U

/* Rebuilds every configured channel's retained newest/read/unread projection
 * from one coherent exact-channel message-ring snapshot. The operation is
 * bounded and retries if the ring changes while channel metadata persists. */
esp_err_t d1l_channel_message_reconcile(void);
bool d1l_channel_message_reconcile_pending(void);
esp_err_t d1l_channel_message_reconcile_if_due(uint32_t now_ms);
