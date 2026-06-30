#include "app_model.h"

#include <stdio.h>
#include <string.h>

#include "app/settings_model.h"
#include "comms/connectivity_manager.h"
#include "d1l_config.h"
#include "diagnostics/health_monitor.h"
#include "mesh/meshcore_service.h"
#include "mesh/read_state.h"
#include "storage/storage_status.h"

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

static bool valid_radio_edit(const d1l_app_radio_profile_edit_t *profile)
{
    if (!profile) {
        return false;
    }
    return profile->frequency_hz >= 902000000UL &&
           profile->frequency_hz <= 928000000UL &&
           profile->bandwidth_tenths_khz >= 78U &&
           profile->bandwidth_tenths_khz <= 5000U &&
           profile->spreading_factor >= 5U &&
           profile->spreading_factor <= 12U &&
           profile->coding_rate >= 5U &&
           profile->coding_rate <= 8U &&
           profile->tx_power_dbm >= -9 &&
           profile->tx_power_dbm <= D1L_RADIO_TX_POWER_DBM;
}

static void radio_edit_from_settings(const d1l_settings_t *settings,
                                     d1l_app_radio_profile_edit_t *profile)
{
    if (!profile) {
        return;
    }
    const d1l_settings_t *src = settings ? settings : d1l_settings_current();
    profile->frequency_hz = src->frequency_hz;
    profile->bandwidth_tenths_khz = src->bandwidth_tenths_khz;
    profile->spreading_factor = src->spreading_factor;
    profile->coding_rate = src->coding_rate;
    profile->tx_power_dbm = src->tx_power_dbm;
    profile->rx_boost = src->rx_boost;
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
    d1l_read_state_stats_t read_state = d1l_read_state_stats();
    d1l_node_store_stats_t nodes = d1l_node_store_stats();
    d1l_contact_store_stats_t contacts = d1l_contact_store_stats();
    d1l_route_store_stats_t routes = d1l_route_store_stats();
    d1l_packet_log_stats_t packets = d1l_packet_log_stats();
    d1l_health_snapshot_t health = d1l_health_snapshot();
    d1l_connectivity_status_t connectivity = {0};
    d1l_connectivity_status(&connectivity);
    d1l_storage_status_t storage = {0};
    d1l_storage_status(&storage);

    snapshot->board_ready = s_model.board_ready;
    snapshot->ui_ready = s_model.ui_ready;
    snapshot->identity_ready = settings->identity_ready || mesh.identity_ready;
    snapshot->radio_ready = mesh.radio_ready;
    snapshot->companion_ready = mesh.companion_framing_ready;
    snapshot->radio_frequency_hz = settings->frequency_hz;
    snapshot->radio_bandwidth_tenths_khz = settings->bandwidth_tenths_khz;
    snapshot->radio_spreading_factor = settings->spreading_factor;
    snapshot->radio_coding_rate = settings->coding_rate;
    snapshot->radio_tx_power_dbm = settings->tx_power_dbm;
    snapshot->radio_rx_boost = settings->rx_boost;
    snapshot->radio_tcxo = d1l_settings_tcxo_name(settings->tcxo_mode);
    snapshot->wifi_enabled = connectivity.wifi_enabled_setting;
    snapshot->ble_companion_enabled = connectivity.ble_companion_enabled_setting;
    snapshot->observer_enabled = connectivity.observer_enabled_setting;
    snapshot->onboarding_complete = settings->onboarding_complete;
    snapshot->wifi_build_enabled = connectivity.wifi_build_enabled;
    snapshot->ble_build_enabled = connectivity.ble_build_enabled;
    snapshot->wifi_state = connectivity.wifi_state;
    snapshot->ble_state = connectivity.ble_state;
    snapshot->coexistence_policy = connectivity.coexistence_policy;
    snapshot->storage_direct_supported = storage.direct_supported;
    snapshot->storage_rp2040_bridge_required = storage.rp2040_bridge_required;
    snapshot->storage_rp2040_bridge_ready = storage.rp2040_bridge_ready;
    snapshot->storage_rp2040_sd_protocol_supported = storage.rp2040_sd_protocol_supported;
    snapshot->storage_sd_present = storage.sd_present;
    snapshot->storage_sd_mounted = storage.sd_mounted;
    snapshot->storage_sd_data_root_ready = storage.sd_data_root_ready;
    snapshot->storage_format_required = storage.format_required;
    snapshot->storage_format_supported = storage.format_supported;
    snapshot->storage_setup_required = storage.setup_required;
    snapshot->storage_setup_supported = storage.setup_supported;
    snapshot->storage_data_enabled = storage.data_enabled;
    snapshot->storage_response_truncated = storage.response_truncated;
    snapshot->storage_capacity_kb = storage.capacity_kb;
    snapshot->storage_free_kb = storage.free_kb;
    snapshot->storage_last_error = storage.last_error;
    snapshot->storage_sd_state = storage.sd_state;
    snapshot->storage_sd_interface = storage.sd_interface;
    snapshot->storage_sd_filesystem = storage.sd_filesystem;
    snapshot->storage_mount_point = storage.mount_point;
    snapshot->storage_data_root = storage.data_root;
    snapshot->storage_backend = storage.data_backend;
    snapshot->message_store_backend = storage.message_store_backend;
    snapshot->dm_store_backend = storage.dm_store_backend;
    snapshot->packet_log_backend = storage.packet_log_backend;
    snapshot->route_store_backend = storage.route_store_backend;
    snapshot->map_tile_backend = storage.map_tile_backend;
    snapshot->export_backend = storage.export_backend;
    snapshot->storage_setup_action = storage.setup_action;
    snapshot->storage_format_action = storage.format_action;
    snapshot->storage_note = storage.note;
    snprintf(snapshot->node_name, sizeof(snapshot->node_name), "%s", settings->node_name);
    if (settings->identity_ready) {
        hex_prefix(snapshot->identity_fingerprint, sizeof(snapshot->identity_fingerprint),
                   settings->identity_public_key, 8U);
    }
    snapshot->mesh_state = d1l_meshcore_service_state_name(mesh.state);
    snapshot->reset_reason = health.reset_reason;
    snapshot->uptime_ms = health.uptime_ms;
    snapshot->heap_free = health.heap_free;
    snapshot->heap_min_free = health.heap_min_free;
    snapshot->heap_largest_free = health.heap_largest_free;
    snapshot->psram_free = health.psram_free;
    snapshot->psram_min_free = health.psram_min_free;
    snapshot->psram_largest_free = health.psram_largest_free;
    snapshot->current_task_stack_free_words = health.current_task_stack_free_words;
    snapshot->ui_task_stack_free_words = health.ui_task_stack_free_words;
    snapshot->lvgl_free_bytes = health.lvgl_free_bytes;
    snapshot->lvgl_largest_free_bytes = health.lvgl_largest_free_bytes;
    snapshot->lvgl_used_pct = health.lvgl_used_pct;
    snapshot->rx_packets = mesh.rx_packets;
    snapshot->rx_adverts = mesh.rx_adverts;
    snapshot->tx_packets = mesh.tx_packets;
    snapshot->rejected_commands = mesh.rejected_commands;
    snapshot->message_total_written = messages.total_written;
    snapshot->message_count = messages.count;
    snapshot->dm_total_written = dms.total_written;
    snapshot->dm_count = dms.count;
    snapshot->public_unread_count = read_state.public_unread_count;
    snapshot->dm_unread_count = read_state.dm_unread_count;
    snapshot->muted_dm_unread_count = read_state.muted_dm_unread_count;
    snapshot->last_public_read_seq = read_state.last_public_read_seq;
    snapshot->last_dm_read_seq = read_state.last_dm_read_seq;
    snapshot->node_total_written = nodes.total_written;
    snapshot->node_count = nodes.count;
    snapshot->contact_total_written = contacts.total_written;
    snapshot->contact_count = contacts.count;
    snapshot->route_total_written = routes.total_written;
    snapshot->route_count = routes.count;
    snapshot->packet_total_written = packets.total_written;
    snapshot->packet_count = packets.count;
    d1l_mesh_inspector_signal_summary(&snapshot->signal_summary);
    snapshot->recent_room_count =
        d1l_mesh_inspector_copy_room_servers(snapshot->recent_rooms, D1L_APP_SNAPSHOT_ROOM_PREVIEW);
    snapshot->recent_repeater_count =
        d1l_mesh_inspector_copy_repeater_candidates(snapshot->recent_repeaters,
                                                    D1L_APP_SNAPSHOT_REPEATER_PREVIEW);
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
    for (size_t i = 0; i < snapshot->recent_dm_count; ++i) {
        snapshot->recent_dm_unread[i] = d1l_read_state_dm_entry_is_unread(&snapshot->recent_dms[i]);
    }
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

size_t d1l_app_model_query_public_messages(d1l_message_entry_t *out_entries,
                                           size_t max_entries, const char *query)
{
    return d1l_message_store_query(out_entries, max_entries, query);
}

esp_err_t d1l_app_model_send_dm_text(const char *fingerprint, const char *text)
{
    return d1l_meshcore_service_send_dm(fingerprint, text);
}

size_t d1l_app_model_copy_dm_thread(const char *fingerprint, d1l_dm_entry_t *out_entries,
                                    bool *out_unread, size_t max_entries)
{
    const size_t copied = d1l_dm_store_copy_thread(fingerprint, out_entries, max_entries);
    if (out_unread) {
        for (size_t i = 0; i < copied; ++i) {
            out_unread[i] = d1l_read_state_dm_entry_is_unread(&out_entries[i]);
        }
    }
    return copied;
}

esp_err_t d1l_app_model_find_contact(const char *fingerprint, d1l_contact_entry_t *out_contact)
{
    if (!fingerprint || fingerprint[0] == '\0' || !out_contact) {
        return ESP_ERR_INVALID_ARG;
    }
    return d1l_contact_store_find_by_fingerprint(fingerprint, out_contact) ?
           ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t d1l_app_model_set_contact_flags(const char *fingerprint, bool favorite, bool muted,
                                          d1l_contact_entry_t *out_contact)
{
    return d1l_contact_store_set_flags(fingerprint, favorite, muted, out_contact);
}

esp_err_t d1l_app_model_rename_contact(const char *fingerprint, const char *alias,
                                       d1l_contact_entry_t *out_contact)
{
    return d1l_contact_store_rename(fingerprint, alias, out_contact);
}

esp_err_t d1l_app_model_delete_contact(const char *fingerprint, d1l_contact_entry_t *out_contact)
{
    return d1l_contact_store_delete(fingerprint, out_contact);
}

esp_err_t d1l_app_model_export_contact_uri(const char *fingerprint, char *dest, size_t dest_size)
{
    if (!fingerprint || fingerprint[0] == '\0' || !dest || dest_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    d1l_contact_entry_t contact = {0};
    if (!d1l_contact_store_find_by_fingerprint(fingerprint, &contact)) {
        return ESP_ERR_NOT_FOUND;
    }
    return d1l_contact_store_export_uri(&contact, dest, dest_size);
}

size_t d1l_app_model_copy_route_trace(const char *fingerprint, d1l_route_entry_t *out_entries,
                                      size_t max_entries)
{
    return d1l_route_store_copy_for_target(fingerprint, out_entries, max_entries);
}

esp_err_t d1l_app_model_mark_messages_read(void)
{
    return d1l_read_state_mark_all_read();
}

esp_err_t d1l_app_model_mark_dm_thread_read(const char *fingerprint)
{
    return d1l_read_state_mark_dm_thread_read(fingerprint);
}

esp_err_t d1l_app_model_request_advert(bool flood)
{
    return d1l_meshcore_service_request_advert(flood);
}

void d1l_app_model_current_radio_profile(d1l_app_radio_profile_edit_t *profile)
{
    radio_edit_from_settings(d1l_settings_current(), profile);
}

void d1l_app_model_default_radio_profile(d1l_app_radio_profile_edit_t *profile)
{
    if (!profile) {
        return;
    }
    const d1l_radio_profile_t *defaults = d1l_radio_profile_uscan_default();
    profile->frequency_hz = defaults->frequency_hz;
    profile->bandwidth_tenths_khz = (uint16_t)((defaults->bandwidth_khz * 10.0f) + 0.5f);
    profile->spreading_factor = defaults->spreading_factor;
    profile->coding_rate = defaults->coding_rate;
    profile->tx_power_dbm = defaults->tx_power_dbm;
    profile->rx_boost = defaults->rx_boost;
}

esp_err_t d1l_app_model_save_radio_profile(const d1l_app_radio_profile_edit_t *profile)
{
    if (!valid_radio_edit(profile)) {
        return ESP_ERR_INVALID_ARG;
    }

    d1l_settings_t settings = *d1l_settings_current();
    settings.frequency_hz = profile->frequency_hz;
    settings.bandwidth_tenths_khz = profile->bandwidth_tenths_khz;
    settings.spreading_factor = profile->spreading_factor;
    settings.coding_rate = profile->coding_rate;
    settings.tx_power_dbm = profile->tx_power_dbm;
    settings.rx_boost = profile->rx_boost;
    settings.tcxo_mode = D1L_TCXO_NONE;
    return d1l_settings_save(&settings);
}

esp_err_t d1l_app_model_complete_onboarding(const char *node_name)
{
    esp_err_t ret = d1l_settings_complete_onboarding(node_name, false, false, false);
    if (ret != ESP_OK) {
        return ret;
    }
    d1l_meshcore_service_init();
    return d1l_meshcore_service_ensure_identity();
}

esp_err_t d1l_app_model_reset_onboarding(void)
{
    return d1l_settings_reset_onboarding();
}
