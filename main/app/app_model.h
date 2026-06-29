#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "mesh/contact_store.h"
#include "mesh/message_store.h"
#include "mesh/node_store.h"
#include "mesh/packet_log.h"

#define D1L_APP_SNAPSHOT_PACKET_PREVIEW 4U
#define D1L_APP_SNAPSHOT_MESSAGE_PREVIEW 4U
#define D1L_APP_SNAPSHOT_NODE_PREVIEW 4U
#define D1L_APP_SNAPSHOT_CONTACT_PREVIEW 2U

typedef struct {
    bool board_ready;
    bool ui_ready;
    esp_err_t board_error;
    uint32_t boot_count;
} d1l_app_model_t;

typedef struct {
    bool board_ready;
    bool ui_ready;
    bool identity_ready;
    bool radio_ready;
    bool companion_ready;
    char node_name[32];
    char identity_fingerprint[17];
    const char *mesh_state;
    uint32_t uptime_ms;
    uint32_t heap_free;
    uint32_t psram_free;
    uint32_t rx_packets;
    uint32_t rx_adverts;
    uint32_t tx_packets;
    uint32_t rejected_commands;
    uint32_t message_total_written;
    size_t message_count;
    uint32_t node_total_written;
    size_t node_count;
    uint32_t contact_total_written;
    size_t contact_count;
    uint32_t packet_total_written;
    size_t packet_count;
    uint8_t path_hash_bytes;
    d1l_contact_entry_t recent_contacts[D1L_APP_SNAPSHOT_CONTACT_PREVIEW];
    size_t recent_contact_count;
    d1l_node_entry_t recent_nodes[D1L_APP_SNAPSHOT_NODE_PREVIEW];
    size_t recent_node_count;
    d1l_message_entry_t recent_messages[D1L_APP_SNAPSHOT_MESSAGE_PREVIEW];
    size_t recent_message_count;
    d1l_packet_log_entry_t recent_packets[D1L_APP_SNAPSHOT_PACKET_PREVIEW];
    size_t recent_packet_count;
} d1l_app_snapshot_t;

d1l_app_model_t *d1l_app_model_get(void);
void d1l_app_model_snapshot(d1l_app_snapshot_t *snapshot);
esp_err_t d1l_app_model_send_public_test(void);
esp_err_t d1l_app_model_send_public_text(const char *text);
esp_err_t d1l_app_model_request_advert(bool flood);
