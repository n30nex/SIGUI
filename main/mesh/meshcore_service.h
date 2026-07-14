#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "mesh/meshcore_trace.h"

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
    uint32_t dm_route_direct_selected;
    uint32_t dm_route_flood_selected;
    uint32_t dm_route_missing_fallback;
    uint32_t dm_route_preboot_fallback;
    uint32_t dm_route_stale_fallback;
    uint32_t dm_route_malformed_fallback;
    uint32_t dm_route_last_path_age_ms;
    uint8_t dm_route_last_reason;
    uint32_t trace_tx_queued;
    uint32_t trace_rx_matched;
    uint32_t trace_rx_duplicates;
    uint32_t trace_pending_expired;
    uint32_t trace_rx_expired;
    uint32_t trace_rx_unmatched;
    uint32_t trace_rx_auth_mismatch;
    uint32_t trace_rx_path_mismatch;
    uint32_t trace_rx_malformed;
    uint32_t trace_rx_source_ignored;
    uint32_t trace_rx_in_flight_ignored;
    uint32_t trace_rx_unsupported;
    uint32_t runtime_command_queue_depth;
    uint32_t runtime_command_queue_high_water;
    uint32_t runtime_event_queue_depth;
    uint32_t runtime_event_queue_high_water;
    uint32_t runtime_queue_drops;
    uint32_t runtime_callback_event_drops;
    uint32_t runtime_task_heartbeat;
    uint32_t runtime_task_stack_free_words;
    uint64_t runtime_last_event_monotonic_us;
    uint8_t path_hash_bytes;
    bool identity_ready;
    bool radio_ready;
    bool radio_applied;
    bool radio_apply_pending;
    esp_err_t radio_apply_error;
    bool companion_framing_ready;
} d1l_meshcore_service_status_t;

typedef struct {
    bool pending;
    bool pending_expired;
    uint32_t pending_tag;
    uint32_t pending_age_ms;
    uint8_t pending_path_hops;
    uint8_t pending_path_hashes[D1L_MESHCORE_TRACE_MAX_HOPS];
    bool last_result_valid;
    uint32_t last_tag;
    uint32_t last_age_ms;
    uint8_t last_path_hops;
    uint8_t last_path_hashes[D1L_MESHCORE_TRACE_MAX_HOPS];
    int8_t last_path_snrs_quarter_db[D1L_MESHCORE_TRACE_MAX_HOPS];
    int last_rssi_dbm;
    int last_radio_snr_quarter_db;
    bool last_retention_attempted;
    bool last_route_summary_accepted;
    bool last_packet_preview_retained;
} d1l_meshcore_trace_snapshot_t;

void d1l_meshcore_service_init(void);
esp_err_t d1l_meshcore_service_start_rx_async(void);
esp_err_t d1l_meshcore_service_ensure_identity(void);
d1l_meshcore_service_status_t d1l_meshcore_service_status(void);
void d1l_meshcore_service_trace_snapshot(d1l_meshcore_trace_snapshot_t *out_snapshot);
esp_err_t d1l_meshcore_service_request_advert(bool flood);
esp_err_t d1l_meshcore_service_send_public(const char *text);
esp_err_t d1l_meshcore_service_send_dm(const char *fingerprint, const char *text);
esp_err_t d1l_meshcore_service_request_path_discovery_probe(
    const char *fingerprint,
    char *out_token,
    size_t out_token_size);
esp_err_t d1l_meshcore_service_send_trace_loop(const uint8_t *path_hashes,
                                               size_t path_hops,
                                               uint32_t *out_tag);
const char *d1l_meshcore_service_state_name(d1l_meshcore_service_state_t state);
