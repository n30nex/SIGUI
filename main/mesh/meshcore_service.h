#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    D1L_MESHCORE_SERVICE_INITIALIZING = 0,
    D1L_MESHCORE_SERVICE_WAITING_FOR_RADIO,
    D1L_MESHCORE_SERVICE_READY,
    D1L_MESHCORE_SERVICE_TX_BUSY,
    D1L_MESHCORE_SERVICE_RADIO_ERROR,
} d1l_meshcore_service_state_t;

typedef struct {
    d1l_meshcore_service_state_t state;
    uint32_t rx_packets;
    uint32_t rx_adverts;
    uint32_t tx_packets;
    uint32_t rejected_commands;
    uint32_t ack_tx_queued;
    uint32_t ack_tx_done;
    uint32_t ack_tx_failed;
    uint32_t ack_tx_duplicate_rows_suppressed;
    uint32_t ack_tx_last_hash;
    esp_err_t ack_tx_last_error;
    uint8_t path_hash_bytes;
    bool identity_ready;
    bool radio_ready;
    bool radio_applied;
    bool radio_apply_pending;
    esp_err_t radio_apply_error;
    bool companion_framing_ready;
} d1l_meshcore_service_status_t;

void d1l_meshcore_service_init(void);
esp_err_t d1l_meshcore_service_start_rx_async(void);
esp_err_t d1l_meshcore_service_ensure_identity(void);
d1l_meshcore_service_status_t d1l_meshcore_service_status(void);
esp_err_t d1l_meshcore_service_request_advert(bool flood);
esp_err_t d1l_meshcore_service_send_public(const char *text);
esp_err_t d1l_meshcore_service_send_dm(const char *fingerprint, const char *text);
esp_err_t d1l_meshcore_service_request_trace_probe(const char *fingerprint,
                                                   char *out_token,
                                                   size_t out_token_size);
const char *d1l_meshcore_service_state_name(d1l_meshcore_service_state_t state);
