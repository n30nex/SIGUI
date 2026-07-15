#include "app_model.h"

#include <stdio.h>
#include <string.h>

#include "app/dm_conversation_list.h"
#include "app/settings_model.h"
#include "comms/connectivity_manager.h"
#include "d1l_config.h"
#include "diagnostics/health_monitor.h"
#include "mesh/meshcore_service.h"
#include "mesh/read_state.h"
#include "platform/time_service.h"
#include "storage/map_tile_store.h"
#include "storage/storage_status.h"

static d1l_app_model_t s_model = {
    .board_ready = false,
    .ui_ready = false,
    .board_error = ESP_ERR_INVALID_STATE,
    .boot_count = 0,
};

/* Snapshot publication is serialized by the UI task.  Keep the bounded
 * retained projection out of that task's stack. */
static d1l_dm_entry_t
    s_dm_conversation_source[D1L_DM_CONVERSATION_SOURCE_CAPACITY];
static bool
    s_dm_conversation_source_unread[D1L_DM_CONVERSATION_SOURCE_CAPACITY];
static d1l_dm_conversation_summary_t
    s_dm_conversation_summaries[D1L_APP_SNAPSHOT_DM_PREVIEW];

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

static uint32_t age_seconds(uint32_t now_ms, uint32_t event_ms)
{
    if (event_ms == 0 || now_ms <= event_ms) {
        return 0;
    }
    return (now_ms - event_ms) / 1000U;
}

static void copy_cstr(char *dest, size_t dest_size, const char *src)
{
    if (!dest || dest_size == 0) {
        return;
    }
    size_t i = 0;
    while (src && src[i] && i + 1U < dest_size) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static void populate_home_messages(d1l_app_snapshot_t *snapshot)
{
    bool public_used[D1L_APP_SNAPSHOT_MESSAGE_PREVIEW] = {0};
    bool dm_used[D1L_APP_SNAPSHOT_DM_PREVIEW] = {0};
    snapshot->home_message_count = 0;

    while (snapshot->home_message_count < D1L_HOME_MESSAGE_PREVIEW) {
        bool found_public = false;
        size_t public_index = 0;
        uint32_t public_uptime = 0;
        for (size_t i = 0; i < snapshot->recent_message_count; ++i) {
            if (!public_used[i] && (!found_public ||
                                   snapshot->recent_messages[i].uptime_ms > public_uptime)) {
                found_public = true;
                public_index = i;
                public_uptime = snapshot->recent_messages[i].uptime_ms;
            }
        }

        bool found_dm = false;
        size_t dm_index = 0;
        uint32_t dm_uptime = 0;
        for (size_t i = 0; i < snapshot->recent_dm_count; ++i) {
            if (!dm_used[i] && (!found_dm || snapshot->recent_dms[i].uptime_ms > dm_uptime)) {
                found_dm = true;
                dm_index = i;
                dm_uptime = snapshot->recent_dms[i].uptime_ms;
            }
        }

        if (!found_public && !found_dm) {
            break;
        }

        d1l_home_message_preview_t *preview =
            &snapshot->home_messages[snapshot->home_message_count++];
        if (found_dm && (!found_public || dm_uptime >= public_uptime)) {
            const d1l_dm_entry_t *entry = &snapshot->recent_dms[dm_index];
            dm_used[dm_index] = true;
            preview->is_dm = true;
            preview->unread = snapshot->recent_dm_unread[dm_index];
            copy_cstr(preview->sender, sizeof(preview->sender),
                      entry->contact_alias[0] ? entry->contact_alias : "Direct");
            copy_cstr(preview->target_fingerprint, sizeof(preview->target_fingerprint),
                      entry->contact_fingerprint);
            copy_cstr(preview->direction, sizeof(preview->direction), entry->direction);
            copy_cstr(preview->status, sizeof(preview->status),
                      preview->unread ? "new DM" :
                      (entry->acked ? "acked" :
                       (entry->direction[0] == 't' ? "sent" : "direct")));
            copy_cstr(preview->text, sizeof(preview->text), entry->text);
            preview->age_sec = age_seconds(snapshot->uptime_ms, entry->uptime_ms);
            preview->rssi_dbm = entry->rssi_dbm;
            preview->snr_tenths = entry->snr_tenths;
            preview->path_hops = entry->path_hops;
        } else {
            const d1l_message_entry_t *entry = &snapshot->recent_messages[public_index];
            public_used[public_index] = true;
            preview->is_dm = false;
            preview->unread = entry->direction[0] == 'r' &&
                              entry->seq > snapshot->last_public_read_seq;
            copy_cstr(preview->sender, sizeof(preview->sender),
                      entry->author[0] ? entry->author : "Public");
            preview->target_fingerprint[0] = '\0';
            copy_cstr(preview->direction, sizeof(preview->direction), entry->direction);
            copy_cstr(preview->status, sizeof(preview->status),
                      preview->unread ? "new" :
                      (entry->direction[0] == 't' ? "sent" : "public"));
            copy_cstr(preview->text, sizeof(preview->text), entry->text);
            preview->age_sec = age_seconds(snapshot->uptime_ms, entry->uptime_ms);
            preview->rssi_dbm = entry->rssi_dbm;
            preview->snr_tenths = entry->snr_tenths;
            preview->path_hops = entry->path_hops;
        }
    }
}

static void populate_dm_conversation_summaries(d1l_app_snapshot_t *snapshot)
{
    if (!snapshot) {
        return;
    }
    memset(s_dm_conversation_source, 0, sizeof(s_dm_conversation_source));
    memset(s_dm_conversation_source_unread, 0,
           sizeof(s_dm_conversation_source_unread));
    memset(s_dm_conversation_summaries, 0,
           sizeof(s_dm_conversation_summaries));
    const size_t source_count = d1l_dm_store_copy_recent(
        s_dm_conversation_source, D1L_DM_CONVERSATION_SOURCE_CAPACITY);
    for (size_t i = 0U; i < source_count; ++i) {
        s_dm_conversation_source_unread[i] =
            d1l_read_state_dm_entry_is_unread(
                &s_dm_conversation_source[i]);
    }
    snapshot->recent_dm_count = d1l_dm_conversation_list_project(
        s_dm_conversation_source, s_dm_conversation_source_unread,
        source_count,
        s_dm_conversation_summaries, D1L_APP_SNAPSHOT_DM_PREVIEW,
        &snapshot->dm_conversation_count);
    for (size_t i = 0U; i < snapshot->recent_dm_count; ++i) {
        d1l_contact_entry_t contact = {0};
        s_dm_conversation_summaries[i].muted =
            d1l_contact_store_find_by_fingerprint(
                s_dm_conversation_summaries[i].latest.contact_fingerprint,
                &contact) && contact.muted;
        snapshot->recent_dms[i] = s_dm_conversation_summaries[i].latest;
        snapshot->recent_dm_unread_count[i] =
            s_dm_conversation_summaries[i].unread_count;
        snapshot->recent_dm_unread[i] =
            snapshot->recent_dm_unread_count[i] > 0U;
        snapshot->recent_dm_muted[i] = s_dm_conversation_summaries[i].muted;
    }
}

static void populate_home_repeaters(d1l_app_snapshot_t *snapshot)
{
    snapshot->home_repeater_count = 0;
    for (size_t i = 0;
         i < snapshot->recent_repeater_count && snapshot->home_repeater_count < D1L_HOME_REPEATER_PREVIEW;
         ++i) {
        const d1l_mesh_repeater_candidate_t *src = &snapshot->recent_repeaters[i];
        d1l_home_repeater_preview_t *dest =
            &snapshot->home_repeaters[snapshot->home_repeater_count++];
        copy_cstr(dest->label, sizeof(dest->label),
                  src->label[0] ? src->label : src->target);
        copy_cstr(dest->route, sizeof(dest->route),
                  src->route[0] ? src->route : "unknown");
        copy_cstr(dest->kind, sizeof(dest->kind),
                  src->kind[0] ? src->kind : "repeater");
        dest->age_sec = age_seconds(snapshot->uptime_ms, src->last_seen_ms);
        dest->rssi_dbm = src->rssi_dbm;
        dest->snr_tenths = src->snr_tenths;
        dest->path_hops = src->path_hops;
    }
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
    d1l_time_service_status_t time_status = {0};
    d1l_time_service_status(&time_status);

    snapshot->board_ready = s_model.board_ready;
    snapshot->ui_ready = s_model.ui_ready;
    snapshot->identity_ready = settings->identity_ready || mesh.identity_ready;
    snapshot->settings_load_status = d1l_settings_load_status();
    snapshot->identity_state = d1l_settings_identity_state(settings);
    snapshot->protocol_tx_ready = time_status.protocol_tx_ready;
    snapshot->protocol_tx_error = time_status.protocol_tx_error;
    snapshot->radio_ready = mesh.radio_ready;
    snapshot->radio_applied = mesh.radio_applied;
    snapshot->radio_apply_pending = mesh.radio_apply_pending;
    snapshot->companion_ready = mesh.companion_framing_ready;
    snapshot->radio_frequency_hz = settings->frequency_hz;
    snapshot->radio_bandwidth_tenths_khz = settings->bandwidth_tenths_khz;
    snapshot->radio_spreading_factor = settings->spreading_factor;
    snapshot->radio_coding_rate = settings->coding_rate;
    snapshot->radio_tx_power_dbm = settings->tx_power_dbm;
    snapshot->radio_rx_boost = settings->rx_boost;
    snapshot->radio_tcxo = d1l_settings_tcxo_name(settings->tcxo_mode);
    snapshot->radio_apply_error = esp_err_to_name(mesh.radio_apply_error);
    snapshot->wifi_enabled = connectivity.wifi_enabled_setting;
    snapshot->ble_companion_enabled = connectivity.ble_companion_enabled_setting;
    snapshot->observer_enabled = connectivity.observer_enabled_setting;
    snapshot->wifi_profile_saved = connectivity.wifi_profile_saved;
    snapshot->wifi_password_saved = connectivity.wifi_password_saved;
    snapshot->wifi_scan_supported = connectivity.wifi_scan_supported;
    snapshot->wifi_stack_active = connectivity.wifi_stack_active;
    snapshot->wifi_connected = connectivity.wifi_connected;
    snapshot->wifi_connecting = connectivity.wifi_connecting;
    snapshot->onboarding_complete = settings->onboarding_complete;
    snapshot->wifi_build_enabled = connectivity.wifi_build_enabled;
    snapshot->ble_build_enabled = connectivity.ble_build_enabled;
    snapshot->ble_transport_supported = D1L_BLE_COMPANION_TRANSPORT_SUPPORTED;
    snapshot->wifi_state = connectivity.wifi_state;
    snapshot->wifi_last_error = connectivity.wifi_last_error;
    snapshot->wifi_rssi_dbm = connectivity.wifi_rssi_dbm;
    snapshot->wifi_channel = connectivity.wifi_channel;
    snapshot->ble_state = connectivity.ble_state;
    snapshot->coexistence_policy = connectivity.coexistence_policy;
    snapshot->storage_direct_supported = storage.direct_supported;
    snapshot->storage_rp2040_bridge_required = storage.rp2040_bridge_required;
    snapshot->storage_rp2040_bridge_ready = storage.rp2040_bridge_ready;
    snapshot->storage_rp2040_sd_protocol_supported = storage.rp2040_sd_protocol_supported;
    snapshot->storage_sd_present = storage.sd_present;
    snapshot->storage_sd_mounted = storage.sd_mounted;
    snapshot->storage_sd_data_root_ready = storage.sd_data_root_ready;
    snapshot->storage_sd_needs_fat32 = storage.sd_needs_fat32;
    snapshot->storage_setup_required = storage.setup_required;
    snapshot->storage_setup_supported = storage.setup_supported;
    snapshot->storage_data_enabled = storage.data_enabled;
    snapshot->storage_response_truncated = storage.response_truncated;
    snapshot->storage_retained_sd_degraded = storage.retained_sd_degraded;
    snapshot->storage_retained_backup_degraded = storage.retained_backup_degraded;
    snapshot->map_page_supported = true;
    snapshot->map_tile_cache_ready = d1l_map_tile_store_sd_ready(&storage);
    snapshot->map_tile_render_supported = D1L_MAP_TILE_RENDER_SUPPORTED;
    snapshot->map_tile_sideload_supported = false;
    snapshot->map_location_set = settings->map_location_set;
    snapshot->map_tile_download_supported = connectivity.wifi_build_enabled &&
                                            connectivity.wifi_connected &&
                                            snapshot->map_tile_cache_ready &&
                                            snapshot->map_location_set &&
                                            snapshot->map_tile_render_supported;
    snapshot->map_lat_e7 = settings->map_lat_e7;
    snapshot->map_lon_e7 = settings->map_lon_e7;
    snapshot->map_tile_zoom = settings->map_tile_zoom;
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
    snapshot->map_tile_cache_policy = D1L_MAP_TILE_CACHE_POLICY;
    snapshot->map_tile_cache_path_template = D1L_MAP_TILE_CACHE_PATH_TEMPLATE;
    snapshot->map_tile_download_state = !snapshot->map_tile_cache_ready ? "sd_cache_required" :
                                        !connectivity.wifi_build_enabled ? "wifi_unavailable" :
                                        !connectivity.wifi_connected ? "wifi_required" :
                                        !settings->map_location_set ? "location_required" :
                                        !snapshot->map_tile_render_supported ? "tile_render_pending" :
                                        "ready";
    snapshot->map_tile_download_requires = D1L_MAP_TILE_DOWNLOAD_REQUIRES;
    snapshot->map_tile_provider_policy = D1L_MAP_TILE_PROVIDER_POLICY;
    snapshot->map_tile_provider_attribution = D1L_MAP_TILE_PROVIDER_ATTRIBUTION;
    snapshot->storage_setup_action = storage.setup_action;
    snapshot->storage_note = storage.note;
    snapshot->time_available = false;
    snprintf(snapshot->time_label, sizeof(snapshot->time_label), "--:--");
    snprintf(snapshot->node_name, sizeof(snapshot->node_name), "%s", settings->node_name);
    copy_cstr(snapshot->wifi_ssid, sizeof(snapshot->wifi_ssid),
              connectivity.wifi_ssid ? connectivity.wifi_ssid : "");
    copy_cstr(snapshot->wifi_ip, sizeof(snapshot->wifi_ip),
              connectivity.wifi_ip ? connectivity.wifi_ip : "");
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
    snapshot->dm_content_revision = dms.content_revision;
    snapshot->dm_count = dms.count;
    snapshot->dm_delivery_state = mesh.dm_delivery_state;
    const d1l_dm_delivery_state_t delivery_state =
        (d1l_dm_delivery_state_t)mesh.dm_delivery_state;
    snapshot->dm_delivery_active =
        mesh.dm_delivery_session_id != 0U &&
        d1l_dm_delivery_state_valid(delivery_state) &&
        !d1l_dm_delivery_state_terminal(delivery_state);
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
    d1l_channel_store_stats_t channel_stats = {0};
    (void)d1l_app_model_copy_channels(
        snapshot->channels, D1L_APP_SNAPSHOT_CHANNEL_PREVIEW,
        &snapshot->channel_count, &snapshot->active_channel_id,
        &channel_stats);
    snapshot->channel_store_revision = channel_stats.revision;
    snapshot->channel_store_load_status = channel_stats.load_status;
    snapshot->channel_store_loaded = channel_stats.loaded;
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
    const d1l_node_query_t node_query = {
        .filter = D1L_NODE_FILTER_ALL,
        .sort = D1L_NODE_SORT_LAST_HEARD,
        .text = NULL,
        .keyed_only = false,
        .reachable_only = false,
    };
    snapshot->recent_node_count =
        d1l_app_model_query_nodes(&node_query, snapshot->recent_nodes,
                                  D1L_APP_SNAPSHOT_NODE_PREVIEW);
    snapshot->recent_message_count =
        d1l_message_store_copy_recent(snapshot->recent_messages, D1L_APP_SNAPSHOT_MESSAGE_PREVIEW);
    populate_dm_conversation_summaries(snapshot);
    snapshot->recent_packet_count =
        d1l_packet_log_copy_recent(snapshot->recent_packets, D1L_APP_SNAPSHOT_PACKET_PREVIEW);
    populate_home_messages(snapshot);
    populate_home_repeaters(snapshot);
}

esp_err_t d1l_app_model_send_public_text(const char *text)
{
    return d1l_meshcore_service_send_public(text);
}

esp_err_t d1l_app_model_copy_channels(d1l_channel_info_t *out_channels,
                                      size_t max_channels,
                                      size_t *out_count,
                                      uint64_t *out_active_channel_id,
                                      d1l_channel_store_stats_t *out_stats)
{
    return d1l_channel_store_snapshot(
        out_channels, max_channels, out_count, out_active_channel_id,
        out_stats);
}

esp_err_t d1l_app_model_select_channel(uint64_t channel_id,
                                       d1l_channel_info_t *out_channel)
{
    if (channel_id == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (out_channel) {
        memset(out_channel, 0, sizeof(*out_channel));
    }

    d1l_channel_mutation_result_t result = D1L_CHANNEL_MUTATION_NONE;
    d1l_channel_info_t selected = {0};
    const esp_err_t ret = d1l_channel_store_select(
        channel_id, &result, &selected);
    if (ret != ESP_OK) {
        return ret;
    }
    if (result != D1L_CHANNEL_MUTATION_UPDATED &&
        result != D1L_CHANNEL_MUTATION_EXISTS) {
        return ESP_ERR_INVALID_STATE;
    }
    if (out_channel) {
        *out_channel = selected;
    }
    return ESP_OK;
}

size_t d1l_app_model_query_public_messages_page(d1l_message_entry_t *out_entries,
                                                size_t max_entries,
                                                size_t skip_newest,
                                                const char *query,
                                                size_t *out_total_matches)
{
    return d1l_message_store_query_page(out_entries, max_entries, skip_newest,
                                        query, out_total_matches);
}

size_t d1l_app_model_query_public_messages(d1l_message_entry_t *out_entries,
                                           size_t max_entries, const char *query)
{
    return d1l_app_model_query_public_messages_page(out_entries, max_entries, 0,
                                                    query, NULL);
}

esp_err_t d1l_app_model_send_dm_text(const char *fingerprint, const char *text)
{
    return d1l_meshcore_service_send_dm(fingerprint, text);
}

esp_err_t d1l_app_model_request_path_discovery_probe(const char *fingerprint,
                                                     char *out_token,
                                                     size_t out_token_size)
{
    return d1l_meshcore_service_request_path_discovery_probe(
        fingerprint, out_token, out_token_size);
}

size_t d1l_app_model_copy_dm_thread_page(const char *fingerprint,
                                         d1l_dm_entry_t *out_entries,
                                         bool *out_unread, size_t max_entries,
                                         size_t skip_newest,
                                         size_t *out_total_matches)
{
    const size_t copied = d1l_dm_store_copy_thread_page(fingerprint, out_entries,
                                                       max_entries, skip_newest,
                                                       out_total_matches);
    if (out_unread) {
        for (size_t i = 0; i < copied; ++i) {
            out_unread[i] = d1l_read_state_dm_entry_is_unread(&out_entries[i]);
        }
    }
    return copied;
}

size_t d1l_app_model_copy_dm_thread(const char *fingerprint, d1l_dm_entry_t *out_entries,
                                    bool *out_unread, size_t max_entries)
{
    return d1l_app_model_copy_dm_thread_page(fingerprint, out_entries, out_unread,
                                             max_entries, 0, NULL);
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

size_t d1l_app_model_query_nodes(const d1l_node_query_t *query, d1l_node_view_t *out_entries,
                                 size_t max_entries)
{
    return d1l_node_store_query(query, out_entries, max_entries);
}

esp_err_t d1l_app_model_mark_public_read(void)
{
    return d1l_read_state_mark_public_read();
}

esp_err_t d1l_app_model_mark_dm_thread_read(const char *fingerprint)
{
    return d1l_read_state_mark_dm_thread_read(fingerprint);
}

esp_err_t d1l_app_model_request_advert(bool flood)
{
    return d1l_meshcore_service_request_advert(flood);
}

esp_err_t d1l_app_model_set_map_location(int32_t lat_e7, int32_t lon_e7)
{
    if (lat_e7 < D1L_MAP_LOCATION_LAT_E7_MIN ||
        lat_e7 > D1L_MAP_LOCATION_LAT_E7_MAX ||
        lon_e7 < D1L_MAP_LOCATION_LON_E7_MIN ||
        lon_e7 > D1L_MAP_LOCATION_LON_E7_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    d1l_settings_t settings = *d1l_settings_current();
    settings.map_location_set = true;
    settings.map_lat_e7 = lat_e7;
    settings.map_lon_e7 = lon_e7;
    return d1l_settings_save(&settings);
}

esp_err_t d1l_app_model_clear_map_location(void)
{
    d1l_settings_t settings = *d1l_settings_current();
    settings.map_location_set = false;
    settings.map_lat_e7 = 0;
    settings.map_lon_e7 = 0;
    return d1l_settings_save(&settings);
}

esp_err_t d1l_app_model_set_wifi_enabled(bool enabled)
{
    return d1l_connectivity_set_wifi_enabled(enabled);
}

esp_err_t d1l_app_model_wifi_scan(d1l_wifi_scan_result_t *out_result)
{
    return d1l_connectivity_wifi_scan(out_result);
}

esp_err_t d1l_app_model_wifi_connect(void)
{
    return d1l_connectivity_wifi_connect();
}

esp_err_t d1l_app_model_wifi_disconnect(void)
{
    return d1l_connectivity_wifi_disconnect();
}

esp_err_t d1l_app_model_save_wifi_profile(const char *ssid, const char *password)
{
    return d1l_connectivity_save_wifi_profile(ssid, password);
}

esp_err_t d1l_app_model_clear_wifi_profile(void)
{
    return d1l_connectivity_clear_wifi_profile();
}

esp_err_t d1l_app_model_set_ble_enabled(bool enabled)
{
    return d1l_connectivity_set_ble_enabled(enabled);
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
