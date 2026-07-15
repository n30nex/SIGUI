#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "app/identity_state.h"
#include "mesh/user_text.h"

typedef enum {
    D1L_UI_COMPOSE_READY = 0,
    D1L_UI_COMPOSE_EMPTY,
    D1L_UI_COMPOSE_TOO_LONG,
    D1L_UI_COMPOSE_INVALID_TEXT,
    D1L_UI_COMPOSE_RADIO_STARTING,
    D1L_UI_COMPOSE_RADIO_WAITING,
    D1L_UI_COMPOSE_RADIO_BUSY,
    D1L_UI_COMPOSE_RADIO_ERROR,
    D1L_UI_COMPOSE_RADIO_UNAVAILABLE,
    D1L_UI_COMPOSE_ROUTE_POLICY_INVALID,
    D1L_UI_COMPOSE_PROTOCOL_TIME_UNAVAILABLE,
    D1L_UI_COMPOSE_SETTINGS_UNAVAILABLE,
    D1L_UI_COMPOSE_IDENTITY_INCONSISTENT,
    D1L_UI_COMPOSE_CHANNEL_MISSING,
    D1L_UI_COMPOSE_CHANNEL_NOT_SENDABLE,
    D1L_UI_COMPOSE_CONTACT_MISSING,
    D1L_UI_COMPOSE_CONTACT_NOT_SENDABLE,
    D1L_UI_COMPOSE_DM_DELIVERY_ACTIVE,
    D1L_UI_COMPOSE_RETRY_TIMEOUT,
    D1L_UI_COMPOSE_RETRY_MEMORY,
    D1L_UI_COMPOSE_RETRY_REJECTED,
    D1L_UI_COMPOSE_EDIT_REQUIRED,
    D1L_UI_COMPOSE_RESELECT_CONTACT,
    D1L_UI_COMPOSE_RECOVERY_REQUIRED,
} d1l_ui_compose_reason_t;

typedef struct {
    bool is_dm;
    d1l_user_text_result_t text_result;
    bool board_ready;
    const char *mesh_state;
    bool radio_ready;
    uint8_t path_hash_bytes;
    bool protocol_tx_ready;
    esp_err_t settings_load_status;
    d1l_identity_state_t identity_state;
    bool channel_found;
    bool channel_sendable;
    bool contact_found;
    bool contact_sendable;
    bool dm_delivery_active;
    esp_err_t previous_send_error;
} d1l_ui_compose_eligibility_input_t;

typedef struct {
    bool send_enabled;
    bool retry_available;
    d1l_ui_compose_reason_t reason;
    const char *status;
} d1l_ui_compose_eligibility_t;

/* Pure projection used both while the modal is visible and immediately before
 * an explicit Send action.  A missing direct path is intentionally not an
 * input: canonical DM routing can use the bounded flood fallback.  A pending
 * profile is also not an input because the runtime reapplies it on demand. */
d1l_ui_compose_eligibility_t d1l_ui_compose_eligibility(
    const d1l_ui_compose_eligibility_input_t *input);

/* Changing the payload can resolve input errors and turns a transient retry
 * into a new explicit send.  It must not clear contact/recovery failures. */
bool d1l_ui_compose_error_clears_on_text_change(esp_err_t error);
