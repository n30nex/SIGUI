#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ui/ui_compose_eligibility.h"

static d1l_ui_compose_eligibility_input_t ready_public(void)
{
    return (d1l_ui_compose_eligibility_input_t) {
        .text_result = D1L_USER_TEXT_OK,
        .board_ready = true,
        .mesh_state = "ready",
        .radio_ready = true,
        .path_hash_bytes = 2U,
        .protocol_tx_ready = true,
        .settings_load_status = ESP_OK,
        .identity_state = D1L_IDENTITY_STATE_ABSENT,
        .channel_found = true,
        .channel_sendable = true,
        .previous_send_error = ESP_OK,
    };
}

static void expect_reason(
    d1l_ui_compose_eligibility_input_t *input,
    d1l_ui_compose_reason_t reason, bool enabled)
{
    const d1l_ui_compose_eligibility_t eligibility =
        d1l_ui_compose_eligibility(input);
    assert(eligibility.reason == reason);
    assert(eligibility.send_enabled == enabled);
    assert(eligibility.status && eligibility.status[0] != '\0');
}

int main(void)
{
    d1l_ui_compose_eligibility_t eligibility =
        d1l_ui_compose_eligibility(NULL);
    assert(!eligibility.send_enabled);
    assert(eligibility.reason == D1L_UI_COMPOSE_RADIO_UNAVAILABLE);

    d1l_ui_compose_eligibility_input_t input = ready_public();
    eligibility = d1l_ui_compose_eligibility(&input);
    assert(eligibility.send_enabled && !eligibility.retry_available);
    assert(eligibility.reason == D1L_UI_COMPOSE_READY);
    assert(strcmp(eligibility.status, "Ready to send") == 0);

    input.text_result = D1L_USER_TEXT_EMPTY;
    expect_reason(&input, D1L_UI_COMPOSE_EMPTY, false);
    input.text_result = D1L_USER_TEXT_TOO_LONG;
    expect_reason(&input, D1L_UI_COMPOSE_TOO_LONG, false);
    input.text_result = D1L_USER_TEXT_INVALID_UTF8;
    expect_reason(&input, D1L_UI_COMPOSE_INVALID_TEXT, false);

    input = ready_public();
    input.board_ready = false;
    expect_reason(&input, D1L_UI_COMPOSE_RADIO_UNAVAILABLE, false);
    input.board_ready = true;
    input.mesh_state = "initializing";
    expect_reason(&input, D1L_UI_COMPOSE_RADIO_STARTING, false);
    input.mesh_state = "waiting_for_radio";
    expect_reason(&input, D1L_UI_COMPOSE_RADIO_WAITING, false);
    input.mesh_state = "tx_busy";
    expect_reason(&input, D1L_UI_COMPOSE_RADIO_BUSY, false);
    input.mesh_state = "radio_error";
    expect_reason(&input, D1L_UI_COMPOSE_RADIO_ERROR, false);
    input.mesh_state = "unknown";
    expect_reason(&input, D1L_UI_COMPOSE_RADIO_UNAVAILABLE, false);
    input.mesh_state = "ready";
    input.radio_ready = false;
    expect_reason(&input, D1L_UI_COMPOSE_RADIO_UNAVAILABLE, false);
    input.radio_ready = true;
    input.path_hash_bytes = 0U;
    expect_reason(&input, D1L_UI_COMPOSE_ROUTE_POLICY_INVALID, false);
    input.path_hash_bytes = 4U;
    expect_reason(&input, D1L_UI_COMPOSE_ROUTE_POLICY_INVALID, false);
    input.path_hash_bytes = 2U;
    input.protocol_tx_ready = false;
    expect_reason(&input, D1L_UI_COMPOSE_PROTOCOL_TIME_UNAVAILABLE, false);

    input = ready_public();
    input.channel_found = false;
    expect_reason(&input, D1L_UI_COMPOSE_CHANNEL_MISSING, false);
    input.channel_found = true;
    input.channel_sendable = false;
    expect_reason(&input, D1L_UI_COMPOSE_CHANNEL_NOT_SENDABLE, false);

    input = ready_public();
    input.settings_load_status = ESP_FAIL;
    input.identity_state = D1L_IDENTITY_STATE_INCONSISTENT;
    input.contact_found = false;
    input.contact_sendable = false;
    input.dm_delivery_active = true;
    expect_reason(&input, D1L_UI_COMPOSE_READY, true);

    input = ready_public();
    input.is_dm = true;
    input.settings_load_status = ESP_FAIL;
    expect_reason(&input, D1L_UI_COMPOSE_SETTINGS_UNAVAILABLE, false);
    input.settings_load_status = ESP_OK;
    input.identity_state = D1L_IDENTITY_STATE_INCONSISTENT;
    expect_reason(&input, D1L_UI_COMPOSE_IDENTITY_INCONSISTENT, false);
    input.identity_state = D1L_IDENTITY_STATE_ABSENT;
    input.contact_found = false;
    expect_reason(&input, D1L_UI_COMPOSE_CONTACT_MISSING, false);
    input.contact_found = true;
    input.contact_sendable = false;
    expect_reason(&input, D1L_UI_COMPOSE_CONTACT_NOT_SENDABLE, false);
    input.contact_sendable = true;
    input.dm_delivery_active = true;
    expect_reason(&input, D1L_UI_COMPOSE_DM_DELIVERY_ACTIVE, false);
    input.dm_delivery_active = false;
    /* No direct-path field exists by design: this valid contact remains
     * eligible for the canonical bounded flood fallback. */
    expect_reason(&input, D1L_UI_COMPOSE_READY, true);

    input.previous_send_error = ESP_ERR_TIMEOUT;
    eligibility = d1l_ui_compose_eligibility(&input);
    assert(eligibility.send_enabled && eligibility.retry_available);
    assert(eligibility.reason == D1L_UI_COMPOSE_RETRY_TIMEOUT);
    input.previous_send_error = ESP_ERR_NO_MEM;
    expect_reason(&input, D1L_UI_COMPOSE_RETRY_MEMORY, true);
    input.previous_send_error = ESP_ERR_INVALID_STATE;
    expect_reason(&input, D1L_UI_COMPOSE_RETRY_REJECTED, true);
    input.previous_send_error = ESP_ERR_INVALID_ARG;
    expect_reason(&input, D1L_UI_COMPOSE_EDIT_REQUIRED, false);
    input.previous_send_error = ESP_ERR_NOT_FOUND;
    expect_reason(&input, D1L_UI_COMPOSE_RESELECT_CONTACT, false);
    input.previous_send_error = ESP_FAIL;
    expect_reason(&input, D1L_UI_COMPOSE_RECOVERY_REQUIRED, false);
    input.mesh_state = "tx_busy";
    expect_reason(&input, D1L_UI_COMPOSE_RADIO_BUSY, false);

    assert(d1l_ui_compose_error_clears_on_text_change(ESP_OK));
    assert(d1l_ui_compose_error_clears_on_text_change(ESP_ERR_INVALID_ARG));
    assert(d1l_ui_compose_error_clears_on_text_change(ESP_ERR_INVALID_SIZE));
    assert(d1l_ui_compose_error_clears_on_text_change(ESP_ERR_TIMEOUT));
    assert(d1l_ui_compose_error_clears_on_text_change(ESP_ERR_NO_MEM));
    assert(d1l_ui_compose_error_clears_on_text_change(ESP_ERR_INVALID_STATE));
    assert(!d1l_ui_compose_error_clears_on_text_change(ESP_ERR_NOT_FOUND));
    assert(!d1l_ui_compose_error_clears_on_text_change(ESP_ERR_NOT_SUPPORTED));
    assert(!d1l_ui_compose_error_clears_on_text_change(ESP_FAIL));

    puts("native UI compose eligibility: ok");
    return 0;
}
