#include "app_model.h"

#include <stdio.h>
#include <string.h>

#include "app/settings_model.h"
#include "diagnostics/health_monitor.h"
#include "mesh/meshcore_service.h"

static d1l_app_model_t s_model = {
    .board_ready = false,
    .ui_ready = false,
    .board_error = ESP_ERR_INVALID_STATE,
    .boot_count = 0,
};

static void hex_prefix(char *dest, size_t dest_size, const uint8_t *src, size_t src_len)
{
    static const char hex[] = "0123456789ABCDEF";
    if (!dest || dest_size == 0) {
        return;
    }
    size_t out = 0;
    for (size_t i = 0; src && i < src_len && out + 2U < dest_size; ++i) {
        dest[out++] = hex[(src[i] >> 4) & 0x0fU];
        dest[out++] = hex[src[i] & 0x0fU];
    }
    dest[out] = '\0';
}

d1l_app_model_t *d1l_app_model_get(void)
{
    return &s_model;
}

void d1l_app_model_snapshot(d1l_app_snapshot_t *snapshot)
{
    if (!snapshot) {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));

    const d1l_settings_t *settings = d1l_settings_current();
    d1l_meshcore_service_status_t mesh = d1l_meshcore_service_status();
    d1l_message_store_stats_t messages = d1l_message_store_stats();
    d1l_dm_store_stats_t dms = d1l_dm_store_stats();
    d1l_node_store_stats_t nodes = d1l_node_store_stats();
    d1l_contact_store_stats_t contacts = d1l_contact_store_stats();
    d1l_route_store_stats_t routes = d1l_route_store_stats();
    d1l_packet_log_stats_t packets = d1l_packet_log_stats();
    d1l_health_snapshot_t health = d1l_health_snapshot();

    snapshot->board_ready = s_model.board_ready;
    snapshot->ui_ready = s_model.ui_ready;
    snapshot->identity_ready = settings->identity_ready || mesh.identity_ready;
    snapshot->radio_ready = mesh.radio_ready;
    snapshot->companion_ready = mesh.companion_framing_ready;
    snprintf(snapshot->node_name, sizeof(snapshot->node_name), "%s", settings->node_name);
    if (settings->identity_ready) {
        hex_prefix(snapshot->identity_fingerprint, sizeof(snapshot->identity_fingerprint),
                   settings->identity_public_key, 8U);
    }
    snapshot->mesh_state = d1l_meshcore_service_state_name(mesh.state);
    snapshot->uptime_ms = health.uptime_ms;
    snapshot->heap_free = health.heap_free;
    snapshot->psram_free = health.psram_free;
    snapshot->rx_packets = mesh.rx_packets;
    snapshot->rx_adverts = mesh.rx_adverts;
    snapshot->tx_packets = mesh.tx_packets;
    snapshot->rejected_commands = mesh.rejected_commands;
    snapshot->message_total_written = messages.total_written;
    snapshot->message_count = messages.count;
    snapshot->dm_total_written = dms.total_written;
    snapshot->dm_count = dms.count;
    snapshot->node_total_written = nodes.total_written;
    snapshot->node_count = nodes.count;
    snapshot->contact_total_written = contacts.total_written;
    snapshot->contact_count = contacts.count;
    snapshot->route_total_written = routes.total_written;
    snapshot->route_count = routes.count;
    snapshot->packet_total_written = packets.total_written;
    snapshot->packet_count = packets.count;
    snapshot->path_hash_bytes = mesh.path_hash_bytes;
    snapshot->recent_contact_count =
        d1l_contact_store_copy_recent(snapshot->recent_contacts, D1L_APP_SNAPSHOT_CONTACT_PREVIEW);
    snapshot->recent_route_count =
        d1l_route_store_copy_recent(snapshot->recent_routes, D1L_APP_SNAPSHOT_ROUTE_PREVIEW);
    snapshot->recent_node_count =
        d1l_node_store_copy_recent(snapshot->recent_nodes, D1L_APP_SNAPSHOT_NODE_PREVIEW);
    snapshot->recent_message_count =
        d1l_message_store_copy_recent(snapshot->recent_messages, D1L_APP_SNAPSHOT_MESSAGE_PREVIEW);
    snapshot->recent_dm_count =
        d1l_dm_store_copy_recent(snapshot->recent_dms, D1L_APP_SNAPSHOT_DM_PREVIEW);
    snapshot->recent_packet_count =
        d1l_packet_log_copy_recent(snapshot->recent_packets, D1L_APP_SNAPSHOT_PACKET_PREVIEW);
}

esp_err_t d1l_app_model_send_public_test(void)
{
    return d1l_meshcore_service_send_public("test");
}

esp_err_t d1l_app_model_send_public_text(const char *text)
{
    return d1l_meshcore_service_send_public(text);
}

esp_err_t d1l_app_model_send_dm_text(const char *fingerprint, const char *text)
{
    return d1l_meshcore_service_send_dm(fingerprint, text);
}

esp_err_t d1l_app_model_find_contact(const char *fingerprint, d1l_contact_entry_t *out_contact)
{
    if (!fingerprint || fingerprint[0] == '\0' || !out_contact) {
        return ESP_ERR_INVALID_ARG;
    }
    return d1l_contact_store_find_by_fingerprint(fingerprint, out_contact) ?
           ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t d1l_app_model_request_advert(bool flood)
{
    return d1l_meshcore_service_request_advert(flood);
}
