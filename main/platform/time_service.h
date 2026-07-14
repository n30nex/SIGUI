#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "platform/time_service_core.h"

#define D1L_TIME_TLS_WAIT_TIMEOUT_MS 15000U
#define D1L_TIME_TLS_WAIT_SLICE_MS 500U

typedef bool (*d1l_time_continue_cb_t)(void *context);

typedef struct {
    d1l_time_core_snapshot_t clock;
    d1l_time_protocol_persistence_state_t protocol_persistence_state;
    esp_err_t protocol_persistence_error;
    esp_err_t protocol_tx_error;
    esp_err_t sntp_init_error;
    bool initialized;
    bool protocol_persistence_ready;
    bool protocol_tx_ready;
    bool sntp_initialized;
} d1l_time_service_status_t;

esp_err_t d1l_time_service_init(void);
uint64_t d1l_time_service_boot_monotonic_us(void);
esp_err_t d1l_time_service_preflight_protocol_timestamp(void);
esp_err_t d1l_time_service_next_protocol_timestamp(uint32_t *out_timestamp);
void d1l_time_service_status(d1l_time_service_status_t *out_status);
bool d1l_time_service_certificate_time_valid(void);
esp_err_t d1l_time_service_wait_for_certificate_time(
    uint32_t timeout_ms,
    uint32_t slice_ms,
    d1l_time_continue_cb_t should_continue,
    void *continue_context);
/* `authenticated` is a capability boundary, not user input.  Only a verified
 * companion session owner may pass true; USB/CLI parsers must not expose it. */
esp_err_t d1l_time_service_set_companion_time(int64_t epoch_sec,
                                              bool authenticated);
esp_err_t d1l_time_service_note_authenticated_lower_bound(
    int64_t epoch_sec,
    bool authenticated);

/* WP-12 follow-up slices retain ownership here: persist validated wall
 * checkpoints without write amplification, wire the authenticated companion
 * transport, and add timezone/display conversion without changing protocol
 * allocation semantics. */
