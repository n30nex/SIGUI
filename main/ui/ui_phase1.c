#include "ui_phase1.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "rom/cache.h"

#include "app/app_model.h"
#include "app/settings_model.h"
#include "bsp_lcd.h"
#include "d1l_config.h"
#include "diagnostics/health_monitor.h"
#include "hal/indicator_board.h"
#include "ui_chrome.h"
#include "ui_home.h"
#include "ui_keyboard.h"
#include "ui_map.h"
#include "ui_modal.h"
#include "ui_navigation.h"
#include "ui_screen.h"
#include "ui_settings.h"
#include "sdkconfig.h"

static const char *TAG = "d1l_ui";
#define D1L_PUBLIC_HISTORY_UI_INITIAL_ROWS 5U
#define D1L_PUBLIC_HISTORY_UI_LOAD_OLDER_STEP 5U
#define D1L_DM_THREAD_UI_INITIAL_ROWS 5U
#define D1L_DM_THREAD_UI_LOAD_OLDER_STEP 5U

static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t s_disp_drv;
static lv_color_t *s_buf1;
static lv_color_t *s_buf2;
static uint8_t *s_capture_shadow;
static uint8_t *s_capture_snapshot;
static SemaphoreHandle_t s_capture_lock;
static bool s_capture_shadow_ready;
static bool s_capture_active;
static bool s_bsp_flush_ready_registered;
static uint32_t s_capture_frame_seq;
static uint32_t s_capture_flush_count;
static uint32_t s_capture_crc32;
static TaskHandle_t s_ui_task_handle;
static TaskHandle_t s_touch_task_handle;
static portMUX_TYPE s_touch_lock = portMUX_INITIALIZER_UNLOCKED;
static d1l_board_touch_state_t s_touch_state;
static bool s_touch_state_ready = false;
static portMUX_TYPE s_content_refresh_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_map_visibility_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_started = false;
static lv_obj_t *s_screen;
static lv_obj_t *s_top_bar;
static lv_obj_t *s_content;
static lv_obj_t *s_title_label;
static lv_obj_t *s_status_label;
static lv_obj_t *s_identity_label;
static lv_obj_t *s_lock_button;
static lv_obj_t *s_toast;
static lv_obj_t *s_sheet;
static lv_obj_t *s_dock;
static lv_obj_t *s_compose_sheet;
static lv_obj_t *s_compose_title;
static lv_obj_t *s_compose_textarea;
static lv_obj_t *s_compose_counter;
static lv_obj_t *s_compose_keyboard;
static lv_obj_t *s_public_history_sheet;
static lv_obj_t *s_public_search_sheet;
static lv_obj_t *s_public_search_textarea;
static lv_obj_t *s_public_search_keyboard;
static lv_obj_t *s_message_detail_sheet;
static lv_obj_t *s_dm_thread_sheet;
static lv_obj_t *s_radio_settings_sheet;
static lv_obj_t *s_storage_sheet;
static lv_obj_t *s_wifi_sheet;
static lv_obj_t *s_wifi_ssid_textarea;
static lv_obj_t *s_wifi_password_textarea;
static lv_obj_t *s_wifi_keyboard;
static d1l_wifi_scan_result_t s_wifi_scan_result;
static bool s_wifi_scan_loaded;
static lv_obj_t *s_ble_sheet;
static lv_obj_t *s_display_sheet;
static lv_obj_t *s_diagnostics_sheet;
static lv_obj_t *s_map_location_sheet;
static lv_obj_t *s_map_lat_textarea;
static lv_obj_t *s_map_lon_textarea;
static lv_obj_t *s_map_location_keyboard;
static lv_obj_t *s_map_options_sheet;
static lv_obj_t *s_contact_detail_sheet;
static lv_obj_t *s_contact_options_sheet;
static lv_obj_t *s_contact_forget_sheet;
static lv_obj_t *s_contact_edit_sheet;
static lv_obj_t *s_contact_edit_title;
static lv_obj_t *s_contact_edit_textarea;
static lv_obj_t *s_contact_edit_keyboard;
static lv_obj_t *s_contact_export_sheet;
static lv_obj_t *s_node_detail_sheet;
static lv_obj_t *s_route_detail_sheet;
static lv_obj_t *s_route_trace_sheet;
static lv_obj_t *s_packet_detail_sheet;
static lv_obj_t *s_packet_search_sheet;
static lv_obj_t *s_packet_search_textarea;
static lv_obj_t *s_packet_search_keyboard;
static lv_obj_t *s_mesh_roles_sheet;
static lv_obj_t *s_lock_overlay;
static lv_obj_t *s_onboarding_sheet;
static lv_obj_t *s_onboarding_name_textarea;
static lv_obj_t *s_onboarding_keyboard;
static bool s_lock_visible;
static bool s_onboarding_visible;
static bool s_onboarding_probe_suppressed;
static uint32_t s_toast_until;
static d1l_app_snapshot_t s_snapshot;
static bool s_compose_dm;
static bool s_messages_show_dms;
static d1l_contact_entry_t s_compose_contact;
static d1l_app_radio_profile_edit_t s_radio_edit;
static int32_t s_map_location_lat_e7;
static int32_t s_map_location_lon_e7;
static d1l_ui_map_controls_t s_map_location_controls;
static bool s_map_location_returns_to_options;
static d1l_contact_entry_t s_contact_detail_contact;
static d1l_contact_entry_t s_contact_export_contact;
static d1l_node_view_t s_node_detail_node;
static bool s_node_detail_returns_to_map;
static d1l_route_entry_t s_route_detail_route;
static d1l_contact_entry_t s_route_trace_contact;
static d1l_message_entry_t s_message_detail_message;
static d1l_packet_log_entry_t s_packet_detail_packet;
static d1l_packet_log_entry_t s_packet_filtered_packets[D1L_PACKET_LOG_CAPACITY];
static size_t s_packet_filtered_count;
static d1l_node_view_t s_node_rows[D1L_NODE_STORE_CAPACITY];
static d1l_route_entry_t s_route_trace_entries[D1L_ROUTE_STORE_CAPACITY];
static d1l_message_entry_t s_public_history_entries[D1L_MESSAGE_STORE_CAPACITY];
static d1l_dm_entry_t s_dm_thread_entries[D1L_DM_STORE_CAPACITY];
static bool s_dm_thread_unread[D1L_DM_STORE_CAPACITY];
static char s_public_search_text[D1L_MESSAGE_TEXT_LEN];
static char s_dm_thread_fingerprint[D1L_NODE_FINGERPRINT_LEN];
static char s_dm_thread_alias[D1L_CONTACT_ALIAS_LEN];
static char s_contact_export_uri[D1L_CONTACT_EXPORT_URI_LEN];
static char s_packet_search_text[D1L_PACKET_LOG_QUERY_TEXT_LEN];
static const char s_contact_action_favorite[] = "favorite";
static const char s_contact_action_mute[] = "mute";
static const uint32_t D1L_UI_TIMER_MIN_SLEEP_MS = 20U;
static const uint32_t D1L_UI_TIMER_MAX_SLEEP_MS = 50U;
static const uint32_t D1L_UI_MAP_VIEWPORT_REFRESH_MS = 500U;
/* A full retained Packet view can legitimately take longer than 1.5 s to rebuild
 * before a nested page opens. Keep the serial proof bounded without turning a
 * populated device into a false timeout. */
static const uint32_t D1L_UI_SCROLL_PROBE_TIMEOUT_MS = 5000U;
static const uint32_t D1L_UI_COMPOSE_PROBE_TIMEOUT_MS = 1500U;
static const size_t D1L_PACKET_UI_INITIAL_ROWS = 100U;
static const size_t D1L_PACKET_UI_LOAD_OLDER_STEP = 100U;
static size_t s_public_history_limit = D1L_PUBLIC_HISTORY_UI_INITIAL_ROWS;
static size_t s_dm_thread_limit = D1L_DM_THREAD_UI_INITIAL_ROWS;
static size_t s_packet_row_limit = 100U;
static size_t s_packet_skip_newest;
static size_t s_packet_total_matches;
static bool s_packet_sd_history_page;
typedef struct {
    uint32_t next_id;
    uint32_t pending_id;
    uint32_t inflight_id;
    uint32_t completed_id;
    uint32_t abandoned_id;
    uint32_t signaled_id;
    bool admission_reserved;
} d1l_ui_probe_gate_t;

static SemaphoreHandle_t s_scroll_probe_done_sem;
static portMUX_TYPE s_scroll_probe_lock = portMUX_INITIALIZER_UNLOCKED;
static d1l_ui_probe_gate_t s_scroll_probe_gate;
static char s_scroll_probe_surface[24];
static d1l_ui_scroll_probe_result_t s_scroll_probe_result;
static SemaphoreHandle_t s_compose_probe_done_sem;
static portMUX_TYPE s_compose_probe_lock = portMUX_INITIALIZER_UNLOCKED;
static d1l_ui_probe_gate_t s_compose_probe_gate;
static char s_compose_probe_target[16];
static d1l_ui_compose_probe_result_t s_compose_probe_result;

#if CONFIG_FREERTOS_UNICORE
#define D1L_UI_TASK_CORE 0
#else
#define D1L_UI_TASK_CORE 1
#endif

#define D1L_TOUCH_TASK_STACK_BYTES 4096U
#define D1L_UI_TASK_STACK_BYTES 6144U

static void render_active_tab(void);
static void render_contact_detail_sheet(void);
static void show_contact_detail_sheet(void);
static void render_contact_options_sheet(void);
static void show_contact_options_sheet(void);
static void render_contact_forget_sheet(void);
static void render_node_detail_sheet(void);
static void open_dm_compose_event_cb(lv_event_t *event);
static void open_public_history_event_cb(lv_event_t *event);
static void open_public_search_event_cb(lv_event_t *event);
static void open_messages_public_event_cb(lv_event_t *event);
static void open_messages_dm_event_cb(lv_event_t *event);
static void open_home_dm_preview_event_cb(lv_event_t *event);
static void open_dm_thread_event_cb(lv_event_t *event);
static void open_contact_detail_event_cb(lv_event_t *event);
static void open_contact_options_event_cb(lv_event_t *event);
static void open_contact_forget_event_cb(lv_event_t *event);
static void open_node_detail_event_cb(lv_event_t *event);
static void open_map_node_detail(const char *fingerprint);
static void node_detail_dm_event_cb(lv_event_t *event);
static void open_node_dm_event_cb(lv_event_t *event);
static void open_contact_edit_event_cb(lv_event_t *event);
static void open_route_trace_event_cb(lv_event_t *event);
static void open_route_detail_event_cb(lv_event_t *event);
static void open_message_detail_event_cb(lv_event_t *event);
static void open_packet_detail_event_cb(lv_event_t *event);
static void open_packet_search_event_cb(lv_event_t *event);
static void open_mesh_roles_event_cb(lv_event_t *event);
static void render_mesh_roles_sheet(void);
static void packet_pause_event_cb(lv_event_t *event);
static void packet_load_older_event_cb(lv_event_t *event);
static void packet_load_newer_event_cb(lv_event_t *event);
static void public_history_load_older_event_cb(lv_event_t *event);
static void dm_thread_load_older_event_cb(lv_event_t *event);
static void packet_detail_mode_event_cb(lv_event_t *event);
static void open_radio_settings_event_cb(lv_event_t *event);
static void open_storage_sheet_event_cb(lv_event_t *event);
static void open_wifi_sheet_event_cb(lv_event_t *event);
static void open_ble_sheet_event_cb(lv_event_t *event);
static void open_display_sheet_event_cb(lv_event_t *event);
static void open_diagnostics_sheet_event_cb(lv_event_t *event);
static void open_sheet_event_cb(lv_event_t *event);

typedef d1l_ui_screen_t d1l_ui_tab_t;

#define D1L_UI_TAB_HOME D1L_UI_SCREEN_HOME
#define D1L_UI_TAB_MESSAGES D1L_UI_SCREEN_MESSAGES
#define D1L_UI_TAB_NODES D1L_UI_SCREEN_NODES
#define D1L_UI_TAB_MAP D1L_UI_SCREEN_MAP
#define D1L_UI_TAB_PACKETS D1L_UI_SCREEN_PACKETS
#define D1L_UI_TAB_SETTINGS D1L_UI_SCREEN_SETTINGS

typedef struct {
    uint32_t rx_packets;
    uint32_t rx_adverts;
    uint32_t tx_packets;
    uint32_t message_total_written;
    uint32_t dm_total_written;
    uint32_t node_total_written;
    uint32_t contact_total_written;
    uint32_t route_total_written;
    uint32_t packet_total_written;
    uint32_t public_unread_count;
    uint32_t dm_unread_count;
    uint32_t muted_dm_unread_count;
    uint32_t last_public_read_seq;
    uint32_t last_dm_read_seq;
    size_t message_count;
    size_t dm_count;
    size_t node_count;
    size_t contact_count;
    size_t route_count;
    size_t packet_count;
    bool storage_sd_present;
    bool storage_sd_mounted;
    bool storage_sd_data_root_ready;
    bool storage_sd_needs_fat32;
    bool storage_setup_required;
    bool storage_data_enabled;
    bool storage_retained_sd_degraded;
    bool storage_retained_backup_degraded;
    bool map_tile_cache_ready;
    bool wifi_connected;
    bool wifi_connecting;
    const char *storage_sd_state;
    const char *storage_setup_action;
} d1l_ui_content_generation_t;

typedef struct {
    bool location_set;
    int32_t lat_e7;
    int32_t lon_e7;
    uint8_t zoom;
} d1l_ui_map_render_input_t;

typedef enum {
    D1L_PACKET_FILTER_ALL = 0,
    D1L_PACKET_FILTER_RX,
    D1L_PACKET_FILTER_TX,
    D1L_PACKET_FILTER_TEXT,
} d1l_packet_filter_mode_t;

typedef enum {
    D1L_RADIO_EDIT_FREQ_DOWN = 0,
    D1L_RADIO_EDIT_FREQ_UP,
    D1L_RADIO_EDIT_BW,
    D1L_RADIO_EDIT_SF_DOWN,
    D1L_RADIO_EDIT_SF_UP,
    D1L_RADIO_EDIT_CR,
    D1L_RADIO_EDIT_TX_DOWN,
    D1L_RADIO_EDIT_TX_UP,
    D1L_RADIO_EDIT_RX_BOOST,
} d1l_radio_edit_action_t;

typedef enum {
    D1L_MESH_ROLES_PAGE_ROOT = 0,
    D1L_MESH_ROLES_PAGE_ROOM_SERVERS,
    D1L_MESH_ROLES_PAGE_REPEATER_CANDIDATES,
} d1l_mesh_roles_page_t;

typedef enum {
    D1L_STORAGE_PAGE_ROOT = 0,
    D1L_STORAGE_PAGE_CARD_STATUS,
    D1L_STORAGE_PAGE_DATA_LOCATIONS,
} d1l_storage_page_t;

static bool s_content_refresh_pending = false;
static bool s_map_interactive_touch_authorized;
static d1l_ui_content_generation_t s_rendered_content_generation;
static bool s_rendered_content_generation_valid = false;
static d1l_ui_map_render_input_t s_rendered_map_input;
static bool s_rendered_map_input_valid = false;
static d1l_packet_filter_mode_t s_packet_filter_mode = D1L_PACKET_FILTER_ALL;
static bool s_packets_paused;
static bool s_packet_detail_advanced;
static bool s_message_detail_advanced;
static d1l_mesh_roles_page_t s_mesh_roles_page = D1L_MESH_ROLES_PAGE_ROOT;
static d1l_storage_page_t s_storage_page = D1L_STORAGE_PAGE_ROOT;
static d1l_ui_map_options_page_t s_map_options_page = D1L_UI_MAP_OPTIONS_ROOT;

static d1l_ui_content_generation_t content_generation_from_snapshot(
    const d1l_app_snapshot_t *snapshot)
{
    return (d1l_ui_content_generation_t) {
        .rx_packets = snapshot->rx_packets,
        .rx_adverts = snapshot->rx_adverts,
        .tx_packets = snapshot->tx_packets,
        .message_total_written = snapshot->message_total_written,
        .dm_total_written = snapshot->dm_total_written,
        .node_total_written = snapshot->node_total_written,
        .contact_total_written = snapshot->contact_total_written,
        .route_total_written = snapshot->route_total_written,
        .packet_total_written = snapshot->packet_total_written,
        .public_unread_count = snapshot->public_unread_count,
        .dm_unread_count = snapshot->dm_unread_count,
        .muted_dm_unread_count = snapshot->muted_dm_unread_count,
        .last_public_read_seq = snapshot->last_public_read_seq,
        .last_dm_read_seq = snapshot->last_dm_read_seq,
        .message_count = snapshot->message_count,
        .dm_count = snapshot->dm_count,
        .node_count = snapshot->node_count,
        .contact_count = snapshot->contact_count,
        .route_count = snapshot->route_count,
        .packet_count = snapshot->packet_count,
        .storage_sd_present = snapshot->storage_sd_present,
        .storage_sd_mounted = snapshot->storage_sd_mounted,
        .storage_sd_data_root_ready = snapshot->storage_sd_data_root_ready,
        .storage_sd_needs_fat32 = snapshot->storage_sd_needs_fat32,
        .storage_setup_required = snapshot->storage_setup_required,
        .storage_data_enabled = snapshot->storage_data_enabled,
        .storage_retained_sd_degraded = snapshot->storage_retained_sd_degraded,
        .storage_retained_backup_degraded = snapshot->storage_retained_backup_degraded,
        .map_tile_cache_ready = snapshot->map_tile_cache_ready,
        .wifi_connected = snapshot->wifi_connected,
        .wifi_connecting = snapshot->wifi_connecting,
        .storage_sd_state = snapshot->storage_sd_state,
        .storage_setup_action = snapshot->storage_setup_action,
    };
}

static bool content_generation_text_equal(const char *left, const char *right)
{
    if (left == right) {
        return true;
    }
    return left && right && strcmp(left, right) == 0;
}

static bool content_generation_equal(const d1l_ui_content_generation_t *left,
                                     const d1l_ui_content_generation_t *right)
{
    return left->rx_packets == right->rx_packets &&
        left->rx_adverts == right->rx_adverts &&
        left->tx_packets == right->tx_packets &&
        left->message_total_written == right->message_total_written &&
        left->dm_total_written == right->dm_total_written &&
        left->node_total_written == right->node_total_written &&
        left->contact_total_written == right->contact_total_written &&
        left->route_total_written == right->route_total_written &&
        left->packet_total_written == right->packet_total_written &&
        left->public_unread_count == right->public_unread_count &&
        left->dm_unread_count == right->dm_unread_count &&
        left->muted_dm_unread_count == right->muted_dm_unread_count &&
        left->last_public_read_seq == right->last_public_read_seq &&
        left->last_dm_read_seq == right->last_dm_read_seq &&
        left->message_count == right->message_count &&
        left->dm_count == right->dm_count &&
        left->node_count == right->node_count &&
        left->contact_count == right->contact_count &&
        left->route_count == right->route_count &&
        left->packet_count == right->packet_count &&
        left->storage_sd_present == right->storage_sd_present &&
        left->storage_sd_mounted == right->storage_sd_mounted &&
        left->storage_sd_data_root_ready == right->storage_sd_data_root_ready &&
        left->storage_sd_needs_fat32 == right->storage_sd_needs_fat32 &&
        left->storage_setup_required == right->storage_setup_required &&
        left->storage_data_enabled == right->storage_data_enabled &&
        left->storage_retained_sd_degraded == right->storage_retained_sd_degraded &&
        left->storage_retained_backup_degraded == right->storage_retained_backup_degraded &&
        left->map_tile_cache_ready == right->map_tile_cache_ready &&
        left->wifi_connected == right->wifi_connected &&
        left->wifi_connecting == right->wifi_connecting &&
        content_generation_text_equal(left->storage_sd_state,
                                      right->storage_sd_state) &&
        content_generation_text_equal(left->storage_setup_action,
                                      right->storage_setup_action);
}

static void remember_rendered_content_generation(const d1l_app_snapshot_t *snapshot)
{
    s_rendered_content_generation = content_generation_from_snapshot(snapshot);
    s_rendered_content_generation_valid = true;
}

static bool content_generation_changed_from_rendered(const d1l_app_snapshot_t *snapshot)
{
    d1l_ui_content_generation_t current = content_generation_from_snapshot(snapshot);
    if (!s_rendered_content_generation_valid) {
        s_rendered_content_generation = current;
        s_rendered_content_generation_valid = true;
        return false;
    }
    return !content_generation_equal(&current, &s_rendered_content_generation);
}

static d1l_ui_map_render_input_t map_render_input_from_snapshot(
    const d1l_app_snapshot_t *snapshot)
{
    return (d1l_ui_map_render_input_t) {
        .location_set = snapshot->map_location_set,
        .lat_e7 = snapshot->map_lat_e7,
        .lon_e7 = snapshot->map_lon_e7,
        .zoom = snapshot->map_tile_zoom,
    };
}

static void remember_rendered_map_input(const d1l_app_snapshot_t *snapshot)
{
    s_rendered_map_input = map_render_input_from_snapshot(snapshot);
    s_rendered_map_input_valid = true;
}

static bool map_render_input_changed_from_rendered(const d1l_app_snapshot_t *snapshot)
{
    const d1l_ui_map_render_input_t current = map_render_input_from_snapshot(snapshot);
    return !s_rendered_map_input_valid ||
           current.location_set != s_rendered_map_input.location_set ||
           current.lat_e7 != s_rendered_map_input.lat_e7 ||
           current.lon_e7 != s_rendered_map_input.lon_e7 ||
           current.zoom != s_rendered_map_input.zoom;
}

static void request_tab_switch(d1l_ui_tab_t tab)
{
    d1l_ui_navigation_request(tab);
}

static bool begin_pending_tab_switch(d1l_ui_tab_t *out_tab)
{
    return d1l_ui_navigation_begin_pending(out_tab);
}

static void finish_pending_tab_switch(d1l_ui_tab_t rendered_tab)
{
    d1l_ui_navigation_finish(rendered_tab);
}

static void request_content_refresh(void)
{
    portENTER_CRITICAL(&s_content_refresh_lock);
    s_content_refresh_pending = true;
    portEXIT_CRITICAL(&s_content_refresh_lock);
}

static bool begin_pending_content_refresh(void)
{
    bool pending;

    portENTER_CRITICAL(&s_content_refresh_lock);
    pending = s_content_refresh_pending;
    s_content_refresh_pending = false;
    portEXIT_CRITICAL(&s_content_refresh_lock);
    return pending;
}

static bool content_refresh_pending(void)
{
    bool pending;

    portENTER_CRITICAL(&s_content_refresh_lock);
    pending = s_content_refresh_pending;
    portEXIT_CRITICAL(&s_content_refresh_lock);
    return pending;
}

static void set_map_interactive_touch_authorized(bool authorized)
{
    portENTER_CRITICAL(&s_map_visibility_lock);
    s_map_interactive_touch_authorized = authorized;
    portEXIT_CRITICAL(&s_map_visibility_lock);
}

static bool map_interactive_touch_authorized(void)
{
    bool authorized;
    portENTER_CRITICAL(&s_map_visibility_lock);
    authorized = s_map_interactive_touch_authorized;
    portEXIT_CRITICAL(&s_map_visibility_lock);
    return authorized;
}

/* Probe-gate helpers are called only while the matching probe lock is held. */
static bool probe_gate_busy(const d1l_ui_probe_gate_t *gate)
{
    return gate && (gate->admission_reserved || gate->pending_id != 0U ||
                    gate->inflight_id != 0U);
}

static bool probe_gate_reserve_admission(d1l_ui_probe_gate_t *gate)
{
    if (!gate || probe_gate_busy(gate)) {
        return false;
    }
    gate->admission_reserved = true;
    return true;
}

static uint32_t probe_gate_commit_admission(d1l_ui_probe_gate_t *gate)
{
    if (!gate || !gate->admission_reserved || gate->pending_id != 0U ||
        gate->inflight_id != 0U) {
        return 0U;
    }
    gate->next_id++;
    if (gate->next_id == 0U) {
        gate->next_id++;
    }
    gate->pending_id = gate->next_id;
    gate->completed_id = 0U;
    gate->abandoned_id = 0U;
    gate->signaled_id = 0U;
    gate->admission_reserved = false;
    return gate->pending_id;
}

static void probe_gate_abort_admission(d1l_ui_probe_gate_t *gate)
{
    if (gate) {
        gate->admission_reserved = false;
    }
}

static uint32_t probe_gate_begin(d1l_ui_probe_gate_t *gate)
{
    if (!gate || gate->pending_id == 0U || gate->inflight_id != 0U) {
        return 0U;
    }
    const uint32_t request_id = gate->pending_id;
    gate->pending_id = 0U;
    gate->inflight_id = request_id;
    return request_id;
}

static void probe_gate_timeout(d1l_ui_probe_gate_t *gate, uint32_t request_id)
{
    if (!gate || request_id == 0U) {
        return;
    }
    if (gate->pending_id == request_id) {
        gate->pending_id = 0U;
    } else if (gate->inflight_id == request_id) {
        if (gate->signaled_id == request_id) {
            gate->inflight_id = 0U;
            gate->completed_id = 0U;
            gate->abandoned_id = 0U;
            gate->signaled_id = 0U;
        } else {
            gate->abandoned_id = request_id;
        }
    }
}

static bool probe_gate_publish(d1l_ui_probe_gate_t *gate, uint32_t request_id)
{
    if (!gate || request_id == 0U || gate->inflight_id != request_id) {
        return false;
    }
    gate->completed_id = request_id;
    return true;
}

static void probe_gate_finish_signal(d1l_ui_probe_gate_t *gate, uint32_t request_id)
{
    if (gate && gate->inflight_id == request_id) {
        gate->signaled_id = request_id;
        if (gate->abandoned_id == request_id) {
            gate->inflight_id = 0U;
            gate->completed_id = 0U;
            gate->abandoned_id = 0U;
            gate->signaled_id = 0U;
        }
    }
}

static bool probe_gate_acknowledge(d1l_ui_probe_gate_t *gate, uint32_t request_id)
{
    if (!gate || gate->inflight_id != request_id || gate->completed_id != request_id) {
        return false;
    }
    gate->inflight_id = 0U;
    gate->completed_id = 0U;
    gate->abandoned_id = 0U;
    gate->signaled_id = 0U;
    return true;
}

static bool scroll_probe_pending(void)
{
    bool pending;

    portENTER_CRITICAL(&s_scroll_probe_lock);
    pending = probe_gate_busy(&s_scroll_probe_gate);
    portEXIT_CRITICAL(&s_scroll_probe_lock);
    return pending;
}

static bool compose_probe_pending(void)
{
    bool pending;

    portENTER_CRITICAL(&s_compose_probe_lock);
    pending = probe_gate_busy(&s_compose_probe_gate);
    portEXIT_CRITICAL(&s_compose_probe_lock);
    return pending;
}

static void render_dm_thread_sheet(void);
static void render_public_history_sheet(void);
static void show_public_history_sheet(void);
static void render_packet_detail_sheet(void);
static void render_radio_settings_sheet(void);
static void render_storage_sheet(void);
static void render_wifi_sheet(void);
static void render_ble_sheet(void);
static void render_display_sheet(void);
static void render_diagnostics_sheet(void);
static void render_map_location_sheet(void);
static void render_map_options_sheet(void);
static void create_contact_edit_sheet(lv_obj_t *screen);
static void hide_node_detail_sheet(void);
static void request_tab_event_cb(lv_event_t *event);
static void open_map_location_sheet_event_cb(lv_event_t *event);
static void open_map_location_from_options_event_cb(lv_event_t *event);
static void open_map_options_sheet_event_cb(lv_event_t *event);
static void open_map_cache_status_event_cb(lv_event_t *event);
static void back_to_map_options_event_cb(lv_event_t *event);
static void close_map_location_sheet_event_cb(lv_event_t *event);
static void map_location_textarea_event_cb(lv_event_t *event);
static void map_location_save_event_cb(lv_event_t *event);
static void map_location_clear_event_cb(lv_event_t *event);
static void close_map_options_sheet_event_cb(lv_event_t *event);
static const char *packet_filter_direction(void);
static const char *packet_filter_kind(void);
static void message_detail_mode_event_cb(lv_event_t *event);
static void map_location_keyboard_event_cb(lv_event_t *event);
static void process_pending_content_refresh(void);
static void process_pending_scroll_probe(void);
static void process_pending_compose_probe(void);
static bool object_is_visible(lv_obj_t *obj);

static const d1l_ui_map_callbacks_t callbacks = {
    .open_options = open_map_options_sheet_event_cb,
    .close_options = close_map_options_sheet_event_cb,
    .open_location = open_map_location_from_options_event_cb,
    .open_cache_status = open_map_cache_status_event_cb,
    .back_to_options = back_to_map_options_event_cb,
    .close_location = close_map_location_sheet_event_cb,
    .save_location = map_location_save_event_cb,
    .clear_location = map_location_clear_event_cb,
    .location_textarea = map_location_textarea_event_cb,
    .location_keyboard = map_location_keyboard_event_cb,
    .open_node_detail = open_map_node_detail,
};

static const char *tab_name(d1l_ui_tab_t tab)
{
    return d1l_ui_screen_name(tab);
}

static bool tab_from_name(const char *name, d1l_ui_tab_t *out_tab)
{
    return d1l_ui_screen_from_name(name, out_tab);
}

static bool scroll_surface_from_name(const char *name, char *out_surface,
                                     size_t out_surface_len, d1l_ui_tab_t *out_tab)
{
    return d1l_ui_scroll_surface_from_name(name, out_surface, out_surface_len, out_tab);
}

static bool compose_probe_target_from_name(const char *name, char *out_target,
                                           size_t out_target_len)
{
    return d1l_ui_keyboard_normalize_probe_target(name, out_target, out_target_len);
}

static void request_full_screen_repaint(void)
{
    if (s_screen) {
        lv_obj_invalidate(s_screen);
    }
}

static void force_ui_layout_repaint(void)
{
    if (!s_screen) {
        return;
    }
    lv_obj_update_layout(s_screen);
    request_full_screen_repaint();
    lv_refr_now(NULL);
}

static void configure_sheet_scroll(lv_obj_t *sheet)
{
    d1l_ui_modal_configure_scroll(sheet);
}

static void lv_tick_task(void *arg)
{
    (void)arg;
    lv_tick_inc(5);
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
    crc = ~crc;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8U; ++bit) {
            crc = (crc >> 1U) ^ (0xEDB88320UL & (uint32_t)-(int32_t)(crc & 1U));
        }
    }
    return ~crc;
}

static uint32_t crc32_bytes(const uint8_t *data, size_t len)
{
    return crc32_update(0U, data, len);
}

static esp_err_t init_capture_buffers(void)
{
    if (!s_capture_lock) {
        s_capture_lock = xSemaphoreCreateMutex();
        if (!s_capture_lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (!s_capture_shadow) {
        s_capture_shadow = heap_caps_malloc(D1L_UI_CAPTURE_TOTAL_BYTES,
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_capture_shadow) {
            s_capture_shadow = heap_caps_malloc(D1L_UI_CAPTURE_TOTAL_BYTES, MALLOC_CAP_8BIT);
        }
        if (!s_capture_shadow) {
            return ESP_ERR_NO_MEM;
        }
        memset(s_capture_shadow, 0, D1L_UI_CAPTURE_TOTAL_BYTES);
    }
    if (!s_capture_snapshot) {
        s_capture_snapshot = heap_caps_malloc(D1L_UI_CAPTURE_TOTAL_BYTES,
                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_capture_snapshot) {
            s_capture_snapshot = heap_caps_malloc(D1L_UI_CAPTURE_TOTAL_BYTES, MALLOC_CAP_8BIT);
        }
        if (!s_capture_snapshot) {
            return ESP_ERR_NO_MEM;
        }
        memset(s_capture_snapshot, 0, D1L_UI_CAPTURE_TOTAL_BYTES);
    }
    return ESP_OK;
}

static void copy_rect_to_capture_shadow(int x1,
                                        int y1,
                                        int x2,
                                        int y2,
                                        const uint8_t *src,
                                        int src_origin_x,
                                        int src_origin_y,
                                        int src_stride_pixels)
{
    if (!s_capture_shadow || !src || !s_capture_lock || src_stride_pixels <= 0) {
        return;
    }
    if (x1 < 0) {
        x1 = 0;
    }
    if (y1 < 0) {
        y1 = 0;
    }
    if (x2 > (int)D1L_UI_CAPTURE_WIDTH) {
        x2 = (int)D1L_UI_CAPTURE_WIDTH;
    }
    if (y2 > (int)D1L_UI_CAPTURE_HEIGHT) {
        y2 = (int)D1L_UI_CAPTURE_HEIGHT;
    }
    if (x1 >= x2 || y1 >= y2) {
        return;
    }
    if (x1 < src_origin_x || y1 < src_origin_y) {
        return;
    }

    const size_t copy_bytes = (size_t)(x2 - x1) * D1L_UI_CAPTURE_BYTES_PER_PIXEL;
    xSemaphoreTake(s_capture_lock, portMAX_DELAY);
    for (int y = y1; y < y2; ++y) {
        const size_t src_offset =
            ((size_t)(y - src_origin_y) * (size_t)src_stride_pixels + (size_t)(x1 - src_origin_x)) *
            D1L_UI_CAPTURE_BYTES_PER_PIXEL;
        const size_t dest_offset =
            ((size_t)y * D1L_UI_CAPTURE_WIDTH + (size_t)x1) *
            D1L_UI_CAPTURE_BYTES_PER_PIXEL;
        memcpy(s_capture_shadow + dest_offset, src + src_offset, copy_bytes);
    }
    s_capture_shadow_ready = true;
    s_capture_flush_count++;
    s_capture_frame_seq++;
    xSemaphoreGive(s_capture_lock);
}

static bool lcd_flush_ready_cb(void *data)
{
    lv_disp_drv_t *drv = data ? (lv_disp_drv_t *)data : &s_disp_drv;
    lv_disp_flush_ready(drv);
    return false;
}

#if CONFIG_LCD_LVGL_DIRECT_MODE
static bool lcd_flush_is_last_cb(void)
{
    return lv_disp_flush_is_last(&s_disp_drv);
}

static void lcd_direct_mode_copy_cb(void)
{
    lv_disp_t *disp_refr = _lv_refr_get_disp_refreshing();
    if (!disp_refr || !disp_refr->driver || !disp_refr->driver->draw_buf) {
        return;
    }

    uint8_t *buf_act = disp_refr->driver->draw_buf->buf_act;
    uint8_t *buf1 = disp_refr->driver->draw_buf->buf1;
    uint8_t *buf2 = disp_refr->driver->draw_buf->buf2;
    const int h_res = disp_refr->driver->hor_res;

    if (!buf_act || !buf1 || !buf2 || h_res <= 0) {
        return;
    }
    uint8_t *fb_from = buf_act;
    uint8_t *fb_to = (fb_from == buf1) ? buf2 : buf1;
    const uint32_t bytes_per_line = (uint32_t)h_res * D1L_UI_CAPTURE_BYTES_PER_PIXEL;

    for (int32_t i = 0; i < disp_refr->inv_p; ++i) {
        if (disp_refr->inv_area_joined[i] != 0) {
            continue;
        }
        const lv_coord_t y_start = disp_refr->inv_areas[i].y1;
        const lv_coord_t y_end = disp_refr->inv_areas[i].y2 + 1;
        uint8_t *from = fb_from + ((uint32_t)y_start * (uint32_t)h_res) *
                                  D1L_UI_CAPTURE_BYTES_PER_PIXEL;
        uint8_t *to = fb_to + ((uint32_t)y_start * (uint32_t)h_res) *
                              D1L_UI_CAPTURE_BYTES_PER_PIXEL;
        for (lv_coord_t y = y_start; y < y_end; ++y) {
            memcpy(to, from, bytes_per_line);
            from += bytes_per_line;
            to += bytes_per_line;
        }

        copy_rect_to_capture_shadow(0, y_start, D1L_UI_CAPTURE_WIDTH, y_end,
                                    fb_to, 0, 0, h_res);
        uint8_t *flush_ptr = fb_to + (uint32_t)y_start * bytes_per_line;
        const uint32_t bytes_to_flush = (uint32_t)(y_end - y_start) * bytes_per_line;
        Cache_WriteBack_Addr((uint32_t)flush_ptr, bytes_to_flush);
    }
}
#endif

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_err_t ret = bsp_lcd_flush(area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_map);
#if !CONFIG_LCD_LVGL_DIRECT_MODE
    copy_rect_to_capture_shadow(area->x1, area->y1, area->x2 + 1, area->y2 + 1,
                                (const uint8_t *)color_map, area->x1, area->y1,
                                area->x2 - area->x1 + 1);
#endif
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "flush failed: %s", esp_err_to_name(ret));
    }
    if (ret != ESP_OK || !s_bsp_flush_ready_registered) {
        lv_disp_flush_ready(drv);
    }
}

static lv_coord_t clamp_touch_coord(int32_t value, lv_coord_t max_value)
{
    if (value < 0) {
        return 0;
    }
    if (value > max_value) {
        return max_value;
    }
    return (lv_coord_t)value;
}

static bool touch_sample_has_valid_point(const d1l_board_touch_state_t *sample)
{
    return sample &&
           sample->coordinate_valid &&
           sample->x < CONFIG_LCD_EVB_SCREEN_WIDTH &&
           sample->y < CONFIG_LCD_EVB_SCREEN_HEIGHT;
}

static void publish_touch_state(const d1l_board_touch_state_t *sample)
{
    portENTER_CRITICAL(&s_touch_lock);
    s_touch_state = *sample;
    s_touch_state_ready = true;
    portEXIT_CRITICAL(&s_touch_lock);
}

static bool read_cached_touch_state(d1l_board_touch_state_t *out_sample)
{
    bool ready;

    portENTER_CRITICAL(&s_touch_lock);
    ready = s_touch_state_ready;
    if (ready) {
        *out_sample = s_touch_state;
    }
    portEXIT_CRITICAL(&s_touch_lock);

    return ready;
}

static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    static lv_coord_t last_x = 0;
    static lv_coord_t last_y = 0;
    static d1l_board_touch_state_t sample;

    data->state = LV_INDEV_STATE_REL;
    data->point.x = last_x;
    data->point.y = last_y;

    memset(&sample, 0, sizeof(sample));
    if (!read_cached_touch_state(&sample)) {
        return;
    }
    if (sample.pressed && touch_sample_has_valid_point(&sample)) {
        data->state = LV_INDEV_STATE_PR;
        last_x = clamp_touch_coord((int32_t)CONFIG_LCD_EVB_SCREEN_WIDTH - 1 - (int32_t)sample.x,
                                   CONFIG_LCD_EVB_SCREEN_WIDTH - 1);
        last_y = clamp_touch_coord((int32_t)CONFIG_LCD_EVB_SCREEN_HEIGHT - 1 - (int32_t)sample.y,
                                   CONFIG_LCD_EVB_SCREEN_HEIGHT - 1);
    }
    data->point.x = last_x;
    data->point.y = last_y;
}

static void label_set_fmt(lv_obj_t *label, const char *fmt, ...)
{
    if (!label || !fmt) {
        return;
    }
    char text[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    lv_label_set_text(label, text);
}

static void label_set_dot_width(lv_obj_t *label, lv_coord_t width)
{
    if (!label) {
        return;
    }
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, width);
}

static void obj_align_if(lv_obj_t *obj, lv_align_t align, lv_coord_t x_ofs, lv_coord_t y_ofs)
{
    if (!obj) {
        return;
    }
    lv_obj_align(obj, align, x_ofs, y_ofs);
}

static void obj_set_pos_if(lv_obj_t *obj, lv_coord_t x, lv_coord_t y)
{
    if (!obj) {
        return;
    }
    lv_obj_set_pos(obj, x, y);
}

static void obj_set_style_text_font_if(lv_obj_t *obj, const lv_font_t *font)
{
    if (!obj || !font) {
        return;
    }
    lv_obj_set_style_text_font(obj, font, 0);
}

static lv_obj_t *create_screen_object(const char *name)
{
    lv_obj_t *obj = lv_obj_create(NULL);
    if (!obj) {
        ESP_LOGE(TAG, "%s allocation failed", name ? name : "screen");
    }
    return obj;
}

static lv_obj_t *create_object(lv_obj_t *parent, const char *name)
{
    if (!parent) {
        ESP_LOGE(TAG, "%s parent missing", name ? name : "object");
        return NULL;
    }
    lv_obj_t *obj = lv_obj_create(parent);
    if (!obj) {
        ESP_LOGE(TAG, "%s allocation failed", name ? name : "object");
    }
    return obj;
}

static lv_obj_t *create_label_object(lv_obj_t *parent, const char *name)
{
    if (!parent) {
        ESP_LOGE(TAG, "%s parent missing", name ? name : "label");
        return NULL;
    }
    lv_obj_t *label = lv_label_create(parent);
    if (!label) {
        ESP_LOGE(TAG, "%s allocation failed", name ? name : "label");
    }
    return label;
}

static lv_obj_t *create_textarea(lv_obj_t *parent, const char *name)
{
    if (!parent) {
        ESP_LOGE(TAG, "%s parent missing", name ? name : "textarea");
        return NULL;
    }
    lv_obj_t *textarea = lv_textarea_create(parent);
    if (!textarea) {
        ESP_LOGE(TAG, "%s allocation failed", name ? name : "textarea");
    }
    return textarea;
}

static lv_obj_t *create_keyboard(lv_obj_t *parent, const char *name)
{
    if (!parent) {
        ESP_LOGE(TAG, "%s parent missing", name ? name : "keyboard");
        return NULL;
    }
    lv_obj_t *keyboard = lv_keyboard_create(parent);
    if (!keyboard) {
        ESP_LOGE(TAG, "%s allocation failed", name ? name : "keyboard");
    }
    return keyboard;
}

#if LV_USE_QRCODE
static lv_obj_t *create_qrcode(lv_obj_t *parent, lv_coord_t size,
                               lv_color_t dark, lv_color_t light, const char *name)
{
    if (!parent) {
        ESP_LOGE(TAG, "%s parent missing", name ? name : "qrcode");
        return NULL;
    }
    lv_obj_t *qr = lv_qrcode_create(parent, size, dark, light);
    if (!qr) {
        ESP_LOGE(TAG, "%s allocation failed", name ? name : "qrcode");
    }
    return qr;
}
#endif

static lv_obj_t *create_label(lv_obj_t *parent, const char *text, uint32_t color)
{
    if (!parent || !text) {
        return NULL;
    }
    lv_obj_t *label = create_label_object(parent, "label");
    if (!label) {
        return NULL;
    }
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

static lv_obj_t *create_panel(lv_obj_t *parent, int x, int y, int w, int h)
{
    if (!parent) {
        return NULL;
    }
    lv_obj_t *panel = create_object(parent, "panel");
    if (!panel) {
        return NULL;
    }
    lv_obj_set_size(panel, w, h);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x263241), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_pad_all(panel, 12, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}

static lv_obj_t *create_button(lv_obj_t *parent, const char *text, int x, int y, int w, int h,
                               lv_event_cb_t cb, void *user_data)
{
    if (!parent || !text) {
        return NULL;
    }
    lv_obj_t *button = lv_btn_create(parent);
    if (!button) {
        ESP_LOGE(TAG, "button allocation failed");
        return NULL;
    }
    lv_obj_set_size(button, w, h);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x1E2A36), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x263545), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_t *label = create_label_object(button, "button label");
    if (label) {
        lv_label_set_text(label, text);
        lv_obj_set_style_text_color(label, lv_color_hex(0xF4F7FB), 0);
        lv_obj_center(label);
    } else {
        ESP_LOGE(TAG, "button label allocation failed");
    }
    if (cb) {
        lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, user_data);
    }
    return button;
}

static void style_danger_button(lv_obj_t *button)
{
    if (!button) {
        return;
    }
    lv_obj_set_style_bg_color(button, lv_color_hex(0x3A1720), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x55202B), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(button, lv_color_hex(0xF87171), 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_t *label = lv_obj_get_child(button, 0);
    if (label) {
        lv_obj_set_style_text_color(label, lv_color_hex(0xFCA5A5), 0);
    }
}

static void style_contact_option_button(lv_obj_t *button, uint32_t accent,
                                        const char *detail)
{
    if (!button) {
        return;
    }
    lv_obj_t *label = lv_obj_get_child(button, 0);
    if (label) {
        lv_obj_set_style_text_color(label, lv_color_hex(accent), 0);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 12, 0);
    }
    lv_obj_t *detail_label = create_label(button, detail ? detail : "", 0x8EA0AE);
    if (detail_label) {
        lv_label_set_long_mode(detail_label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(detail_label, 220);
        lv_obj_set_style_text_align(detail_label, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(detail_label, LV_ALIGN_RIGHT_MID, -12, 0);
    }
}

static void show_toast(const char *action, esp_err_t ret)
{
    if (!s_toast) {
        return;
    }
    if (ret == ESP_OK) {
        label_set_fmt(s_toast, "%s queued", action);
        lv_obj_set_style_bg_color(s_toast, lv_color_hex(0x12362F), 0);
        lv_obj_set_style_border_color(s_toast, lv_color_hex(0x5EEAD4), 0);
    } else {
        label_set_fmt(s_toast, "%s: %s", action, esp_err_to_name(ret));
        lv_obj_set_style_bg_color(s_toast, lv_color_hex(0x3A1720), 0);
        lv_obj_set_style_border_color(s_toast, lv_color_hex(0xF87171), 0);
    }
    lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_toast);
    s_toast_until = lv_tick_get() + 3000U;
}

static void show_toast_text(const char *text, bool ok)
{
    if (!s_toast) {
        return;
    }
    lv_label_set_text(s_toast, text);
    lv_obj_set_style_bg_color(s_toast, lv_color_hex(ok ? 0x12362F : 0x3A1720), 0);
    lv_obj_set_style_border_color(s_toast, lv_color_hex(ok ? 0x5EEAD4 : 0xF87171), 0);
    lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_toast);
    s_toast_until = lv_tick_get() + 3000U;
}

static void set_dock_hidden(bool hidden)
{
    if (!s_dock) {
        return;
    }
    if (lv_obj_has_flag(s_dock, LV_OBJ_FLAG_HIDDEN) == hidden) {
        return;
    }
    if (hidden) {
        lv_obj_add_flag(s_dock, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(s_dock, LV_OBJ_FLAG_HIDDEN);
    }
    request_full_screen_repaint();
}

static void restore_dock_for_active_tab(void)
{
    if (d1l_ui_modal_has_active()) {
        set_dock_hidden(true);
        return;
    }
    const d1l_ui_chrome_layout_t layout =
        d1l_ui_chrome_layout_for_screen(d1l_ui_navigation_active());
    set_dock_hidden(!layout.dock_visible);
}

static void show_modal(lv_obj_t *obj)
{
    if (!obj) {
        return;
    }
    if (d1l_ui_navigation_active() == D1L_UI_TAB_MAP) {
        d1l_ui_map_viewport_prepare_cover();
    }
    d1l_ui_map_viewport_release();
    set_dock_hidden(true);
    d1l_ui_modal_show(obj);
}

static void layout_content_for_active_tab(void)
{
    if (s_content) {
        const d1l_ui_chrome_layout_t layout =
            d1l_ui_chrome_layout_for_screen(d1l_ui_navigation_active());
        lv_obj_set_pos(s_content, 0, layout.content_y);
        lv_obj_set_size(s_content, 480, layout.content_height);
        d1l_ui_screen_configure_content_root(s_content, layout.content_scrollable);
    }
    restore_dock_for_active_tab();
}

static void hide_sheet(void)
{
    d1l_ui_modal_hide(s_sheet);
    restore_dock_for_active_tab();
}

static void hide_compose_sheet(void)
{
    d1l_ui_modal_hide(s_compose_sheet);
    s_onboarding_probe_suppressed = false;
    restore_dock_for_active_tab();
    s_compose_dm = false;
    memset(&s_compose_contact, 0, sizeof(s_compose_contact));
    request_full_screen_repaint();
}

static void hide_public_history_sheet(void)
{
    d1l_ui_modal_hide(s_public_history_sheet);
    s_public_history_limit = D1L_PUBLIC_HISTORY_UI_INITIAL_ROWS;
    restore_dock_for_active_tab();
}

static void hide_public_search_sheet(void)
{
    d1l_ui_modal_hide(s_public_search_sheet);
    restore_dock_for_active_tab();
}

static void hide_message_detail_sheet(void)
{
    d1l_ui_modal_hide(s_message_detail_sheet);
    s_message_detail_advanced = false;
    memset(&s_message_detail_message, 0, sizeof(s_message_detail_message));
    restore_dock_for_active_tab();
}

static void hide_dm_thread_sheet(void)
{
    d1l_ui_modal_hide(s_dm_thread_sheet);
    s_dm_thread_fingerprint[0] = '\0';
    s_dm_thread_alias[0] = '\0';
    s_dm_thread_limit = D1L_DM_THREAD_UI_INITIAL_ROWS;
    restore_dock_for_active_tab();
}

static void hide_radio_settings_sheet(void)
{
    d1l_ui_modal_hide(s_radio_settings_sheet);
    restore_dock_for_active_tab();
}

static void hide_storage_sheet(void)
{
    d1l_ui_modal_hide(s_storage_sheet);
    s_storage_page = D1L_STORAGE_PAGE_ROOT;
    restore_dock_for_active_tab();
}

static void hide_wifi_sheet(void)
{
    d1l_ui_modal_hide(s_wifi_sheet);
    restore_dock_for_active_tab();
}

static void hide_ble_sheet(void)
{
    d1l_ui_modal_hide(s_ble_sheet);
    restore_dock_for_active_tab();
}

static void hide_display_sheet(void)
{
    d1l_ui_modal_hide(s_display_sheet);
    restore_dock_for_active_tab();
}

static void hide_diagnostics_sheet(void)
{
    d1l_ui_modal_hide(s_diagnostics_sheet);
    restore_dock_for_active_tab();
}

static void hide_map_location_sheet(void)
{
    d1l_ui_modal_hide(s_map_location_sheet);
    restore_dock_for_active_tab();
}

static void hide_map_options_sheet(void)
{
    d1l_ui_modal_hide(s_map_options_sheet);
    s_map_options_page = D1L_UI_MAP_OPTIONS_ROOT;
    restore_dock_for_active_tab();
}

static void hide_contact_edit_sheet(void)
{
    d1l_ui_modal_hide(s_contact_edit_sheet);
    if (s_contact_edit_textarea) {
        lv_textarea_set_text(s_contact_edit_textarea, "");
    }
    restore_dock_for_active_tab();
}

static void hide_contact_options_sheet(void)
{
    d1l_ui_modal_hide(s_contact_options_sheet);
    restore_dock_for_active_tab();
}

static void hide_contact_forget_sheet(void)
{
    d1l_ui_modal_hide(s_contact_forget_sheet);
    restore_dock_for_active_tab();
}

static void hide_contact_detail_sheet(void)
{
    d1l_ui_modal_hide(s_contact_detail_sheet);
    d1l_ui_modal_hide(s_contact_options_sheet);
    d1l_ui_modal_hide(s_contact_forget_sheet);
    d1l_ui_modal_hide(s_contact_edit_sheet);
    d1l_ui_modal_hide(s_contact_export_sheet);
    d1l_ui_modal_hide(s_route_trace_sheet);
    if (s_contact_edit_textarea) {
        lv_textarea_set_text(s_contact_edit_textarea, "");
    }
    hide_node_detail_sheet();
    memset(&s_contact_detail_contact, 0, sizeof(s_contact_detail_contact));
    memset(&s_contact_export_contact, 0, sizeof(s_contact_export_contact));
    memset(&s_route_trace_contact, 0, sizeof(s_route_trace_contact));
    s_contact_export_uri[0] = '\0';
    restore_dock_for_active_tab();
}

static void hide_contact_export_sheet(void)
{
    d1l_ui_modal_hide(s_contact_export_sheet);
    memset(&s_contact_export_contact, 0, sizeof(s_contact_export_contact));
    s_contact_export_uri[0] = '\0';
    restore_dock_for_active_tab();
}

static void hide_node_detail_sheet(void)
{
    d1l_ui_modal_hide(s_node_detail_sheet);
    memset(&s_node_detail_node, 0, sizeof(s_node_detail_node));
    s_node_detail_returns_to_map = false;
    restore_dock_for_active_tab();
}

static void hide_route_detail_sheet(void)
{
    d1l_ui_modal_hide(s_route_detail_sheet);
    memset(&s_route_detail_route, 0, sizeof(s_route_detail_route));
    restore_dock_for_active_tab();
}

static void hide_route_trace_sheet(void)
{
    d1l_ui_modal_hide(s_route_trace_sheet);
    memset(&s_route_trace_contact, 0, sizeof(s_route_trace_contact));
    restore_dock_for_active_tab();
}

static void hide_packet_detail_sheet(void)
{
    d1l_ui_modal_hide(s_packet_detail_sheet);
    memset(&s_packet_detail_packet, 0, sizeof(s_packet_detail_packet));
    restore_dock_for_active_tab();
}

static void hide_packet_search_sheet(void)
{
    d1l_ui_modal_hide(s_packet_search_sheet);
    restore_dock_for_active_tab();
}

static void hide_mesh_roles_sheet(void)
{
    d1l_ui_modal_hide(s_mesh_roles_sheet);
    s_mesh_roles_page = D1L_MESH_ROLES_PAGE_ROOT;
    restore_dock_for_active_tab();
}

static void hide_onboarding_sheet(void)
{
    d1l_ui_modal_hide(s_onboarding_sheet);
    s_onboarding_visible = false;
    restore_dock_for_active_tab();
    request_full_screen_repaint();
}

static void update_onboarding_visibility(const d1l_app_snapshot_t *snapshot)
{
    if (!snapshot || !s_onboarding_sheet) {
        return;
    }
    if (snapshot->onboarding_complete) {
        s_onboarding_probe_suppressed = false;
        hide_onboarding_sheet();
        return;
    }
    if (s_onboarding_probe_suppressed) {
        hide_onboarding_sheet();
        return;
    }
    s_onboarding_visible = true;
    show_modal(s_onboarding_sheet);
    request_full_screen_repaint();
}

static void set_object_hidden(lv_obj_t *obj, bool hidden)
{
    if (!obj) {
        return;
    }
    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_chrome(const d1l_app_snapshot_t *snapshot)
{
    if (!snapshot || !s_title_label || !s_status_label || !s_identity_label) {
        return;
    }
    const d1l_ui_chrome_layout_t layout =
        d1l_ui_chrome_layout_for_screen(d1l_ui_navigation_active());
    lv_label_set_text(s_title_label, layout.title);
    if (s_top_bar) {
        lv_obj_set_size(s_top_bar, 480, layout.content_y);
        lv_obj_set_style_pad_all(s_top_bar, layout.header_detail_visible ? 8 : 1, 0);
    }
    if (layout.header_detail_visible) {
        lv_obj_align(s_title_label, LV_ALIGN_LEFT_MID, 2, 0);
    } else {
        lv_obj_align(s_title_label, LV_ALIGN_TOP_LEFT, 10, 0);
    }
    set_object_hidden(s_status_label, !layout.header_detail_visible);
    set_object_hidden(s_identity_label, !layout.header_detail_visible);
    set_object_hidden(s_lock_button, !layout.header_detail_visible);
    if (!layout.header_detail_visible) {
        return;
    }
    label_set_fmt(s_status_label, "%s  Mesh %s",
                  snapshot->time_label[0] ? snapshot->time_label : "--:--",
                  snapshot->mesh_state ? snapshot->mesh_state : "starting");
    label_set_fmt(s_identity_label, "Wi-Fi %s  BLE %s  SD %s",
                  snapshot->wifi_state ? snapshot->wifi_state : "off",
                  snapshot->ble_state ? snapshot->ble_state : "off",
                  d1l_ui_home_sd_state(snapshot));
}

static lv_obj_t *render_metric_card(lv_obj_t *parent, int x, int y, const char *title,
                                    const char *value, const char *detail, uint32_t accent)
{
    lv_obj_t *card = create_panel(parent, x, y, 204, 104);
    if (!card) {
        return NULL;
    }
    lv_obj_t *title_label = create_label(card, title, 0x8EA0AE);
    obj_align_if(title_label, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *value_label = create_label(card, value, accent);
    obj_set_style_text_font_if(value_label, &lv_font_montserrat_24);
    obj_align_if(value_label, LV_ALIGN_TOP_LEFT, 0, 26);

    lv_obj_t *detail_label = create_label(card, detail, 0xD7E1EA);
    label_set_dot_width(detail_label, 176);
    obj_align_if(detail_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    return card;
}

static void format_snr_tenths(char *dest, size_t dest_size, int snr_tenths)
{
    const int snr_abs = snr_tenths < 0 ? -snr_tenths : snr_tenths;
    snprintf(dest, dest_size, "%s%d.%d", snr_tenths < 0 ? "-" : "",
             snr_abs / 10, snr_abs % 10);
}

static void map_location_from_snapshot(const d1l_app_snapshot_t *snapshot)
{
    if (snapshot && snapshot->map_location_set) {
        s_map_location_lat_e7 = snapshot->map_lat_e7;
        s_map_location_lon_e7 = snapshot->map_lon_e7;
        return;
    }
    s_map_location_lat_e7 = 0;
    s_map_location_lon_e7 = 0;
}

static bool parse_coord_text_e7(const char *text, int32_t min_value, int32_t max_value,
                                int32_t *out_value)
{
    if (!text || !out_value) {
        return false;
    }
    while (*text == ' ' || *text == '\t') {
        ++text;
    }
    bool negative = false;
    if (*text == '-' || *text == '+') {
        negative = (*text == '-');
        ++text;
    }

    int64_t whole = 0;
    int64_t fraction = 0;
    uint8_t fraction_digits = 0;
    bool saw_digit = false;
    while (*text >= '0' && *text <= '9') {
        saw_digit = true;
        whole = (whole * 10) + (*text - '0');
        if (whole > 180) {
            return false;
        }
        ++text;
    }
    if (*text == '.') {
        ++text;
        while (*text >= '0' && *text <= '9') {
            saw_digit = true;
            if (fraction_digits < 7U) {
                fraction = (fraction * 10) + (*text - '0');
                fraction_digits++;
            }
            ++text;
        }
    }
    while (fraction_digits < 7U) {
        fraction *= 10;
        fraction_digits++;
    }
    while (*text == ' ' || *text == '\t') {
        ++text;
    }
    if (!saw_digit || *text != '\0') {
        return false;
    }

    int64_t scaled = (whole * 10000000LL) + fraction;
    if (negative) {
        scaled = -scaled;
    }
    if (scaled < min_value || scaled > max_value) {
        return false;
    }
    *out_value = (int32_t)scaled;
    return true;
}

static void format_radio_profile_line(char *dest, size_t dest_size,
                                      const d1l_app_radio_profile_edit_t *profile)
{
    if (!dest || dest_size == 0 || !profile) {
        return;
    }
    snprintf(dest, dest_size, "US/CAN %.3f  BW%.1f  SF%u  CR%u",
             ((double)profile->frequency_hz) / 1000000.0,
             ((double)profile->bandwidth_tenths_khz) / 10.0,
             profile->spreading_factor, profile->coding_rate);
}

static const char *node_role_badge_text(const char *role)
{
    if (!role || role[0] == '\0') {
        return "NODE";
    }
    if (strcmp(role, "room") == 0) {
        return "ROOM";
    }
    if (strcmp(role, "repeater") == 0) {
        return "RPT";
    }
    if (strcmp(role, "sensor") == 0) {
        return "SNS";
    }
    if (strcmp(role, "companion") == 0) {
        return "CMP";
    }
    return "NODE";
}

static const char *node_role_display_label(const char *role)
{
    if (!role || role[0] == '\0') {
        return "Node";
    }
    if (strcmp(role, "room") == 0) {
        return "Room Server";
    }
    if (strcmp(role, "repeater") == 0) {
        return "Repeater";
    }
    if (strcmp(role, "sensor") == 0) {
        return "Sensor";
    }
    if (strcmp(role, "companion") == 0) {
        return "Companion";
    }
    return role;
}

static uint32_t node_role_color(const char *role)
{
    if (!role || role[0] == '\0') {
        return 0x93C5FD;
    }
    if (strcmp(role, "room") == 0) {
        return 0xA7F3D0;
    }
    if (strcmp(role, "repeater") == 0) {
        return 0xFBBF24;
    }
    if (strcmp(role, "sensor") == 0) {
        return 0xC4B5FD;
    }
    if (strcmp(role, "companion") == 0) {
        return 0x5EEAD4;
    }
    return 0x93C5FD;
}

static bool node_role_is_managed_service(const char *role)
{
    return role && (strcmp(role, "room") == 0 || strcmp(role, "repeater") == 0);
}

static bool node_view_can_dm(const d1l_node_view_t *view)
{
    return view &&
           view->node.fingerprint[0] != '\0' &&
           view->node.public_key_hex[0] != '\0' &&
           strcmp(view->role, "companion") == 0;
}

static bool contact_can_dm(const d1l_contact_entry_t *entry)
{
    return entry && entry->fingerprint[0] != '\0' &&
           entry->public_key_hex[0] != '\0' && strcmp(entry->type, "chat") == 0;
}

static bool contact_can_export(const d1l_contact_entry_t *entry)
{
    return entry && d1l_contact_store_has_export_key(entry) &&
           d1l_contact_store_meshcore_type_id(entry->type) != 0U;
}

static bool node_view_management_gated(const d1l_node_view_t *view)
{
    return view && node_role_is_managed_service(view->role);
}

static lv_obj_t *render_node_role_badge(lv_obj_t *parent, const char *role,
                                        lv_coord_t x, lv_coord_t y,
                                        lv_coord_t width)
{
    lv_obj_t *badge = create_object(parent, "node role badge");
    if (!badge) {
        return NULL;
    }
    lv_obj_set_size(badge, width, 24);
    lv_obj_set_pos(badge, x, y);
    lv_obj_set_style_radius(badge, 6, 0);
    lv_obj_set_style_bg_color(badge, lv_color_hex(0x10202A), 0);
    lv_obj_set_style_border_color(badge, lv_color_hex(node_role_color(role)), 0);
    lv_obj_set_style_border_width(badge, 1, 0);
    lv_obj_set_style_pad_all(badge, 0, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = create_label(badge, node_role_badge_text(role), node_role_color(role));
    label_set_dot_width(label, width - 6);
    obj_align_if(label, LV_ALIGN_CENTER, 0, 0);
    return badge;
}

static void radio_edit_from_snapshot(const d1l_app_snapshot_t *snapshot)
{
    if (!snapshot) {
        d1l_app_model_current_radio_profile(&s_radio_edit);
        return;
    }
    s_radio_edit.frequency_hz = snapshot->radio_frequency_hz;
    s_radio_edit.bandwidth_tenths_khz = snapshot->radio_bandwidth_tenths_khz;
    s_radio_edit.spreading_factor = snapshot->radio_spreading_factor;
    s_radio_edit.coding_rate = snapshot->radio_coding_rate;
    s_radio_edit.tx_power_dbm = snapshot->radio_tx_power_dbm;
    s_radio_edit.rx_boost = snapshot->radio_rx_boost;
}

static void handle_home_action(d1l_ui_home_action_t action)
{
    switch (action) {
    case D1L_UI_HOME_ACTION_MESSAGES:
        request_tab_switch(D1L_UI_TAB_MESSAGES);
        break;
    case D1L_UI_HOME_ACTION_NETWORK:
        request_tab_switch(D1L_UI_TAB_NODES);
        break;
    case D1L_UI_HOME_ACTION_MAP:
        set_map_interactive_touch_authorized(true);
        request_tab_switch(D1L_UI_TAB_MAP);
        break;
    case D1L_UI_HOME_ACTION_MORE:
        request_tab_switch(D1L_UI_TAB_SETTINGS);
        break;
    case D1L_UI_HOME_ACTION_NONE:
    default:
        break;
    }
}

static void render_home_screen(lv_obj_t *content, const d1l_app_snapshot_t *snapshot)
{
    d1l_ui_home_render(content, snapshot, handle_home_action);
}

static void handle_settings_action(d1l_ui_settings_action_t action)
{
    switch (action) {
    case D1L_UI_SETTINGS_ACTION_PACKETS:
        request_tab_switch(D1L_UI_TAB_PACKETS);
        break;
    case D1L_UI_SETTINGS_ACTION_STORAGE:
        open_storage_sheet_event_cb(NULL);
        break;
    case D1L_UI_SETTINGS_ACTION_WIFI:
        open_wifi_sheet_event_cb(NULL);
        break;
    case D1L_UI_SETTINGS_ACTION_BLE:
        open_ble_sheet_event_cb(NULL);
        break;
    case D1L_UI_SETTINGS_ACTION_RADIO:
        open_radio_settings_event_cb(NULL);
        break;
    case D1L_UI_SETTINGS_ACTION_MAP_TILES:
        open_map_options_sheet_event_cb(NULL);
        break;
    case D1L_UI_SETTINGS_ACTION_DISPLAY:
        open_display_sheet_event_cb(NULL);
        break;
    case D1L_UI_SETTINGS_ACTION_DIAGNOSTICS:
        open_diagnostics_sheet_event_cb(NULL);
        break;
    case D1L_UI_SETTINGS_ACTION_ADVANCED:
        open_sheet_event_cb(NULL);
        break;
    case D1L_UI_SETTINGS_ACTION_NONE:
    case D1L_UI_SETTINGS_ACTION_COUNT:
    default:
        break;
    }
}

static void render_settings(lv_obj_t *content, const d1l_app_snapshot_t *snapshot)
{
    d1l_ui_settings_render(content, snapshot, handle_settings_action);
}

static void render_health_line(lv_obj_t *parent, int y, const d1l_app_snapshot_t *snapshot)
{
    lv_obj_t *line = create_label(parent, "", 0x8EA0AE);
    label_set_fmt(line, "reset %s  heap %luK/%luK  ui stk %lu",
                  snapshot->reset_reason ? snapshot->reset_reason : "UNKNOWN",
                  (unsigned long)(snapshot->heap_free / 1024U),
                  (unsigned long)(snapshot->heap_min_free / 1024U),
                  (unsigned long)snapshot->ui_task_stack_free_words);
    label_set_dot_width(line, 390);
    obj_set_pos_if(line, 0, y);
}

static void render_message_row(lv_obj_t *parent, int y, const d1l_message_entry_t *entry)
{
    lv_obj_t *row = create_panel(parent, 18, y, 424, 72);
    if (!row) {
        return;
    }
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, open_message_detail_event_cb, LV_EVENT_CLICKED, (void *)entry);
    lv_obj_set_style_pad_all(row, 8, 0);
    const bool unread = entry->direction[0] == 'r' && entry->seq > s_snapshot.last_public_read_seq;
    char snr[16];
    char meta[96];
    format_snr_tenths(snr, sizeof(snr), entry->snr_tenths);
    lv_obj_t *author = create_label(row, entry->author[0] ? entry->author : "unknown",
                                    unread ? 0xFBBF24 :
                                    (entry->direction[0] == 't' ? 0x93C5FD : 0x5EEAD4));
    label_set_dot_width(author, 250);
    lv_obj_set_pos(author, 8, 4);
    lv_obj_t *state = create_label(row,
                                   unread ? "new" :
                                   (entry->direction[0] == 't' ? "queued" : "received"),
                                   0x8EA0AE);
    label_set_dot_width(state, 118);
    lv_obj_set_pos(state, 292, 4);
    lv_obj_t *text = create_label(row, entry->text[0] ? entry->text : "-", 0xE5EDF5);
    label_set_dot_width(text, 392);
    lv_obj_set_pos(text, 8, 28);
    snprintf(meta, sizeof(meta), "rssi %d  snr %s  hops %u",
             entry->rssi_dbm, snr, entry->path_hops);
    lv_obj_t *details = create_label(row, meta, 0x8EA0AE);
    label_set_dot_width(details, 392);
    lv_obj_set_pos(details, 8, 50);
}

static void render_dm_row(lv_obj_t *parent, int y, const d1l_dm_entry_t *entry, bool unread)
{
    lv_obj_t *row = create_panel(parent, 18, y, 424, 72);
    if (!row) {
        return;
    }
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, open_dm_thread_event_cb, LV_EVENT_CLICKED, (void *)entry);
    lv_obj_set_style_pad_all(row, 8, 0);
    char snr[16];
    char meta[96];
    format_snr_tenths(snr, sizeof(snr), entry->snr_tenths);
    lv_obj_t *alias = create_label(row, entry->contact_alias[0] ? entry->contact_alias : entry->contact_fingerprint,
                                    unread ? 0xFBBF24 :
                                    (entry->direction[0] == 't' ? 0xC4B5FD : 0xA7F3D0));
    label_set_dot_width(alias, 250);
    lv_obj_set_pos(alias, 8, 4);
    lv_obj_t *state = create_label(row,
                                   unread ? "new" :
                                   (entry->acked ? "acked" :
                                    (entry->direction[0] == 't' ? "sent" : "received")),
                                   0x8EA0AE);
    label_set_dot_width(state, 118);
    lv_obj_set_pos(state, 292, 4);
    lv_obj_t *text = create_label(row, entry->text[0] ? entry->text : "-", 0xE5EDF5);
    label_set_dot_width(text, 392);
    lv_obj_set_pos(text, 8, 28);
    snprintf(meta, sizeof(meta), "rssi %d  snr %s  hops %u",
             entry->rssi_dbm, snr, entry->path_hops);
    lv_obj_t *details = create_label(row, meta, 0x8EA0AE);
    label_set_dot_width(details, 392);
    lv_obj_set_pos(details, 8, 50);
}

static char packet_lower_ascii(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

static bool packet_text_has_token(const char *text, const char *token)
{
    if (!text || !token || token[0] == '\0') {
        return false;
    }
    for (const char *cursor = text; *cursor; ++cursor) {
        const char *left = cursor;
        const char *right = token;
        while (*left && *right && packet_lower_ascii(*left) == packet_lower_ascii(*right)) {
            ++left;
            ++right;
        }
        if (*right == '\0') {
            return true;
        }
    }
    return false;
}

static uint32_t packet_entry_color(const d1l_packet_log_entry_t *entry)
{
    if (!entry) {
        return 0x8EA0AE;
    }
    if (packet_text_has_token(entry->kind, "fail") ||
        packet_text_has_token(entry->note, "fail") ||
        packet_text_has_token(entry->kind, "error") ||
        packet_text_has_token(entry->note, "error")) {
        return 0xF87171;
    }
    if (packet_text_has_token(entry->kind, "raw") ||
        packet_text_has_token(entry->kind, "control")) {
        return 0xFBBF24;
    }
    if (packet_text_has_token(entry->direction, "tx")) {
        return 0x93C5FD;
    }
    if (packet_text_has_token(entry->direction, "rx")) {
        return 0x5EEAD4;
    }
    return 0xA7F3D0;
}

static const char *packet_entry_status(const d1l_packet_log_entry_t *entry)
{
    if (!entry) {
        return "UNK";
    }
    if (packet_text_has_token(entry->kind, "fail") ||
        packet_text_has_token(entry->note, "fail")) {
        return "FAIL";
    }
    if (packet_text_has_token(entry->kind, "error") ||
        packet_text_has_token(entry->note, "error")) {
        return "ERR";
    }
    if (packet_text_has_token(entry->direction, "tx")) {
        return "TX";
    }
    if (packet_text_has_token(entry->direction, "rx")) {
        return "RX";
    }
    return "LOG";
}

static size_t refresh_packet_terminal_rows(void)
{
    if (s_packets_paused) {
        return s_packet_filtered_count;
    }
    if (s_packet_row_limit < D1L_PACKET_UI_INITIAL_ROWS) {
        s_packet_row_limit = D1L_PACKET_UI_INITIAL_ROWS;
    }
    if (s_packet_row_limit > D1L_PACKET_LOG_CAPACITY) {
        s_packet_row_limit = D1L_PACKET_LOG_CAPACITY;
    }
    size_t total_matches = 0;
    bool sd_used = false;
    s_packet_filtered_count = d1l_packet_log_query_page(s_packet_filtered_packets,
                                                        s_packet_row_limit,
                                                        s_packet_skip_newest,
                                                        packet_filter_direction(),
                                                        packet_filter_kind(),
                                                        s_packet_search_text,
                                                        &total_matches, &sd_used);
    if (s_packet_filtered_count == 0 && s_packet_skip_newest > 0) {
        s_packet_skip_newest = 0;
        s_packet_filtered_count = d1l_packet_log_query_page(s_packet_filtered_packets,
                                                            s_packet_row_limit,
                                                            s_packet_skip_newest,
                                                            packet_filter_direction(),
                                                            packet_filter_kind(),
                                                            s_packet_search_text,
                                                            &total_matches, &sd_used);
    }
    s_packet_total_matches = total_matches;
    s_packet_sd_history_page = sd_used;
    return s_packet_filtered_count;
}

static bool packet_feed_can_load_older(size_t packet_rows)
{
    return packet_rows > 0 && s_packet_total_matches > s_packet_skip_newest + packet_rows;
}

static bool packet_feed_can_load_newer(void)
{
    return s_packet_skip_newest > 0;
}

static void render_packet_row(lv_obj_t *parent, int y, const d1l_packet_log_entry_t *entry)
{
    if (!entry) {
        return;
    }
    const uint32_t accent_color = packet_entry_color(entry);
    char snr[16];
    format_snr_tenths(snr, sizeof(snr), entry->snr_tenths);

    lv_obj_t *row = create_panel(parent, 18, y, 424, 46);
    if (!row) {
        return;
    }
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, open_packet_detail_event_cb, LV_EVENT_CLICKED, (void *)entry);
    lv_obj_set_style_pad_all(row, 7, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x071018), 0);
    lv_obj_set_style_border_color(row, lv_color_hex(accent_color), 0);

    lv_obj_t *accent = create_object(row, "packet row accent");
    if (accent) {
        lv_obj_set_size(accent, 4, 30);
        lv_obj_align(accent, LV_ALIGN_LEFT_MID, -2, 0);
        lv_obj_set_style_radius(accent, 2, 0);
        lv_obj_set_style_bg_color(accent, lv_color_hex(accent_color), 0);
        lv_obj_set_style_border_width(accent, 0, 0);
    }

    lv_obj_t *status = create_label(row, packet_entry_status(entry), accent_color);
    label_set_dot_width(status, 38);
    obj_align_if(status, LV_ALIGN_TOP_LEFT, 8, 0);
    lv_obj_t *kind = create_label(row, entry->kind[0] ? entry->kind : "packet", 0xF4F7FB);
    label_set_dot_width(kind, 128);
    obj_align_if(kind, LV_ALIGN_TOP_LEFT, 54, 0);
    lv_obj_t *meta = create_label(row, "", 0x8EA0AE);
    label_set_fmt(meta, "#%lu  rssi %d  snr %s",
                  (unsigned long)entry->seq, entry->rssi_dbm, snr);
    label_set_dot_width(meta, 190);
    obj_align_if(meta, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_t *note = create_label(row, entry->note[0] ? entry->note : "-", 0xE5EDF5);
    label_set_dot_width(note, 380);
    obj_align_if(note, LV_ALIGN_BOTTOM_LEFT, 8, 0);
}

static void render_route_row(lv_obj_t *parent, int y, const d1l_route_entry_t *entry)
{
    lv_obj_t *row = create_panel(parent, 18, y, 424, 48);
    if (!row) {
        return;
    }
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, open_route_detail_event_cb, LV_EVENT_CLICKED, (void *)entry);
    lv_obj_set_style_pad_all(row, 8, 0);
    lv_obj_t *label = create_label(row, entry->label[0] ? entry->label : entry->target, 0xA7F3D0);
    label_set_dot_width(label, 176);
    obj_align_if(label, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *meta = create_label(row, "", 0x8EA0AE);
    label_set_fmt(meta, "#%lu %s %s", (unsigned long)entry->seq, entry->direction, entry->route);
    label_set_dot_width(meta, 190);
    obj_align_if(meta, LV_ALIGN_TOP_RIGHT, 0, 0);
    const int snr_abs = entry->last_snr_tenths < 0 ? -entry->last_snr_tenths : entry->last_snr_tenths;
    lv_obj_t *signal = create_label(row, "", 0xE5EDF5);
    label_set_fmt(signal, "%s  rssi %d  snr %s%d.%d  hops %u",
                  entry->kind, entry->last_rssi_dbm,
                  entry->last_snr_tenths < 0 ? "-" : "", snr_abs / 10, snr_abs % 10,
                  entry->path_hops);
    label_set_dot_width(signal, 392);
    obj_align_if(signal, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static void render_node_row(lv_obj_t *parent, int y, const d1l_node_view_t *view)
{
    if (!view) {
        return;
    }
    const d1l_node_entry_t *entry = &view->node;
    lv_obj_t *row = create_panel(parent, 18, y, 424, 56);
    if (!row) {
        return;
    }
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, open_node_detail_event_cb, LV_EVENT_CLICKED, (void *)view);
    lv_obj_set_style_pad_all(row, 8, 0);
    lv_obj_t *name = create_label(row,
                                  view->display_name[0] ? view->display_name :
                                  (entry->name[0] ? entry->name : entry->fingerprint),
                                  0xF4F7FB);
    label_set_dot_width(name, 240);
    obj_align_if(name, LV_ALIGN_TOP_LEFT, 0, 0);
    render_node_role_badge(row, view->role, node_view_can_dm(view) ? 278 : 336, 0,
                           node_view_can_dm(view) ? 60 : 68);
    if (node_view_can_dm(view)) {
        create_button(row, "DM", 350, -1, 52, 34, open_node_dm_event_cb, (void *)view);
    }
    lv_obj_t *meta = create_label(row, "", 0x8EA0AE);
    const int snr_abs = entry->snr_tenths < 0 ? -entry->snr_tenths : entry->snr_tenths;
    label_set_fmt(meta, "%.8s  %s  %s  rssi %d  snr %s%d.%d",
                  entry->fingerprint, view->keyed ? "key" : "no key",
                  view->reachable ? "reachable" : "quiet",
                  entry->rssi_dbm,
                  entry->snr_tenths < 0 ? "-" : "", snr_abs / 10, snr_abs % 10);
    label_set_dot_width(meta, 392);
    obj_align_if(meta, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static void render_contact_row(lv_obj_t *parent, int y, const d1l_contact_entry_t *entry)
{
    lv_obj_t *row = create_panel(parent, 18, y, 424, 48);
    if (!row) {
        return;
    }
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, open_contact_detail_event_cb, LV_EVENT_CLICKED, (void *)entry);
    lv_obj_set_style_pad_all(row, 8, 0);
    lv_obj_t *alias = create_label(row, entry->alias, 0xF4F7FB);
    label_set_dot_width(alias, 166);
    obj_align_if(alias, LV_ALIGN_TOP_LEFT, 0, 0);
    if (contact_can_dm(entry)) {
        create_button(row, "DM", 350, -1, 48, 34, open_dm_compose_event_cb, (void *)entry);
    } else {
        lv_obj_t *type = create_label(row, entry->type[0] ? entry->type : "unknown",
                                      0xA7F3D0);
        obj_align_if(type, LV_ALIGN_TOP_RIGHT, 0, 0);
    }
    lv_obj_t *meta = create_label(row, "", 0x8EA0AE);
    label_set_fmt(meta, "%.8s  %s  %s  rssi %d", entry->fingerprint,
                  entry->public_key_hex[0] ? "key" : "no key",
                  entry->out_path_valid ? "path" : "flood", entry->last_rssi_dbm);
    label_set_dot_width(meta, 320);
    obj_align_if(meta, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static void update_compose_counter(void)
{
    if (!s_compose_counter) {
        return;
    }
    const char *text = s_compose_textarea ? lv_textarea_get_text(s_compose_textarea) : "";
    const size_t used = text ? strlen(text) : 0;
    label_set_fmt(s_compose_counter, "%u/%u",
                  (unsigned)used, (unsigned)D1L_MESSAGE_MAX_CHARS);
    lv_obj_set_style_text_color(s_compose_counter,
                                lv_color_hex(used >= D1L_MESSAGE_MAX_CHARS ? 0xFBBF24 : 0x8EA0AE),
                                0);
}

static void layout_compose_sheet_controls(void)
{
    if (s_compose_sheet) {
        lv_obj_set_size(s_compose_sheet, 480, 424);
        lv_obj_set_pos(s_compose_sheet, 0, 56);
        lv_obj_set_style_pad_all(s_compose_sheet, 0, 0);
        lv_obj_clear_flag(s_compose_sheet, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(s_compose_sheet, LV_DIR_NONE);
        lv_obj_set_scrollbar_mode(s_compose_sheet, LV_SCROLLBAR_MODE_OFF);
        lv_obj_scroll_to_y(s_compose_sheet, 0, LV_ANIM_OFF);
    }
    if (s_compose_textarea) {
        lv_obj_set_size(s_compose_textarea, 448, 78);
        lv_obj_set_pos(s_compose_textarea, 16, 58);
    }
    if (s_compose_counter) {
        lv_obj_set_width(s_compose_counter, 86);
        lv_obj_set_pos(s_compose_counter, 378, 140);
    }
    if (s_compose_keyboard) {
        lv_obj_set_size(s_compose_keyboard, 448, 258);
        lv_obj_set_align(s_compose_keyboard, LV_ALIGN_TOP_LEFT);
        lv_obj_set_pos(s_compose_keyboard, 16, 158);
    }
}

static void show_public_compose_sheet(const char *title, const char *placeholder)
{
    hide_sheet();
    hide_public_history_sheet();
    hide_public_search_sheet();
    hide_message_detail_sheet();
    hide_dm_thread_sheet();
    hide_radio_settings_sheet();
    hide_map_location_sheet();
    hide_map_options_sheet();
    hide_contact_detail_sheet();
    hide_contact_export_sheet();
    hide_route_detail_sheet();
    hide_route_trace_sheet();
    hide_packet_detail_sheet();
    hide_packet_search_sheet();
    hide_mesh_roles_sheet();
    s_compose_dm = false;
    memset(&s_compose_contact, 0, sizeof(s_compose_contact));
    if (s_compose_title) {
        lv_label_set_text(s_compose_title, title && title[0] ? title : "Compose Public");
    }
    if (s_compose_sheet) {
        show_modal(s_compose_sheet);
    }
    if (s_compose_textarea && s_compose_keyboard) {
        lv_textarea_set_text(s_compose_textarea, "");
        lv_textarea_set_placeholder_text(s_compose_textarea,
                                         placeholder && placeholder[0] ? placeholder : "Public message");
        lv_keyboard_set_textarea(s_compose_keyboard, s_compose_textarea);
        layout_compose_sheet_controls();
        update_compose_counter();
    }
    request_full_screen_repaint();
}

static void public_test_event_cb(lv_event_t *event)
{
    (void)event;
    show_toast("Public test", d1l_app_model_send_public_test());
}

static void open_compose_event_cb(lv_event_t *event)
{
    (void)event;
    show_public_compose_sheet("Compose Public", "Public message");
}

static void mark_messages_read_event_cb(lv_event_t *event)
{
    (void)event;
    esp_err_t ret = d1l_app_model_mark_messages_read();
    show_toast("Read", ret);
    if (ret == ESP_OK) {
        request_content_refresh();
    }
}

static void open_dm_compose_for_contact(const d1l_contact_entry_t *entry)
{
    if (!contact_can_dm(entry)) {
        show_toast("DM", ESP_ERR_INVALID_STATE);
        return;
    }
    d1l_contact_entry_t selected = *entry;
    hide_sheet();
    hide_public_history_sheet();
    hide_public_search_sheet();
    hide_message_detail_sheet();
    hide_dm_thread_sheet();
    hide_radio_settings_sheet();
    hide_contact_detail_sheet();
    hide_map_location_sheet();
    hide_map_options_sheet();
    hide_contact_export_sheet();
    hide_route_detail_sheet();
    hide_route_trace_sheet();
    hide_packet_detail_sheet();
    hide_packet_search_sheet();
    hide_mesh_roles_sheet();
    s_compose_dm = true;
    s_compose_contact = selected;
    if (s_compose_title) {
        char title[48];
        snprintf(title, sizeof(title), "DM %.32s",
                 selected.alias[0] ? selected.alias : selected.fingerprint);
        lv_label_set_text(s_compose_title, title);
    }
    if (s_compose_sheet) {
        show_modal(s_compose_sheet);
    }
    if (s_compose_textarea && s_compose_keyboard) {
        lv_textarea_set_text(s_compose_textarea, "");
        lv_textarea_set_placeholder_text(s_compose_textarea, "Direct message");
        lv_keyboard_set_textarea(s_compose_keyboard, s_compose_textarea);
        layout_compose_sheet_controls();
        update_compose_counter();
    }
    request_full_screen_repaint();
}

static void open_dm_compose_event_cb(lv_event_t *event)
{
    const d1l_contact_entry_t *entry = (const d1l_contact_entry_t *)lv_event_get_user_data(event);
    open_dm_compose_for_contact(entry);
}

static bool contact_from_node_view(const d1l_node_view_t *view, d1l_contact_entry_t *out_contact)
{
    if (!node_view_can_dm(view) || !out_contact) {
        return false;
    }
    if (d1l_app_model_find_contact(view->node.fingerprint, out_contact) == ESP_OK &&
        out_contact->public_key_hex[0] != '\0') {
        return true;
    }
    memset(out_contact, 0, sizeof(*out_contact));
    snprintf(out_contact->fingerprint, sizeof(out_contact->fingerprint), "%s", view->node.fingerprint);
    snprintf(out_contact->public_key_hex, sizeof(out_contact->public_key_hex), "%s",
             view->node.public_key_hex);
    snprintf(out_contact->alias, sizeof(out_contact->alias), "%s",
             view->display_name[0] ? view->display_name :
             (view->node.name[0] ? view->node.name : view->node.fingerprint));
    snprintf(out_contact->heard_name, sizeof(out_contact->heard_name), "%s",
             view->node.name[0] ? view->node.name : view->node.fingerprint);
    snprintf(out_contact->type, sizeof(out_contact->type), "%s",
             view->node.type[0] ? view->node.type : "node");
    out_contact->last_rssi_dbm = view->node.rssi_dbm;
    out_contact->last_snr_tenths = view->node.snr_tenths;
    out_contact->path_hash_bytes = view->node.path_hash_bytes;
    out_contact->path_hops = view->node.path_hops;
    return true;
}

static void node_detail_dm_event_cb(lv_event_t *event)
{
    (void)event;
    d1l_contact_entry_t contact = {0};
    if (!contact_from_node_view(&s_node_detail_node, &contact)) {
        show_toast("DM", ESP_ERR_INVALID_STATE);
        return;
    }
    open_dm_compose_for_contact(&contact);
}

static void open_node_dm_event_cb(lv_event_t *event)
{
    const d1l_node_view_t *view = (const d1l_node_view_t *)lv_event_get_user_data(event);
    d1l_contact_entry_t contact = {0};
    if (!contact_from_node_view(view, &contact)) {
        show_toast("DM", ESP_ERR_INVALID_STATE);
        return;
    }
    open_dm_compose_for_contact(&contact);
}

static void close_contact_detail_event_cb(lv_event_t *event)
{
    (void)event;
    hide_contact_detail_sheet();
}

static void contact_detail_dm_event_cb(lv_event_t *event)
{
    (void)event;
    open_dm_compose_for_contact(&s_contact_detail_contact);
}

static void close_contact_options_event_cb(lv_event_t *event)
{
    (void)event;
    hide_contact_options_sheet();
    show_contact_detail_sheet();
}

static void open_contact_options_event_cb(lv_event_t *event)
{
    (void)event;
    if (s_contact_detail_contact.fingerprint[0] == '\0') {
        show_toast("Contact", ESP_ERR_INVALID_STATE);
        return;
    }
    show_contact_options_sheet();
}

static void cancel_contact_forget_event_cb(lv_event_t *event)
{
    (void)event;
    hide_contact_forget_sheet();
    show_contact_options_sheet();
}

static void confirm_forget_contact_event_cb(lv_event_t *event)
{
    (void)event;
    if (s_contact_detail_contact.fingerprint[0] == '\0') {
        show_toast("Contact", ESP_ERR_INVALID_STATE);
        return;
    }
    d1l_contact_entry_t removed = {0};
    esp_err_t ret = d1l_app_model_delete_contact(s_contact_detail_contact.fingerprint, &removed);
    if (ret == ESP_OK) {
        hide_contact_detail_sheet();
        request_content_refresh();
    }
    show_toast("Forget contact", ret);
}

static void open_contact_forget_event_cb(lv_event_t *event)
{
    (void)event;
    if (s_contact_detail_contact.fingerprint[0] == '\0') {
        show_toast("Contact", ESP_ERR_INVALID_STATE);
        return;
    }
    render_contact_forget_sheet();
    if (s_contact_forget_sheet) {
        show_modal(s_contact_forget_sheet);
    }
}

static void close_contact_export_event_cb(lv_event_t *event)
{
    (void)event;
    hide_contact_export_sheet();
    show_contact_options_sheet();
}

static void render_contact_export_sheet(void)
{
    if (!s_contact_export_sheet) {
        return;
    }
    lv_obj_clean(s_contact_export_sheet);

    const d1l_contact_entry_t *entry = &s_contact_export_contact;
    lv_obj_t *title = create_label(s_contact_export_sheet, "Contact Export", 0xF4F7FB);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, 364);
    lv_obj_set_pos(title, 100, 10);

    create_button(s_contact_export_sheet, "Back", 12, 6, 72, 44,
                  close_contact_export_event_cb, NULL);

    lv_obj_t *subtitle = create_label(s_contact_export_sheet, "", 0x8EA0AE);
    label_set_fmt(subtitle, "MeshCore QR  %.16s  type %u", entry->fingerprint,
                  (unsigned)d1l_contact_store_meshcore_type_id(entry->type));
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_DOT);
    lv_obj_set_width(subtitle, 448);
    lv_obj_set_pos(subtitle, 16, 60);

    if (s_contact_export_uri[0] == '\0') {
        lv_obj_t *missing = create_label(s_contact_export_sheet,
                                         "No retained public key for QR export", 0xF87171);
        lv_obj_set_pos(missing, 16, 104);
        return;
    }

#if LV_USE_QRCODE
    lv_obj_t *qr = create_qrcode(s_contact_export_sheet, 166,
                                 lv_color_hex(0x02060A), lv_color_hex(0xF8FAFC),
                                 "contact export qr");
    if (!qr) {
        lv_obj_t *qr_error = create_label(s_contact_export_sheet, "QR allocation failed", 0xF87171);
        lv_obj_set_pos(qr_error, 24, 150);
        return;
    }
    lv_obj_set_pos(qr, 16, 92);
    lv_obj_set_style_border_width(qr, 5, 0);
    lv_obj_set_style_border_color(qr, lv_color_hex(0xF8FAFC), 0);
    if (lv_qrcode_update(qr, s_contact_export_uri, strlen(s_contact_export_uri)) != LV_RES_OK) {
        lv_obj_del(qr);
        lv_obj_t *qr_error = create_label(s_contact_export_sheet, "QR payload too long", 0xF87171);
        lv_obj_set_pos(qr_error, 24, 150);
    }
#else
    lv_obj_t *qr_disabled = create_label(s_contact_export_sheet, "QR renderer disabled in LVGL", 0xFBBF24);
    lv_obj_set_pos(qr_disabled, 24, 150);
#endif

    lv_obj_t *name = create_label(s_contact_export_sheet,
                                  entry->alias[0] ? entry->alias : entry->fingerprint,
                                  0xE5EDF5);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(name, 264);
    lv_obj_set_pos(name, 200, 100);

    lv_obj_t *key = create_label(s_contact_export_sheet, "public key retained", 0x5EEAD4);
    lv_obj_set_pos(key, 200, 134);

    lv_obj_t *uri_title = create_label(s_contact_export_sheet, "URI", 0x8EA0AE);
    lv_obj_set_pos(uri_title, 200, 168);
    lv_obj_t *uri = create_label(s_contact_export_sheet, s_contact_export_uri, 0x93C5FD);
    lv_label_set_long_mode(uri, LV_LABEL_LONG_DOT);
    lv_obj_set_width(uri, 264);
    lv_obj_set_pos(uri, 200, 194);

    lv_obj_t *note = create_label(s_contact_export_sheet,
                                  "Scan with a MeshCore client or copy from serial",
                                  0x8EA0AE);
    lv_label_set_long_mode(note, LV_LABEL_LONG_DOT);
    lv_obj_set_width(note, 448);
    lv_obj_set_pos(note, 16, 278);
}

static void contact_detail_export_event_cb(lv_event_t *event)
{
    (void)event;
    s_contact_export_contact = s_contact_detail_contact;
    esp_err_t ret = d1l_app_model_export_contact_uri(s_contact_detail_contact.fingerprint,
                                                     s_contact_export_uri,
                                                     sizeof(s_contact_export_uri));
    if (ret != ESP_OK) {
        show_toast("Export", ret);
        return;
    }
    hide_sheet();
    hide_public_history_sheet();
    hide_public_search_sheet();
    hide_dm_thread_sheet();
    hide_radio_settings_sheet();
    hide_compose_sheet();
    hide_route_detail_sheet();
    hide_route_trace_sheet();
    hide_packet_detail_sheet();
    hide_packet_search_sheet();
    hide_mesh_roles_sheet();
    render_contact_export_sheet();
    if (s_contact_export_sheet) {
        show_modal(s_contact_export_sheet);
    }
}

static void show_contact_detail_sheet(void)
{
    request_content_refresh();
    render_contact_detail_sheet();
    if (s_contact_detail_sheet) {
        show_modal(s_contact_detail_sheet);
    }
}

static void render_contact_options_sheet(void)
{
    if (!s_contact_options_sheet) {
        return;
    }
    lv_obj_clean(s_contact_options_sheet);

    const d1l_contact_entry_t *entry = &s_contact_detail_contact;
    create_button(s_contact_options_sheet, "Back", 12, 6, 72, 44,
                  close_contact_options_event_cb, NULL);
    lv_obj_t *title = create_label(s_contact_options_sheet, "Contact Options", 0xF4F7FB);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, 364);
    lv_obj_set_pos(title, 100, 8);

    lv_obj_t *subtitle = create_label(s_contact_options_sheet,
                                      entry->alias[0] ? entry->alias : entry->fingerprint,
                                      0x8EA0AE);
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_DOT);
    lv_obj_set_width(subtitle, 364);
    lv_obj_set_pos(subtitle, 100, 38);

    lv_obj_t *route = create_button(s_contact_options_sheet, "Route trace", 16, 64, 448, 48,
                                    open_route_trace_event_cb, NULL);
    style_contact_option_button(route, 0x93C5FD, "Trace path  >");
    lv_obj_t *rename = create_button(s_contact_options_sheet, "Rename", 16, 118, 448, 48,
                                     open_contact_edit_event_cb, NULL);
    style_contact_option_button(rename, 0x5EEAD4, "Change alias  >");
    lv_obj_t *favorite = create_button(s_contact_options_sheet,
                                       entry->favorite ? "Remove from favorites" : "Add to favorites",
                                       16, 172, 448, 48, open_contact_detail_event_cb,
                                       (void *)s_contact_action_favorite);
    style_contact_option_button(favorite, 0xFBBF24,
                                entry->favorite ? "On  >" : "Off  >");
    lv_obj_t *mute = create_button(s_contact_options_sheet,
                                   entry->muted ? "Unmute notifications" : "Mute notifications",
                                   16, 226, 448, 48, open_contact_detail_event_cb,
                                   (void *)s_contact_action_mute);
    style_contact_option_button(mute, 0xC4B5FD,
                                entry->muted ? "On  >" : "Off  >");
    if (contact_can_export(entry)) {
        lv_obj_t *export = create_button(s_contact_options_sheet, "Export QR", 16, 280,
                                         448, 48, contact_detail_export_event_cb, NULL);
        style_contact_option_button(export, 0xA7F3D0, "Share QR  >");
    } else {
        lv_obj_t *export = create_panel(s_contact_options_sheet, 16, 280, 448, 48);
        if (export) {
            lv_obj_set_style_pad_all(export, 0, 0);
            lv_obj_t *title = create_label(export, "Export unavailable", 0x8EA0AE);
            obj_align_if(title, LV_ALIGN_LEFT_MID, 12, 0);
            lv_obj_t *reason = create_label(export, "Missing key or role", 0x8EA0AE);
            obj_align_if(reason, LV_ALIGN_RIGHT_MID, -12, 0);
        }
    }
    lv_obj_t *forget = create_button(s_contact_options_sheet, "Forget contact", 16, 334, 448, 48,
                                     open_contact_forget_event_cb, NULL);
    style_danger_button(forget);
    style_contact_option_button(forget, 0xFCA5A5, "Requires confirmation  >");
}

static void show_contact_options_sheet(void)
{
    request_content_refresh();
    render_contact_options_sheet();
    if (s_contact_options_sheet) {
        show_modal(s_contact_options_sheet);
    }
}

static void render_contact_forget_sheet(void)
{
    if (!s_contact_forget_sheet) {
        return;
    }
    lv_obj_clean(s_contact_forget_sheet);

    create_button(s_contact_forget_sheet, "Back", 12, 6, 72, 44,
                  cancel_contact_forget_event_cb, NULL);
    lv_obj_t *title = create_label(s_contact_forget_sheet, "Forget Contact?", 0xF4F7FB);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, 364);
    lv_obj_set_pos(title, 100, 10);

    lv_obj_t *alias = create_label(s_contact_forget_sheet,
                                   s_contact_detail_contact.alias[0] ?
                                   s_contact_detail_contact.alias :
                                   s_contact_detail_contact.fingerprint,
                                   0xFCA5A5);
    lv_label_set_long_mode(alias, LV_LABEL_LONG_DOT);
    lv_obj_set_width(alias, 448);
    lv_obj_set_pos(alias, 16, 76);

    lv_obj_t *warning = create_panel(s_contact_forget_sheet, 16, 112, 448, 122);
    if (warning) {
        lv_obj_t *line1 = create_label(warning,
                                       "This removes the saved contact and its routing preferences.",
                                       0xF4F7FB);
        lv_label_set_long_mode(line1, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(line1, 424);
        lv_obj_set_pos(line1, 0, 2);
        lv_obj_t *line2 = create_label(warning,
                                       "Message history remains on this device.",
                                       0x8EA0AE);
        lv_label_set_long_mode(line2, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(line2, 424);
        lv_obj_set_pos(line2, 0, 62);
    }

    create_button(s_contact_forget_sheet, "Cancel", 16, 274, 448, 52,
                  cancel_contact_forget_event_cb, NULL);
    lv_obj_t *confirm = create_button(s_contact_forget_sheet, "Forget Contact", 16, 340, 448, 52,
                                      confirm_forget_contact_event_cb, NULL);
    style_danger_button(confirm);
}

static void close_contact_edit_event_cb(lv_event_t *event)
{
    (void)event;
    hide_contact_edit_sheet();
    show_contact_options_sheet();
}

static void save_contact_edit_event_cb(lv_event_t *event)
{
    (void)event;
    if (s_contact_detail_contact.fingerprint[0] == '\0' || !s_contact_edit_textarea) {
        show_toast("Contact", ESP_ERR_INVALID_STATE);
        return;
    }
    const char *alias = lv_textarea_get_text(s_contact_edit_textarea);
    d1l_contact_entry_t updated = {0};
    esp_err_t ret = d1l_app_model_rename_contact(s_contact_detail_contact.fingerprint,
                                                  alias, &updated);
    if (ret == ESP_OK) {
        s_contact_detail_contact = updated;
        hide_contact_edit_sheet();
        show_contact_options_sheet();
    }
    show_toast("Rename", ret);
}

static void contact_edit_keyboard_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_READY) {
        save_contact_edit_event_cb(event);
    } else if (code == LV_EVENT_CANCEL) {
        close_contact_edit_event_cb(event);
    }
}

static void open_contact_edit_event_cb(lv_event_t *event)
{
    (void)event;
    if (s_contact_detail_contact.fingerprint[0] == '\0') {
        show_toast("Contact", ESP_ERR_INVALID_STATE);
        return;
    }
    if (!s_contact_edit_sheet) {
        create_contact_edit_sheet(s_screen);
    }
    if (!s_contact_edit_sheet || !s_contact_edit_textarea || !s_contact_edit_keyboard) {
        show_toast("Contact", ESP_ERR_NO_MEM);
        return;
    }
    hide_sheet();
    hide_public_history_sheet();
    hide_public_search_sheet();
    hide_dm_thread_sheet();
    hide_radio_settings_sheet();
    hide_compose_sheet();
    hide_contact_export_sheet();
    hide_contact_edit_sheet();
    hide_route_detail_sheet();
    hide_route_trace_sheet();
    hide_packet_detail_sheet();
    hide_packet_search_sheet();
    hide_mesh_roles_sheet();
    d1l_ui_modal_hide(s_contact_options_sheet);
    d1l_ui_modal_hide(s_contact_forget_sheet);
    if (s_contact_detail_sheet) {
        d1l_ui_modal_hide(s_contact_detail_sheet);
    }
    if (s_contact_edit_title) {
        char title[48];
        snprintf(title, sizeof(title), "Rename %.32s",
                 s_contact_detail_contact.alias[0] ?
                 s_contact_detail_contact.alias : s_contact_detail_contact.fingerprint);
        lv_label_set_text(s_contact_edit_title, title);
    }
    if (s_contact_edit_textarea && s_contact_edit_keyboard) {
        lv_textarea_set_text(s_contact_edit_textarea,
                             s_contact_detail_contact.alias[0] ?
                             s_contact_detail_contact.alias : s_contact_detail_contact.fingerprint);
        lv_keyboard_set_textarea(s_contact_edit_keyboard, s_contact_edit_textarea);
    }
    show_modal(s_contact_edit_sheet);
}

static bool ui_route_trace_is_direct(const d1l_route_entry_t *entry)
{
    return entry && (entry->path_hops == 0 ||
                     strcmp(entry->route, "direct") == 0);
}

static size_t ui_route_trace_best_index(const d1l_route_entry_t *entries, size_t count)
{
    size_t best = 0;
    bool best_set = false;
    for (size_t i = 0; i < count; ++i) {
        const bool candidate_direct = ui_route_trace_is_direct(&entries[i]);
        const bool best_direct = best_set && ui_route_trace_is_direct(&entries[best]);
        if (!best_set ||
            (candidate_direct && !best_direct) ||
            (candidate_direct == best_direct && entries[i].confidence > entries[best].confidence) ||
            (candidate_direct == best_direct && entries[i].confidence == entries[best].confidence &&
             entries[i].seq > entries[best].seq)) {
            best = i;
            best_set = true;
        }
    }
    return best;
}

static void close_route_trace_event_cb(lv_event_t *event)
{
    (void)event;
    hide_route_trace_sheet();
    show_contact_options_sheet();
}

static void route_trace_probe_event_cb(lv_event_t *event)
{
    (void)event;
    if (s_route_trace_contact.fingerprint[0] == '\0') {
        show_toast("Trace ping", ESP_ERR_INVALID_STATE);
        return;
    }
    char token[D1L_MESSAGE_TEXT_LEN] = {0};
    esp_err_t ret = d1l_app_model_request_trace_probe(s_route_trace_contact.fingerprint,
                                                       token, sizeof(token));
    if (ret == ESP_OK) {
        show_toast_text("Trace ping queued", true);
    } else {
        show_toast("Trace ping", ret);
    }
}

static void render_route_trace_sheet(void)
{
    if (!s_route_trace_sheet) {
        return;
    }
    lv_obj_clean(s_route_trace_sheet);

    const d1l_contact_entry_t *contact = &s_route_trace_contact;
    const char *alias = contact->alias[0] ? contact->alias : contact->fingerprint;
    const size_t route_count =
        d1l_app_model_copy_route_trace(contact->fingerprint, s_route_trace_entries,
                                       D1L_ROUTE_STORE_CAPACITY);
    const size_t best = route_count ? ui_route_trace_best_index(s_route_trace_entries, route_count) : 0;

    create_button(s_route_trace_sheet, "Back", 12, 6, 72, 44,
                  close_route_trace_event_cb, NULL);
    lv_obj_t *title = create_label(s_route_trace_sheet, alias, 0xF4F7FB);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, 260);
    lv_obj_set_pos(title, 100, 10);

    create_button(s_route_trace_sheet, "Ping", 376, 6, 88, 44,
                  route_trace_probe_event_cb, NULL);

    lv_obj_t *meta = create_label(s_route_trace_sheet, "", 0x8EA0AE);
    label_set_fmt(meta, "Trace %.16s  rows %u/%u",
                  contact->fingerprint, (unsigned)route_count,
                  (unsigned)D1L_ROUTE_STORE_CAPACITY);
    lv_obj_set_pos(meta, 16, 60);

    lv_obj_t *contact_path = create_label(s_route_trace_sheet, "", 0x8EA0AE);
    label_set_fmt(contact_path, "%s  contact path %s hops %u",
                  contact->public_key_hex[0] ? "key retained" : "no key",
                  contact->out_path_valid ? "known" : "unknown",
                  contact->out_path_valid ? contact->path_hops : 0);
    lv_label_set_long_mode(contact_path, LV_LABEL_LONG_DOT);
    lv_obj_set_width(contact_path, 448);
    lv_obj_set_pos(contact_path, 16, 88);

    lv_obj_t *best_label = create_label(s_route_trace_sheet, "", route_count ? 0x5EEAD4 : 0xFBBF24);
    if (route_count) {
        label_set_fmt(best_label, "best %s  seq %lu  confidence %u  hops %u",
                      s_route_trace_entries[best].route,
                      (unsigned long)s_route_trace_entries[best].seq,
                      s_route_trace_entries[best].confidence,
                      s_route_trace_entries[best].path_hops);
    } else {
        label_set_fmt(best_label, "No retained route evidence yet");
    }
    lv_label_set_long_mode(best_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(best_label, 448);
    lv_obj_set_pos(best_label, 16, 116);

    lv_obj_t *list = create_object(s_route_trace_sheet, "route trace list");
    if (!list) {
        return;
    }
    lv_obj_set_size(list, 448, 200);
    lv_obj_set_pos(list, 16, 148);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x0B111B), 0);
    lv_obj_set_style_border_color(list, lv_color_hex(0x253447), 0);
    lv_obj_set_style_border_width(list, 1, 0);
    lv_obj_set_style_radius(list, 8, 0);
    lv_obj_set_style_pad_all(list, 8, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    int y = 0;
    for (size_t i = 0; i < route_count; ++i) {
        const d1l_route_entry_t *entry = &s_route_trace_entries[i];
        lv_obj_t *row = create_object(list, "route trace row");
        if (!row) {
            y += 58;
            continue;
        }
        lv_obj_set_size(row, 416, 50);
        lv_obj_set_pos(row, 0, y);
        lv_obj_set_style_bg_color(row, lv_color_hex(ui_route_trace_is_direct(entry) ? 0x132A32 : 0x182230), 0);
        lv_obj_set_style_border_color(row, lv_color_hex(0x334155), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *line1 = create_label(row, "", 0xF4F7FB);
        label_set_fmt(line1, "#%lu %s %s %s",
                      (unsigned long)entry->seq, entry->kind, entry->route, entry->direction);
        lv_label_set_long_mode(line1, LV_LABEL_LONG_DOT);
        lv_obj_set_width(line1, 386);
        lv_obj_set_pos(line1, 10, 6);

        const int snr_abs = entry->last_snr_tenths < 0 ? -entry->last_snr_tenths : entry->last_snr_tenths;
        lv_obj_t *line2 = create_label(row, "", 0x8EA0AE);
        label_set_fmt(line2, "rssi %d snr %s%d.%d hops %u conf %u seen %lu",
                      entry->last_rssi_dbm,
                      entry->last_snr_tenths < 0 ? "-" : "", snr_abs / 10, snr_abs % 10,
                      entry->path_hops, entry->confidence, (unsigned long)entry->seen_count);
        lv_label_set_long_mode(line2, LV_LABEL_LONG_DOT);
        lv_obj_set_width(line2, 386);
        lv_obj_set_pos(line2, 10, 28);
        y += 58;
    }

    lv_obj_t *note = create_label(s_route_trace_sheet,
                                  "DM-only trace probe; no Public RF",
                                  0x8EA0AE);
    lv_label_set_long_mode(note, LV_LABEL_LONG_DOT);
    lv_obj_set_width(note, 448);
    lv_obj_set_pos(note, 16, 360);
}

static void open_route_trace_event_cb(lv_event_t *event)
{
    (void)event;
    if (s_contact_detail_contact.fingerprint[0] == '\0') {
        show_toast("Trace", ESP_ERR_INVALID_STATE);
        return;
    }
    s_route_trace_contact = s_contact_detail_contact;
    hide_sheet();
    hide_public_history_sheet();
    hide_public_search_sheet();
    hide_dm_thread_sheet();
    hide_radio_settings_sheet();
    hide_compose_sheet();
    d1l_ui_modal_hide(s_contact_options_sheet);
    hide_contact_export_sheet();
    hide_route_detail_sheet();
    hide_packet_detail_sheet();
    hide_packet_search_sheet();
    hide_mesh_roles_sheet();
    render_route_trace_sheet();
    if (s_route_trace_sheet) {
        show_modal(s_route_trace_sheet);
    }
}

static void render_contact_detail_sheet(void)
{
    if (!s_contact_detail_sheet) {
        return;
    }
    lv_obj_clean(s_contact_detail_sheet);

    const d1l_contact_entry_t *entry = &s_contact_detail_contact;
    create_button(s_contact_detail_sheet, "Back", 12, 6, 72, 44,
                  close_contact_detail_event_cb, NULL);
    lv_obj_t *title = create_label(s_contact_detail_sheet,
                                   entry->alias[0] ? entry->alias : entry->fingerprint,
                                   0xF4F7FB);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, 364);
    lv_obj_set_pos(title, 100, 10);

    lv_obj_t *flags = create_label(s_contact_detail_sheet, "", 0x8EA0AE);
    label_set_fmt(flags, "%s  %s  %s", entry->type[0] ? entry->type : "node",
                  entry->favorite ? "favorite" : "normal",
                  entry->muted ? "muted" : "audible");
    lv_obj_set_pos(flags, 16, 64);

    lv_obj_t *fingerprint = create_label(s_contact_detail_sheet, "", 0xE5EDF5);
    label_set_fmt(fingerprint, "fp %.16s", entry->fingerprint);
    lv_obj_set_pos(fingerprint, 16, 98);

    lv_obj_t *key = create_label(s_contact_detail_sheet, "", 0x8EA0AE);
    label_set_fmt(key, "%s  route %s  hops %u",
                  entry->public_key_hex[0] ? "public key retained" : "no public key",
                  entry->out_path_valid ? "direct" : "flood",
                  entry->out_path_valid ? entry->path_hops : 0);
    lv_label_set_long_mode(key, LV_LABEL_LONG_DOT);
    lv_obj_set_width(key, 448);
    lv_obj_set_pos(key, 16, 132);

    const int snr_abs = entry->last_snr_tenths < 0 ? -entry->last_snr_tenths : entry->last_snr_tenths;
    lv_obj_t *signal = create_label(s_contact_detail_sheet, "", 0x8EA0AE);
    label_set_fmt(signal, "rssi %d  snr %s%d.%d  heard %.18s",
                  entry->last_rssi_dbm,
                  entry->last_snr_tenths < 0 ? "-" : "", snr_abs / 10, snr_abs % 10,
                  entry->heard_name[0] ? entry->heard_name : "-");
    lv_label_set_long_mode(signal, LV_LABEL_LONG_DOT);
    lv_obj_set_width(signal, 448);
    lv_obj_set_pos(signal, 16, 166);

    lv_obj_t *actions = create_label(s_contact_detail_sheet, "Actions", 0x5EEAD4);
    lv_obj_set_pos(actions, 16, 210);
    if (contact_can_dm(entry)) {
        create_button(s_contact_detail_sheet, "Message", 16, 238, 448, 52,
                      contact_detail_dm_event_cb, NULL);
    } else {
        lv_obj_t *unavailable = create_label(s_contact_detail_sheet,
                                             "Messaging unavailable for this role",
                                             0x8EA0AE);
        lv_obj_set_pos(unavailable, 16, 254);
    }
    create_button(s_contact_detail_sheet, "Contact options", 16, 304, 448, 52,
                  open_contact_options_event_cb, NULL);
}

static void update_contact_detail_flags(bool favorite, bool muted)
{
    d1l_contact_entry_t updated = {0};
    esp_err_t ret = d1l_app_model_set_contact_flags(s_contact_detail_contact.fingerprint,
                                                    favorite, muted, &updated);
    if (ret == ESP_OK) {
        s_contact_detail_contact = updated;
        request_content_refresh();
        show_contact_options_sheet();
    }
    show_toast("Contact", ret);
}

static void open_contact_detail_event_cb(lv_event_t *event)
{
    const void *user_data = lv_event_get_user_data(event);
    if (user_data == s_contact_action_favorite) {
        update_contact_detail_flags(!s_contact_detail_contact.favorite, s_contact_detail_contact.muted);
        return;
    }
    if (user_data == s_contact_action_mute) {
        update_contact_detail_flags(s_contact_detail_contact.favorite, !s_contact_detail_contact.muted);
        return;
    }

    const d1l_contact_entry_t *entry = (const d1l_contact_entry_t *)user_data;
    if (!entry || entry->fingerprint[0] == '\0') {
        show_toast("Contact", ESP_ERR_INVALID_STATE);
        return;
    }
    s_contact_detail_contact = *entry;
    hide_sheet();
    hide_public_history_sheet();
    hide_public_search_sheet();
    hide_dm_thread_sheet();
    hide_radio_settings_sheet();
    hide_compose_sheet();
    hide_route_detail_sheet();
    hide_route_trace_sheet();
    hide_packet_detail_sheet();
    hide_packet_search_sheet();
    hide_mesh_roles_sheet();
    hide_contact_export_sheet();
    hide_contact_edit_sheet();
    d1l_ui_modal_hide(s_contact_options_sheet);
    d1l_ui_modal_hide(s_contact_forget_sheet);
    hide_node_detail_sheet();
    render_contact_detail_sheet();
    if (s_contact_detail_sheet) {
        show_modal(s_contact_detail_sheet);
    }
}

static void close_node_detail_event_cb(lv_event_t *event)
{
    (void)event;
    const bool return_to_map = s_node_detail_returns_to_map;
    hide_node_detail_sheet();
    if (return_to_map && d1l_ui_navigation_active() == D1L_UI_TAB_MAP &&
        !d1l_ui_modal_has_active() && !s_lock_visible && !s_onboarding_visible &&
        map_interactive_touch_authorized() && !d1l_ui_map_viewport_reacquire()) {
        request_content_refresh();
    }
}

static void format_advert_coordinate(char *dest, size_t dest_len, int32_t value_e6)
{
    if (!dest || dest_len == 0U) {
        return;
    }
    const int64_t value = value_e6;
    const bool negative = value < 0;
    const uint64_t magnitude = (uint64_t)(negative ? -value : value);
    snprintf(dest, dest_len, "%s%llu.%06llu", negative ? "-" : "",
             (unsigned long long)(magnitude / 1000000ULL),
             (unsigned long long)(magnitude % 1000000ULL));
}

static void render_node_detail_sheet(void)
{
    if (!s_node_detail_sheet) {
        return;
    }
    lv_obj_clean(s_node_detail_sheet);

    const d1l_node_view_t *view = &s_node_detail_node;
    const d1l_node_entry_t *entry = &view->node;
    const char *name = view->display_name[0] ? view->display_name :
                       (entry->name[0] ? entry->name : entry->fingerprint);

    lv_obj_t *title = create_label(s_node_detail_sheet, "Node Detail", 0xF4F7FB);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, 160);
    lv_obj_set_pos(title, 8, 4);
    if (node_view_can_dm(view)) {
        create_button(s_node_detail_sheet, "DM", 208, 0, 54, 40,
                      node_detail_dm_event_cb, NULL);
    } else if (node_view_management_gated(view)) {
        lv_obj_t *gated = create_label(s_node_detail_sheet, "Manage locked", 0x8EA0AE);
        lv_label_set_long_mode(gated, LV_LABEL_LONG_DOT);
        lv_obj_set_width(gated, 124);
        lv_obj_set_pos(gated, 178, 12);
    }
    create_button(s_node_detail_sheet, "Close", 316, 0, 76, 40,
                  close_node_detail_event_cb, NULL);

    lv_obj_t *name_label = create_label(s_node_detail_sheet, name, 0xE5EDF5);
    lv_label_set_long_mode(name_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(name_label, 392);
    lv_obj_set_pos(name_label, 8, 48);

    render_node_role_badge(s_node_detail_sheet, view->role, 8, 78, 74);
    lv_obj_t *role = create_label(s_node_detail_sheet, "", node_role_color(view->role));
    label_set_fmt(role, "Role %s", node_role_display_label(view->role));
    lv_label_set_long_mode(role, LV_LABEL_LONG_DOT);
    lv_obj_set_width(role, 300);
    lv_obj_set_pos(role, 94, 82);

    lv_obj_t *fingerprint = create_label(s_node_detail_sheet, "", 0xE5EDF5);
    label_set_fmt(fingerprint, "Fingerprint %.16s", entry->fingerprint[0] ? entry->fingerprint : "-");
    lv_label_set_long_mode(fingerprint, LV_LABEL_LONG_DOT);
    lv_obj_set_width(fingerprint, 392);
    lv_obj_set_pos(fingerprint, 8, 116);

    lv_obj_t *key = create_label(s_node_detail_sheet, "", 0x8EA0AE);
    label_set_fmt(key, "Public key %s  %s  %s",
                  view->keyed ? "retained" : "missing",
                  view->favorite ? "favorite" : "normal",
                  view->muted ? "muted" : "audible");
    lv_label_set_long_mode(key, LV_LABEL_LONG_DOT);
    lv_obj_set_width(key, 392);
    lv_obj_set_pos(key, 8, 150);

    const int snr_abs = entry->snr_tenths < 0 ? -entry->snr_tenths : entry->snr_tenths;
    lv_obj_t *signal = create_label(s_node_detail_sheet, "", 0x8EA0AE);
    label_set_fmt(signal, "Signal rssi %d  snr %s%d.%d  %s",
                  entry->rssi_dbm,
                  entry->snr_tenths < 0 ? "-" : "", snr_abs / 10, snr_abs % 10,
                  view->reachable ? "reachable" : "quiet");
    lv_label_set_long_mode(signal, LV_LABEL_LONG_DOT);
    lv_obj_set_width(signal, 392);
    lv_obj_set_pos(signal, 8, 184);

    lv_obj_t *path = create_label(s_node_detail_sheet, "", 0x8EA0AE);
    label_set_fmt(path, "Path hops %u  hash %u byte  advert %lums",
                  entry->path_hops, entry->path_hash_bytes,
                  (unsigned long)entry->advert_timestamp);
    lv_label_set_long_mode(path, LV_LABEL_LONG_DOT);
    lv_obj_set_width(path, 392);
    lv_obj_set_pos(path, 8, 218);

    lv_obj_t *location = create_label(s_node_detail_sheet, "", 0x93C5FD);
    if (entry->location_valid) {
        char latitude[16];
        char longitude[16];
        format_advert_coordinate(latitude, sizeof(latitude), entry->lat_e6);
        format_advert_coordinate(longitude, sizeof(longitude), entry->lon_e6);
        label_set_fmt(location, "Advert location %s, %s", latitude, longitude);
    } else {
        lv_label_set_text(location, "Advert location not provided");
    }
    lv_label_set_long_mode(location, LV_LABEL_LONG_DOT);
    lv_obj_set_width(location, 392);
    lv_obj_set_pos(location, 8, 242);

    lv_obj_t *heard = create_label(s_node_detail_sheet, "", 0x8EA0AE);
    label_set_fmt(heard, "Last heard %lums  first %lums  count %lu",
                  (unsigned long)entry->last_heard_ms,
                  (unsigned long)entry->first_heard_ms,
                  (unsigned long)entry->heard_count);
    lv_label_set_long_mode(heard, LV_LABEL_LONG_DOT);
    lv_obj_set_width(heard, 392);
    lv_obj_set_pos(heard, 8, 266);
}

static void show_node_detail_view(const d1l_node_view_t *view, bool return_to_map)
{
    if (!view || view->node.fingerprint[0] == '\0') {
        show_toast("Node", ESP_ERR_INVALID_STATE);
        return;
    }
    hide_sheet();
    hide_public_history_sheet();
    hide_public_search_sheet();
    hide_compose_sheet();
    hide_dm_thread_sheet();
    hide_radio_settings_sheet();
    hide_contact_detail_sheet();
    hide_contact_export_sheet();
    hide_route_detail_sheet();
    hide_route_trace_sheet();
    hide_packet_detail_sheet();
    hide_packet_search_sheet();
    hide_mesh_roles_sheet();
    s_node_detail_node = *view;
    s_node_detail_returns_to_map = return_to_map;
    render_node_detail_sheet();
    if (s_node_detail_sheet) {
        show_modal(s_node_detail_sheet);
    }
}

static void open_node_detail_event_cb(lv_event_t *event)
{
    show_node_detail_view((const d1l_node_view_t *)lv_event_get_user_data(event), false);
}

static void open_map_node_detail(const char *fingerprint)
{
    if (!fingerprint || fingerprint[0] == '\0') {
        show_toast("Node", ESP_ERR_INVALID_ARG);
        return;
    }
    const d1l_node_query_t query = {
        .filter = D1L_NODE_FILTER_ALL,
        .sort = D1L_NODE_SORT_LAST_HEARD,
        .text = fingerprint,
    };
    const size_t count = d1l_app_model_query_nodes(
        &query, s_node_rows, D1L_NODE_STORE_CAPACITY);
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(s_node_rows[i].node.fingerprint, fingerprint) == 0) {
            show_node_detail_view(&s_node_rows[i], true);
            return;
        }
    }
    show_toast("Node", ESP_ERR_NOT_FOUND);
}

static void close_route_detail_event_cb(lv_event_t *event)
{
    (void)event;
    hide_route_detail_sheet();
}

static void render_route_detail_sheet(void)
{
    if (!s_route_detail_sheet) {
        return;
    }
    lv_obj_clean(s_route_detail_sheet);

    const d1l_route_entry_t *entry = &s_route_detail_route;
    lv_obj_t *title = create_label(s_route_detail_sheet,
                                   entry->label[0] ? entry->label : entry->target,
                                   0xF4F7FB);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, 260);
    lv_obj_set_pos(title, 8, 4);

    create_button(s_route_detail_sheet, "Close", 316, 0, 76, 40, close_route_detail_event_cb, NULL);

    lv_obj_t *target = create_label(s_route_detail_sheet, "", 0xE5EDF5);
    label_set_fmt(target, "target %.16s", entry->target[0] ? entry->target : "-");
    lv_label_set_long_mode(target, LV_LABEL_LONG_DOT);
    lv_obj_set_width(target, 392);
    lv_obj_set_pos(target, 8, 48);

    lv_obj_t *route = create_label(s_route_detail_sheet, "", 0x8EA0AE);
    label_set_fmt(route, "#%lu  %s  %s  %s",
                  (unsigned long)entry->seq,
                  entry->kind[0] ? entry->kind : "packet",
                  entry->route[0] ? entry->route : "unknown",
                  entry->direction[0] ? entry->direction : "-");
    lv_label_set_long_mode(route, LV_LABEL_LONG_DOT);
    lv_obj_set_width(route, 392);
    lv_obj_set_pos(route, 8, 82);

    const int snr_abs = entry->last_snr_tenths < 0 ? -entry->last_snr_tenths : entry->last_snr_tenths;
    lv_obj_t *signal = create_label(s_route_detail_sheet, "", 0x8EA0AE);
    label_set_fmt(signal, "rssi %d  snr %s%d.%d  payload %u",
                  entry->last_rssi_dbm,
                  entry->last_snr_tenths < 0 ? "-" : "", snr_abs / 10, snr_abs % 10,
                  entry->payload_len);
    lv_label_set_long_mode(signal, LV_LABEL_LONG_DOT);
    lv_obj_set_width(signal, 392);
    lv_obj_set_pos(signal, 8, 116);

    lv_obj_t *path = create_label(s_route_detail_sheet, "", 0x8EA0AE);
    label_set_fmt(path, "hash %u byte  hops %u  confidence %u",
                  entry->path_hash_bytes, entry->path_hops, entry->confidence);
    lv_label_set_long_mode(path, LV_LABEL_LONG_DOT);
    lv_obj_set_width(path, 392);
    lv_obj_set_pos(path, 8, 150);

    lv_obj_t *seen = create_label(s_route_detail_sheet, "", 0x8EA0AE);
    label_set_fmt(seen, "seen %lu  first %lums  last %lums",
                  (unsigned long)entry->seen_count,
                  (unsigned long)entry->first_seen_ms,
                  (unsigned long)entry->last_seen_ms);
    lv_label_set_long_mode(seen, LV_LABEL_LONG_DOT);
    lv_obj_set_width(seen, 392);
    lv_obj_set_pos(seen, 8, 184);
}

static void open_route_detail_event_cb(lv_event_t *event)
{
    const d1l_route_entry_t *entry = (const d1l_route_entry_t *)lv_event_get_user_data(event);
    if (!entry || entry->seq == 0) {
        show_toast("Route", ESP_ERR_INVALID_STATE);
        return;
    }
    s_route_detail_route = *entry;
    hide_sheet();
    hide_public_history_sheet();
    hide_public_search_sheet();
    hide_compose_sheet();
    hide_message_detail_sheet();
    hide_dm_thread_sheet();
    hide_radio_settings_sheet();
    hide_contact_detail_sheet();
    hide_contact_export_sheet();
    hide_packet_detail_sheet();
    hide_packet_search_sheet();
    hide_mesh_roles_sheet();
    render_route_detail_sheet();
    if (s_route_detail_sheet) {
        show_modal(s_route_detail_sheet);
    }
}

static void close_message_detail_event_cb(lv_event_t *event)
{
    (void)event;
    hide_message_detail_sheet();
}

static const char *message_delivery_label(const d1l_message_entry_t *entry)
{
    if (!entry) {
        return "unknown";
    }
    if (entry->direction[0] == 't') {
        return entry->delivered ? "delivered" : "queued";
    }
    return entry->seq > s_snapshot.last_public_read_seq ? "new" : "received";
}

static void reply_message_detail_event_cb(lv_event_t *event)
{
    (void)event;
    const d1l_message_entry_t entry = s_message_detail_message;
    if (entry.seq == 0 || entry.author[0] == '\0') {
        show_toast("Reply", ESP_ERR_INVALID_STATE);
        return;
    }
    char title[48];
    char placeholder[64];
    snprintf(title, sizeof(title), "Reply %.32s", entry.author);
    snprintf(placeholder, sizeof(placeholder), "Reply to %.48s", entry.author);
    show_public_compose_sheet(title, placeholder);
}

static lv_obj_t *create_nested_page_body(lv_obj_t *page, const char *name)
{
    lv_obj_t *body = create_object(page, name);
    if (!body) {
        return NULL;
    }
    lv_obj_set_size(body, 448, 292);
    lv_obj_set_pos(body, 16, 60);
    lv_obj_set_style_radius(body, 8, 0);
    lv_obj_set_style_bg_color(body, lv_color_hex(0x071018), 0);
    lv_obj_set_style_border_color(body, lv_color_hex(0x263241), 0);
    lv_obj_set_style_border_width(body, 1, 0);
    lv_obj_set_style_pad_all(body, 12, 0);
    lv_obj_set_style_pad_row(body, 10, 0);
    lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    return body;
}

static lv_obj_t *create_nested_page_label(lv_obj_t *parent, const char *text,
                                           uint32_t color, bool wrap)
{
    lv_obj_t *label = create_label(parent, text, color);
    if (!label) {
        return NULL;
    }
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_height(label, LV_SIZE_CONTENT);
    lv_label_set_long_mode(label, wrap ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_DOT);
    return label;
}

static void render_message_detail_sheet(void)
{
    if (!s_message_detail_sheet) {
        return;
    }
    lv_obj_clean(s_message_detail_sheet);

    const d1l_message_entry_t *entry = &s_message_detail_message;
    create_button(s_message_detail_sheet, "Back", 12, 6, 72, 44,
                  close_message_detail_event_cb, NULL);
    lv_obj_t *title = create_label(s_message_detail_sheet, "Message Detail", 0xF4F7FB);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, 364);
    lv_obj_set_pos(title, 100, 10);

    lv_obj_t *body = create_nested_page_body(s_message_detail_sheet, "message detail body");
    if (!body) {
        return;
    }

    create_nested_page_label(body, "Sender", 0x5EEAD4, false);
    lv_obj_t *sender = create_nested_page_label(body, "", 0xE5EDF5, false);
    if (sender) {
        label_set_fmt(sender, "%s  %s",
                      entry->author[0] ? entry->author : "unknown",
                      message_delivery_label(entry));
    }

    create_nested_page_label(body, "Message", 0x5EEAD4, false);
    create_nested_page_label(body, entry->text[0] ? entry->text : "-", 0xF4F7FB, true);

    lv_obj_t *technical = create_button(body,
                                        s_message_detail_advanced ?
                                        "Hide technical details" : "Technical details",
                                        0, 0, 424, 48, message_detail_mode_event_cb, NULL);
    if (technical) {
        lv_obj_set_width(technical, LV_PCT(100));
    }

    if (s_message_detail_advanced) {
        const int snr_abs = entry->snr_tenths < 0 ? -entry->snr_tenths : entry->snr_tenths;
        lv_obj_t *signal = create_nested_page_label(body, "", 0x8EA0AE, true);
        if (signal) {
            label_set_fmt(signal, "Signal  rssi %d  snr %s%d.%d",
                          entry->rssi_dbm,
                          entry->snr_tenths < 0 ? "-" : "", snr_abs / 10, snr_abs % 10);
        }
        lv_obj_t *path = create_nested_page_label(body, "", 0x8EA0AE, true);
        if (path) {
            label_set_fmt(path, "Path  %u hop%s",
                          entry->path_hops, entry->path_hops == 1 ? "" : "s");
        }
        lv_obj_t *seq = create_nested_page_label(body, "", 0x8EA0AE, true);
        if (seq) {
            label_set_fmt(seq, "Sequence  %lu  uptime %lums  direction %s",
                          (unsigned long)entry->seq,
                          (unsigned long)entry->uptime_ms,
                          entry->direction[0] ? entry->direction : "?");
        }
        lv_obj_t *hash = create_nested_page_label(body, "", 0x8EA0AE, true);
        if (hash) {
            label_set_fmt(hash, "Path hash  %u byte  delivered %s",
                          entry->path_hash_bytes, entry->delivered ? "yes" : "no");
        }
    }

    if (entry->direction[0] != 't') {
        create_button(s_message_detail_sheet, "Reply", 16, 360, 448, 52,
                      reply_message_detail_event_cb, NULL);
    }
}

static void open_message_detail_event_cb(lv_event_t *event)
{
    const d1l_message_entry_t *entry = (const d1l_message_entry_t *)lv_event_get_user_data(event);
    if (!entry || entry->seq == 0) {
        show_toast("Message", ESP_ERR_INVALID_STATE);
        return;
    }
    s_message_detail_message = *entry;
    s_message_detail_advanced = false;
    hide_sheet();
    hide_public_history_sheet();
    hide_public_search_sheet();
    hide_compose_sheet();
    hide_dm_thread_sheet();
    hide_radio_settings_sheet();
    hide_map_location_sheet();
    hide_map_options_sheet();
    hide_contact_detail_sheet();
    hide_contact_export_sheet();
    hide_route_detail_sheet();
    hide_route_trace_sheet();
    hide_packet_detail_sheet();
    hide_packet_search_sheet();
    hide_mesh_roles_sheet();
    hide_node_detail_sheet();
    render_message_detail_sheet();
    if (s_message_detail_sheet) {
        show_modal(s_message_detail_sheet);
    }
}

static void message_detail_mode_event_cb(lv_event_t *event)
{
    (void)event;
    s_message_detail_advanced = !s_message_detail_advanced;
    render_message_detail_sheet();
}

static void close_packet_detail_event_cb(lv_event_t *event)
{
    (void)event;
    hide_packet_detail_sheet();
}

static void packet_detail_mode_event_cb(lv_event_t *event)
{
    (void)event;
    s_packet_detail_advanced = !s_packet_detail_advanced;
    render_packet_detail_sheet();
}

static void render_packet_detail_sheet(void)
{
    if (!s_packet_detail_sheet) {
        return;
    }
    lv_obj_clean(s_packet_detail_sheet);

    const d1l_packet_log_entry_t *entry = &s_packet_detail_packet;
    const uint32_t accent_color = packet_entry_color(entry);
    char snr[16];
    format_snr_tenths(snr, sizeof(snr), entry->snr_tenths);

    lv_obj_t *title = create_label(s_packet_detail_sheet, "Packet Detail", 0xF4F7FB);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, 190);
    lv_obj_set_pos(title, 8, 4);

    create_button(s_packet_detail_sheet,
                  s_packet_detail_advanced ? "Normal" : "Advanced",
                  218, 0, 92, 40, packet_detail_mode_event_cb, NULL);
    create_button(s_packet_detail_sheet, "Close", 320, 0, 76, 40, close_packet_detail_event_cb, NULL);

    lv_obj_t *kind_title = create_label(s_packet_detail_sheet, "Kind", 0x8EA0AE);
    lv_obj_set_pos(kind_title, 8, 54);
    lv_obj_t *kind = create_label(s_packet_detail_sheet, "", accent_color);
    label_set_fmt(kind, "%s  %s  #%lu",
                  entry->kind[0] ? entry->kind : "packet",
                  packet_entry_status(entry),
                  (unsigned long)entry->seq);
    label_set_dot_width(kind, 392);
    lv_obj_set_pos(kind, 8, 76);

    lv_obj_t *signal_title = create_label(s_packet_detail_sheet, "Signal", 0x8EA0AE);
    lv_obj_set_pos(signal_title, 8, 108);
    lv_obj_t *signal = create_label(s_packet_detail_sheet, "", 0xE5EDF5);
    label_set_fmt(signal, "direction %s  rssi %d  snr %s",
                  entry->direction[0] ? entry->direction : "-",
                  entry->rssi_dbm, snr);
    label_set_dot_width(signal, 392);
    lv_obj_set_pos(signal, 8, 130);

    lv_obj_t *payload_title = create_label(s_packet_detail_sheet, "Payload", 0x8EA0AE);
    lv_obj_set_pos(payload_title, 8, 162);
    lv_obj_t *payload = create_label(s_packet_detail_sheet, "", 0xE5EDF5);
    label_set_fmt(payload, "%u bytes  raw %u%s",
                  entry->payload_len,
                  entry->raw_len,
                  entry->raw_truncated ? " truncated" : "");
    label_set_dot_width(payload, 392);
    lv_obj_set_pos(payload, 8, 184);

    lv_obj_t *note = create_label(s_packet_detail_sheet,
                                  entry->note[0] ? entry->note : "No packet note",
                                  0xF4F7FB);
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(note, 392);
    lv_obj_set_pos(note, 8, 216);

    if (!s_packet_detail_advanced) {
        lv_obj_t *hint = create_label(s_packet_detail_sheet, "Advanced shows path and raw hex", 0x8EA0AE);
        lv_label_set_long_mode(hint, LV_LABEL_LONG_DOT);
        lv_obj_set_width(hint, 392);
        lv_obj_set_pos(hint, 8, 252);
        return;
    }

    lv_obj_t *path_title = create_label(s_packet_detail_sheet, "Path", 0x8EA0AE);
    lv_obj_set_pos(path_title, 8, 252);
    lv_obj_t *path = create_label(s_packet_detail_sheet, "", 0xE5EDF5);
    label_set_fmt(path, "hash %u byte  hops %u  uptime %lums",
                  entry->path_hash_bytes, entry->path_hops, (unsigned long)entry->uptime_ms);
    lv_obj_set_width(path, 392);
    lv_obj_set_pos(path, 8, 274);

    lv_obj_t *raw_title = create_label(s_packet_detail_sheet, "Raw Hex", 0x8EA0AE);
    lv_obj_set_pos(raw_title, 8, 306);

    lv_obj_t *raw = create_label(s_packet_detail_sheet, entry->raw_hex[0] ? entry->raw_hex : "-", 0x93C5FD);
    lv_label_set_long_mode(raw, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(raw, 392);
    lv_obj_set_pos(raw, 8, 328);
}

static void open_packet_detail_event_cb(lv_event_t *event)
{
    const d1l_packet_log_entry_t *entry = (const d1l_packet_log_entry_t *)lv_event_get_user_data(event);
    if (!entry || entry->seq == 0) {
        show_toast("Packet", ESP_ERR_INVALID_STATE);
        return;
    }
    s_packet_detail_packet = *entry;
    s_packet_detail_advanced = false;
    hide_sheet();
    hide_public_history_sheet();
    hide_public_search_sheet();
    hide_compose_sheet();
    hide_message_detail_sheet();
    hide_dm_thread_sheet();
    hide_radio_settings_sheet();
    hide_contact_detail_sheet();
    hide_contact_export_sheet();
    hide_route_detail_sheet();
    hide_route_trace_sheet();
    hide_packet_search_sheet();
    hide_mesh_roles_sheet();
    render_packet_detail_sheet();
    if (s_packet_detail_sheet) {
        show_modal(s_packet_detail_sheet);
    }
}

static void packet_filter_event_cb(lv_event_t *event)
{
    s_packet_filter_mode = (d1l_packet_filter_mode_t)(uintptr_t)lv_event_get_user_data(event);
    s_packet_row_limit = D1L_PACKET_UI_INITIAL_ROWS;
    s_packet_skip_newest = 0;
    s_packets_paused = false;
    request_content_refresh();
}

static void packet_pause_event_cb(lv_event_t *event)
{
    (void)event;
    if (s_packets_paused) {
        s_packets_paused = false;
    } else {
        size_t total_matches = 0;
        bool sd_used = false;
        s_packet_filtered_count = d1l_packet_log_query_page(s_packet_filtered_packets,
                                                            s_packet_row_limit,
                                                            s_packet_skip_newest,
                                                            packet_filter_direction(),
                                                            packet_filter_kind(),
                                                            s_packet_search_text,
                                                            &total_matches, &sd_used);
        s_packet_total_matches = total_matches;
        s_packet_sd_history_page = sd_used;
        s_packets_paused = true;
    }
    request_content_refresh();
}

static void packet_load_older_event_cb(lv_event_t *event)
{
    (void)event;
    if (s_packet_filtered_count > 0 &&
        s_packet_total_matches > s_packet_skip_newest + s_packet_filtered_count) {
        s_packet_skip_newest += s_packet_filtered_count;
    }
    s_packets_paused = false;
    request_content_refresh();
}

static void packet_load_newer_event_cb(lv_event_t *event)
{
    (void)event;
    if (s_packet_skip_newest > D1L_PACKET_UI_LOAD_OLDER_STEP) {
        s_packet_skip_newest -= D1L_PACKET_UI_LOAD_OLDER_STEP;
    } else {
        s_packet_skip_newest = 0;
    }
    s_packets_paused = false;
    request_content_refresh();
}

static void close_packet_search_event_cb(lv_event_t *event)
{
    (void)event;
    hide_packet_search_sheet();
}

static void clear_packet_search_event_cb(lv_event_t *event)
{
    (void)event;
    s_packet_search_text[0] = '\0';
    if (s_packet_search_textarea) {
        lv_textarea_set_text(s_packet_search_textarea, "");
    }
    s_packets_paused = false;
    s_packet_row_limit = D1L_PACKET_UI_INITIAL_ROWS;
    s_packet_skip_newest = 0;
    hide_packet_search_sheet();
    request_content_refresh();
}

static void apply_packet_search_event_cb(lv_event_t *event)
{
    (void)event;
    s_packet_search_text[0] = '\0';
    if (s_packet_search_textarea) {
        const char *text = lv_textarea_get_text(s_packet_search_textarea);
        if (text) {
            snprintf(s_packet_search_text, sizeof(s_packet_search_text), "%s", text);
        }
    }
    s_packets_paused = false;
    s_packet_row_limit = D1L_PACKET_UI_INITIAL_ROWS;
    s_packet_skip_newest = 0;
    hide_packet_search_sheet();
    request_content_refresh();
}

static void packet_search_keyboard_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_READY) {
        apply_packet_search_event_cb(event);
    } else if (code == LV_EVENT_CANCEL) {
        hide_packet_search_sheet();
    }
}

static void open_packet_search_event_cb(lv_event_t *event)
{
    (void)event;
    if (!s_packet_search_sheet) {
        return;
    }
    hide_sheet();
    hide_public_history_sheet();
    hide_public_search_sheet();
    hide_compose_sheet();
    hide_message_detail_sheet();
    hide_dm_thread_sheet();
    hide_radio_settings_sheet();
    hide_contact_detail_sheet();
    hide_contact_export_sheet();
    hide_route_detail_sheet();
    hide_route_trace_sheet();
    hide_packet_detail_sheet();
    hide_mesh_roles_sheet();
    if (s_packet_search_textarea && s_packet_search_keyboard) {
        lv_textarea_set_text(s_packet_search_textarea, s_packet_search_text);
        lv_keyboard_set_textarea(s_packet_search_keyboard, s_packet_search_textarea);
    }
    show_modal(s_packet_search_sheet);
}

static void close_mesh_roles_event_cb(lv_event_t *event)
{
    (void)event;
    hide_mesh_roles_sheet();
}

static void mesh_roles_subpage_back_event_cb(lv_event_t *event)
{
    (void)event;
    s_mesh_roles_page = D1L_MESH_ROLES_PAGE_ROOT;
    render_mesh_roles_sheet();
    show_modal(s_mesh_roles_sheet);
}

static void open_mesh_room_servers_event_cb(lv_event_t *event)
{
    (void)event;
    s_mesh_roles_page = D1L_MESH_ROLES_PAGE_ROOM_SERVERS;
    render_mesh_roles_sheet();
    show_modal(s_mesh_roles_sheet);
}

static void open_mesh_repeater_candidates_event_cb(lv_event_t *event)
{
    (void)event;
    s_mesh_roles_page = D1L_MESH_ROLES_PAGE_REPEATER_CANDIDATES;
    render_mesh_roles_sheet();
    show_modal(s_mesh_roles_sheet);
}

static void render_mesh_roles_header(const char *title, lv_event_cb_t back_cb)
{
    create_button(s_mesh_roles_sheet, "Back", 12, 8, 76, 44, back_cb, NULL);
    lv_obj_t *heading = create_label(s_mesh_roles_sheet, title, 0xF4F7FB);
    lv_obj_set_style_text_font(heading, &lv_font_montserrat_24, 0);
    lv_label_set_long_mode(heading, LV_LABEL_LONG_DOT);
    lv_obj_set_width(heading, 360);
    lv_obj_set_pos(heading, 104, 13);
}

static void render_room_server_role_row(lv_obj_t *parent, int y, const d1l_mesh_room_server_t *entry)
{
    char snr[16];
    lv_obj_t *row = create_panel(parent, 0, y, 424, 56);
    lv_obj_set_style_pad_all(row, 8, 0);
    lv_obj_t *name = create_label(row, entry->name[0] ? entry->name : entry->fingerprint, 0xA7F3D0);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(name, 406);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, 0);
    format_snr_tenths(snr, sizeof(snr), entry->snr_tenths);
    lv_obj_t *meta = create_label(row, "", 0xE5EDF5);
    label_set_fmt(meta, "%.12s  %d dBm / %s dB",
                  entry->fingerprint, entry->rssi_dbm, snr);
    lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
    lv_obj_set_width(meta, 406);
    lv_obj_align(meta, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static const char *friendly_repeater_route(const d1l_mesh_repeater_candidate_t *entry)
{
    const char *route = entry && entry->route[0] ? entry->route : "";
    if (strcmp(route, "heard_path") == 0) {
        return "heard path";
    }
    if (strcmp(route, "flood") == 0) {
        return "flood path";
    }
    if (strcmp(route, "direct") == 0) {
        return "direct path";
    }
    if (!route[0] || strcmp(route, "unknown") == 0 || strchr(route, '_')) {
        return "route evidence";
    }
    return route;
}

static void render_repeater_role_row(lv_obj_t *parent, int y,
                                     const d1l_mesh_repeater_candidate_t *entry)
{
    char snr[16];
    lv_obj_t *row = create_panel(parent, 0, y, 424, 56);
    lv_obj_set_style_pad_all(row, 8, 0);
    lv_obj_t *label = create_label(row, entry->label[0] ? entry->label : entry->target, 0xFBBF24);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, 406);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);
    format_snr_tenths(snr, sizeof(snr), entry->snr_tenths);
    lv_obj_t *meta = create_label(row, "", 0xE5EDF5);
    label_set_fmt(meta, "%d dBm / %s dB  %u hop%s via %.16s",
                  entry->rssi_dbm, snr, entry->path_hops,
                  entry->path_hops == 1U ? "" : "s",
                  friendly_repeater_route(entry));
    lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
    lv_obj_set_width(meta, 406);
    lv_obj_align(meta, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static lv_obj_t *create_mesh_roles_list(const char *title, uint32_t accent,
                                        size_t loaded_count, uint32_t total_count)
{
    const size_t visible_count = loaded_count < 4U ? loaded_count : 4U;
    if (total_count < loaded_count) {
        total_count = (uint32_t)loaded_count;
    }

    lv_obj_t *panel = create_panel(s_mesh_roles_sheet, 16, 64, 448, 352);
    if (!panel) {
        return NULL;
    }
    lv_obj_set_style_pad_all(panel, 0, 0);

    lv_obj_t *section = create_label(panel, title, accent);
    lv_obj_set_pos(section, 12, 10);
    lv_obj_t *showing = create_label(panel, "", 0x8EA0AE);
    label_set_fmt(showing, "showing %u/%u",
                  (unsigned)visible_count, (unsigned)loaded_count);
    lv_label_set_long_mode(showing, LV_LABEL_LONG_DOT);
    lv_obj_set_width(showing, 160);
    lv_obj_set_style_text_align(showing, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(showing, 276, 10);

    lv_obj_t *list = create_object(panel, "mesh roles bounded list");
    if (!list) {
        return NULL;
    }
    lv_obj_set_size(list, 424, 256);
    lv_obj_set_pos(list, 12, 44);
    lv_obj_set_style_radius(list, 0, 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t *footer = create_label(panel, "", 0x8EA0AE);
    if (total_count > loaded_count) {
        label_set_fmt(footer, "Newest %u of %lu loaded - scroll",
                      (unsigned)loaded_count, (unsigned long)total_count);
    } else if (loaded_count > visible_count) {
        label_set_fmt(footer, "Scroll for %u more",
                      (unsigned)(loaded_count - visible_count));
        lv_obj_set_style_text_color(footer, lv_color_hex(accent), 0);
    } else {
        lv_label_set_text(footer, "All shown");
    }
    lv_label_set_long_mode(footer, LV_LABEL_LONG_DOT);
    lv_obj_set_width(footer, 424);
    lv_obj_set_style_text_align(footer, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(footer, 12, 312);
    return list;
}

static void render_mesh_roles_root(void)
{
    char room_count[32];
    char repeater_count[32];
    uint32_t room_total = s_snapshot.signal_summary.room_server_count;
    uint32_t repeater_total = s_snapshot.signal_summary.repeater_candidate_count;
    if (room_total < s_snapshot.recent_room_count) {
        room_total = (uint32_t)s_snapshot.recent_room_count;
    }
    if (repeater_total < s_snapshot.recent_repeater_count) {
        repeater_total = (uint32_t)s_snapshot.recent_repeater_count;
    }
    render_mesh_roles_header("Mesh Roles", close_mesh_roles_event_cb);

    lv_obj_t *intro = create_label(s_mesh_roles_sheet,
                                   "Browse one role group at a time.", 0x8EA0AE);
    lv_label_set_long_mode(intro, LV_LABEL_LONG_DOT);
    lv_obj_set_width(intro, 360);
    lv_obj_set_pos(intro, 104, 36);

    snprintf(room_count, sizeof(room_count), "%lu found  >",
             (unsigned long)room_total);
    lv_obj_t *rooms = create_button(s_mesh_roles_sheet, "Rooms",
                                    16, 76, 448, 84,
                                    open_mesh_room_servers_event_cb, NULL);
    style_contact_option_button(rooms, 0xA7F3D0, room_count);

    snprintf(repeater_count, sizeof(repeater_count), "%lu found  >",
             (unsigned long)repeater_total);
    lv_obj_t *repeaters = create_button(s_mesh_roles_sheet, "Repeaters",
                                        16, 176, 448, 84,
                                        open_mesh_repeater_candidates_event_cb, NULL);
    style_contact_option_button(repeaters, 0xFBBF24, repeater_count);

    lv_obj_t *note = create_panel(s_mesh_roles_sheet, 16, 288, 448, 76);
    lv_obj_set_style_pad_all(note, 12, 0);
    lv_obj_t *note_title = create_label(note, "Large meshes stay bounded", 0x93C5FD);
    lv_obj_align(note_title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *note_text = create_label(
        note, "Read-only lists. Each role scrolls separately.", 0x8EA0AE);
    lv_label_set_long_mode(note_text, LV_LABEL_LONG_DOT);
    lv_obj_set_width(note_text, 420);
    lv_obj_align(note_text, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static void render_mesh_roles_room_servers(void)
{
    uint32_t total = s_snapshot.signal_summary.room_server_count;
    if (total < s_snapshot.recent_room_count) {
        total = (uint32_t)s_snapshot.recent_room_count;
    }
    render_mesh_roles_header("Rooms", mesh_roles_subpage_back_event_cb);
    lv_obj_t *meta = create_label(s_mesh_roles_sheet, "", 0x8EA0AE);
    label_set_fmt(meta, "%lu room server%s",
                  (unsigned long)total, total == 1U ? "" : "s");
    lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
    lv_obj_set_width(meta, 360);
    lv_obj_set_pos(meta, 104, 36);

    lv_obj_t *list = create_mesh_roles_list("Room servers", 0xA7F3D0,
                                            s_snapshot.recent_room_count, total);
    if (!list) {
        return;
    }
    int y = 0;
    for (size_t i = 0; i < s_snapshot.recent_room_count; ++i) {
        render_room_server_role_row(list, y, &s_snapshot.recent_rooms[i]);
        y += 64;
    }
    if (s_snapshot.recent_room_count == 0) {
        lv_obj_t *empty = create_label(list, "No room servers heard yet.", 0x8EA0AE);
        lv_obj_set_pos(empty, 8, 12);
    }
}

static void render_mesh_roles_repeater_candidates(void)
{
    uint32_t total = s_snapshot.signal_summary.repeater_candidate_count;
    if (total < s_snapshot.recent_repeater_count) {
        total = (uint32_t)s_snapshot.recent_repeater_count;
    }
    render_mesh_roles_header("Repeaters", mesh_roles_subpage_back_event_cb);
    lv_obj_t *meta = create_label(s_mesh_roles_sheet, "", 0x8EA0AE);
    label_set_fmt(meta, "%lu repeater candidate%s",
                  (unsigned long)total, total == 1U ? "" : "s");
    lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
    lv_obj_set_width(meta, 360);
    lv_obj_set_pos(meta, 104, 36);

    lv_obj_t *list = create_mesh_roles_list("Repeater candidates", 0xFBBF24,
                                            s_snapshot.recent_repeater_count, total);
    if (!list) {
        return;
    }
    int y = 0;
    for (size_t i = 0; i < s_snapshot.recent_repeater_count; ++i) {
        render_repeater_role_row(list, y, &s_snapshot.recent_repeaters[i]);
        y += 64;
    }
    if (s_snapshot.recent_repeater_count == 0) {
        lv_obj_t *empty = create_label(list, "No path-hop candidates yet.", 0x8EA0AE);
        lv_obj_set_pos(empty, 8, 12);
    }
}

static void render_mesh_roles_sheet(void)
{
    if (!s_mesh_roles_sheet) {
        return;
    }
    d1l_app_model_snapshot(&s_snapshot);
    update_chrome(&s_snapshot);
    lv_obj_clean(s_mesh_roles_sheet);

    switch (s_mesh_roles_page) {
    case D1L_MESH_ROLES_PAGE_ROOM_SERVERS:
        render_mesh_roles_room_servers();
        break;
    case D1L_MESH_ROLES_PAGE_REPEATER_CANDIDATES:
        render_mesh_roles_repeater_candidates();
        break;
    case D1L_MESH_ROLES_PAGE_ROOT:
    default:
        s_mesh_roles_page = D1L_MESH_ROLES_PAGE_ROOT;
        render_mesh_roles_root();
        break;
    }
}

static void open_mesh_roles_event_cb(lv_event_t *event)
{
    (void)event;
    hide_sheet();
    hide_public_history_sheet();
    hide_public_search_sheet();
    hide_compose_sheet();
    hide_dm_thread_sheet();
    hide_radio_settings_sheet();
    hide_contact_detail_sheet();
    hide_contact_export_sheet();
    hide_route_detail_sheet();
    hide_route_trace_sheet();
    hide_packet_detail_sheet();
    hide_packet_search_sheet();
    s_mesh_roles_page = D1L_MESH_ROLES_PAGE_ROOT;
    render_mesh_roles_sheet();
    if (s_mesh_roles_sheet) {
        show_modal(s_mesh_roles_sheet);
    }
}

static void close_compose_event_cb(lv_event_t *event)
{
    (void)event;
    hide_compose_sheet();
}

static void clear_compose_event_cb(lv_event_t *event)
{
    (void)event;
    if (s_compose_textarea) {
        lv_textarea_set_text(s_compose_textarea, "");
    }
    update_compose_counter();
}

static void send_compose_text(void)
{
    if (!s_compose_textarea) {
        return;
    }
    const char *text = lv_textarea_get_text(s_compose_textarea);
    esp_err_t ret = s_compose_dm ?
                    d1l_app_model_send_dm_text(s_compose_contact.fingerprint, text) :
                    d1l_app_model_send_public_text(text);
    show_toast(s_compose_dm ? "DM" : "Public message", ret);
    if (ret == ESP_OK) {
        lv_textarea_set_text(s_compose_textarea, "");
        update_compose_counter();
        hide_compose_sheet();
        request_content_refresh();
    }
}

static void send_compose_event_cb(lv_event_t *event)
{
    (void)event;
    send_compose_text();
}

static void compose_keyboard_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_READY) {
        send_compose_text();
    } else if (code == LV_EVENT_CANCEL) {
        hide_compose_sheet();
    }
}

static void compose_textarea_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) {
        update_compose_counter();
    }
}

static void complete_onboarding_from_ui(const char *fallback_name)
{
    const char *name = fallback_name;
    if (s_onboarding_name_textarea) {
        const char *entered = lv_textarea_get_text(s_onboarding_name_textarea);
        if (entered && entered[0] != '\0') {
            name = entered;
        }
    }
    if (!name || name[0] == '\0') {
        name = "D1L Desk";
    }
    esp_err_t ret = d1l_app_model_complete_onboarding(name);
    if (ret == ESP_OK) {
        hide_onboarding_sheet();
        request_content_refresh();
    }
    show_toast("Onboarding", ret);
}

static void onboarding_start_event_cb(lv_event_t *event)
{
    (void)event;
    complete_onboarding_from_ui("D1L Desk");
}

static void onboarding_defaults_event_cb(lv_event_t *event)
{
    (void)event;
    if (s_onboarding_name_textarea) {
        lv_textarea_set_text(s_onboarding_name_textarea, "D1L Desk");
    }
    complete_onboarding_from_ui("D1L Desk");
}

static void onboarding_keyboard_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_READY) {
        complete_onboarding_from_ui("D1L Desk");
    }
}

static const char *public_row_state(const d1l_message_entry_t *entry)
{
    if (entry->direction[0] == 'r' && entry->seq > s_snapshot.last_public_read_seq) {
        return "new";
    }
    return entry->direction[0] == 't' ? "queued" : "received";
}

static void close_public_history_event_cb(lv_event_t *event)
{
    (void)event;
    hide_public_search_sheet();
    hide_public_history_sheet();
}

static void close_public_search_event_cb(lv_event_t *event)
{
    (void)event;
    hide_public_search_sheet();
    show_public_history_sheet();
}

static void public_history_load_older_event_cb(lv_event_t *event)
{
    (void)event;
    if (s_public_history_limit < D1L_PUBLIC_HISTORY_UI_INITIAL_ROWS) {
        s_public_history_limit = D1L_PUBLIC_HISTORY_UI_INITIAL_ROWS;
    }
    if (s_public_history_limit < D1L_MESSAGE_STORE_CAPACITY) {
        s_public_history_limit += D1L_PUBLIC_HISTORY_UI_LOAD_OLDER_STEP;
        if (s_public_history_limit > D1L_MESSAGE_STORE_CAPACITY) {
            s_public_history_limit = D1L_MESSAGE_STORE_CAPACITY;
        }
    }
    show_public_history_sheet();
}

static void render_public_history_sheet(void)
{
    if (!s_public_history_sheet) {
        return;
    }
    d1l_app_model_snapshot(&s_snapshot);
    update_chrome(&s_snapshot);
    lv_obj_clean(s_public_history_sheet);

    lv_obj_t *title = create_label(s_public_history_sheet, "Public History", 0xF4F7FB);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, 178);
    lv_obj_set_pos(title, 8, 4);

    create_button(s_public_history_sheet, "Search", 196, 0, 76, 40,
                  open_public_search_event_cb, NULL);
    create_button(s_public_history_sheet, "Clear", 280, 0, 64, 40,
                  open_public_search_event_cb, (void *)"clear");
    create_button(s_public_history_sheet, "Close", 352, 0, 72, 40,
                  close_public_history_event_cb, NULL);

    if (s_public_history_limit < D1L_PUBLIC_HISTORY_UI_INITIAL_ROWS) {
        s_public_history_limit = D1L_PUBLIC_HISTORY_UI_INITIAL_ROWS;
    }
    if (s_public_history_limit > D1L_MESSAGE_STORE_CAPACITY) {
        s_public_history_limit = D1L_MESSAGE_STORE_CAPACITY;
    }
    size_t total_matches = 0;
    const size_t history_count =
        d1l_app_model_query_public_messages_page(s_public_history_entries,
                                                 s_public_history_limit, 0,
                                                 s_public_search_text,
                                                 &total_matches);
    lv_obj_t *meta = create_label(s_public_history_sheet, "", 0x8EA0AE);
    if (s_public_search_text[0]) {
        label_set_fmt(meta, "showing %u/%u  find %.18s",
                      (unsigned)history_count, (unsigned)total_matches,
                      s_public_search_text);
    } else {
        label_set_fmt(meta, "showing %u/%u retained",
                      (unsigned)history_count, (unsigned)total_matches);
    }
    lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
    lv_obj_set_width(meta, 392);
    lv_obj_set_pos(meta, 8, 42);

    lv_obj_t *list = create_object(s_public_history_sheet, "public history list");
    if (!list) {
        return;
    }
    lv_obj_set_size(list, 424, 190);
    lv_obj_set_pos(list, 0, 70);
    lv_obj_set_style_radius(list, 6, 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    int y = 0;
    for (size_t i = 0; i < history_count; ++i) {
        const d1l_message_entry_t *entry = &s_public_history_entries[i];
        const bool unread = entry->direction[0] == 'r' && entry->seq > s_snapshot.last_public_read_seq;
        lv_obj_t *row = create_panel(list, 0, y, 404, 52);
        lv_obj_set_style_pad_all(row, 8, 0);
        lv_obj_t *author = create_label(row, entry->author,
                                        unread ? 0xFBBF24 :
                                        (entry->direction[0] == 't' ? 0x93C5FD : 0x5EEAD4));
        lv_label_set_long_mode(author, LV_LABEL_LONG_DOT);
        lv_obj_set_width(author, 156);
        lv_obj_align(author, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_t *state = create_label(row, "", 0x8EA0AE);
        label_set_fmt(state, "#%lu %s", (unsigned long)entry->seq, public_row_state(entry));
        lv_obj_align(state, LV_ALIGN_TOP_RIGHT, 0, 0);
        lv_obj_t *text = create_label(row, entry->text, 0xE5EDF5);
        lv_label_set_long_mode(text, LV_LABEL_LONG_DOT);
        lv_obj_set_width(text, 372);
        lv_obj_align(text, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        y += 58;
    }
    if (history_count == 0) {
        lv_obj_t *empty = create_label(list,
                                       s_public_search_text[0] ?
                                       "No Public rows match" : "No retained Public rows",
                                       0x8EA0AE);
        lv_obj_align(empty, LV_ALIGN_CENTER, 0, 0);
    } else {
        lv_obj_scroll_to_y(list, LV_COORD_MAX, LV_ANIM_OFF);
    }
    if (history_count < total_matches && s_public_history_limit < D1L_MESSAGE_STORE_CAPACITY) {
        create_button(s_public_history_sheet, "Load Older", 8, 272, 126, 36,
                      public_history_load_older_event_cb, NULL);
    }
}

static void show_public_history_sheet(void)
{
    render_public_history_sheet();
    if (s_public_history_sheet) {
        show_modal(s_public_history_sheet);
    }
}

static void apply_public_search_event_cb(lv_event_t *event)
{
    (void)event;
    s_public_search_text[0] = '\0';
    if (s_public_search_textarea) {
        const char *text = lv_textarea_get_text(s_public_search_textarea);
        if (text) {
            snprintf(s_public_search_text, sizeof(s_public_search_text), "%s", text);
        }
    }
    s_public_history_limit = D1L_PUBLIC_HISTORY_UI_INITIAL_ROWS;
    hide_public_search_sheet();
    show_public_history_sheet();
}

static void clear_public_search_event_cb(lv_event_t *event)
{
    (void)event;
    s_public_search_text[0] = '\0';
    if (s_public_search_textarea) {
        lv_textarea_set_text(s_public_search_textarea, "");
    }
    s_public_history_limit = D1L_PUBLIC_HISTORY_UI_INITIAL_ROWS;
    hide_public_search_sheet();
    show_public_history_sheet();
}

static void public_search_keyboard_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_READY) {
        apply_public_search_event_cb(event);
    } else if (code == LV_EVENT_CANCEL) {
        close_public_search_event_cb(event);
    }
}

static void open_public_search_event_cb(lv_event_t *event)
{
    const void *user_data = event ? lv_event_get_user_data(event) : NULL;
    if (user_data && strcmp((const char *)user_data, "clear") == 0) {
        clear_public_search_event_cb(event);
        return;
    }
    if (!s_public_search_sheet) {
        return;
    }
    if (s_public_search_textarea && s_public_search_keyboard) {
        lv_textarea_set_text(s_public_search_textarea, s_public_search_text);
        lv_keyboard_set_textarea(s_public_search_keyboard, s_public_search_textarea);
    }
    show_modal(s_public_search_sheet);
}

static void open_public_history_event_cb(lv_event_t *event)
{
    (void)event;
    s_public_history_limit = D1L_PUBLIC_HISTORY_UI_INITIAL_ROWS;
    hide_sheet();
    hide_compose_sheet();
    hide_public_search_sheet();
    hide_message_detail_sheet();
    hide_dm_thread_sheet();
    hide_radio_settings_sheet();
    hide_contact_detail_sheet();
    hide_contact_export_sheet();
    hide_route_detail_sheet();
    hide_route_trace_sheet();
    hide_packet_detail_sheet();
    hide_packet_search_sheet();
    hide_mesh_roles_sheet();
    show_public_history_sheet();
}

static const char *dm_row_state(const d1l_dm_entry_t *entry, bool unread)
{
    if (unread) {
        return "new";
    }
    if (entry->acked) {
        return "acked";
    }
    return entry->direction[0] == 't' ? "sent" : "received";
}

static void close_dm_thread_event_cb(lv_event_t *event)
{
    (void)event;
    hide_dm_thread_sheet();
}

static void dm_thread_load_older_event_cb(lv_event_t *event)
{
    (void)event;
    if (s_dm_thread_fingerprint[0] == '\0') {
        return;
    }
    if (s_dm_thread_limit < D1L_DM_THREAD_UI_INITIAL_ROWS) {
        s_dm_thread_limit = D1L_DM_THREAD_UI_INITIAL_ROWS;
    }
    if (s_dm_thread_limit < D1L_DM_STORE_CAPACITY) {
        s_dm_thread_limit += D1L_DM_THREAD_UI_LOAD_OLDER_STEP;
        if (s_dm_thread_limit > D1L_DM_STORE_CAPACITY) {
            s_dm_thread_limit = D1L_DM_STORE_CAPACITY;
        }
    }
    render_dm_thread_sheet();
    if (s_dm_thread_sheet) {
        show_modal(s_dm_thread_sheet);
    }
}

static void reply_dm_thread_event_cb(lv_event_t *event)
{
    (void)event;
    d1l_contact_entry_t contact = {0};
    esp_err_t ret = d1l_app_model_find_contact(s_dm_thread_fingerprint, &contact);
    if (ret != ESP_OK) {
        show_toast("DM", ret);
        return;
    }
    open_dm_compose_for_contact(&contact);
}

static void render_dm_thread_sheet(void)
{
    if (!s_dm_thread_sheet) {
        return;
    }
    d1l_app_model_snapshot(&s_snapshot);
    update_chrome(&s_snapshot);
    lv_obj_clean(s_dm_thread_sheet);

    const char *alias = s_dm_thread_alias[0] ? s_dm_thread_alias : s_dm_thread_fingerprint;
    create_button(s_dm_thread_sheet, "Back", 12, 6, 72, 44,
                  close_dm_thread_event_cb, NULL);
    lv_obj_t *title = create_label(s_dm_thread_sheet, alias, 0xF4F7FB);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, 364);
    lv_obj_set_pos(title, 100, 10);

    if (s_dm_thread_limit < D1L_DM_THREAD_UI_INITIAL_ROWS) {
        s_dm_thread_limit = D1L_DM_THREAD_UI_INITIAL_ROWS;
    }
    if (s_dm_thread_limit > D1L_DM_STORE_CAPACITY) {
        s_dm_thread_limit = D1L_DM_STORE_CAPACITY;
    }
    size_t total_matches = 0;
    const size_t thread_count =
        d1l_app_model_copy_dm_thread_page(s_dm_thread_fingerprint, s_dm_thread_entries,
                                          s_dm_thread_unread, s_dm_thread_limit,
                                          0, &total_matches);

    lv_obj_t *body = create_nested_page_body(s_dm_thread_sheet, "dm thread body");
    if (!body) {
        return;
    }
    lv_obj_t *meta = create_nested_page_label(body, "", 0x8EA0AE, false);
    if (meta) {
        label_set_fmt(meta, "%.16s  showing %u/%u", s_dm_thread_fingerprint,
                      (unsigned)thread_count, (unsigned)total_matches);
    }
    if (thread_count < total_matches && s_dm_thread_limit < D1L_DM_STORE_CAPACITY) {
        lv_obj_t *older = create_button(body, "Load Older", 0, 0, 424, 48,
                                        dm_thread_load_older_event_cb, NULL);
        if (older) {
            lv_obj_set_width(older, LV_PCT(100));
        }
    }

    for (size_t i = 0; i < thread_count; ++i) {
        const d1l_dm_entry_t *entry = &s_dm_thread_entries[i];
        const bool unread = s_dm_thread_unread[i];
        lv_obj_t *row = create_panel(body, 0, 0, 424, 56);
        if (!row) {
            continue;
        }
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_style_pad_all(row, 8, 0);
        const char *who = entry->direction[0] == 't' ? "You" : alias;
        lv_obj_t *sender = create_label(row, who,
                                        unread ? 0xFBBF24 :
                                        (entry->direction[0] == 't' ? 0xC4B5FD : 0xA7F3D0));
        lv_label_set_long_mode(sender, LV_LABEL_LONG_DOT);
        lv_obj_set_width(sender, 160);
        lv_obj_align(sender, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_t *state = create_label(row, "", 0x8EA0AE);
        label_set_fmt(state, "#%lu %s", (unsigned long)entry->seq, dm_row_state(entry, unread));
        lv_obj_align(state, LV_ALIGN_TOP_RIGHT, 0, 0);
        lv_obj_t *text = create_label(row, entry->text, 0xE5EDF5);
        lv_label_set_long_mode(text, LV_LABEL_LONG_DOT);
        lv_obj_set_width(text, 372);
        lv_obj_align(text, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }
    if (thread_count < 3) {
        const char *notes[] = {
            "Thread keeps delivery, ACK, and PATH state together.",
            "Older retained rows appear here after RF traffic or SD restore.",
            "Use Reply for a direct MeshCore DM when contact keys exist.",
        };
        for (size_t i = 0; i < sizeof(notes) / sizeof(notes[0]); ++i) {
            lv_obj_t *note = create_panel(body, 0, 0, 424, 56);
            if (!note) {
                continue;
            }
            lv_obj_set_width(note, LV_PCT(100));
            lv_obj_set_style_pad_all(note, 8, 0);
            lv_obj_t *text = create_label(note, notes[i], 0x8EA0AE);
            lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(text, 372);
            lv_obj_align(text, LV_ALIGN_LEFT_MID, 0, 0);
        }
    }
    if (thread_count > 0) {
        lv_obj_update_layout(body);
        lv_obj_scroll_to_y(body, LV_COORD_MAX, LV_ANIM_OFF);
    }
    create_button(s_dm_thread_sheet, "Reply", 16, 360, 448, 52,
                  reply_dm_thread_event_cb, NULL);
}

static void show_dm_thread_for(const char *fingerprint, const char *alias)
{
    if (!fingerprint || fingerprint[0] == '\0') {
        show_toast("DM", ESP_ERR_INVALID_STATE);
        return;
    }
    snprintf(s_dm_thread_fingerprint, sizeof(s_dm_thread_fingerprint), "%s", fingerprint);
    snprintf(s_dm_thread_alias, sizeof(s_dm_thread_alias), "%s",
             alias && alias[0] ? alias : fingerprint);
    s_dm_thread_limit = D1L_DM_THREAD_UI_INITIAL_ROWS;
    if (d1l_app_model_mark_dm_thread_read(fingerprint) == ESP_OK) {
        request_content_refresh();
    }
    hide_sheet();
    hide_public_history_sheet();
    hide_public_search_sheet();
    hide_compose_sheet();
    hide_radio_settings_sheet();
    hide_contact_detail_sheet();
    hide_contact_export_sheet();
    hide_route_detail_sheet();
    hide_route_trace_sheet();
    hide_packet_detail_sheet();
    hide_packet_search_sheet();
    hide_mesh_roles_sheet();
    render_dm_thread_sheet();
    if (s_dm_thread_sheet) {
        show_modal(s_dm_thread_sheet);
    }
}

static void open_home_dm_preview_event_cb(lv_event_t *event)
{
    const d1l_home_message_preview_t *entry =
        (const d1l_home_message_preview_t *)lv_event_get_user_data(event);
    if (!entry || !entry->is_dm) {
        show_toast("DM", ESP_ERR_INVALID_STATE);
        return;
    }
    show_dm_thread_for(entry->target_fingerprint, entry->sender);
}

static void open_dm_thread_event_cb(lv_event_t *event)
{
    const d1l_dm_entry_t *entry = (const d1l_dm_entry_t *)lv_event_get_user_data(event);
    if (!entry || entry->contact_fingerprint[0] == '\0') {
        show_toast("DM", ESP_ERR_INVALID_STATE);
        return;
    }
    show_dm_thread_for(entry->contact_fingerprint,
                       entry->contact_alias[0] ? entry->contact_alias : entry->contact_fingerprint);
}

static void set_messages_mode(bool show_dms)
{
    s_messages_show_dms = show_dms;
    if (d1l_ui_navigation_active() == D1L_UI_TAB_MESSAGES) {
        request_content_refresh();
        return;
    }
    request_tab_switch(D1L_UI_TAB_MESSAGES);
}

static void open_messages_public_event_cb(lv_event_t *event)
{
    (void)event;
    set_messages_mode(false);
}

static void open_messages_dm_event_cb(lv_event_t *event)
{
    (void)event;
    set_messages_mode(true);
}

static void render_messages(lv_obj_t *content, const d1l_app_snapshot_t *snapshot)
{
    lv_obj_t *header = create_panel(content, 18, 16, 424, 108);
    lv_obj_t *title = create_label(header, "Messages", 0xF4F7FB);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, 170);
    lv_obj_set_pos(title, 8, 4);
    lv_obj_t *meta = create_label(header, "", 0x8EA0AE);
    label_set_fmt(meta, "public %u new %lu  dm %u new %lu muted %lu",
                  (unsigned)snapshot->message_count,
                  (unsigned long)snapshot->public_unread_count,
                  (unsigned)snapshot->dm_count,
                  (unsigned long)snapshot->dm_unread_count,
                  (unsigned long)snapshot->muted_dm_unread_count);
    lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
    lv_obj_set_width(meta, 392);
    lv_obj_set_pos(meta, 8, 34);
    create_button(header, "Read", 8, 62, 54, 40, mark_messages_read_event_cb, NULL);
    create_button(header, "Compose", 70, 62, 88, 40, open_compose_event_cb, NULL);
    create_button(header, "History", 166, 62, 80, 40, open_public_history_event_cb, NULL);
    create_button(header, "Test", 254, 62, 64, 40, public_test_event_cb, NULL);

    create_button(content, "Public", 18, 136, 96, 40, open_messages_public_event_cb, NULL);
    create_button(content, "DMs", 122, 136, 80, 40, open_messages_dm_event_cb, NULL);

    lv_obj_t *mode_label = create_label(content,
                                        s_messages_show_dms ? "DM Conversations" : "Public Channel",
                                        s_messages_show_dms ? 0xA7F3D0 : 0x5EEAD4);
    lv_obj_set_pos(mode_label, 26, 190);

    int y = 218;
    if (s_messages_show_dms) {
        for (size_t i = 0; i < snapshot->recent_dm_count; ++i) {
            render_dm_row(content, y, &snapshot->recent_dms[i], snapshot->recent_dm_unread[i]);
            y += 80;
        }
        if (snapshot->recent_dm_count == 0) {
            lv_obj_t *empty = create_label(content, "No direct messages", 0x8EA0AE);
            lv_obj_set_pos(empty, 26, y);
        }
    } else {
        for (size_t i = 0; i < snapshot->recent_message_count; ++i) {
            render_message_row(content, y, &snapshot->recent_messages[i]);
            y += 80;
        }
        if (snapshot->recent_message_count == 0) {
            lv_obj_t *empty = create_label(content, "No Public messages", 0x8EA0AE);
            lv_obj_set_pos(empty, 26, y);
        }
    }
}

static void render_nodes(lv_obj_t *content, const d1l_app_snapshot_t *snapshot)
{
    char value[32];
    char detail[64];
    const d1l_node_query_t node_query = {
        .filter = D1L_NODE_FILTER_ALL,
        .sort = D1L_NODE_SORT_LAST_HEARD,
        .text = NULL,
        .keyed_only = false,
        .reachable_only = false,
    };
    const size_t node_rows = d1l_app_model_query_nodes(&node_query, s_node_rows,
                                                       D1L_NODE_STORE_CAPACITY);
    snprintf(value, sizeof(value), "%u", (unsigned)snapshot->node_count);
    snprintf(detail, sizeof(detail), "rooms %lu  rpt %lu  writes %lu",
             (unsigned long)snapshot->signal_summary.room_server_count,
             (unsigned long)snapshot->signal_summary.repeater_candidate_count,
             (unsigned long)snapshot->node_total_written);
    render_metric_card(content, 18, 16, "Heard Nodes", value, detail, 0x5EEAD4);
    snprintf(value, sizeof(value), "%u", (unsigned)snapshot->contact_count);
    snprintf(detail, sizeof(detail), "contacts  writes %lu",
             (unsigned long)snapshot->contact_total_written);
    render_metric_card(content, 238, 16, "Contacts", value, detail, 0xA7F3D0);

    int y = 136;
    if (snapshot->recent_contact_count > 0) {
        for (size_t i = 0; i < snapshot->recent_contact_count && y <= 190; ++i) {
            render_contact_row(content, y, &snapshot->recent_contacts[i]);
            y += 54;
        }
        lv_obj_t *heard = create_label(content, "Heard", 0x8EA0AE);
        lv_obj_set_pos(heard, 26, y + 4);
        y += 28;
    }
    lv_obj_t *all_heard = create_label(content, "All Heard", 0x8EA0AE);
    lv_obj_set_pos(all_heard, 26, y + 4);
    y += 28;
    for (size_t i = 0; i < node_rows; ++i) {
        render_node_row(content, y, &s_node_rows[i]);
        y += 62;
    }
    if (node_rows == 0) {
        lv_obj_t *empty = create_label(content, "No heard nodes yet", 0x8EA0AE);
        lv_obj_align(empty, LV_ALIGN_TOP_MID, 0, 154);
        const char *notes[] = {
            "Listening for signed adverts from nearby MeshCore nodes.",
            "Contacts appear separately when a retained public key exists.",
            "Signal, route, and repeater summaries update from RX packets.",
            "Scroll proof keeps this empty-state layout validated too.",
        };
        int note_y = 206;
        for (size_t i = 0; i < sizeof(notes) / sizeof(notes[0]); ++i) {
            lv_obj_t *note = create_panel(content, 18, note_y, 424, 52);
            lv_obj_set_style_pad_all(note, 8, 0);
            lv_obj_t *text = create_label(note, notes[i], 0x8EA0AE);
            lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(text, 392);
            lv_obj_align(text, LV_ALIGN_LEFT_MID, 0, 0);
            note_y += 58;
        }
    }
}

static void close_map_location_sheet_event_cb(lv_event_t *event)
{
    (void)event;
    hide_map_location_sheet();
    if (s_map_location_returns_to_options) {
        s_map_location_returns_to_options = false;
        d1l_app_model_snapshot(&s_snapshot);
        s_map_options_page = D1L_UI_MAP_OPTIONS_ROOT;
        render_map_options_sheet();
        if (s_map_options_sheet) {
            show_modal(s_map_options_sheet);
        }
    } else {
        request_content_refresh();
    }
}

static void map_location_textarea_event_cb(lv_event_t *event)
{
    if (d1l_ui_keyboard_focus_textarea_from_event(s_map_location_keyboard, event,
                                                   s_map_lat_textarea,
                                                   s_map_lon_textarea)) {
        lv_obj_clear_flag(s_map_location_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_map_location_keyboard);
    }
}

static void map_location_save_event_cb(lv_event_t *event)
{
    (void)event;
    const char *lat_text = s_map_lat_textarea ? lv_textarea_get_text(s_map_lat_textarea) : "";
    const char *lon_text = s_map_lon_textarea ? lv_textarea_get_text(s_map_lon_textarea) : "";
    int32_t lat_e7 = 0;
    int32_t lon_e7 = 0;
    if (!parse_coord_text_e7(lat_text, D1L_MAP_LOCATION_LAT_E7_MIN,
                             D1L_MAP_LOCATION_LAT_E7_MAX, &lat_e7)) {
        show_toast_text("Latitude must be -90 to 90", false);
        return;
    }
    if (!parse_coord_text_e7(lon_text, D1L_MAP_LOCATION_LON_E7_MIN,
                             D1L_MAP_LOCATION_LON_E7_MAX, &lon_e7)) {
        show_toast_text("Longitude must be -180 to 180", false);
        return;
    }
    s_map_location_lat_e7 = lat_e7;
    s_map_location_lon_e7 = lon_e7;
    esp_err_t ret = d1l_app_model_set_map_location(s_map_location_lat_e7,
                                                   s_map_location_lon_e7);
    if (ret == ESP_OK) {
        s_map_location_returns_to_options = false;
        hide_map_location_sheet();
        hide_map_options_sheet();
        set_map_interactive_touch_authorized(true);
        request_tab_switch(D1L_UI_TAB_MAP);
        show_toast_text("D1L pin saved", true);
    } else {
        show_toast_text("Location rejected", false);
    }
}

static void map_location_clear_event_cb(lv_event_t *event)
{
    (void)event;
    esp_err_t ret = d1l_app_model_clear_map_location();
    if (ret == ESP_OK) {
        s_map_location_returns_to_options = false;
        map_location_from_snapshot(NULL);
        hide_map_location_sheet();
        hide_map_options_sheet();
        request_content_refresh();
        show_toast_text("D1L pin cleared", true);
    } else {
        show_toast_text("Clear location failed", false);
    }
}

static void render_map_location_sheet(void)
{
    if (!s_map_location_sheet) {
        return;
    }
    d1l_ui_map_render_location(s_map_location_sheet, &s_snapshot,
                               s_map_location_lat_e7, s_map_location_lon_e7,
                               &callbacks, &s_map_location_controls);
    s_map_lat_textarea = s_map_location_controls.latitude_textarea;
    s_map_lon_textarea = s_map_location_controls.longitude_textarea;
    s_map_location_keyboard = s_map_location_controls.location_keyboard;
}

static void open_map_location_sheet_event_cb(lv_event_t *event)
{
    (void)event;
    s_map_location_returns_to_options = false;
    d1l_app_model_snapshot(&s_snapshot);
    map_location_from_snapshot(&s_snapshot);
    hide_sheet();
    hide_public_history_sheet();
    hide_public_search_sheet();
    hide_compose_sheet();
    hide_message_detail_sheet();
    hide_dm_thread_sheet();
    hide_radio_settings_sheet();
    hide_storage_sheet();
    hide_wifi_sheet();
    hide_ble_sheet();
    hide_display_sheet();
    hide_diagnostics_sheet();
    hide_map_location_sheet();
    hide_map_options_sheet();
    hide_contact_detail_sheet();
    hide_contact_export_sheet();
    hide_route_detail_sheet();
    hide_route_trace_sheet();
    hide_packet_detail_sheet();
    hide_packet_search_sheet();
    hide_mesh_roles_sheet();
    render_map_location_sheet();
    if (s_map_location_sheet) {
        show_modal(s_map_location_sheet);
    }
}

static void open_map_location_from_options_event_cb(lv_event_t *event)
{
    open_map_location_sheet_event_cb(event);
    s_map_location_returns_to_options = true;
}

static void render_map_options_sheet(void)
{
    if (!s_map_options_sheet) {
        return;
    }
    d1l_ui_map_render_options(s_map_options_sheet, &s_snapshot,
                              s_map_options_page, &callbacks);
}

static void close_map_options_sheet_event_cb(lv_event_t *event)
{
    (void)event;
    hide_map_options_sheet();
    set_map_interactive_touch_authorized(true);
    request_tab_switch(D1L_UI_TAB_MAP);
}

static void open_map_cache_status_event_cb(lv_event_t *event)
{
    (void)event;
    d1l_app_model_snapshot(&s_snapshot);
    s_map_options_page = D1L_UI_MAP_OPTIONS_CACHE_STATUS;
    render_map_options_sheet();
}

static void back_to_map_options_event_cb(lv_event_t *event)
{
    (void)event;
    d1l_app_model_snapshot(&s_snapshot);
    s_map_options_page = D1L_UI_MAP_OPTIONS_ROOT;
    render_map_options_sheet();
}

static void open_map_options_sheet_event_cb(lv_event_t *event)
{
    (void)event;
    d1l_app_model_snapshot(&s_snapshot);
    s_map_options_page = D1L_UI_MAP_OPTIONS_ROOT;
    hide_sheet();
    hide_public_history_sheet();
    hide_public_search_sheet();
    hide_compose_sheet();
    hide_message_detail_sheet();
    hide_dm_thread_sheet();
    hide_radio_settings_sheet();
    hide_storage_sheet();
    hide_wifi_sheet();
    hide_ble_sheet();
    hide_display_sheet();
    hide_diagnostics_sheet();
    hide_map_location_sheet();
    hide_contact_detail_sheet();
    hide_contact_export_sheet();
    hide_route_detail_sheet();
    hide_route_trace_sheet();
    hide_packet_detail_sheet();
    hide_packet_search_sheet();
    hide_mesh_roles_sheet();
    render_map_options_sheet();
    if (s_map_options_sheet) {
        show_modal(s_map_options_sheet);
    }
}

static void render_map(lv_obj_t *content, const d1l_app_snapshot_t *snapshot)
{
    if (s_lock_visible || s_onboarding_visible || d1l_ui_modal_has_active() ||
        !map_interactive_touch_authorized()) {
        d1l_ui_map_viewport_suppress_next_acquire();
    }
    d1l_ui_map_render(content, snapshot, &callbacks);
}

static const char *packet_filter_direction(void)
{
    switch (s_packet_filter_mode) {
    case D1L_PACKET_FILTER_RX:
        return "rx";
    case D1L_PACKET_FILTER_TX:
        return "tx";
    default:
        return "any";
    }
}

static const char *packet_filter_kind(void)
{
    return s_packet_filter_mode == D1L_PACKET_FILTER_TEXT ? "text" : "any";
}

static lv_obj_t *render_packet_filter_button(lv_obj_t *parent, const char *label, int x,
                                             d1l_packet_filter_mode_t mode)
{
    lv_obj_t *button = create_button(parent, label, x, 112, 58, 34, packet_filter_event_cb,
                                     (void *)(uintptr_t)mode);
    if (button && s_packet_filter_mode == mode) {
        lv_obj_set_style_bg_color(button, lv_color_hex(0x12362F), 0);
        lv_obj_set_style_border_color(button, lv_color_hex(0x5EEAD4), 0);
        lv_obj_set_style_border_width(button, 1, 0);
    }
    return button;
}

static void render_packets(lv_obj_t *content, const d1l_app_snapshot_t *snapshot)
{
    char snr[16];
    format_snr_tenths(snr, sizeof(snr), snapshot->signal_summary.latest_snr_tenths);
    lv_obj_t *title = create_label(content, "Packets", 0xF4F7FB);
    obj_set_style_text_font_if(title, &lv_font_montserrat_24);
    obj_set_pos_if(title, 18, 16);

    lv_obj_t *terminal = create_panel(content, 18, 52, 424, 50);
    if (terminal) {
        lv_obj_set_style_pad_all(terminal, 8, 0);
        lv_obj_set_style_bg_color(terminal, lv_color_hex(0x071018), 0);
        lv_obj_set_style_border_color(terminal, lv_color_hex(0x334155), 0);
        lv_obj_t *line1 = create_label(terminal, "", 0x5EEAD4);
        label_set_fmt(line1, "live %s  rssi %d  snr %s  avg %d",
                      s_packets_paused ? "paused" : "tail",
                      snapshot->signal_summary.latest_rssi_dbm,
                      snr,
                      snapshot->signal_summary.avg_rssi_dbm);
        label_set_dot_width(line1, 392);
        obj_align_if(line1, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_t *line2 = create_label(terminal, "", 0x8EA0AE);
        label_set_fmt(line2, "rooms %lu  repeaters %lu  samples %lu",
                      (unsigned long)snapshot->signal_summary.room_server_count,
                      (unsigned long)snapshot->signal_summary.repeater_candidate_count,
                      (unsigned long)snapshot->signal_summary.sample_count);
        label_set_dot_width(line2, 392);
        obj_align_if(line2, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }

    render_packet_filter_button(content, "All", 18, D1L_PACKET_FILTER_ALL);
    render_packet_filter_button(content, "RX", 82, D1L_PACKET_FILTER_RX);
    render_packet_filter_button(content, "TX", 146, D1L_PACKET_FILTER_TX);
    render_packet_filter_button(content, "Text", 210, D1L_PACKET_FILTER_TEXT);
    create_button(content, "Search", 278, 112, 74, 34, open_packet_search_event_cb, NULL);
    create_button(content, s_packets_paused ? "Resume" : "Pause", 362, 112, 80, 34,
                  packet_pause_event_cb, NULL);
    if (s_packet_search_text[0]) {
        lv_obj_t *search = create_label(content, "", 0xFBBF24);
        label_set_fmt(search, "find %.18s", s_packet_search_text);
        lv_label_set_long_mode(search, LV_LABEL_LONG_DOT);
        lv_obj_set_width(search, 408);
        lv_obj_set_pos(search, 26, 152);
    }

    lv_obj_t *feed_title = create_label(content, "Packet Feed", 0x8EA0AE);
    obj_set_pos_if(feed_title, 26, s_packet_search_text[0] ? 176 : 156);

    size_t packet_rows = refresh_packet_terminal_rows();
    int y = s_packet_search_text[0] ? 204 : 184;
    lv_obj_t *feed_count = create_label(content, "", 0x8EA0AE);
    const size_t page_first = packet_rows > 0 ? s_packet_skip_newest + 1U : 0;
    const size_t page_last = s_packet_skip_newest + packet_rows;
    label_set_fmt(feed_count, "page %u-%u/%u%s",
                  (unsigned)page_first, (unsigned)page_last,
                  (unsigned)s_packet_total_matches,
                  s_packet_sd_history_page ? " SD" : "");
    lv_label_set_long_mode(feed_count, LV_LABEL_LONG_DOT);
    lv_obj_set_width(feed_count, 210);
    lv_obj_set_pos(feed_count, 218, s_packet_search_text[0] ? 176 : 156);
    for (size_t i = 0; i < packet_rows; ++i) {
        render_packet_row(content, y, &s_packet_filtered_packets[i]);
        y += 52;
    }
    if (packet_rows == 0 && snapshot->recent_packet_count > 0) {
        lv_obj_t *empty = create_label(content, "No packets match filter", 0x8EA0AE);
        lv_obj_set_pos(empty, 26, y + 8);
        y += 34;
    } else if (packet_rows == 0) {
        lv_obj_t *empty = create_label(content, "Packet log is empty", 0x8EA0AE);
        lv_obj_set_pos(empty, 26, y + 8);
        y += 34;
    }

    if (packet_feed_can_load_older(packet_rows)) {
        create_button(content, "Load Older", 18, y + 4, 128, 40,
                      packet_load_older_event_cb, NULL);
        if (packet_feed_can_load_newer()) {
            create_button(content, "Newer", 154, y + 4, 92, 40,
                          packet_load_newer_event_cb, NULL);
        }
        y += 54;
    } else if (packet_feed_can_load_newer()) {
        create_button(content, "Newer", 18, y + 4, 92, 40,
                      packet_load_newer_event_cb, NULL);
        y += 54;
    }

    y += 10;
    lv_obj_t *roles = create_button(content, "Mesh Roles", 18, y, 130, 36,
                                    open_mesh_roles_event_cb, NULL);
    if (roles) {
        lv_obj_set_style_bg_color(roles, lv_color_hex(0x12362F), 0);
    }
    y += 48;
    if (snapshot->recent_route_count > 0) {
        lv_obj_t *routes = create_label(content, "Routes", 0x8EA0AE);
        lv_obj_set_pos(routes, 26, y);
        y += 24;
        for (size_t i = 0; i < snapshot->recent_route_count; ++i) {
            render_route_row(content, y, &snapshot->recent_routes[i]);
            y += 54;
        }
    }
}

static uint16_t next_radio_bandwidth(uint16_t current)
{
    static const uint16_t bandwidths[] = {625U, 1250U, 2500U, 5000U};
    for (size_t i = 0; i < sizeof(bandwidths) / sizeof(bandwidths[0]); ++i) {
        if (current < bandwidths[i]) {
            return bandwidths[i];
        }
    }
    return bandwidths[0];
}

static void radio_edit_adjust_event_cb(lv_event_t *event)
{
    d1l_radio_edit_action_t action =
        (d1l_radio_edit_action_t)(uintptr_t)lv_event_get_user_data(event);
    switch (action) {
    case D1L_RADIO_EDIT_FREQ_DOWN:
        if (s_radio_edit.frequency_hz >= 902025000UL) {
            s_radio_edit.frequency_hz -= 25000UL;
        }
        break;
    case D1L_RADIO_EDIT_FREQ_UP:
        if (s_radio_edit.frequency_hz <= 927975000UL) {
            s_radio_edit.frequency_hz += 25000UL;
        }
        break;
    case D1L_RADIO_EDIT_BW:
        s_radio_edit.bandwidth_tenths_khz =
            next_radio_bandwidth(s_radio_edit.bandwidth_tenths_khz);
        break;
    case D1L_RADIO_EDIT_SF_DOWN:
        if (s_radio_edit.spreading_factor > 5U) {
            s_radio_edit.spreading_factor--;
        }
        break;
    case D1L_RADIO_EDIT_SF_UP:
        if (s_radio_edit.spreading_factor < 12U) {
            s_radio_edit.spreading_factor++;
        }
        break;
    case D1L_RADIO_EDIT_CR:
        s_radio_edit.coding_rate =
            s_radio_edit.coding_rate >= 8U ? 5U : (uint8_t)(s_radio_edit.coding_rate + 1U);
        break;
    case D1L_RADIO_EDIT_TX_DOWN:
        if (s_radio_edit.tx_power_dbm > -9) {
            s_radio_edit.tx_power_dbm--;
        }
        break;
    case D1L_RADIO_EDIT_TX_UP:
        if (s_radio_edit.tx_power_dbm < D1L_RADIO_TX_POWER_DBM) {
            s_radio_edit.tx_power_dbm++;
        }
        break;
    case D1L_RADIO_EDIT_RX_BOOST:
        s_radio_edit.rx_boost = !s_radio_edit.rx_boost;
        break;
    }
    render_radio_settings_sheet();
}

static void radio_defaults_event_cb(lv_event_t *event)
{
    (void)event;
    d1l_app_model_default_radio_profile(&s_radio_edit);
    render_radio_settings_sheet();
}

static void radio_save_event_cb(lv_event_t *event)
{
    (void)event;
    esp_err_t ret = d1l_app_model_save_radio_profile(&s_radio_edit);
    if (ret == ESP_OK) {
        d1l_app_model_snapshot(&s_snapshot);
        request_content_refresh();
        render_radio_settings_sheet();
        if (s_radio_settings_sheet) {
            show_modal(s_radio_settings_sheet);
        }
        show_toast_text("Radio saved; RF apply pending", true);
    } else {
        show_toast_text("Radio profile rejected", false);
    }
}

static void close_radio_settings_event_cb(lv_event_t *event)
{
    (void)event;
    hide_radio_settings_sheet();
}

static void render_radio_settings_sheet(void)
{
    if (!s_radio_settings_sheet) {
        return;
    }
    lv_obj_clean(s_radio_settings_sheet);

    char profile[80];
    char line[96];
    format_radio_profile_line(profile, sizeof(profile), &s_radio_edit);

    lv_obj_t *title = create_label(s_radio_settings_sheet, "Radio Settings", 0xF4F7FB);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(title, 8, 4);

    create_button(s_radio_settings_sheet, "Close", 340, 0, 76, 40,
                  close_radio_settings_event_cb, NULL);

    lv_obj_t *summary = create_label(s_radio_settings_sheet, profile, 0x5EEAD4);
    lv_label_set_long_mode(summary, LV_LABEL_LONG_DOT);
    lv_obj_set_width(summary, 408);
    lv_obj_set_pos(summary, 8, 44);

    const char *radio_apply_text =
        s_snapshot.radio_applied ? "Live RF matches saved profile" :
        s_snapshot.radio_apply_pending ? "Saved profile pending next radio start/apply" :
        "Radio apply status unavailable";
    lv_obj_t *warning = create_label(s_radio_settings_sheet, radio_apply_text,
                                     s_snapshot.radio_applied ? 0x5EEAD4 : 0xFBBF24);
    lv_label_set_long_mode(warning, LV_LABEL_LONG_DOT);
    lv_obj_set_width(warning, 408);
    lv_obj_set_pos(warning, 8, 68);

    lv_obj_t *freq = create_label(s_radio_settings_sheet, "", 0xE5EDF5);
    label_set_fmt(freq, "Freq %.3f MHz", ((double)s_radio_edit.frequency_hz) / 1000000.0);
    lv_obj_set_pos(freq, 8, 102);
    create_button(s_radio_settings_sheet, "-25k", 226, 92, 72, 36, radio_edit_adjust_event_cb,
                  (void *)(uintptr_t)D1L_RADIO_EDIT_FREQ_DOWN);
    create_button(s_radio_settings_sheet, "+25k", 306, 92, 72, 36, radio_edit_adjust_event_cb,
                  (void *)(uintptr_t)D1L_RADIO_EDIT_FREQ_UP);

    snprintf(line, sizeof(line), "BW %.1f kHz", ((double)s_radio_edit.bandwidth_tenths_khz) / 10.0);
    lv_obj_t *bw = create_label(s_radio_settings_sheet, line, 0xE5EDF5);
    lv_obj_set_pos(bw, 8, 142);
    create_button(s_radio_settings_sheet, "Cycle BW", 226, 132, 152, 36,
                  radio_edit_adjust_event_cb, (void *)(uintptr_t)D1L_RADIO_EDIT_BW);

    lv_obj_t *sf = create_label(s_radio_settings_sheet, "", 0xE5EDF5);
    label_set_fmt(sf, "SF %u", s_radio_edit.spreading_factor);
    lv_obj_set_pos(sf, 8, 182);
    create_button(s_radio_settings_sheet, "SF-", 92, 172, 62, 36, radio_edit_adjust_event_cb,
                  (void *)(uintptr_t)D1L_RADIO_EDIT_SF_DOWN);
    create_button(s_radio_settings_sheet, "SF+", 162, 172, 62, 36, radio_edit_adjust_event_cb,
                  (void *)(uintptr_t)D1L_RADIO_EDIT_SF_UP);
    lv_obj_t *cr = create_label(s_radio_settings_sheet, "", 0xE5EDF5);
    label_set_fmt(cr, "CR %u", s_radio_edit.coding_rate);
    lv_obj_set_pos(cr, 244, 182);
    create_button(s_radio_settings_sheet, "Cycle", 306, 172, 72, 36, radio_edit_adjust_event_cb,
                  (void *)(uintptr_t)D1L_RADIO_EDIT_CR);

    lv_obj_t *tx = create_label(s_radio_settings_sheet, "", 0xE5EDF5);
    label_set_fmt(tx, "TX %d dBm", s_radio_edit.tx_power_dbm);
    lv_obj_set_pos(tx, 8, 222);
    create_button(s_radio_settings_sheet, "TX-", 106, 212, 62, 36, radio_edit_adjust_event_cb,
                  (void *)(uintptr_t)D1L_RADIO_EDIT_TX_DOWN);
    create_button(s_radio_settings_sheet, "TX+", 176, 212, 62, 36, radio_edit_adjust_event_cb,
                  (void *)(uintptr_t)D1L_RADIO_EDIT_TX_UP);
    create_button(s_radio_settings_sheet, s_radio_edit.rx_boost ? "RX Boost On" : "RX Boost Off",
                  250, 212, 128, 36, radio_edit_adjust_event_cb,
                  (void *)(uintptr_t)D1L_RADIO_EDIT_RX_BOOST);

    create_button(s_radio_settings_sheet, "US/CAN", 8, 266, 104, 40, radio_defaults_event_cb, NULL);
    create_button(s_radio_settings_sheet, "Save", 124, 266, 104, 40, radio_save_event_cb, NULL);
    create_button(s_radio_settings_sheet, "Close", 240, 266, 104, 40,
                  close_radio_settings_event_cb, NULL);
}

static void open_radio_settings_event_cb(lv_event_t *event)
{
    (void)event;
    d1l_app_model_snapshot(&s_snapshot);
    radio_edit_from_snapshot(&s_snapshot);
    hide_sheet();
    hide_public_history_sheet();
    hide_public_search_sheet();
    hide_compose_sheet();
    hide_dm_thread_sheet();
    hide_storage_sheet();
    hide_wifi_sheet();
    hide_ble_sheet();
    hide_display_sheet();
    hide_diagnostics_sheet();
    hide_map_location_sheet();
    hide_map_options_sheet();
    hide_contact_detail_sheet();
    hide_contact_export_sheet();
    hide_route_detail_sheet();
    hide_route_trace_sheet();
    hide_packet_detail_sheet();
    hide_packet_search_sheet();
    hide_mesh_roles_sheet();
    render_radio_settings_sheet();
    if (s_radio_settings_sheet) {
        show_modal(s_radio_settings_sheet);
    }
}

static void close_storage_sheet_event_cb(lv_event_t *event)
{
    (void)event;
    hide_storage_sheet();
}

static void storage_subpage_back_event_cb(lv_event_t *event)
{
    (void)event;
    s_storage_page = D1L_STORAGE_PAGE_ROOT;
    render_storage_sheet();
    show_modal(s_storage_sheet);
}

static void open_storage_card_status_event_cb(lv_event_t *event)
{
    (void)event;
    s_storage_page = D1L_STORAGE_PAGE_CARD_STATUS;
    render_storage_sheet();
    show_modal(s_storage_sheet);
}

static void open_storage_data_locations_event_cb(lv_event_t *event)
{
    (void)event;
    s_storage_page = D1L_STORAGE_PAGE_DATA_LOCATIONS;
    render_storage_sheet();
    show_modal(s_storage_sheet);
}

static bool storage_text_equals(const char *value, const char *expected)
{
    return value && expected && strcmp(value, expected) == 0;
}

static bool storage_snapshot_needs_attention(const d1l_app_snapshot_t *snapshot)
{
    if (!snapshot) {
        return false;
    }
    return snapshot->storage_retained_sd_degraded ||
           storage_text_equals(snapshot->storage_sd_state, "error") ||
           storage_text_equals(snapshot->storage_sd_state, "bridge_reported") ||
           storage_text_equals(snapshot->storage_setup_action,
                               "inspect_rp2040_sd_cmd0_firmware_path") ||
           storage_text_equals(snapshot->storage_setup_action,
                               "inspect_rp2040_sd_mount_error_firmware_path");
}

static const char *storage_card_state_friendly(const d1l_app_snapshot_t *snapshot)
{
    if (!snapshot) {
        return "Status unavailable";
    }
    if (storage_snapshot_needs_attention(snapshot)) {
        return "Card needs attention";
    }
    if (storage_text_equals(snapshot->storage_setup_action,
                            "wait_for_storage_reconnect")) {
        return "Card reader reconnecting";
    }
    if (storage_text_equals(snapshot->storage_setup_action, "run_storage_mount") ||
        storage_text_equals(snapshot->storage_setup_action, "wait_for_storage_mount")) {
        return "Checking card";
    }
    if (snapshot->storage_rp2040_bridge_required &&
        !snapshot->storage_rp2040_bridge_ready) {
        return "Card reader unavailable";
    }
    if (snapshot->storage_rp2040_bridge_required &&
        !snapshot->storage_rp2040_sd_protocol_supported) {
        return "Card reader starting";
    }
    if (!snapshot->storage_sd_present) {
        return "No card inserted";
    }
    if (snapshot->storage_sd_needs_fat32 ||
        storage_text_equals(snapshot->storage_sd_state, "not_fat32_or_unmountable")) {
        return "FAT32 card required";
    }
    if (storage_text_equals(snapshot->storage_sd_state, "deskos_manifest_invalid")) {
        return "DeskOS files need attention";
    }
    if (storage_text_equals(snapshot->storage_sd_state, "checking") ||
        storage_text_equals(snapshot->storage_sd_state, "mount_pending")) {
        return "Checking card";
    }
    if (storage_text_equals(snapshot->storage_sd_state, "creating_deskos_files") ||
        (snapshot->storage_sd_mounted && !snapshot->storage_sd_data_root_ready)) {
        return "Preparing DeskOS folders";
    }
    if (snapshot->storage_sd_mounted && snapshot->storage_sd_data_root_ready) {
        return "Ready";
    }
    return "Card detected, not ready";
}

static const char *storage_filesystem_friendly(const d1l_app_snapshot_t *snapshot)
{
    if (!snapshot || !snapshot->storage_sd_present) {
        return "Not available";
    }
    if (snapshot->storage_sd_needs_fat32) {
        return "FAT32 required";
    }
    if (storage_text_equals(snapshot->storage_sd_filesystem, "fat32") ||
        storage_text_equals(snapshot->storage_sd_filesystem, "fatfs")) {
        return "FAT32";
    }
    if (storage_text_equals(snapshot->storage_sd_filesystem, "exfat")) {
        return "exFAT (not supported)";
    }
    return snapshot->storage_sd_mounted ? "Detected" : "Not available";
}

static const char *storage_readiness_friendly(const d1l_app_snapshot_t *snapshot)
{
    if (!snapshot) {
        return "Not available";
    }
    const char *action = snapshot->storage_setup_action;
    if (storage_snapshot_needs_attention(snapshot) ||
        storage_text_equals(snapshot->storage_sd_state, "deskos_manifest_invalid")) {
        return "Needs attention";
    }
    if (storage_text_equals(action, "wait_for_storage_reconnect")) {
        return "Reconnecting";
    }
    if (storage_text_equals(action, "run_storage_mount") ||
        storage_text_equals(action, "wait_for_storage_mount")) {
        return "Checking";
    }
    if (storage_text_equals(action, "bridge_unavailable") ||
        storage_text_equals(action, "bridge_protocol_pending")) {
        return "Not available";
    }
    if (storage_text_equals(action, "insert_card") || !snapshot->storage_sd_present) {
        return "No card";
    }
    if (snapshot->storage_sd_needs_fat32) {
        return "Needs FAT32";
    }
    if (storage_text_equals(snapshot->storage_sd_state, "checking") ||
        storage_text_equals(snapshot->storage_sd_state, "mount_pending")) {
        return "Checking";
    }
    if (snapshot->storage_sd_mounted && snapshot->storage_sd_data_root_ready) {
        return "Ready";
    }
    if (snapshot->storage_sd_present || snapshot->storage_sd_mounted) {
        return "Setup incomplete";
    }
    return "Not ready";
}

static uint32_t storage_card_value_accent(const char *value)
{
    if (storage_text_equals(value, "Ready")) {
        return 0xA7F3D0;
    }
    if (storage_text_equals(value, "Needs attention") ||
        storage_text_equals(value, "Card needs attention") ||
        storage_text_equals(value, "DeskOS files need attention")) {
        return 0xFCA5A5;
    }
    if (storage_text_equals(value, "Not available") ||
        storage_text_equals(value, "No card") ||
        storage_text_equals(value, "No card inserted") ||
        storage_text_equals(value, "Status unavailable") ||
        storage_text_equals(value, "Card reader unavailable")) {
        return 0x93C5FD;
    }
    return 0xFBBF24;
}

static void format_storage_kb(char *dest, size_t dest_size, uint32_t kb, bool known)
{
    if (!dest || dest_size == 0U) {
        return;
    }
    if (!known) {
        snprintf(dest, dest_size, "%s", "Not reported");
    } else if (kb >= 1024U * 1024U) {
        const uint32_t whole = kb / (1024U * 1024U);
        const uint32_t tenth = ((kb % (1024U * 1024U)) * 10U) / (1024U * 1024U);
        snprintf(dest, dest_size, "%lu.%lu GB",
                 (unsigned long)whole, (unsigned long)tenth);
    } else if (kb >= 1024U) {
        snprintf(dest, dest_size, "%lu MB", (unsigned long)(kb / 1024U));
    } else {
        snprintf(dest, dest_size, "%lu KB", (unsigned long)kb);
    }
}

static bool storage_backend_uses_sd(const char *backend)
{
    return storage_text_equals(backend, "sd") ||
           storage_text_equals(backend, "mixed") ||
           storage_text_equals(backend, "sd_map_tiles_ready") ||
           storage_text_equals(backend, "sd_diagnostic_exports_ready");
}

static const char *storage_retained_backend_friendly(
    const d1l_app_snapshot_t *snapshot, const char *backend)
{
    if (storage_text_equals(backend, "sd") || storage_text_equals(backend, "mixed")) {
        return snapshot && snapshot->storage_retained_backup_degraded ?
            "SD; backup degraded" : "SD + internal backup";
    }
    if (storage_text_equals(backend, "nvs")) {
        return snapshot && snapshot->storage_retained_backup_degraded ?
            "Internal issue" : "Internal";
    }
    return "Unavailable";
}

static const char *storage_map_backend_friendly(const char *backend)
{
    if (storage_text_equals(backend, "sd_map_tiles_ready")) {
        return "SD card";
    }
    if (storage_text_equals(backend, "sd_pending_store_migration")) {
        return "Pending";
    }
    return "Unavailable";
}

static const char *storage_export_backend_friendly(const char *backend)
{
    if (storage_text_equals(backend, "sd_diagnostic_exports_ready")) {
        return "SD card";
    }
    return "USB only";
}

static const char *storage_data_summary(const d1l_app_snapshot_t *snapshot)
{
    const bool uses_sd = snapshot &&
        (storage_backend_uses_sd(snapshot->message_store_backend) ||
         storage_backend_uses_sd(snapshot->dm_store_backend) ||
         storage_backend_uses_sd(snapshot->packet_log_backend) ||
         storage_backend_uses_sd(snapshot->route_store_backend) ||
         storage_backend_uses_sd(snapshot->map_tile_backend) ||
         storage_backend_uses_sd(snapshot->export_backend));
    if (snapshot && snapshot->storage_retained_backup_degraded) {
        return uses_sd ? "SD; backup issue" : "Storage issue";
    }
    if (uses_sd) {
        return "SD + internal";
    }
    return "Internal";
}

static uint32_t storage_backend_accent(const char *backend)
{
    if (storage_backend_uses_sd(backend)) {
        return 0xA7F3D0;
    }
    if (storage_text_equals(backend, "sd_pending_store_migration")) {
        return 0xFBBF24;
    }
    return 0x93C5FD;
}

typedef struct {
    const char *state;
    const char *detail;
    const char *guidance;
    uint32_t accent;
} storage_hero_copy_t;

static storage_hero_copy_t storage_hero_copy(const d1l_app_snapshot_t *snapshot)
{
    storage_hero_copy_t copy = {
        .state = "Storage starting",
        .detail = "Checking saved-data storage.",
        .guidance = "Status updates automatically.",
        .accent = 0xFBBF24,
    };
    if (!snapshot) {
        return copy;
    }

    if (snapshot->storage_retained_backup_degraded) {
        if (snapshot->storage_retained_sd_degraded) {
            copy.state = "Saved storage needs attention";
            copy.detail = "SD and internal backup reported errors.";
            copy.guidance = "See USB diagnostics before relying on saved history.";
            copy.accent = 0xFCA5A5;
        } else if (snapshot->storage_data_enabled) {
            copy.state = "SD card ready";
            copy.detail = "Saved data is using SD.";
            copy.guidance = "Internal backup needs attention.";
            copy.accent = 0xFBBF24;
        } else {
            copy.state = "Storage needs attention";
            copy.detail = "Internal saved-data storage is unavailable.";
            copy.guidance = "See USB diagnostics before relying on saved history.";
            copy.accent = 0xFCA5A5;
        }
        return copy;
    }

    if (snapshot->storage_retained_sd_degraded) {
        copy.state = "SD needs attention";
        copy.detail = "Internal storage is active.";
        copy.guidance = "Saved data remains available.";
        copy.accent = 0xFCA5A5;
        return copy;
    }

    const char *action = snapshot->storage_setup_action;
    if (storage_snapshot_needs_attention(snapshot)) {
        copy.state = "Card needs attention";
        copy.detail = "Internal storage is active.";
        copy.guidance = "Technical details are available over USB.";
        copy.accent = 0xFCA5A5;
        return copy;
    }
    if (storage_text_equals(action, "wait_for_storage_reconnect")) {
        copy.state = "Card reader reconnecting";
        copy.detail = snapshot->storage_data_enabled ?
            "Last confirmed SD remains active briefly." :
            "Internal storage is active.";
        copy.guidance = snapshot->storage_data_enabled ?
            "Internal fallback takes over if status retries fail." :
            "SD access resumes after a valid status reply.";
    } else if (storage_text_equals(action, "bridge_unavailable")) {
        copy.detail = "SD support is unavailable.";
        copy.guidance = "Internal storage remains active.";
    } else if (storage_text_equals(action, "bridge_protocol_pending")) {
        copy.detail = "SD support is starting.";
    } else if (storage_text_equals(action, "run_storage_mount") ||
               storage_text_equals(action, "wait_for_storage_mount")) {
        copy.state = "Checking SD card";
        copy.detail = "Using internal storage for now.";
        copy.guidance = "The card check finishes automatically.";
    } else if (storage_text_equals(action, "insert_card")) {
        copy.state = "No SD card";
        copy.detail = "Internal storage is active.";
        copy.guidance = "Insert a FAT32 card for more space.";
    } else if (storage_text_equals(action, "prepare_fat32_on_computer")) {
        copy.state = "Card needs FAT32";
        copy.detail = "Prepare it on a computer.";
        copy.guidance = "Prepare as FAT32, then reinsert the card.";
    } else if (storage_text_equals(action, "backup_reformat_fat32_on_computer")) {
        copy.state = "Card needs FAT32";
        copy.detail = "Prepare it on a computer.";
        copy.guidance = "Prepare as FAT32, then reinsert the card.";
    } else if (storage_text_equals(action, "retry_storage_mount") ||
               storage_text_equals(action, "use_nvs_fallback")) {
        copy.state = "Card setup incomplete";
        copy.detail = "Internal storage is active.";
        copy.guidance = "Reinsert the card to finish DeskOS folders.";
    } else if (storage_text_equals(action, "forced_nvs")) {
        copy.state = "Internal storage only";
        copy.detail = "SD storage is paused.";
        copy.guidance = "Internal storage remains active.";
    } else if (snapshot->storage_data_enabled) {
        copy.state = "SD card ready";
        copy.detail = "SD is used with internal backup.";
        copy.guidance = "Saved data stays mirrored internally.";
        copy.accent = 0xA7F3D0;
    } else if (snapshot->storage_sd_data_root_ready) {
        copy.state = "SD card ready";
        copy.detail = "Using internal storage.";
        copy.guidance = "SD is ready; saved data stays internal.";
        copy.accent = 0xA7F3D0;
    }
    return copy;
}

static void render_storage_header(const char *title, lv_event_cb_t back_cb)
{
    create_button(s_storage_sheet, "Back", 12, 8, 76, 44, back_cb, NULL);
    lv_obj_t *heading = create_label(s_storage_sheet, title, 0xF4F7FB);
    lv_obj_set_style_text_font(heading, &lv_font_montserrat_24, 0);
    lv_label_set_long_mode(heading, LV_LABEL_LONG_DOT);
    lv_obj_set_width(heading, 360);
    lv_obj_set_pos(heading, 104, 13);
}

static void render_storage_safety_copy(void)
{
    lv_obj_t *panel = create_panel(s_storage_sheet, 16, 368, 448, 44);
    if (!panel) {
        return;
    }
    lv_obj_set_style_pad_all(panel, 8, 0);
    lv_obj_t *label = create_label(
        panel, "FAT32 only - This device never formats cards.", 0xFBBF24);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
}

static void render_storage_detail_row(lv_obj_t *parent, int y,
                                      const char *name, const char *value,
                                      uint32_t accent)
{
    lv_obj_t *name_label = create_label(parent, name, accent);
    lv_obj_set_width(name_label, 150);
    lv_obj_set_pos(name_label, 12, y);
    lv_obj_t *value_label = create_label(parent, value, 0xE5EDF5);
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(value_label, 254);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(value_label, 170, y);
}

static void render_storage_root(void)
{
    const storage_hero_copy_t hero = storage_hero_copy(&s_snapshot);
    render_storage_header("Storage", close_storage_sheet_event_cb);
    lv_obj_t *subtitle = create_label(s_storage_sheet,
                                      "Card and saved-data overview", 0x8EA0AE);
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_DOT);
    lv_obj_set_width(subtitle, 360);
    lv_obj_set_pos(subtitle, 104, 40);

    lv_obj_t *hero_panel = create_panel(s_storage_sheet, 16, 64, 448, 92);
    if (hero_panel) {
        lv_obj_set_style_pad_all(hero_panel, 0, 0);
        lv_obj_t *eyebrow = create_label(hero_panel, "Current storage", 0x8EA0AE);
        lv_obj_set_pos(eyebrow, 12, 8);
        lv_obj_t *state = create_label(hero_panel, hero.state, hero.accent);
        lv_obj_set_style_text_font(state, &lv_font_montserrat_24, 0);
        lv_label_set_long_mode(state, LV_LABEL_LONG_DOT);
        lv_obj_set_width(state, 424);
        lv_obj_set_pos(state, 12, 28);
        lv_obj_t *detail = create_label(hero_panel, hero.detail, 0x8EA0AE);
        lv_label_set_long_mode(detail, LV_LABEL_LONG_DOT);
        lv_obj_set_width(detail, 424);
        lv_obj_set_pos(detail, 12, 64);
    }

    lv_obj_t *card = create_button(s_storage_sheet, "Card status",
                                    16, 168, 448, 68,
                                    open_storage_card_status_event_cb, NULL);
    style_contact_option_button(card, hero.accent,
                                storage_card_state_friendly(&s_snapshot));

    lv_obj_t *data = create_button(s_storage_sheet, "Data locations",
                                    16, 248, 448, 68,
                                    open_storage_data_locations_event_cb, NULL);
    style_contact_option_button(data, 0x93C5FD, storage_data_summary(&s_snapshot));

    lv_obj_t *guidance = create_panel(s_storage_sheet, 16, 324, 448, 40);
    if (guidance) {
        lv_obj_set_style_pad_all(guidance, 0, 0);
        lv_obj_t *copy = create_label(guidance, hero.guidance, hero.accent);
        lv_label_set_long_mode(copy, LV_LABEL_LONG_DOT);
        lv_obj_set_width(copy, 424);
        lv_obj_set_pos(copy, 12, 10);
    }
}

static void render_storage_card_status(void)
{
    char capacity[24];
    char free_space[24];
    const char *state = storage_card_state_friendly(&s_snapshot);
    const char *readiness = storage_readiness_friendly(&s_snapshot);
    const bool capacity_known = s_snapshot.storage_sd_present &&
                                s_snapshot.storage_capacity_kb > 0U;
    format_storage_kb(capacity, sizeof(capacity), s_snapshot.storage_capacity_kb,
                      capacity_known);
    format_storage_kb(free_space, sizeof(free_space), s_snapshot.storage_free_kb,
                      capacity_known);

    render_storage_header("Card status", storage_subpage_back_event_cb);
    lv_obj_t *subtitle = create_label(s_storage_sheet,
                                      "Read-only card details", 0x8EA0AE);
    lv_obj_set_pos(subtitle, 104, 40);

    lv_obj_t *panel = create_panel(s_storage_sheet, 16, 68, 448, 288);
    if (!panel) {
        return;
    }
    lv_obj_set_style_pad_all(panel, 0, 0);
    render_storage_detail_row(panel, 18, "State", state,
                              storage_card_value_accent(state));
    render_storage_detail_row(panel, 70, "Filesystem",
                              storage_filesystem_friendly(&s_snapshot), 0x93C5FD);
    render_storage_detail_row(panel, 122, "Capacity", capacity, 0xC4B5FD);
    render_storage_detail_row(panel, 174, "Free space", free_space, 0xC4B5FD);
    render_storage_detail_row(panel, 226, "Readiness", readiness,
                              storage_card_value_accent(readiness));
}

static void render_storage_location_row(lv_obj_t *parent, int y,
                                        const char *name, const char *location_text,
                                        uint32_t accent)
{
    lv_obj_t *row = create_panel(parent, 0, y, 424, 48);
    if (!row) {
        return;
    }
    lv_obj_set_style_pad_all(row, 8, 0);
    lv_obj_t *name_label = create_label(row, name, 0xF4F7FB);
    lv_obj_set_width(name_label, 180);
    lv_obj_align(name_label, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *location = create_label(row, location_text, accent);
    lv_label_set_long_mode(location, LV_LABEL_LONG_DOT);
    lv_obj_set_width(location, 220);
    lv_obj_set_style_text_align(location, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(location, LV_ALIGN_RIGHT_MID, 0, 0);
}

static void render_storage_data_locations(void)
{
    render_storage_header("Data locations", storage_subpage_back_event_cb);
    lv_obj_t *subtitle = create_label(s_storage_sheet,
                                      "Where saved data is kept", 0x8EA0AE);
    lv_obj_set_pos(subtitle, 104, 40);

    lv_obj_t *list = create_object(s_storage_sheet, "storage locations list");
    if (!list) {
        return;
    }
    lv_obj_set_size(list, 448, 288);
    lv_obj_set_pos(list, 16, 68);
    lv_obj_set_style_radius(list, 8, 0);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x0B121A), 0);
    lv_obj_set_style_border_color(list, lv_color_hex(0x263241), 0);
    lv_obj_set_style_border_width(list, 1, 0);
    lv_obj_set_style_pad_all(list, 8, 0);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

    render_storage_location_row(list, 0, "Messages",
                                storage_retained_backend_friendly(&s_snapshot, s_snapshot.message_store_backend),
                                storage_backend_accent(s_snapshot.message_store_backend));
    render_storage_location_row(list, 56, "Direct messages",
                                storage_retained_backend_friendly(&s_snapshot, s_snapshot.dm_store_backend),
                                storage_backend_accent(s_snapshot.dm_store_backend));
    render_storage_location_row(list, 112, "Packets",
                                storage_retained_backend_friendly(&s_snapshot, s_snapshot.packet_log_backend),
                                storage_backend_accent(s_snapshot.packet_log_backend));
    render_storage_location_row(list, 168, "Routes",
                                storage_retained_backend_friendly(&s_snapshot, s_snapshot.route_store_backend),
                                storage_backend_accent(s_snapshot.route_store_backend));
    render_storage_location_row(list, 224, "Map tiles",
                                storage_map_backend_friendly(s_snapshot.map_tile_backend),
                                storage_backend_accent(s_snapshot.map_tile_backend));
    render_storage_location_row(list, 280, "Exports",
                                storage_export_backend_friendly(s_snapshot.export_backend),
                                storage_backend_accent(s_snapshot.export_backend));
}

static void render_storage_sheet(void)
{
    if (!s_storage_sheet) {
        return;
    }
    d1l_app_model_snapshot(&s_snapshot);
    lv_obj_clean(s_storage_sheet);

    switch (s_storage_page) {
    case D1L_STORAGE_PAGE_CARD_STATUS:
        render_storage_card_status();
        break;
    case D1L_STORAGE_PAGE_DATA_LOCATIONS:
        render_storage_data_locations();
        break;
    case D1L_STORAGE_PAGE_ROOT:
    default:
        s_storage_page = D1L_STORAGE_PAGE_ROOT;
        render_storage_root();
        break;
    }
    render_storage_safety_copy();
}

static void open_storage_sheet_event_cb(lv_event_t *event)
{
    (void)event;
    hide_sheet();
    hide_public_history_sheet();
    hide_public_search_sheet();
    hide_compose_sheet();
    hide_dm_thread_sheet();
    hide_radio_settings_sheet();
    hide_wifi_sheet();
    hide_ble_sheet();
    hide_display_sheet();
    hide_diagnostics_sheet();
    hide_map_location_sheet();
    hide_map_options_sheet();
    hide_contact_detail_sheet();
    hide_contact_export_sheet();
    hide_route_detail_sheet();
    hide_route_trace_sheet();
    hide_packet_detail_sheet();
    hide_packet_search_sheet();
    hide_mesh_roles_sheet();
    s_storage_page = D1L_STORAGE_PAGE_ROOT;
    render_storage_sheet();
    if (s_storage_sheet) {
        show_modal(s_storage_sheet);
    }
}

static void close_wifi_sheet_event_cb(lv_event_t *event)
{
    (void)event;
    hide_wifi_sheet();
}

static void close_ble_sheet_event_cb(lv_event_t *event)
{
    (void)event;
    hide_ble_sheet();
}

static void close_display_sheet_event_cb(lv_event_t *event)
{
    (void)event;
    hide_display_sheet();
}

static void close_diagnostics_sheet_event_cb(lv_event_t *event)
{
    (void)event;
    hide_diagnostics_sheet();
}

static void wifi_refresh_sheet(void)
{
    d1l_app_model_snapshot(&s_snapshot);
    render_wifi_sheet();
}

static void wifi_textarea_event_cb(lv_event_t *event)
{
    d1l_ui_keyboard_focus_textarea_from_event(s_wifi_keyboard, event,
                                              s_wifi_ssid_textarea,
                                              s_wifi_password_textarea);
}

static void wifi_save_event_cb(lv_event_t *event)
{
    (void)event;
    const char *ssid = s_wifi_ssid_textarea ? lv_textarea_get_text(s_wifi_ssid_textarea) : "";
    const char *password = s_wifi_password_textarea ? lv_textarea_get_text(s_wifi_password_textarea) : "";
    esp_err_t ret = d1l_app_model_save_wifi_profile(ssid, password && password[0] ? password : NULL);
    show_toast("Wi-Fi profile", ret);
    if (ret == ESP_OK) {
        wifi_refresh_sheet();
        request_content_refresh();
    }
}

static void map_location_keyboard_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_READY) {
        map_location_save_event_cb(event);
    } else if (code == LV_EVENT_CANCEL) {
        d1l_ui_keyboard_clear_textarea(s_map_location_keyboard);
        if (s_map_location_keyboard) {
            lv_obj_add_flag(s_map_location_keyboard, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void wifi_clear_event_cb(lv_event_t *event)
{
    (void)event;
    esp_err_t ret = d1l_app_model_clear_wifi_profile();
    show_toast("Wi-Fi clear", ret);
    if (ret == ESP_OK) {
        wifi_refresh_sheet();
        request_content_refresh();
    }
}

static void wifi_toggle_event_cb(lv_event_t *event)
{
    (void)event;
    esp_err_t ret = d1l_app_model_set_wifi_enabled(!s_snapshot.wifi_enabled);
    show_toast("Wi-Fi", ret);
    if (ret == ESP_OK) {
        wifi_refresh_sheet();
        request_content_refresh();
    }
}

static void wifi_scan_event_cb(lv_event_t *event)
{
    (void)event;
    esp_err_t ret = d1l_app_model_wifi_scan(&s_wifi_scan_result);
    s_wifi_scan_loaded = true;
    show_toast("Wi-Fi scan", ret);
    wifi_refresh_sheet();
    request_content_refresh();
}

static void wifi_connect_event_cb(lv_event_t *event)
{
    (void)event;
    esp_err_t ret = d1l_app_model_wifi_connect();
    show_toast("Wi-Fi connect", ret);
    wifi_refresh_sheet();
    request_content_refresh();
}

static void wifi_keyboard_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_READY) {
        wifi_save_event_cb(event);
    } else if (code == LV_EVENT_CANCEL) {
        hide_wifi_sheet();
    }
}

static void ble_toggle_event_cb(lv_event_t *event)
{
    (void)event;
    esp_err_t ret = d1l_app_model_set_ble_enabled(!s_snapshot.ble_companion_enabled);
    show_toast("BLE", ret);
    if (ret == ESP_OK) {
        d1l_app_model_snapshot(&s_snapshot);
        render_ble_sheet();
        request_content_refresh();
    }
}

static void render_wifi_sheet(void)
{
    if (!s_wifi_sheet) {
        return;
    }
    lv_obj_clean(s_wifi_sheet);
    s_wifi_ssid_textarea = NULL;
    s_wifi_password_textarea = NULL;
    s_wifi_keyboard = NULL;

    lv_obj_t *title = create_label(s_wifi_sheet, "Wi-Fi Setup", 0xF4F7FB);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, 180);
    lv_obj_set_pos(title, 16, 10);
    create_button(s_wifi_sheet, "Close", 392, 10, 72, 40, close_wifi_sheet_event_cb, NULL);
    lv_obj_t *subtitle = create_label(s_wifi_sheet, "Profile and state", 0x8EA0AE);
    lv_obj_set_pos(subtitle, 16, 36);

    lv_obj_t *state = create_label(s_wifi_sheet, "", s_snapshot.wifi_enabled ? 0x5EEAD4 : 0xFBBF24);
    label_set_fmt(state, "State %s  build %s",
                  s_snapshot.wifi_state ? s_snapshot.wifi_state : "off",
                  s_snapshot.wifi_build_enabled ? "enabled" : "not built");
    label_set_dot_width(state, 448);
    lv_obj_set_pos(state, 16, 58);

    lv_obj_t *link = create_label(s_wifi_sheet, "", 0x8EA0AE);
    if (s_snapshot.wifi_connected && s_snapshot.wifi_ip[0]) {
        label_set_fmt(link, "IP %s  RSSI %d  ch %u",
                      s_snapshot.wifi_ip,
                      s_snapshot.wifi_rssi_dbm,
                      (unsigned)s_snapshot.wifi_channel);
    } else {
        label_set_fmt(link, "Last %s",
                      s_snapshot.wifi_last_error ? s_snapshot.wifi_last_error : "none");
    }
    label_set_dot_width(link, 448);
    lv_obj_set_pos(link, 16, 80);

    lv_obj_t *profile = create_label(s_wifi_sheet, "", 0xE5EDF5);
    label_set_fmt(profile, "Profile %s  password %s",
                  s_snapshot.wifi_profile_saved ?
                  (s_snapshot.wifi_ssid[0] ? s_snapshot.wifi_ssid : "saved") : "not saved",
                  s_snapshot.wifi_password_saved ? "saved" : "open/empty");
    label_set_dot_width(profile, 448);
    lv_obj_set_pos(profile, 16, 102);

    lv_obj_t *ssid_label = create_label(s_wifi_sheet, "Network name", 0x5EEAD4);
    lv_obj_set_pos(ssid_label, 16, 130);
    s_wifi_ssid_textarea = create_textarea(s_wifi_sheet, "wifi ssid textarea");
    if (s_wifi_ssid_textarea) {
        lv_obj_set_size(s_wifi_ssid_textarea, 448, 36);
        lv_obj_set_pos(s_wifi_ssid_textarea, 16, 150);
        lv_textarea_set_one_line(s_wifi_ssid_textarea, true);
        lv_textarea_set_max_length(s_wifi_ssid_textarea, D1L_WIFI_SSID_LEN - 1U);
        lv_textarea_set_placeholder_text(s_wifi_ssid_textarea, "SSID");
        lv_textarea_set_text(s_wifi_ssid_textarea, s_snapshot.wifi_ssid);
        lv_obj_set_style_radius(s_wifi_ssid_textarea, 8, 0);
        lv_obj_set_style_bg_color(s_wifi_ssid_textarea, lv_color_hex(0x071018), 0);
        lv_obj_set_style_border_color(s_wifi_ssid_textarea, lv_color_hex(0x263241), 0);
        lv_obj_set_style_text_color(s_wifi_ssid_textarea, lv_color_hex(0xF4F7FB), 0);
        lv_obj_add_event_cb(s_wifi_ssid_textarea, wifi_textarea_event_cb, LV_EVENT_FOCUSED, NULL);
        lv_obj_add_event_cb(s_wifi_ssid_textarea, wifi_textarea_event_cb, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_t *password_label = create_label(s_wifi_sheet, "Password", 0x5EEAD4);
    lv_obj_set_pos(password_label, 16, 192);
    s_wifi_password_textarea = create_textarea(s_wifi_sheet, "wifi password textarea");
    if (s_wifi_password_textarea) {
        lv_obj_set_size(s_wifi_password_textarea, 448, 36);
        lv_obj_set_pos(s_wifi_password_textarea, 16, 212);
        lv_textarea_set_one_line(s_wifi_password_textarea, true);
        lv_textarea_set_password_mode(s_wifi_password_textarea, true);
        lv_textarea_set_max_length(s_wifi_password_textarea, D1L_WIFI_PASSWORD_LEN - 1U);
        lv_textarea_set_placeholder_text(s_wifi_password_textarea,
                                         s_snapshot.wifi_password_saved ? "Saved; enter to replace" : "Optional");
        lv_textarea_set_text(s_wifi_password_textarea, "");
        lv_obj_set_style_radius(s_wifi_password_textarea, 8, 0);
        lv_obj_set_style_bg_color(s_wifi_password_textarea, lv_color_hex(0x071018), 0);
        lv_obj_set_style_border_color(s_wifi_password_textarea, lv_color_hex(0x263241), 0);
        lv_obj_set_style_text_color(s_wifi_password_textarea, lv_color_hex(0xF4F7FB), 0);
        lv_obj_add_event_cb(s_wifi_password_textarea, wifi_textarea_event_cb, LV_EVENT_FOCUSED, NULL);
        lv_obj_add_event_cb(s_wifi_password_textarea, wifi_textarea_event_cb, LV_EVENT_CLICKED, NULL);
    }

    create_button(s_wifi_sheet, "Save", 16, 258, 62, 38, wifi_save_event_cb, NULL);
    create_button(s_wifi_sheet, "Clear", 86, 258, 66, 38, wifi_clear_event_cb, NULL);
    create_button(s_wifi_sheet, "Scan", 160, 258, 62, 38, wifi_scan_event_cb, NULL);
    create_button(s_wifi_sheet, "Connect", 230, 258, 86, 38, wifi_connect_event_cb, NULL);
    create_button(s_wifi_sheet, s_snapshot.wifi_enabled ? "Disable" : "Enable",
                  324, 258, 86, 38, wifi_toggle_event_cb, NULL);

    lv_obj_t *scan = create_label(s_wifi_sheet, "", 0x8EA0AE);
    if (s_wifi_scan_loaded) {
        const char *first = s_wifi_scan_result.returned_count > 0U ?
            s_wifi_scan_result.aps[0].ssid : "none";
        label_set_fmt(scan, "Scan %s  %u/%u networks  strongest %s",
                      s_wifi_scan_result.reason ? s_wifi_scan_result.reason : "unknown",
                      (unsigned)s_wifi_scan_result.returned_count,
                      (unsigned)s_wifi_scan_result.total_count,
                      first);
    } else {
        label_set_fmt(scan, "Scan to list nearby 2.4 GHz networks");
    }
    label_set_dot_width(scan, 448);
    lv_obj_set_pos(scan, 16, 304);

    s_wifi_keyboard = create_keyboard(s_wifi_sheet, "wifi keyboard");
    if (s_wifi_keyboard) {
        d1l_ui_keyboard_configure_input(s_wifi_keyboard, s_wifi_ssid_textarea,
                                        16, 330, 448, 82);
        lv_obj_add_event_cb(s_wifi_keyboard, wifi_keyboard_event_cb, LV_EVENT_READY, NULL);
        lv_obj_add_event_cb(s_wifi_keyboard, wifi_keyboard_event_cb, LV_EVENT_CANCEL, NULL);
    }
}

static void render_ble_sheet(void)
{
    if (!s_ble_sheet) {
        return;
    }
    lv_obj_clean(s_ble_sheet);

    lv_obj_t *title = create_label(s_ble_sheet, "BLE Setup", 0xF4F7FB);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(title, 8, 4);
    create_button(s_ble_sheet, "Close", 340, 0, 76, 40, close_ble_sheet_event_cb, NULL);

    lv_obj_t *state = create_label(s_ble_sheet, "", s_snapshot.ble_companion_enabled ? 0xA7F3D0 : 0xFBBF24);
    label_set_fmt(state, "State %s  build %s",
                  s_snapshot.ble_state ? s_snapshot.ble_state : "off",
                  s_snapshot.ble_build_enabled ? "enabled" : "not built");
    label_set_dot_width(state, 408);
    lv_obj_set_pos(state, 8, 54);

    const bool ble_transport_supported = s_snapshot.ble_transport_supported;
    lv_obj_t *purpose = create_label(s_ble_sheet,
                                     ble_transport_supported ?
                                     "Companion BLE is available for measured local setup." :
                                     "BLE companion transport is unavailable in this release.",
                                     0xE5EDF5);
    lv_label_set_long_mode(purpose, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(purpose, 408);
    lv_obj_set_pos(purpose, 8, 88);

    lv_obj_t *runtime = create_label(s_ble_sheet,
                                     ble_transport_supported ?
                                     "Pairing controls require a measured BLE runtime artifact." :
                                     "No BLE pairing or transport artifact is present for public release.",
                                     0xFBBF24);
    lv_label_set_long_mode(runtime, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(runtime, 408);
    lv_obj_set_pos(runtime, 8, 144);

    if (ble_transport_supported) {
        lv_obj_t *enable = create_button(s_ble_sheet,
                                         s_snapshot.ble_companion_enabled ? "Disable" : "Enable",
                                         8, 206, 98, 40, ble_toggle_event_cb, NULL);
        lv_obj_t *pair = create_button(s_ble_sheet, "Pair", 116, 206, 98, 40, NULL, NULL);
        lv_obj_t *forget = create_button(s_ble_sheet, "Forget", 224, 206, 98, 40, NULL, NULL);
        if (pair) {
            lv_obj_add_state(pair, LV_STATE_DISABLED);
        }
        if (forget) {
            lv_obj_add_state(forget, LV_STATE_DISABLED);
        }
        (void)enable;
    } else {
        lv_obj_t *enable_status = create_label(s_ble_sheet, "Enable unavailable", 0xFBBF24);
        lv_obj_t *pair = create_label(s_ble_sheet, "Pair unavailable", 0x8EA0AE);
        lv_obj_t *forget = create_label(s_ble_sheet, "Forget unavailable", 0x8EA0AE);
        lv_obj_set_pos(enable_status, 8, 206);
        lv_obj_set_pos(pair, 8, 232);
        lv_obj_set_pos(forget, 210, 232);
    }

    lv_obj_t *note = create_label(s_ble_sheet,
                                  "USB remains the reliable companion path for production validation.",
                                  0x8EA0AE);
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(note, 408);
    lv_obj_set_pos(note, 8, 278);
}

static void render_display_sheet(void)
{
    if (!s_display_sheet) {
        return;
    }
    lv_obj_clean(s_display_sheet);

    lv_obj_t *title = create_label(s_display_sheet, "Display", 0xF4F7FB);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(title, 8, 4);
    create_button(s_display_sheet, "Close", 340, 0, 76, 40,
                  close_display_sheet_event_cb, NULL);

    lv_obj_t *state = create_label(s_display_sheet, "Screen controls", 0x5EEAD4);
    lv_obj_set_pos(state, 8, 54);
    lv_obj_t *summary = create_label(s_display_sheet,
                                     "Brightness, night mode, contrast, and timeout belong here.",
                                     0xE5EDF5);
    lv_label_set_long_mode(summary, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(summary, 408);
    lv_obj_set_pos(summary, 8, 88);

    lv_obj_t *brightness = create_button(s_display_sheet, "Brightness", 8, 144, 126, 44, NULL, NULL);
    lv_obj_t *night = create_button(s_display_sheet, "Night", 144, 144, 86, 44, NULL, NULL);
    lv_obj_t *contrast = create_button(s_display_sheet, "Contrast", 240, 144, 106, 44, NULL, NULL);
    lv_obj_t *timeout = create_button(s_display_sheet, "Timeout", 8, 196, 126, 44, NULL, NULL);
    if (brightness) {
        lv_obj_add_state(brightness, LV_STATE_DISABLED);
    }
    if (night) {
        lv_obj_add_state(night, LV_STATE_DISABLED);
    }
    if (contrast) {
        lv_obj_add_state(contrast, LV_STATE_DISABLED);
    }
    if (timeout) {
        lv_obj_add_state(timeout, LV_STATE_DISABLED);
    }

    lv_obj_t *note = create_label(s_display_sheet,
                                  "Touch display controls are staged until backlight/runtime persistence is wired.",
                                  0xFBBF24);
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(note, 408);
    lv_obj_set_pos(note, 8, 260);
}

static void render_diagnostics_sheet(void)
{
    if (!s_diagnostics_sheet) {
        return;
    }
    d1l_app_model_snapshot(&s_snapshot);
    lv_obj_clean(s_diagnostics_sheet);

    lv_obj_t *title = create_label(s_diagnostics_sheet, "Diagnostics", 0xF4F7FB);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(title, 8, 4);
    create_button(s_diagnostics_sheet, "Close", 340, 0, 76, 40,
                  close_diagnostics_sheet_event_cb, NULL);

    lv_obj_t *health = create_label(s_diagnostics_sheet, "Health", 0x5EEAD4);
    lv_obj_set_pos(health, 8, 50);
    render_health_line(s_diagnostics_sheet, 76, &s_snapshot);

    lv_obj_t *uptime = create_label(s_diagnostics_sheet, "", 0xE5EDF5);
    label_set_fmt(uptime, "uptime %lus  mesh %s",
                  (unsigned long)(s_snapshot.uptime_ms / 1000U),
                  s_snapshot.mesh_state ? s_snapshot.mesh_state : "unknown");
    label_set_dot_width(uptime, 408);
    lv_obj_set_pos(uptime, 8, 104);

    lv_obj_t *heap = create_label(s_diagnostics_sheet, "", 0x8EA0AE);
    label_set_fmt(heap, "heap %luK free  min %luK  largest %luK",
                  (unsigned long)(s_snapshot.heap_free / 1024U),
                  (unsigned long)(s_snapshot.heap_min_free / 1024U),
                  (unsigned long)(s_snapshot.heap_largest_free / 1024U));
    label_set_dot_width(heap, 408);
    lv_obj_set_pos(heap, 8, 132);

    lv_obj_t *lvgl = create_label(s_diagnostics_sheet, "", 0x8EA0AE);
    label_set_fmt(lvgl, "LVGL free %lu  largest %lu  used %u%%",
                  (unsigned long)s_snapshot.lvgl_free_bytes,
                  (unsigned long)s_snapshot.lvgl_largest_free_bytes,
                  s_snapshot.lvgl_used_pct);
    label_set_dot_width(lvgl, 408);
    lv_obj_set_pos(lvgl, 8, 160);

    lv_obj_t *stack = create_label(s_diagnostics_sheet, "", 0x8EA0AE);
    label_set_fmt(stack, "ui stack %lu words  console stack %lu",
                  (unsigned long)s_snapshot.ui_task_stack_free_words,
                  (unsigned long)s_snapshot.current_task_stack_free_words);
    label_set_dot_width(stack, 408);
    lv_obj_set_pos(stack, 8, 188);

    lv_obj_t *mesh = create_label(s_diagnostics_sheet, "", 0x93C5FD);
    label_set_fmt(mesh, "packets rx %lu tx %lu  rejected %lu",
                  (unsigned long)s_snapshot.rx_packets,
                  (unsigned long)s_snapshot.tx_packets,
                  (unsigned long)s_snapshot.rejected_commands);
    label_set_dot_width(mesh, 408);
    lv_obj_set_pos(mesh, 8, 216);

    lv_obj_t *stores = create_label(s_diagnostics_sheet, "Crashlog  Exports  Serial", 0xF4F7FB);
    lv_obj_set_pos(stores, 8, 246);
    lv_obj_t *note = create_label(s_diagnostics_sheet,
                                  "Advanced details stay here so normal screens remain simple.",
                                  0xFBBF24);
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(note, 408);
    lv_obj_set_pos(note, 8, 268);

    lv_obj_t *crashlog = create_button(s_diagnostics_sheet, "Crashlog", 8, 316, 112, 44, NULL, NULL);
    lv_obj_t *export = create_button(s_diagnostics_sheet, "Export", 132, 316, 96, 44, NULL, NULL);
    lv_obj_t *soak = create_button(s_diagnostics_sheet, "Soak", 240, 316, 86, 44, NULL, NULL);
    if (crashlog) {
        lv_obj_add_state(crashlog, LV_STATE_DISABLED);
    }
    if (export) {
        lv_obj_add_state(export, LV_STATE_DISABLED);
    }
    if (soak) {
        lv_obj_add_state(soak, LV_STATE_DISABLED);
    }
}

static void open_wifi_sheet_event_cb(lv_event_t *event)
{
    (void)event;
    d1l_app_model_snapshot(&s_snapshot);
    hide_sheet();
    hide_public_history_sheet();
    hide_public_search_sheet();
    hide_compose_sheet();
    hide_dm_thread_sheet();
    hide_radio_settings_sheet();
    hide_storage_sheet();
    hide_ble_sheet();
    hide_display_sheet();
    hide_diagnostics_sheet();
    hide_map_location_sheet();
    hide_map_options_sheet();
    hide_contact_detail_sheet();
    hide_contact_export_sheet();
    hide_route_detail_sheet();
    hide_route_trace_sheet();
    hide_packet_detail_sheet();
    hide_packet_search_sheet();
    hide_mesh_roles_sheet();
    render_wifi_sheet();
    if (s_wifi_sheet) {
        show_modal(s_wifi_sheet);
    }
}

static void open_ble_sheet_event_cb(lv_event_t *event)
{
    (void)event;
    d1l_app_model_snapshot(&s_snapshot);
    hide_sheet();
    hide_public_history_sheet();
    hide_public_search_sheet();
    hide_compose_sheet();
    hide_dm_thread_sheet();
    hide_radio_settings_sheet();
    hide_storage_sheet();
    hide_wifi_sheet();
    hide_display_sheet();
    hide_diagnostics_sheet();
    hide_map_location_sheet();
    hide_map_options_sheet();
    hide_contact_detail_sheet();
    hide_contact_export_sheet();
    hide_route_detail_sheet();
    hide_route_trace_sheet();
    hide_packet_detail_sheet();
    hide_packet_search_sheet();
    hide_mesh_roles_sheet();
    render_ble_sheet();
    if (s_ble_sheet) {
        show_modal(s_ble_sheet);
    }
}

static void open_display_sheet_event_cb(lv_event_t *event)
{
    (void)event;
    d1l_app_model_snapshot(&s_snapshot);
    hide_sheet();
    hide_public_history_sheet();
    hide_public_search_sheet();
    hide_compose_sheet();
    hide_dm_thread_sheet();
    hide_radio_settings_sheet();
    hide_storage_sheet();
    hide_wifi_sheet();
    hide_ble_sheet();
    hide_diagnostics_sheet();
    hide_map_location_sheet();
    hide_map_options_sheet();
    hide_contact_detail_sheet();
    hide_contact_export_sheet();
    hide_route_detail_sheet();
    hide_route_trace_sheet();
    hide_packet_detail_sheet();
    hide_packet_search_sheet();
    hide_mesh_roles_sheet();
    render_display_sheet();
    if (s_display_sheet) {
        show_modal(s_display_sheet);
    }
}

static void open_diagnostics_sheet_event_cb(lv_event_t *event)
{
    (void)event;
    d1l_app_model_snapshot(&s_snapshot);
    hide_sheet();
    hide_public_history_sheet();
    hide_public_search_sheet();
    hide_compose_sheet();
    hide_dm_thread_sheet();
    hide_radio_settings_sheet();
    hide_storage_sheet();
    hide_wifi_sheet();
    hide_ble_sheet();
    hide_display_sheet();
    hide_map_location_sheet();
    hide_map_options_sheet();
    hide_contact_detail_sheet();
    hide_contact_export_sheet();
    hide_route_detail_sheet();
    hide_route_trace_sheet();
    hide_packet_detail_sheet();
    hide_packet_search_sheet();
    hide_mesh_roles_sheet();
    render_diagnostics_sheet();
    if (s_diagnostics_sheet) {
        show_modal(s_diagnostics_sheet);
    }
}

static void advert_zero_event_cb(lv_event_t *event)
{
    (void)event;
    hide_sheet();
    hide_public_history_sheet();
    hide_public_search_sheet();
    show_toast("Zero advert", d1l_app_model_request_advert(false));
}

static void advert_flood_event_cb(lv_event_t *event)
{
    (void)event;
    hide_sheet();
    hide_public_history_sheet();
    hide_public_search_sheet();
    show_toast("Flood advert", d1l_app_model_request_advert(true));
}

static void open_sheet_event_cb(lv_event_t *event)
{
    (void)event;
    if (s_sheet) {
        hide_public_history_sheet();
        hide_public_search_sheet();
        hide_dm_thread_sheet();
        hide_radio_settings_sheet();
        hide_storage_sheet();
        hide_wifi_sheet();
        hide_ble_sheet();
        hide_display_sheet();
        hide_diagnostics_sheet();
        hide_map_location_sheet();
        hide_map_options_sheet();
        hide_contact_detail_sheet();
        hide_contact_export_sheet();
        hide_route_detail_sheet();
        hide_route_trace_sheet();
        hide_packet_detail_sheet();
        hide_packet_search_sheet();
        hide_mesh_roles_sheet();
        show_modal(s_sheet);
    }
}

static void close_sheet_event_cb(lv_event_t *event)
{
    (void)event;
    hide_sheet();
}

static void render_active_tab(void)
{
    static const d1l_ui_screen_renderer_t renderers[] = {
        {D1L_UI_TAB_HOME, render_home_screen},
        {D1L_UI_TAB_MESSAGES, render_messages},
        {D1L_UI_TAB_NODES, render_nodes},
        {D1L_UI_TAB_MAP, render_map},
        {D1L_UI_TAB_PACKETS, render_packets},
        {D1L_UI_TAB_SETTINGS, render_settings},
    };

    if (!s_content) {
        return;
    }
    /* Release before d1l_ui_screen_render cleans the old Map viewport. */
    d1l_ui_map_viewport_release();
    d1l_app_model_snapshot(&s_snapshot);
    update_chrome(&s_snapshot);
    layout_content_for_active_tab();
    (void)d1l_ui_screen_render(d1l_ui_navigation_active(), &s_snapshot, s_content,
                               renderers, sizeof(renderers) / sizeof(renderers[0]));
    update_onboarding_visibility(&s_snapshot);
    remember_rendered_content_generation(&s_snapshot);
    remember_rendered_map_input(&s_snapshot);
    request_full_screen_repaint();
}

esp_err_t d1l_ui_phase1_request_tab(const char *name)
{
    d1l_ui_tab_t tab = D1L_UI_TAB_HOME;
    if (!tab_from_name(name, &tab)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_started || !s_content) {
        return ESP_ERR_INVALID_STATE;
    }
    set_map_interactive_touch_authorized(false);
    if (tab == D1L_UI_TAB_MAP) {
        d1l_ui_map_viewport_suppress_next_acquire();
    }
    request_tab_switch(tab);
    return ESP_OK;
}

esp_err_t d1l_ui_phase1_scroll_probe(const char *surface,
                                      d1l_ui_scroll_probe_result_t *result)
{
    char canonical[24] = {0};
    d1l_ui_tab_t tab = D1L_UI_TAB_HOME;
    uint32_t request_id = 0U;
    if (!result || !scroll_surface_from_name(surface, canonical, sizeof(canonical), &tab)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_started || !s_content || !s_scroll_probe_done_sem) {
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL(&s_scroll_probe_lock);
    if (!probe_gate_reserve_admission(&s_scroll_probe_gate)) {
        portEXIT_CRITICAL(&s_scroll_probe_lock);
        return ESP_ERR_INVALID_STATE;
    }
    portEXIT_CRITICAL(&s_scroll_probe_lock);

    /* The idle gate is reserved before stale-token drain, so a late producer
     * cannot publish between drain and admission. */
    while (xSemaphoreTake(s_scroll_probe_done_sem, 0) == pdTRUE) {
    }

    portENTER_CRITICAL(&s_scroll_probe_lock);
    request_id = probe_gate_commit_admission(&s_scroll_probe_gate);
    if (request_id == 0U) {
        probe_gate_abort_admission(&s_scroll_probe_gate);
        portEXIT_CRITICAL(&s_scroll_probe_lock);
        return ESP_ERR_INVALID_STATE;
    }
    memset(&s_scroll_probe_result, 0, sizeof(s_scroll_probe_result));
    snprintf(s_scroll_probe_surface, sizeof(s_scroll_probe_surface), "%s", canonical);
    portEXIT_CRITICAL(&s_scroll_probe_lock);

    if (xSemaphoreTake(s_scroll_probe_done_sem,
                       pdMS_TO_TICKS(D1L_UI_SCROLL_PROBE_TIMEOUT_MS)) != pdTRUE) {
        portENTER_CRITICAL(&s_scroll_probe_lock);
        probe_gate_timeout(&s_scroll_probe_gate, request_id);
        portEXIT_CRITICAL(&s_scroll_probe_lock);
        return ESP_ERR_TIMEOUT;
    }

    portENTER_CRITICAL(&s_scroll_probe_lock);
    if (s_scroll_probe_gate.completed_id != request_id ||
        s_scroll_probe_gate.inflight_id != request_id) {
        probe_gate_timeout(&s_scroll_probe_gate, request_id);
        portEXIT_CRITICAL(&s_scroll_probe_lock);
        return ESP_ERR_INVALID_STATE;
    }
    *result = s_scroll_probe_result;
    if (!probe_gate_acknowledge(&s_scroll_probe_gate, request_id)) {
        portEXIT_CRITICAL(&s_scroll_probe_lock);
        return ESP_ERR_INVALID_STATE;
    }
    portEXIT_CRITICAL(&s_scroll_probe_lock);
    return ESP_OK;
}

esp_err_t d1l_ui_phase1_compose_probe(const char *target,
                                       d1l_ui_compose_probe_result_t *result)
{
    char canonical[16] = {0};
    uint32_t request_id = 0U;
    if (!result || !compose_probe_target_from_name(target, canonical, sizeof(canonical))) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_started || !s_content || !s_compose_probe_done_sem) {
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL(&s_compose_probe_lock);
    if (!probe_gate_reserve_admission(&s_compose_probe_gate)) {
        portEXIT_CRITICAL(&s_compose_probe_lock);
        return ESP_ERR_INVALID_STATE;
    }
    portEXIT_CRITICAL(&s_compose_probe_lock);

    while (xSemaphoreTake(s_compose_probe_done_sem, 0) == pdTRUE) {
    }

    portENTER_CRITICAL(&s_compose_probe_lock);
    request_id = probe_gate_commit_admission(&s_compose_probe_gate);
    if (request_id == 0U) {
        probe_gate_abort_admission(&s_compose_probe_gate);
        portEXIT_CRITICAL(&s_compose_probe_lock);
        return ESP_ERR_INVALID_STATE;
    }
    memset(&s_compose_probe_result, 0, sizeof(s_compose_probe_result));
    snprintf(s_compose_probe_target, sizeof(s_compose_probe_target), "%s", canonical);
    portEXIT_CRITICAL(&s_compose_probe_lock);

    if (xSemaphoreTake(s_compose_probe_done_sem,
                       pdMS_TO_TICKS(D1L_UI_COMPOSE_PROBE_TIMEOUT_MS)) != pdTRUE) {
        portENTER_CRITICAL(&s_compose_probe_lock);
        probe_gate_timeout(&s_compose_probe_gate, request_id);
        portEXIT_CRITICAL(&s_compose_probe_lock);
        return ESP_ERR_TIMEOUT;
    }

    portENTER_CRITICAL(&s_compose_probe_lock);
    if (s_compose_probe_gate.completed_id != request_id ||
        s_compose_probe_gate.inflight_id != request_id) {
        probe_gate_timeout(&s_compose_probe_gate, request_id);
        portEXIT_CRITICAL(&s_compose_probe_lock);
        return ESP_ERR_INVALID_STATE;
    }
    *result = s_compose_probe_result;
    if (!probe_gate_acknowledge(&s_compose_probe_gate, request_id)) {
        portEXIT_CRITICAL(&s_compose_probe_lock);
        return ESP_ERR_INVALID_STATE;
    }
    portEXIT_CRITICAL(&s_compose_probe_lock);
    return ESP_OK;
}

const char *d1l_ui_phase1_active_tab_name(void)
{
    return tab_name(d1l_ui_navigation_active());
}

const char *d1l_ui_phase1_pending_tab_name(void)
{
    return tab_name(d1l_ui_navigation_pending());
}

bool d1l_ui_phase1_tab_switch_pending(void)
{
    return d1l_ui_navigation_switch_pending();
}

static void fill_capture_status(d1l_ui_capture_status_t *status)
{
    if (!status) {
        return;
    }
    memset(status, 0, sizeof(*status));
    status->available = s_capture_shadow != NULL && s_capture_snapshot != NULL && s_capture_lock != NULL;
    status->active = s_capture_active;
    status->shadow_ready = s_capture_shadow_ready;
    status->started = s_started;
    status->onboarding_visible = s_onboarding_visible;
    status->lock_visible = s_lock_visible;
    status->tab_pending = d1l_ui_phase1_tab_switch_pending();
    status->content_pending = content_refresh_pending();
    status->render_pending = status->tab_pending || status->content_pending ||
        scroll_probe_pending() || compose_probe_pending();
    status->width = D1L_UI_CAPTURE_WIDTH;
    status->height = D1L_UI_CAPTURE_HEIGHT;
    status->bytes_per_pixel = D1L_UI_CAPTURE_BYTES_PER_PIXEL;
    status->total_bytes = D1L_UI_CAPTURE_TOTAL_BYTES;
    status->max_chunk_bytes = D1L_UI_CAPTURE_MAX_CHUNK_BYTES;
    status->frame_seq = s_capture_frame_seq;
    status->flush_count = s_capture_flush_count;
    status->capture_crc32 = s_capture_crc32;
    snprintf(status->active_tab, sizeof(status->active_tab), "%s", d1l_ui_phase1_active_tab_name());
    snprintf(status->pending_tab, sizeof(status->pending_tab), "%s", d1l_ui_phase1_pending_tab_name());
}

esp_err_t d1l_ui_capture_status(d1l_ui_capture_status_t *out_status)
{
    if (!out_status) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_capture_lock) {
        fill_capture_status(out_status);
        return ESP_OK;
    }
    xSemaphoreTake(s_capture_lock, portMAX_DELAY);
    fill_capture_status(out_status);
    xSemaphoreGive(s_capture_lock);
    return ESP_OK;
}

esp_err_t d1l_ui_capture_begin(d1l_ui_capture_status_t *out_status)
{
    if (!s_capture_lock || !s_capture_shadow || !s_capture_snapshot) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_capture_lock, portMAX_DELAY);
    if (!s_capture_shadow_ready || s_capture_active) {
        xSemaphoreGive(s_capture_lock);
        return ESP_ERR_INVALID_STATE;
    }
    d1l_ui_map_viewport_set_suppressed(true);
    memcpy(s_capture_snapshot, s_capture_shadow, D1L_UI_CAPTURE_TOTAL_BYTES);
    s_capture_crc32 = crc32_bytes(s_capture_snapshot, D1L_UI_CAPTURE_TOTAL_BYTES);
    s_capture_active = true;
    fill_capture_status(out_status);
    xSemaphoreGive(s_capture_lock);
    return ESP_OK;
}

esp_err_t d1l_ui_capture_chunk(size_t offset,
                               size_t requested_len,
                               uint8_t *out_data,
                               size_t out_capacity,
                               size_t *out_len,
                               uint32_t *out_crc32,
                               d1l_ui_capture_status_t *out_status)
{
    if (!out_data || !out_len || out_capacity == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_len = 0U;
    if (out_crc32) {
        *out_crc32 = 0U;
    }
    if (!s_capture_lock || !s_capture_snapshot) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_capture_lock, portMAX_DELAY);
    if (!s_capture_active) {
        xSemaphoreGive(s_capture_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (offset >= D1L_UI_CAPTURE_TOTAL_BYTES || requested_len == 0U) {
        xSemaphoreGive(s_capture_lock);
        return ESP_ERR_INVALID_SIZE;
    }
    size_t len = requested_len;
    const size_t remaining = D1L_UI_CAPTURE_TOTAL_BYTES - offset;
    if (len > remaining) {
        len = remaining;
    }
    if (len > D1L_UI_CAPTURE_MAX_CHUNK_BYTES) {
        len = D1L_UI_CAPTURE_MAX_CHUNK_BYTES;
    }
    if (len > out_capacity) {
        len = out_capacity;
    }
    memcpy(out_data, s_capture_snapshot + offset, len);
    if (out_crc32) {
        *out_crc32 = crc32_bytes(out_data, len);
    }
    *out_len = len;
    fill_capture_status(out_status);
    xSemaphoreGive(s_capture_lock);
    return ESP_OK;
}

esp_err_t d1l_ui_capture_end(d1l_ui_capture_status_t *out_status)
{
    if (!s_capture_lock) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_capture_lock, portMAX_DELAY);
    if (!s_capture_active) {
        xSemaphoreGive(s_capture_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_capture_active = false;
    fill_capture_status(out_status);
    xSemaphoreGive(s_capture_lock);
    d1l_ui_map_viewport_set_suppressed(false);
    if (d1l_ui_navigation_active() == D1L_UI_TAB_MAP &&
        !d1l_ui_modal_has_active() && !s_lock_visible && !s_onboarding_visible &&
        map_interactive_touch_authorized()) {
        request_content_refresh();
    }
    return ESP_OK;
}

static void request_tab_event_cb(lv_event_t *event)
{
    if (!event) {
        return;
    }
    const d1l_ui_tab_t *tab = (const d1l_ui_tab_t *)lv_event_get_user_data(event);
    if (tab) {
        set_map_interactive_touch_authorized(*tab == D1L_UI_TAB_MAP);
        request_tab_switch(*tab);
    }
}

static void dock_event_cb(lv_event_t *event)
{
    request_tab_event_cb(event);
}

static void process_pending_tab_switch(void)
{
    d1l_ui_tab_t rendered_tab;
    if (!begin_pending_tab_switch(&rendered_tab)) {
        return;
    }
    d1l_ui_map_viewport_release();
    hide_sheet();
    hide_public_history_sheet();
    hide_public_search_sheet();
    hide_compose_sheet();
    hide_message_detail_sheet();
    hide_dm_thread_sheet();
    hide_radio_settings_sheet();
    hide_storage_sheet();
    hide_wifi_sheet();
    hide_ble_sheet();
    hide_display_sheet();
    hide_diagnostics_sheet();
    hide_map_location_sheet();
    hide_map_options_sheet();
    hide_contact_detail_sheet();
    hide_contact_export_sheet();
    hide_route_detail_sheet();
    hide_route_trace_sheet();
    hide_packet_detail_sheet();
    hide_packet_search_sheet();
    hide_mesh_roles_sheet();
    render_active_tab();
    force_ui_layout_repaint();
    finish_pending_tab_switch(rendered_tab);
}

static void process_pending_content_refresh(void)
{
    if (!begin_pending_content_refresh()) {
        return;
    }
    if (d1l_ui_phase1_tab_switch_pending()) {
        request_content_refresh();
        return;
    }
    render_active_tab();
    force_ui_layout_repaint();
}

static lv_obj_t *find_scrollable_descendant(lv_obj_t *root, lv_obj_t **fallback)
{
    if (!root) {
        return NULL;
    }
    lv_obj_update_layout(root);
    if (lv_obj_has_flag(root, LV_OBJ_FLAG_SCROLLABLE)) {
        if (!*fallback) {
            *fallback = root;
        }
        if (lv_obj_get_scroll_bottom(root) > 0 || lv_obj_get_scroll_top(root) > 0) {
            return root;
        }
    }

    const uint32_t child_count = lv_obj_get_child_cnt(root);
    for (uint32_t i = 0; i < child_count; ++i) {
        lv_obj_t *child = lv_obj_get_child(root, (int32_t)i);
        lv_obj_t *target = find_scrollable_descendant(child, fallback);
        if (target) {
            return target;
        }
    }
    return NULL;
}

static lv_obj_t *scroll_probe_find_target(lv_obj_t *root)
{
    lv_obj_t *fallback = NULL;
    lv_obj_t *target = find_scrollable_descendant(root, &fallback);
    return target ? target : fallback;
}

static void scroll_probe_contact_entry(d1l_contact_entry_t *entry)
{
    if (!entry) {
        return;
    }
    memset(entry, 0, sizeof(*entry));
    d1l_app_model_snapshot(&s_snapshot);
    if (s_snapshot.recent_contact_count > 0 &&
        s_snapshot.recent_contacts[0].fingerprint[0] != '\0') {
        *entry = s_snapshot.recent_contacts[0];
        return;
    }
    snprintf(entry->fingerprint, sizeof(entry->fingerprint), "%s", "C0DEC0DEC0DEC0DE");
    snprintf(entry->public_key_hex, sizeof(entry->public_key_hex), "%s",
             "C0DEC0DEC0DEC0DEC0DEC0DEC0DEC0DEC0DEC0DEC0DEC0DEC0DEC0DEC0DEC0DE");
    snprintf(entry->alias, sizeof(entry->alias), "%s", "Contact Probe");
    snprintf(entry->heard_name, sizeof(entry->heard_name), "%s", "Contact Probe");
    snprintf(entry->type, sizeof(entry->type), "%s", "chat");
    entry->last_rssi_dbm = -42;
    entry->last_snr_tenths = 30;
    entry->out_path_valid = true;
}

static lv_obj_t *scroll_probe_open_contact_surface(const char *surface)
{
    d1l_contact_entry_t contact = {0};
    scroll_probe_contact_entry(&contact);
    hide_contact_detail_sheet();
    s_contact_detail_contact = contact;

    if (strcmp(surface, "contact_detail") == 0) {
        show_contact_detail_sheet();
        return s_contact_detail_sheet;
    }
    if (strcmp(surface, "contact_options") == 0) {
        show_contact_options_sheet();
        return s_contact_options_sheet;
    }
    if (strcmp(surface, "contact_forget") == 0) {
        render_contact_forget_sheet();
        if (s_contact_forget_sheet) {
            show_modal(s_contact_forget_sheet);
        }
        return s_contact_forget_sheet;
    }
    if (strcmp(surface, "contact_route") == 0) {
        s_route_trace_contact = contact;
        render_route_trace_sheet();
        if (s_route_trace_sheet) {
            show_modal(s_route_trace_sheet);
        }
        return s_route_trace_sheet;
    }
    return NULL;
}

static lv_obj_t *scroll_probe_open_mesh_surface(const char *surface)
{
    open_mesh_roles_event_cb(NULL);
    if (strcmp(surface, "mesh_rooms") == 0) {
        open_mesh_room_servers_event_cb(NULL);
    } else if (strcmp(surface, "mesh_repeaters") == 0) {
        open_mesh_repeater_candidates_event_cb(NULL);
    } else if (strcmp(surface, "mesh_roles") != 0) {
        return NULL;
    }
    return s_mesh_roles_sheet;
}

static lv_obj_t *scroll_probe_open_storage_surface(const char *surface)
{
    open_storage_sheet_event_cb(NULL);
    if (strcmp(surface, "storage_card") == 0) {
        open_storage_card_status_event_cb(NULL);
    } else if (strcmp(surface, "storage_data") == 0) {
        open_storage_data_locations_event_cb(NULL);
        return scroll_probe_find_target(s_storage_sheet);
    } else if (strcmp(surface, "storage") != 0) {
        return NULL;
    }
    return s_storage_sheet;
}

static lv_obj_t *scroll_probe_open_map_surface(const char *surface)
{
    if (strcmp(surface, "map") == 0) {
        render_active_tab();
        return s_content;
    }
    if (strcmp(surface, "map_location") == 0) {
        open_map_location_sheet_event_cb(NULL);
        return s_map_location_sheet;
    }
    open_map_options_sheet_event_cb(NULL);
    if (strcmp(surface, "map_cache") == 0) {
        open_map_cache_status_event_cb(NULL);
    } else if (strcmp(surface, "map_options") != 0) {
        return NULL;
    }
    return s_map_options_sheet;
}

static void measure_scroll_probe_target(lv_obj_t *target,
                                        d1l_ui_scroll_probe_result_t *result)
{
    if (!target || !result) {
        return;
    }
    lv_obj_update_layout(target);
    result->target_found = true;
    result->scrollable = lv_obj_has_flag(target, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_scroll_to_y(target, 0, LV_ANIM_OFF);
    lv_obj_update_layout(target);
    result->before_y = (int32_t)lv_obj_get_scroll_y(target);
    result->scroll_top_before = (int32_t)lv_obj_get_scroll_top(target);
    result->scroll_bottom_before = (int32_t)lv_obj_get_scroll_bottom(target);

    lv_obj_scroll_to_y(target, LV_COORD_MAX, LV_ANIM_OFF);
    lv_obj_update_layout(target);
    result->after_y = (int32_t)lv_obj_get_scroll_y(target);
    result->scroll_top_after = (int32_t)lv_obj_get_scroll_top(target);
    result->scroll_bottom_after = (int32_t)lv_obj_get_scroll_bottom(target);
    result->moved = result->before_y != result->after_y ||
        result->scroll_top_before != result->scroll_top_after ||
        result->scroll_bottom_before != result->scroll_bottom_after;
    result->ok = result->surface_supported && result->target_found &&
        result->scrollable && result->moved;
}

static lv_obj_t *scroll_probe_open_surface(const char *surface)
{
    if (strcmp(surface, "public_messages") == 0) {
        s_messages_show_dms = false;
        render_active_tab();
        show_public_history_sheet();
        return scroll_probe_find_target(s_public_history_sheet);
    }
    if (strcmp(surface, "dm_thread") == 0) {
        s_messages_show_dms = true;
        render_active_tab();
        d1l_app_model_snapshot(&s_snapshot);
        if (s_snapshot.recent_dm_count == 0 ||
            s_snapshot.recent_dms[0].contact_fingerprint[0] == '\0') {
            show_dm_thread_for("0000000000000000", "Scroll Probe DM");
            return scroll_probe_find_target(s_dm_thread_sheet);
        }
        show_dm_thread_for(s_snapshot.recent_dms[0].contact_fingerprint,
                           s_snapshot.recent_dms[0].contact_alias[0] ?
                           s_snapshot.recent_dms[0].contact_alias :
                           s_snapshot.recent_dms[0].contact_fingerprint);
        return scroll_probe_find_target(s_dm_thread_sheet);
    }
    if (strcmp(surface, "storage") == 0 ||
        strcmp(surface, "storage_card") == 0 ||
        strcmp(surface, "storage_data") == 0) {
        return scroll_probe_open_storage_surface(surface);
    }
    if (strcmp(surface, "map") == 0 ||
        strcmp(surface, "map_options") == 0 ||
        strcmp(surface, "map_location") == 0 ||
        strcmp(surface, "map_cache") == 0) {
        return scroll_probe_open_map_surface(surface);
    }
    if (strcmp(surface, "wifi") == 0) {
        open_wifi_sheet_event_cb(NULL);
        return scroll_probe_find_target(s_wifi_sheet);
    }
    if (strncmp(surface, "contact_", strlen("contact_")) == 0) {
        return scroll_probe_open_contact_surface(surface);
    }
    if (strcmp(surface, "mesh_roles") == 0 ||
        strcmp(surface, "mesh_rooms") == 0 ||
        strcmp(surface, "mesh_repeaters") == 0) {
        return scroll_probe_open_mesh_surface(surface);
    }
    return scroll_probe_find_target(s_content);
}

static void run_scroll_probe_on_ui_task(const char *surface,
                                        d1l_ui_scroll_probe_result_t *result)
{
    d1l_ui_tab_t tab = D1L_UI_TAB_HOME;
    char canonical[24] = {0};
    set_map_interactive_touch_authorized(false);
    d1l_ui_map_viewport_set_suppressed(true);
    if (!scroll_surface_from_name(surface, canonical, sizeof(canonical), &tab)) {
        d1l_ui_map_viewport_set_suppressed(false);
        return;
    }

    result->surface_supported = true;
    snprintf(result->surface, sizeof(result->surface), "%s", canonical);
    snprintf(result->tab, sizeof(result->tab), "%s", tab_name(tab));
    request_tab_switch(tab);
    process_pending_tab_switch();

    lv_obj_t *target = scroll_probe_open_surface(canonical);
    const bool static_page =
        strncmp(canonical, "contact_", strlen("contact_")) == 0 ||
        strcmp(canonical, "map") == 0 ||
        strcmp(canonical, "map_options") == 0 ||
        strcmp(canonical, "map_location") == 0 ||
        strcmp(canonical, "map_cache") == 0 ||
        strcmp(canonical, "storage") == 0 ||
        strcmp(canonical, "storage_card") == 0 ||
        strcmp(canonical, "mesh_roles") == 0 ||
        strcmp(canonical, "mesh_rooms") == 0 ||
        strcmp(canonical, "mesh_repeaters") == 0;
    force_ui_layout_repaint();
    if (static_page) {
        result->target_found = target != NULL;
        result->scrollable = target && lv_obj_has_flag(target, LV_OBJ_FLAG_SCROLLABLE);
        result->ok = result->surface_supported && result->target_found;
        d1l_ui_map_viewport_set_suppressed(false);
        return;
    }
    measure_scroll_probe_target(target, result);
    if (strcmp(canonical, "storage_data") == 0 && target) {
        lv_obj_scroll_to_y(target, 0, LV_ANIM_OFF);
        lv_obj_update_layout(target);
        force_ui_layout_repaint();
    }
    d1l_ui_map_viewport_set_suppressed(false);
}

static uint32_t begin_pending_scroll_probe(char *surface, size_t surface_len)
{
    uint32_t request_id;

    portENTER_CRITICAL(&s_scroll_probe_lock);
    request_id = probe_gate_begin(&s_scroll_probe_gate);
    if (request_id != 0U && surface && surface_len > 0) {
        snprintf(surface, surface_len, "%s", s_scroll_probe_surface);
    }
    portEXIT_CRITICAL(&s_scroll_probe_lock);
    return request_id;
}

static void finish_pending_scroll_probe(const d1l_ui_scroll_probe_result_t *result,
                                        uint32_t request_id)
{
    bool signal_completion;
    portENTER_CRITICAL(&s_scroll_probe_lock);
    signal_completion = probe_gate_publish(&s_scroll_probe_gate, request_id);
    if (signal_completion) {
        s_scroll_probe_result = *result;
    }
    portEXIT_CRITICAL(&s_scroll_probe_lock);

    /* Publish before releasing the in-flight owner. A timed-out request can
     * leave a semaphore token, but no later request can be admitted until the
     * token has been published and can be drained safely. */
    if (signal_completion && s_scroll_probe_done_sem) {
        (void)xSemaphoreGive(s_scroll_probe_done_sem);
    }
    portENTER_CRITICAL(&s_scroll_probe_lock);
    probe_gate_finish_signal(&s_scroll_probe_gate, request_id);
    portEXIT_CRITICAL(&s_scroll_probe_lock);
}

static void process_pending_scroll_probe(void)
{
    char surface[24] = {0};
    const uint32_t request_id = begin_pending_scroll_probe(surface, sizeof(surface));
    if (request_id == 0U) {
        return;
    }

    d1l_ui_scroll_probe_result_t result = {0};
    run_scroll_probe_on_ui_task(surface, &result);
    finish_pending_scroll_probe(&result, request_id);
}

static bool object_is_visible(lv_obj_t *obj)
{
    return d1l_ui_modal_visible(obj);
}

static bool object_is_hidden_or_missing(lv_obj_t *obj)
{
    return !obj || lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static const char *compose_probe_sample_text(const char *target)
{
    if (target && strstr(target, "long")) {
        return "Long hardware compose validation text checks the 480 screen keyboard without RF.";
    }
    return "test from DeskOS D1L";
}

static d1l_ui_tab_t compose_probe_tab_for_target(const char *target)
{
    if (!target) {
        return D1L_UI_TAB_HOME;
    }
    if (d1l_ui_keyboard_probe_target_is_compose(target) ||
        strcmp(target, "public_search") == 0) {
        return D1L_UI_TAB_MESSAGES;
    }
    if (strcmp(target, "packet_search") == 0) {
        return D1L_UI_TAB_PACKETS;
    }
    if (strcmp(target, "contact_edit") == 0) {
        return D1L_UI_TAB_NODES;
    }
    if (strcmp(target, "map_location") == 0) {
        return D1L_UI_TAB_MAP;
    }
    if (strcmp(target, "wifi_ssid") == 0 ||
        strcmp(target, "wifi_password") == 0) {
        return D1L_UI_TAB_SETTINGS;
    }
    return D1L_UI_TAB_HOME;
}

static void hide_keyboard_probe_sheets(bool include_onboarding)
{
    hide_sheet();
    hide_public_history_sheet();
    hide_public_search_sheet();
    hide_message_detail_sheet();
    hide_dm_thread_sheet();
    hide_radio_settings_sheet();
    hide_storage_sheet();
    hide_wifi_sheet();
    hide_ble_sheet();
    hide_display_sheet();
    hide_diagnostics_sheet();
    hide_map_location_sheet();
    hide_map_options_sheet();
    hide_contact_detail_sheet();
    hide_contact_export_sheet();
    hide_route_detail_sheet();
    hide_route_trace_sheet();
    hide_packet_detail_sheet();
    hide_packet_search_sheet();
    hide_mesh_roles_sheet();
    hide_compose_sheet();
    if (include_onboarding) {
        hide_onboarding_sheet();
    }
}

static void fill_compose_probe_objects(const char *target, lv_obj_t **sheet,
                                       lv_obj_t **textarea, lv_obj_t **keyboard)
{
    if (!sheet || !textarea || !keyboard) {
        return;
    }
    *sheet = NULL;
    *textarea = NULL;
    *keyboard = NULL;
    if (d1l_ui_keyboard_probe_target_is_compose(target)) {
        *sheet = s_compose_sheet;
        *textarea = s_compose_textarea;
        *keyboard = s_compose_keyboard;
    } else if (strcmp(target, "public_search") == 0) {
        *sheet = s_public_search_sheet;
        *textarea = s_public_search_textarea;
        *keyboard = s_public_search_keyboard;
    } else if (strcmp(target, "packet_search") == 0) {
        *sheet = s_packet_search_sheet;
        *textarea = s_packet_search_textarea;
        *keyboard = s_packet_search_keyboard;
    } else if (strcmp(target, "contact_edit") == 0) {
        *sheet = s_contact_edit_sheet;
        *textarea = s_contact_edit_textarea;
        *keyboard = s_contact_edit_keyboard;
    } else if (d1l_ui_keyboard_probe_target_is_onboarding(target)) {
        *sheet = s_onboarding_sheet;
        *textarea = s_onboarding_name_textarea;
        *keyboard = s_onboarding_keyboard;
    } else if (strcmp(target, "map_location") == 0) {
        *sheet = s_map_location_sheet;
        *textarea = s_map_lat_textarea;
        *keyboard = s_map_location_keyboard;
    } else if (strcmp(target, "wifi_ssid") == 0) {
        *sheet = s_wifi_sheet;
        *textarea = s_wifi_ssid_textarea;
        *keyboard = s_wifi_keyboard;
    } else if (strcmp(target, "wifi_password") == 0) {
        *sheet = s_wifi_sheet;
        *textarea = s_wifi_password_textarea;
        *keyboard = s_wifi_keyboard;
    }
}

static void open_compose_probe_on_ui_task(const char *target)
{
    hide_keyboard_probe_sheets(true);
    s_onboarding_probe_suppressed = true;
    request_tab_switch(D1L_UI_TAB_MESSAGES);
    process_pending_tab_switch();
    s_onboarding_probe_suppressed = true;
    s_messages_show_dms = d1l_ui_keyboard_probe_target_is_dm(target);
    render_active_tab();
    hide_onboarding_sheet();

    if (d1l_ui_keyboard_probe_target_is_dm(target)) {
        d1l_contact_entry_t contact = {0};
        snprintf(contact.fingerprint, sizeof(contact.fingerprint), "%s", "0000000000000000");
        snprintf(contact.public_key_hex, sizeof(contact.public_key_hex), "%s", "00");
        snprintf(contact.alias, sizeof(contact.alias), "%s", "Probe DM");
        open_dm_compose_for_contact(&contact);
    } else {
        show_public_compose_sheet("Compose Public", "Public message");
    }

    if (s_compose_textarea) {
        lv_textarea_set_text(s_compose_textarea, compose_probe_sample_text(target));
        update_compose_counter();
    }
    if (s_compose_keyboard) {
        lv_keyboard_set_mode(s_compose_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_keyboard_set_textarea(s_compose_keyboard, s_compose_textarea);
        layout_compose_sheet_controls();
    }
    lv_obj_update_layout(s_screen);
    layout_compose_sheet_controls();
    lv_obj_update_layout(s_screen);
    request_full_screen_repaint();
    force_ui_layout_repaint();
}

static void open_keyboard_probe_on_ui_task(const char *target)
{
    if (d1l_ui_keyboard_probe_target_is_compose(target)) {
        open_compose_probe_on_ui_task(target);
        return;
    }

    hide_keyboard_probe_sheets(true);
    s_onboarding_probe_suppressed = !d1l_ui_keyboard_probe_target_is_onboarding(target);
    request_tab_switch(compose_probe_tab_for_target(target));
    process_pending_tab_switch();
    s_onboarding_probe_suppressed = !d1l_ui_keyboard_probe_target_is_onboarding(target);
    if (!d1l_ui_keyboard_probe_target_is_onboarding(target)) {
        hide_onboarding_sheet();
    }

    if (strcmp(target, "public_search") == 0) {
        s_messages_show_dms = false;
        render_active_tab();
        hide_onboarding_sheet();
        open_public_search_event_cb(NULL);
        if (s_public_search_textarea) {
            lv_textarea_set_text(s_public_search_textarea, "probe");
        }
        if (s_public_search_keyboard) {
            lv_keyboard_set_textarea(s_public_search_keyboard, s_public_search_textarea);
        }
    } else if (strcmp(target, "packet_search") == 0) {
        open_packet_search_event_cb(NULL);
        if (s_packet_search_textarea) {
            lv_textarea_set_text(s_packet_search_textarea, "probe");
        }
        if (s_packet_search_keyboard) {
            lv_keyboard_set_textarea(s_packet_search_keyboard, s_packet_search_textarea);
        }
    } else if (strcmp(target, "contact_edit") == 0) {
        snprintf(s_contact_detail_contact.fingerprint,
                 sizeof(s_contact_detail_contact.fingerprint),
                 "%s", "0000000000000000");
        snprintf(s_contact_detail_contact.public_key_hex,
                 sizeof(s_contact_detail_contact.public_key_hex),
                 "%s", "00");
        snprintf(s_contact_detail_contact.alias,
                 sizeof(s_contact_detail_contact.alias),
                 "%s", "Probe Contact");
        open_contact_edit_event_cb(NULL);
    } else if (d1l_ui_keyboard_probe_target_is_onboarding(target)) {
        s_onboarding_probe_suppressed = false;
        if (s_onboarding_sheet) {
            s_onboarding_visible = true;
            show_modal(s_onboarding_sheet);
        }
        if (s_onboarding_name_textarea) {
            lv_textarea_set_text(s_onboarding_name_textarea, "DeskOS Probe");
        }
        if (s_onboarding_keyboard) {
            lv_keyboard_set_textarea(s_onboarding_keyboard, s_onboarding_name_textarea);
        }
    } else if (strcmp(target, "map_location") == 0) {
        open_map_location_sheet_event_cb(NULL);
        if (s_map_lat_textarea) {
            lv_textarea_set_text(s_map_lat_textarea, "43.6532");
        }
        if (s_map_location_keyboard) {
            lv_keyboard_set_textarea(s_map_location_keyboard, s_map_lat_textarea);
            lv_obj_clear_flag(s_map_location_keyboard, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(s_map_location_keyboard);
        }
    } else if (strcmp(target, "wifi_ssid") == 0 ||
               strcmp(target, "wifi_password") == 0) {
        open_wifi_sheet_event_cb(NULL);
        if (strcmp(target, "wifi_password") == 0) {
            if (s_wifi_password_textarea) {
                lv_textarea_set_text(s_wifi_password_textarea, "probe-pass");
            }
            if (s_wifi_keyboard) {
                lv_keyboard_set_textarea(s_wifi_keyboard, s_wifi_password_textarea);
            }
        } else {
            if (s_wifi_ssid_textarea) {
                lv_textarea_set_text(s_wifi_ssid_textarea, "ProbeNet");
            }
            if (s_wifi_keyboard) {
                lv_keyboard_set_textarea(s_wifi_keyboard, s_wifi_ssid_textarea);
            }
        }
    }

    lv_obj_update_layout(s_screen);
    request_full_screen_repaint();
    force_ui_layout_repaint();
}

static void fill_compose_probe_geometry(d1l_ui_compose_probe_result_t *result,
                                        lv_obj_t *sheet, lv_obj_t *textarea,
                                        lv_obj_t *keyboard)
{
    if (!result) {
        return;
    }
    if (sheet) {
        result->sheet_x = (int32_t)lv_obj_get_x(sheet);
        result->sheet_y = (int32_t)lv_obj_get_y(sheet);
        result->sheet_w = (int32_t)lv_obj_get_width(sheet);
        result->sheet_h = (int32_t)lv_obj_get_height(sheet);
    }
    if (textarea) {
        result->textarea_x = (int32_t)lv_obj_get_x(textarea);
        result->textarea_y = (int32_t)lv_obj_get_y(textarea);
        result->textarea_w = (int32_t)lv_obj_get_width(textarea);
        result->textarea_h = (int32_t)lv_obj_get_height(textarea);
    }
    if (keyboard) {
        result->keyboard_x = (int32_t)lv_obj_get_x(keyboard);
        result->keyboard_y = (int32_t)lv_obj_get_y(keyboard);
        result->keyboard_w = (int32_t)lv_obj_get_width(keyboard);
        result->keyboard_h = (int32_t)lv_obj_get_height(keyboard);
    }
}

static void run_compose_probe_on_ui_task(const char *target,
                                         d1l_ui_compose_probe_result_t *result)
{
    char canonical[16] = {0};
    set_map_interactive_touch_authorized(false);
    d1l_ui_map_viewport_set_suppressed(true);
    if (!compose_probe_target_from_name(target, canonical, sizeof(canonical))) {
        d1l_ui_map_viewport_set_suppressed(false);
        return;
    }
    result->target_supported = true;
    snprintf(result->target, sizeof(result->target), "%s", canonical);

    open_keyboard_probe_on_ui_task(canonical);
    snprintf(result->active_tab, sizeof(result->active_tab), "%s", tab_name(d1l_ui_navigation_active()));

    lv_obj_t *sheet = NULL;
    lv_obj_t *textarea = NULL;
    lv_obj_t *keyboard = NULL;
    fill_compose_probe_objects(canonical, &sheet, &textarea, &keyboard);
    result->sheet_visible = object_is_visible(sheet);
    result->textarea_visible = object_is_visible(textarea);
    result->keyboard_visible = object_is_visible(keyboard);
    result->onboarding_visible = s_onboarding_visible;
    result->dock_hidden = object_is_hidden_or_missing(s_dock);
    result->dm_mode = s_compose_dm;
    fill_compose_probe_geometry(result, sheet, textarea, keyboard);

    const bool sheet_inside_screen =
        result->sheet_x >= 0 &&
        result->sheet_y >= 0 &&
        result->sheet_x + result->sheet_w <= (int32_t)D1L_UI_CAPTURE_WIDTH &&
        result->sheet_y + result->sheet_h <= (int32_t)D1L_UI_CAPTURE_HEIGHT;
    const bool keyboard_inside_sheet =
        result->keyboard_x >= 0 &&
        result->keyboard_y >= 0 &&
        result->keyboard_x + result->keyboard_w <= result->sheet_w &&
        result->keyboard_y + result->keyboard_h <= result->sheet_h;
    const bool keyboard_inside_screen =
        result->sheet_x + result->keyboard_x >= 0 &&
        result->sheet_y + result->keyboard_y >= 0 &&
        result->sheet_x + result->keyboard_x + result->keyboard_w <= (int32_t)D1L_UI_CAPTURE_WIDTH &&
        result->sheet_y + result->keyboard_y + result->keyboard_h <= (int32_t)D1L_UI_CAPTURE_HEIGHT;
    const bool textarea_inside_sheet =
        result->textarea_x >= 0 &&
        result->textarea_y >= 0 &&
        result->textarea_x + result->textarea_w <= result->sheet_w &&
        result->textarea_y + result->textarea_h <= result->keyboard_y;
    const bool onboarding_state_ok =
        result->onboarding_visible == d1l_ui_keyboard_probe_target_is_onboarding(canonical);
    const bool dock_state_ok =
        !d1l_ui_keyboard_probe_requires_hidden_dock(canonical) || result->dock_hidden;
    const bool dm_state_ok =
        !d1l_ui_keyboard_probe_target_is_compose(canonical) ||
        result->dm_mode == d1l_ui_keyboard_probe_target_is_dm(canonical);

    result->ok = result->target_supported &&
        result->sheet_visible &&
        result->textarea_visible &&
        result->keyboard_visible &&
        onboarding_state_ok &&
        dock_state_ok &&
        dm_state_ok &&
        result->keyboard_w >= d1l_ui_keyboard_probe_min_width(canonical) &&
        result->keyboard_h >= d1l_ui_keyboard_probe_min_height(canonical) &&
        sheet_inside_screen &&
        keyboard_inside_sheet &&
        keyboard_inside_screen &&
        textarea_inside_sheet;
    d1l_ui_map_viewport_set_suppressed(false);
}

static uint32_t begin_pending_compose_probe(char *target, size_t target_len)
{
    uint32_t request_id;

    portENTER_CRITICAL(&s_compose_probe_lock);
    request_id = probe_gate_begin(&s_compose_probe_gate);
    if (request_id != 0U && target && target_len > 0) {
        snprintf(target, target_len, "%s", s_compose_probe_target);
    }
    portEXIT_CRITICAL(&s_compose_probe_lock);
    return request_id;
}

static void finish_pending_compose_probe(const d1l_ui_compose_probe_result_t *result,
                                         uint32_t request_id)
{
    bool signal_completion;
    portENTER_CRITICAL(&s_compose_probe_lock);
    signal_completion = probe_gate_publish(&s_compose_probe_gate, request_id);
    if (signal_completion) {
        s_compose_probe_result = *result;
    }
    portEXIT_CRITICAL(&s_compose_probe_lock);
    if (signal_completion && s_compose_probe_done_sem) {
        (void)xSemaphoreGive(s_compose_probe_done_sem);
    }
    portENTER_CRITICAL(&s_compose_probe_lock);
    probe_gate_finish_signal(&s_compose_probe_gate, request_id);
    portEXIT_CRITICAL(&s_compose_probe_lock);
}

static void process_pending_compose_probe(void)
{
    char target[16] = {0};
    const uint32_t request_id = begin_pending_compose_probe(target, sizeof(target));
    if (request_id == 0U) {
        return;
    }

    d1l_ui_compose_probe_result_t result = {0};
    run_compose_probe_on_ui_task(target, &result);
    finish_pending_compose_probe(&result, request_id);
}

static void lock_event_cb(lv_event_t *event)
{
    (void)event;
    if (s_lock_overlay) {
        if (d1l_ui_navigation_active() == D1L_UI_TAB_MAP) {
            d1l_ui_map_viewport_prepare_cover();
        }
        d1l_ui_map_viewport_release();
        s_lock_visible = true;
        lv_obj_clear_flag(s_lock_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

static void unlock_event_cb(lv_event_t *event)
{
    (void)event;
    if (s_lock_overlay) {
        s_lock_visible = false;
        lv_obj_add_flag(s_lock_overlay, LV_OBJ_FLAG_HIDDEN);
        if (d1l_ui_navigation_active() == D1L_UI_TAB_MAP &&
            !d1l_ui_modal_has_active() && !s_onboarding_visible) {
            request_content_refresh();
        }
    }
}

static void map_viewport_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    const bool map_uncovered =
        d1l_ui_navigation_active() == D1L_UI_TAB_MAP &&
        !d1l_ui_modal_has_active() && !s_lock_visible && !s_onboarding_visible &&
        map_interactive_touch_authorized();
    if (map_uncovered) {
        (void)d1l_ui_map_viewport_refresh();
    }
}

static void refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    d1l_app_model_snapshot(&s_snapshot);
    update_chrome(&s_snapshot);
    update_onboarding_visibility(&s_snapshot);
    const bool map_active = d1l_ui_navigation_active() == D1L_UI_TAB_MAP;
    const bool map_uncovered = map_active &&
        !d1l_ui_modal_has_active() && !s_lock_visible && !s_onboarding_visible &&
        map_interactive_touch_authorized();
    if (map_uncovered) {
        (void)d1l_ui_map_viewport_refresh();
    }
    if (map_uncovered && map_render_input_changed_from_rendered(&s_snapshot)) {
        request_content_refresh();
    } else if (content_generation_changed_from_rendered(&s_snapshot)) {
        if (map_active) {
            /* Ambient mesh/message counters do not change the Map surface.
             * A full content render tears down its visible lease and can
             * cancel a bounded tile batch or an in-progress drag, so simply
             * acknowledge these counters here.  Explicit Map changes such as
             * saving/clearing a location still call request_content_refresh(). */
            remember_rendered_content_generation(&s_snapshot);
        } else {
            request_content_refresh();
        }
    }
    if (s_toast && s_toast_until != 0 && lv_tick_get() > s_toast_until) {
        lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
        s_toast_until = 0;
    }
}

static void create_top_bar(lv_obj_t *screen)
{
    lv_obj_t *bar = create_object(screen, "top bar");
    if (!bar) {
        return;
    }
    s_top_bar = bar;
    lv_obj_set_size(bar, 480, 56);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x071018), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 8, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    s_title_label = create_label(bar, "DeskOS", 0xF4F7FB);
    lv_obj_align(s_title_label, LV_ALIGN_LEFT_MID, 2, 0);

    s_status_label = create_label(bar, "starting", 0x5EEAD4);
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_status_label, 190);
    lv_obj_align(s_status_label, LV_ALIGN_TOP_RIGHT, -48, 1);

    s_identity_label = create_label(bar, "ID --------", 0x8EA0AE);
    lv_label_set_long_mode(s_identity_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_identity_label, 190);
    lv_obj_align(s_identity_label, LV_ALIGN_BOTTOM_RIGHT, -48, -1);

    s_lock_button = create_button(bar, "Lock", 404, 6, 64, 40, lock_event_cb, NULL);
}

static void create_dock(lv_obj_t *screen)
{
    s_dock = create_object(screen, "dock");
    if (!s_dock) {
        return;
    }
    lv_obj_set_size(s_dock, 480, 62);
    lv_obj_set_pos(s_dock, 0, 418);
    lv_obj_set_style_bg_color(s_dock, lv_color_hex(0x09131D), 0);
    lv_obj_set_style_border_width(s_dock, 0, 0);
    lv_obj_set_style_pad_all(s_dock, 5, 0);
    lv_obj_clear_flag(s_dock, LV_OBJ_FLAG_SCROLLABLE);

    static const d1l_ui_tab_t tabs[] = {
        D1L_UI_TAB_HOME,
        D1L_UI_TAB_MESSAGES,
        D1L_UI_TAB_NODES,
        D1L_UI_TAB_MAP,
        D1L_UI_TAB_SETTINGS,
    };
    static const char *const labels[] = {"Home", "Msg", "Network", "Map", "More"};
    for (size_t i = 0; i < sizeof(tabs) / sizeof(tabs[0]); ++i) {
        create_button(s_dock, labels[i], 4 + (int)i * 96, 5, 88, 50, dock_event_cb,
                      (void *)&tabs[i]);
    }
}

static void create_toast(lv_obj_t *screen)
{
    s_toast = create_label_object(screen, "toast");
    if (!s_toast) {
        return;
    }
    lv_obj_set_size(s_toast, 420, 42);
    lv_obj_set_pos(s_toast, 30, 366);
    lv_obj_set_style_radius(s_toast, 8, 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_toast, lv_color_hex(0x12362F), 0);
    lv_obj_set_style_border_width(s_toast, 1, 0);
    lv_obj_set_style_border_color(s_toast, lv_color_hex(0x5EEAD4), 0);
    lv_obj_set_style_pad_all(s_toast, 11, 0);
    lv_obj_set_style_text_color(s_toast, lv_color_hex(0xF4F7FB), 0);
    lv_label_set_text(s_toast, "");
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
}

static void create_sheet(lv_obj_t *screen)
{
    s_sheet = create_object(screen, "advert sheet");
    if (!s_sheet) {
        return;
    }
    lv_obj_set_size(s_sheet, 440, 160);
    lv_obj_set_pos(s_sheet, 20, 250);
    lv_obj_set_style_radius(s_sheet, 8, 0);
    lv_obj_set_style_bg_color(s_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_color(s_sheet, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(s_sheet, 1, 0);
    lv_obj_set_style_pad_all(s_sheet, 14, 0);
    lv_obj_clear_flag(s_sheet, LV_OBJ_FLAG_SCROLLABLE);

    create_label(s_sheet, "Advert", 0xF4F7FB);
    create_button(s_sheet, "Zero Hop", 16, 52, 118, 52, advert_zero_event_cb, NULL);
    create_button(s_sheet, "Flood", 156, 52, 118, 52, advert_flood_event_cb, NULL);
    create_button(s_sheet, "Close", 296, 52, 96, 52, close_sheet_event_cb, NULL);
    d1l_ui_modal_hide(s_sheet);
}

static void create_compose_sheet(lv_obj_t *screen)
{
    s_compose_sheet = create_object(screen, "compose sheet");
    if (!s_compose_sheet) {
        return;
    }
    lv_obj_set_style_radius(s_compose_sheet, 0, 0);
    lv_obj_set_style_bg_color(s_compose_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_color(s_compose_sheet, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(s_compose_sheet, 1, 0);

    s_compose_title = create_label(s_compose_sheet, "Public", 0xF4F7FB);
    lv_obj_set_style_text_font(s_compose_title, &lv_font_montserrat_24, 0);
    lv_label_set_long_mode(s_compose_title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_compose_title, 210);
    lv_obj_set_pos(s_compose_title, 16, 8);

    create_button(s_compose_sheet, "Send", 252, 8, 62, 40, send_compose_event_cb, NULL);
    create_button(s_compose_sheet, "Clear", 322, 8, 62, 40, clear_compose_event_cb, NULL);
    create_button(s_compose_sheet, "Close", 392, 8, 72, 40, close_compose_event_cb, NULL);

    s_compose_textarea = create_textarea(s_compose_sheet, "compose textarea");
    if (!s_compose_textarea) {
        d1l_ui_modal_hide(s_compose_sheet);
        return;
    }
    lv_textarea_set_placeholder_text(s_compose_textarea, "Public message");
    lv_textarea_set_max_length(s_compose_textarea, D1L_MESSAGE_MAX_CHARS);
    lv_textarea_set_one_line(s_compose_textarea, false);
    lv_textarea_set_text(s_compose_textarea, "");
    lv_obj_set_style_radius(s_compose_textarea, 8, 0);
    lv_obj_set_style_bg_color(s_compose_textarea, lv_color_hex(0x071018), 0);
    lv_obj_set_style_border_color(s_compose_textarea, lv_color_hex(0x263241), 0);
    lv_obj_set_style_text_color(s_compose_textarea, lv_color_hex(0xF4F7FB), 0);
    lv_obj_set_style_text_color(s_compose_textarea, lv_color_hex(0x8EA0AE), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_add_event_cb(s_compose_textarea, compose_textarea_event_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    s_compose_counter = create_label(s_compose_sheet, "0/138", 0x8EA0AE);
    lv_label_set_long_mode(s_compose_counter, LV_LABEL_LONG_DOT);

    s_compose_keyboard = create_keyboard(s_compose_sheet, "compose keyboard");
    if (!s_compose_keyboard) {
        d1l_ui_modal_hide(s_compose_sheet);
        return;
    }
    d1l_ui_keyboard_configure_compose(s_compose_keyboard);
    lv_keyboard_set_textarea(s_compose_keyboard, s_compose_textarea);
    layout_compose_sheet_controls();
    lv_obj_add_event_cb(s_compose_keyboard, compose_keyboard_event_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_compose_keyboard, compose_keyboard_event_cb, LV_EVENT_CANCEL, NULL);

    d1l_ui_modal_hide(s_compose_sheet);
}

static void create_public_history_sheet(lv_obj_t *screen)
{
    s_public_history_sheet = create_object(screen, "public history sheet");
    if (!s_public_history_sheet) {
        return;
    }
    lv_obj_set_size(s_public_history_sheet, 448, 328);
    lv_obj_set_pos(s_public_history_sheet, 16, 82);
    lv_obj_set_style_radius(s_public_history_sheet, 8, 0);
    lv_obj_set_style_bg_color(s_public_history_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_color(s_public_history_sheet, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(s_public_history_sheet, 1, 0);
    lv_obj_set_style_pad_all(s_public_history_sheet, 12, 0);
    configure_sheet_scroll(s_public_history_sheet);
    d1l_ui_modal_hide(s_public_history_sheet);
}

static void create_message_detail_sheet(lv_obj_t *screen)
{
    s_message_detail_sheet = create_object(screen, "message detail sheet");
    if (!s_message_detail_sheet) {
        return;
    }
    lv_obj_set_size(s_message_detail_sheet, 480, 424);
    lv_obj_set_pos(s_message_detail_sheet, 0, 56);
    lv_obj_set_style_radius(s_message_detail_sheet, 0, 0);
    lv_obj_set_style_bg_color(s_message_detail_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_width(s_message_detail_sheet, 0, 0);
    lv_obj_set_style_pad_all(s_message_detail_sheet, 0, 0);
    lv_obj_clear_flag(s_message_detail_sheet, LV_OBJ_FLAG_SCROLLABLE);
    d1l_ui_modal_hide(s_message_detail_sheet);
}

static void create_public_search_sheet(lv_obj_t *screen)
{
    s_public_search_sheet = create_object(screen, "public search sheet");
    if (!s_public_search_sheet) {
        return;
    }
    lv_obj_set_size(s_public_search_sheet, 448, 320);
    lv_obj_set_pos(s_public_search_sheet, 16, 82);
    lv_obj_set_style_radius(s_public_search_sheet, 8, 0);
    lv_obj_set_style_bg_color(s_public_search_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_color(s_public_search_sheet, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(s_public_search_sheet, 1, 0);
    lv_obj_set_style_pad_all(s_public_search_sheet, 12, 0);
    lv_obj_clear_flag(s_public_search_sheet, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = create_label(s_public_search_sheet, "Public Search", 0xF4F7FB);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(title, 8, 4);

    create_button(s_public_search_sheet, "Apply", 212, 0, 66, 40,
                  apply_public_search_event_cb, NULL);
    create_button(s_public_search_sheet, "Clear", 286, 0, 64, 40,
                  clear_public_search_event_cb, NULL);
    create_button(s_public_search_sheet, "Close", 358, 0, 66, 40,
                  close_public_search_event_cb, NULL);

    s_public_search_textarea = create_textarea(s_public_search_sheet, "public search textarea");
    if (!s_public_search_textarea) {
        d1l_ui_modal_hide(s_public_search_sheet);
        return;
    }
    lv_obj_set_size(s_public_search_textarea, 424, 48);
    lv_obj_set_pos(s_public_search_textarea, 0, 54);
    lv_textarea_set_one_line(s_public_search_textarea, true);
    lv_textarea_set_max_length(s_public_search_textarea, D1L_MESSAGE_TEXT_LEN - 1U);
    lv_textarea_set_placeholder_text(s_public_search_textarea, "Search author or message");
    lv_obj_set_style_radius(s_public_search_textarea, 8, 0);
    lv_obj_set_style_bg_color(s_public_search_textarea, lv_color_hex(0x071018), 0);
    lv_obj_set_style_border_color(s_public_search_textarea, lv_color_hex(0x263241), 0);
    lv_obj_set_style_text_color(s_public_search_textarea, lv_color_hex(0xF4F7FB), 0);
    lv_obj_set_style_text_color(s_public_search_textarea, lv_color_hex(0x8EA0AE),
                                LV_PART_TEXTAREA_PLACEHOLDER);

    s_public_search_keyboard = create_keyboard(s_public_search_sheet, "public search keyboard");
    if (!s_public_search_keyboard) {
        d1l_ui_modal_hide(s_public_search_sheet);
        return;
    }
    d1l_ui_keyboard_configure_input(s_public_search_keyboard, s_public_search_textarea,
                                    0, 114, 424, 194);
    lv_obj_add_event_cb(s_public_search_keyboard, public_search_keyboard_event_cb,
                        LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_public_search_keyboard, public_search_keyboard_event_cb,
                        LV_EVENT_CANCEL, NULL);

    d1l_ui_modal_hide(s_public_search_sheet);
}

static void create_dm_thread_sheet(lv_obj_t *screen)
{
    s_dm_thread_sheet = create_object(screen, "dm thread sheet");
    if (!s_dm_thread_sheet) {
        return;
    }
    lv_obj_set_size(s_dm_thread_sheet, 480, 424);
    lv_obj_set_pos(s_dm_thread_sheet, 0, 56);
    lv_obj_set_style_radius(s_dm_thread_sheet, 0, 0);
    lv_obj_set_style_bg_color(s_dm_thread_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_width(s_dm_thread_sheet, 0, 0);
    lv_obj_set_style_pad_all(s_dm_thread_sheet, 0, 0);
    lv_obj_clear_flag(s_dm_thread_sheet, LV_OBJ_FLAG_SCROLLABLE);
    d1l_ui_modal_hide(s_dm_thread_sheet);
}

static void create_radio_settings_sheet(lv_obj_t *screen)
{
    s_radio_settings_sheet = create_object(screen, "radio settings sheet");
    if (!s_radio_settings_sheet) {
        return;
    }
    lv_obj_set_size(s_radio_settings_sheet, 448, 320);
    lv_obj_set_pos(s_radio_settings_sheet, 16, 82);
    lv_obj_set_style_radius(s_radio_settings_sheet, 8, 0);
    lv_obj_set_style_bg_color(s_radio_settings_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_color(s_radio_settings_sheet, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(s_radio_settings_sheet, 1, 0);
    lv_obj_set_style_pad_all(s_radio_settings_sheet, 12, 0);
    lv_obj_clear_flag(s_radio_settings_sheet, LV_OBJ_FLAG_SCROLLABLE);
    d1l_ui_modal_hide(s_radio_settings_sheet);
}

static void create_storage_sheet(lv_obj_t *screen)
{
    s_storage_sheet = create_object(screen, "storage sheet");
    if (!s_storage_sheet) {
        return;
    }
    lv_obj_set_size(s_storage_sheet, 480, 424);
    lv_obj_set_pos(s_storage_sheet, 0, 56);
    lv_obj_set_style_radius(s_storage_sheet, 0, 0);
    lv_obj_set_style_bg_color(s_storage_sheet, lv_color_hex(0x071018), 0);
    lv_obj_set_style_border_width(s_storage_sheet, 0, 0);
    lv_obj_set_style_pad_all(s_storage_sheet, 0, 0);
    lv_obj_clear_flag(s_storage_sheet, LV_OBJ_FLAG_SCROLLABLE);
    d1l_ui_modal_hide(s_storage_sheet);
}

static void create_wifi_sheet(lv_obj_t *screen)
{
    s_wifi_sheet = create_object(screen, "wifi setup sheet");
    if (!s_wifi_sheet) {
        return;
    }
    lv_obj_set_size(s_wifi_sheet, 480, 424);
    lv_obj_set_pos(s_wifi_sheet, 0, 56);
    lv_obj_set_style_radius(s_wifi_sheet, 0, 0);
    lv_obj_set_style_bg_color(s_wifi_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_color(s_wifi_sheet, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(s_wifi_sheet, 1, 0);
    lv_obj_set_style_pad_all(s_wifi_sheet, 12, 0);
    configure_sheet_scroll(s_wifi_sheet);
    d1l_ui_modal_hide(s_wifi_sheet);
}

static void create_ble_sheet(lv_obj_t *screen)
{
    s_ble_sheet = create_object(screen, "ble setup sheet");
    if (!s_ble_sheet) {
        return;
    }
    lv_obj_set_size(s_ble_sheet, 448, 320);
    lv_obj_set_pos(s_ble_sheet, 16, 82);
    lv_obj_set_style_radius(s_ble_sheet, 8, 0);
    lv_obj_set_style_bg_color(s_ble_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_color(s_ble_sheet, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(s_ble_sheet, 1, 0);
    lv_obj_set_style_pad_all(s_ble_sheet, 12, 0);
    lv_obj_clear_flag(s_ble_sheet, LV_OBJ_FLAG_SCROLLABLE);
    d1l_ui_modal_hide(s_ble_sheet);
}

static void create_display_sheet(lv_obj_t *screen)
{
    s_display_sheet = create_object(screen, "display settings sheet");
    if (!s_display_sheet) {
        return;
    }
    lv_obj_set_size(s_display_sheet, 448, 320);
    lv_obj_set_pos(s_display_sheet, 16, 82);
    lv_obj_set_style_radius(s_display_sheet, 8, 0);
    lv_obj_set_style_bg_color(s_display_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_color(s_display_sheet, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(s_display_sheet, 1, 0);
    lv_obj_set_style_pad_all(s_display_sheet, 12, 0);
    lv_obj_clear_flag(s_display_sheet, LV_OBJ_FLAG_SCROLLABLE);
    d1l_ui_modal_hide(s_display_sheet);
}

static void create_diagnostics_sheet(lv_obj_t *screen)
{
    s_diagnostics_sheet = create_object(screen, "diagnostics sheet");
    if (!s_diagnostics_sheet) {
        return;
    }
    lv_obj_set_size(s_diagnostics_sheet, 448, 320);
    lv_obj_set_pos(s_diagnostics_sheet, 16, 82);
    lv_obj_set_style_radius(s_diagnostics_sheet, 8, 0);
    lv_obj_set_style_bg_color(s_diagnostics_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_color(s_diagnostics_sheet, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(s_diagnostics_sheet, 1, 0);
    lv_obj_set_style_pad_all(s_diagnostics_sheet, 12, 0);
    lv_obj_set_scrollbar_mode(s_diagnostics_sheet, LV_SCROLLBAR_MODE_AUTO);
    d1l_ui_modal_hide(s_diagnostics_sheet);
}

static void create_map_location_sheet(lv_obj_t *screen)
{
    s_map_location_sheet = create_object(screen, "map location sheet");
    if (!s_map_location_sheet) {
        return;
    }
    lv_obj_set_size(s_map_location_sheet, 480, 424);
    lv_obj_set_pos(s_map_location_sheet, 0, 56);
    lv_obj_set_style_radius(s_map_location_sheet, 0, 0);
    lv_obj_set_style_bg_color(s_map_location_sheet, lv_color_hex(0x071018), 0);
    lv_obj_set_style_border_width(s_map_location_sheet, 0, 0);
    lv_obj_set_style_pad_all(s_map_location_sheet, 0, 0);
    lv_obj_clear_flag(s_map_location_sheet, LV_OBJ_FLAG_SCROLLABLE);
    d1l_ui_modal_hide(s_map_location_sheet);
}

static void create_map_options_sheet(lv_obj_t *screen)
{
    s_map_options_sheet = create_object(screen, "map options sheet");
    if (!s_map_options_sheet) {
        return;
    }
    lv_obj_set_size(s_map_options_sheet, 480, 424);
    lv_obj_set_pos(s_map_options_sheet, 0, 56);
    lv_obj_set_style_radius(s_map_options_sheet, 0, 0);
    lv_obj_set_style_bg_color(s_map_options_sheet, lv_color_hex(0x071018), 0);
    lv_obj_set_style_border_width(s_map_options_sheet, 0, 0);
    lv_obj_set_style_pad_all(s_map_options_sheet, 0, 0);
    lv_obj_clear_flag(s_map_options_sheet, LV_OBJ_FLAG_SCROLLABLE);
    d1l_ui_modal_hide(s_map_options_sheet);
}

static void create_contact_detail_sheet(lv_obj_t *screen)
{
    s_contact_detail_sheet = create_object(screen, "contact detail sheet");
    if (!s_contact_detail_sheet) {
        return;
    }
    lv_obj_set_size(s_contact_detail_sheet, 480, 424);
    lv_obj_set_pos(s_contact_detail_sheet, 0, 56);
    lv_obj_set_style_radius(s_contact_detail_sheet, 0, 0);
    lv_obj_set_style_bg_color(s_contact_detail_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_width(s_contact_detail_sheet, 0, 0);
    lv_obj_set_style_pad_all(s_contact_detail_sheet, 0, 0);
    lv_obj_clear_flag(s_contact_detail_sheet, LV_OBJ_FLAG_SCROLLABLE);
    d1l_ui_modal_hide(s_contact_detail_sheet);
}

static void create_contact_options_sheet(lv_obj_t *screen)
{
    s_contact_options_sheet = create_object(screen, "contact options sheet");
    if (!s_contact_options_sheet) {
        return;
    }
    lv_obj_set_size(s_contact_options_sheet, 480, 424);
    lv_obj_set_pos(s_contact_options_sheet, 0, 56);
    lv_obj_set_style_radius(s_contact_options_sheet, 0, 0);
    lv_obj_set_style_bg_color(s_contact_options_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_width(s_contact_options_sheet, 0, 0);
    lv_obj_set_style_pad_all(s_contact_options_sheet, 0, 0);
    lv_obj_clear_flag(s_contact_options_sheet, LV_OBJ_FLAG_SCROLLABLE);
    d1l_ui_modal_hide(s_contact_options_sheet);
}

static void create_contact_forget_sheet(lv_obj_t *screen)
{
    s_contact_forget_sheet = create_object(screen, "contact forget sheet");
    if (!s_contact_forget_sheet) {
        return;
    }
    lv_obj_set_size(s_contact_forget_sheet, 480, 424);
    lv_obj_set_pos(s_contact_forget_sheet, 0, 56);
    lv_obj_set_style_radius(s_contact_forget_sheet, 0, 0);
    lv_obj_set_style_bg_color(s_contact_forget_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_width(s_contact_forget_sheet, 0, 0);
    lv_obj_set_style_pad_all(s_contact_forget_sheet, 0, 0);
    lv_obj_clear_flag(s_contact_forget_sheet, LV_OBJ_FLAG_SCROLLABLE);
    d1l_ui_modal_hide(s_contact_forget_sheet);
}

static void create_node_detail_sheet(lv_obj_t *screen)
{
    s_node_detail_sheet = create_object(screen, "node detail sheet");
    if (!s_node_detail_sheet) {
        return;
    }
    lv_obj_set_size(s_node_detail_sheet, 448, 320);
    lv_obj_set_pos(s_node_detail_sheet, 16, 82);
    lv_obj_set_style_radius(s_node_detail_sheet, 8, 0);
    lv_obj_set_style_bg_color(s_node_detail_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_color(s_node_detail_sheet, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(s_node_detail_sheet, 1, 0);
    lv_obj_set_style_pad_all(s_node_detail_sheet, 12, 0);
    lv_obj_clear_flag(s_node_detail_sheet, LV_OBJ_FLAG_SCROLLABLE);
    d1l_ui_modal_hide(s_node_detail_sheet);
}

static void create_contact_edit_sheet(lv_obj_t *screen)
{
    if (!screen || s_contact_edit_sheet) {
        return;
    }
    s_contact_edit_sheet = create_object(screen, "contact edit sheet");
    if (!s_contact_edit_sheet) {
        return;
    }
    lv_obj_set_size(s_contact_edit_sheet, 480, 424);
    lv_obj_set_pos(s_contact_edit_sheet, 0, 56);
    lv_obj_set_style_radius(s_contact_edit_sheet, 0, 0);
    lv_obj_set_style_bg_color(s_contact_edit_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_width(s_contact_edit_sheet, 0, 0);
    lv_obj_set_style_pad_all(s_contact_edit_sheet, 0, 0);
    lv_obj_clear_flag(s_contact_edit_sheet, LV_OBJ_FLAG_SCROLLABLE);

    s_contact_edit_title = create_label(s_contact_edit_sheet, "Rename Contact", 0xF4F7FB);
    if (!s_contact_edit_title) {
        goto fail;
    }
    lv_obj_set_style_text_font(s_contact_edit_title, &lv_font_montserrat_24, 0);
    lv_label_set_long_mode(s_contact_edit_title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_contact_edit_title, 364);
    lv_obj_set_pos(s_contact_edit_title, 100, 10);

    create_button(s_contact_edit_sheet, "Back", 12, 6, 72, 44,
                  close_contact_edit_event_cb, NULL);
    create_button(s_contact_edit_sheet, "Save", 16, 360, 448, 52,
                  save_contact_edit_event_cb, NULL);

    lv_obj_t *meta = create_label(s_contact_edit_sheet,
                                  "Alias only; retained history remains", 0x8EA0AE);
    if (!meta) {
        goto fail;
    }
    lv_obj_set_pos(meta, 16, 60);

    s_contact_edit_textarea = create_textarea(s_contact_edit_sheet, "contact edit textarea");
    if (!s_contact_edit_textarea) {
        goto fail;
    }
    lv_obj_set_size(s_contact_edit_textarea, 448, 48);
    lv_obj_set_pos(s_contact_edit_textarea, 16, 88);
    lv_textarea_set_one_line(s_contact_edit_textarea, true);
    lv_textarea_set_max_length(s_contact_edit_textarea, D1L_CONTACT_ALIAS_LEN - 1U);
    lv_textarea_set_placeholder_text(s_contact_edit_textarea, "Contact alias");
    lv_obj_set_style_radius(s_contact_edit_textarea, 8, 0);
    lv_obj_set_style_bg_color(s_contact_edit_textarea, lv_color_hex(0x071018), 0);
    lv_obj_set_style_border_color(s_contact_edit_textarea, lv_color_hex(0x263241), 0);
    lv_obj_set_style_text_color(s_contact_edit_textarea, lv_color_hex(0xF4F7FB), 0);
    lv_obj_set_style_text_color(s_contact_edit_textarea, lv_color_hex(0x8EA0AE),
                                LV_PART_TEXTAREA_PLACEHOLDER);

    s_contact_edit_keyboard = create_keyboard(s_contact_edit_sheet, "contact edit keyboard");
    if (!s_contact_edit_keyboard) {
        goto fail;
    }
    d1l_ui_keyboard_configure_input(s_contact_edit_keyboard, s_contact_edit_textarea,
                                    16, 148, 448, 200);
    lv_obj_add_event_cb(s_contact_edit_keyboard, contact_edit_keyboard_event_cb,
                        LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_contact_edit_keyboard, contact_edit_keyboard_event_cb,
                        LV_EVENT_CANCEL, NULL);

    d1l_ui_modal_hide(s_contact_edit_sheet);
    return;

fail:
    if (s_contact_edit_sheet) {
        lv_obj_del(s_contact_edit_sheet);
    }
    s_contact_edit_sheet = NULL;
    s_contact_edit_title = NULL;
    s_contact_edit_textarea = NULL;
    s_contact_edit_keyboard = NULL;
}

static void create_contact_export_sheet(lv_obj_t *screen)
{
    s_contact_export_sheet = create_object(screen, "contact export sheet");
    if (!s_contact_export_sheet) {
        return;
    }
    lv_obj_set_size(s_contact_export_sheet, 480, 424);
    lv_obj_set_pos(s_contact_export_sheet, 0, 56);
    lv_obj_set_style_radius(s_contact_export_sheet, 0, 0);
    lv_obj_set_style_bg_color(s_contact_export_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_width(s_contact_export_sheet, 0, 0);
    lv_obj_set_style_pad_all(s_contact_export_sheet, 0, 0);
    lv_obj_clear_flag(s_contact_export_sheet, LV_OBJ_FLAG_SCROLLABLE);
    d1l_ui_modal_hide(s_contact_export_sheet);
}

static void create_route_detail_sheet(lv_obj_t *screen)
{
    s_route_detail_sheet = create_object(screen, "route detail sheet");
    if (!s_route_detail_sheet) {
        return;
    }
    lv_obj_set_size(s_route_detail_sheet, 448, 250);
    lv_obj_set_pos(s_route_detail_sheet, 16, 132);
    lv_obj_set_style_radius(s_route_detail_sheet, 8, 0);
    lv_obj_set_style_bg_color(s_route_detail_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_color(s_route_detail_sheet, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(s_route_detail_sheet, 1, 0);
    lv_obj_set_style_pad_all(s_route_detail_sheet, 12, 0);
    lv_obj_clear_flag(s_route_detail_sheet, LV_OBJ_FLAG_SCROLLABLE);
    d1l_ui_modal_hide(s_route_detail_sheet);
}

static void create_route_trace_sheet(lv_obj_t *screen)
{
    s_route_trace_sheet = create_object(screen, "route trace sheet");
    if (!s_route_trace_sheet) {
        return;
    }
    lv_obj_set_size(s_route_trace_sheet, 480, 424);
    lv_obj_set_pos(s_route_trace_sheet, 0, 56);
    lv_obj_set_style_radius(s_route_trace_sheet, 0, 0);
    lv_obj_set_style_bg_color(s_route_trace_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_width(s_route_trace_sheet, 0, 0);
    lv_obj_set_style_pad_all(s_route_trace_sheet, 0, 0);
    lv_obj_clear_flag(s_route_trace_sheet, LV_OBJ_FLAG_SCROLLABLE);
    d1l_ui_modal_hide(s_route_trace_sheet);
}

static void create_packet_detail_sheet(lv_obj_t *screen)
{
    s_packet_detail_sheet = create_object(screen, "packet detail sheet");
    if (!s_packet_detail_sheet) {
        return;
    }
    lv_obj_set_size(s_packet_detail_sheet, 448, 326);
    lv_obj_set_pos(s_packet_detail_sheet, 16, 76);
    lv_obj_set_style_radius(s_packet_detail_sheet, 8, 0);
    lv_obj_set_style_bg_color(s_packet_detail_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_color(s_packet_detail_sheet, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(s_packet_detail_sheet, 1, 0);
    lv_obj_set_style_pad_all(s_packet_detail_sheet, 12, 0);
    lv_obj_set_scrollbar_mode(s_packet_detail_sheet, LV_SCROLLBAR_MODE_AUTO);
    d1l_ui_modal_hide(s_packet_detail_sheet);
}

static void create_packet_search_sheet(lv_obj_t *screen)
{
    s_packet_search_sheet = create_object(screen, "packet search sheet");
    if (!s_packet_search_sheet) {
        return;
    }
    lv_obj_set_size(s_packet_search_sheet, 448, 320);
    lv_obj_set_pos(s_packet_search_sheet, 16, 82);
    lv_obj_set_style_radius(s_packet_search_sheet, 8, 0);
    lv_obj_set_style_bg_color(s_packet_search_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_color(s_packet_search_sheet, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(s_packet_search_sheet, 1, 0);
    lv_obj_set_style_pad_all(s_packet_search_sheet, 12, 0);
    lv_obj_clear_flag(s_packet_search_sheet, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = create_label(s_packet_search_sheet, "Packet Search", 0xF4F7FB);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(title, 8, 4);

    create_button(s_packet_search_sheet, "Apply", 212, 0, 66, 40, apply_packet_search_event_cb, NULL);
    create_button(s_packet_search_sheet, "Clear", 286, 0, 64, 40, clear_packet_search_event_cb, NULL);
    create_button(s_packet_search_sheet, "Close", 358, 0, 66, 40, close_packet_search_event_cb, NULL);

    s_packet_search_textarea = create_textarea(s_packet_search_sheet, "packet search textarea");
    if (!s_packet_search_textarea) {
        d1l_ui_modal_hide(s_packet_search_sheet);
        return;
    }
    lv_obj_set_size(s_packet_search_textarea, 424, 48);
    lv_obj_set_pos(s_packet_search_textarea, 0, 54);
    lv_textarea_set_one_line(s_packet_search_textarea, true);
    lv_textarea_set_max_length(s_packet_search_textarea, D1L_PACKET_LOG_QUERY_TEXT_LEN - 1U);
    lv_textarea_set_placeholder_text(s_packet_search_textarea, "Search kind, note, raw hex");
    lv_obj_set_style_radius(s_packet_search_textarea, 8, 0);
    lv_obj_set_style_bg_color(s_packet_search_textarea, lv_color_hex(0x071018), 0);
    lv_obj_set_style_border_color(s_packet_search_textarea, lv_color_hex(0x263241), 0);
    lv_obj_set_style_text_color(s_packet_search_textarea, lv_color_hex(0xF4F7FB), 0);
    lv_obj_set_style_text_color(s_packet_search_textarea, lv_color_hex(0x8EA0AE), LV_PART_TEXTAREA_PLACEHOLDER);

    s_packet_search_keyboard = create_keyboard(s_packet_search_sheet, "packet search keyboard");
    if (!s_packet_search_keyboard) {
        d1l_ui_modal_hide(s_packet_search_sheet);
        return;
    }
    d1l_ui_keyboard_configure_input(s_packet_search_keyboard, s_packet_search_textarea,
                                    0, 114, 424, 194);
    lv_obj_add_event_cb(s_packet_search_keyboard, packet_search_keyboard_event_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_packet_search_keyboard, packet_search_keyboard_event_cb, LV_EVENT_CANCEL, NULL);

    d1l_ui_modal_hide(s_packet_search_sheet);
}

static void create_mesh_roles_sheet(lv_obj_t *screen)
{
    s_mesh_roles_sheet = create_object(screen, "mesh roles sheet");
    if (!s_mesh_roles_sheet) {
        return;
    }
    lv_obj_set_size(s_mesh_roles_sheet, 480, 424);
    lv_obj_set_pos(s_mesh_roles_sheet, 0, 56);
    lv_obj_set_style_radius(s_mesh_roles_sheet, 0, 0);
    lv_obj_set_style_bg_color(s_mesh_roles_sheet, lv_color_hex(0x071018), 0);
    lv_obj_set_style_border_width(s_mesh_roles_sheet, 0, 0);
    lv_obj_set_style_pad_all(s_mesh_roles_sheet, 0, 0);
    lv_obj_clear_flag(s_mesh_roles_sheet, LV_OBJ_FLAG_SCROLLABLE);
    d1l_ui_modal_hide(s_mesh_roles_sheet);
}

static void create_onboarding_sheet(lv_obj_t *screen)
{
    s_onboarding_sheet = create_object(screen, "onboarding sheet");
    if (!s_onboarding_sheet) {
        return;
    }
    lv_obj_set_size(s_onboarding_sheet, 480, 480);
    lv_obj_set_pos(s_onboarding_sheet, 0, 0);
    lv_obj_set_style_radius(s_onboarding_sheet, 0, 0);
    lv_obj_set_style_bg_color(s_onboarding_sheet, lv_color_hex(0x071018), 0);
    lv_obj_set_style_border_width(s_onboarding_sheet, 0, 0);
    lv_obj_set_style_pad_all(s_onboarding_sheet, 12, 0);
    lv_obj_clear_flag(s_onboarding_sheet, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = create_label(s_onboarding_sheet, "MeshCore DeskOS D1L", 0xF4F7FB);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(title, 8, 4);

    lv_obj_t *subtitle = create_label(s_onboarding_sheet, "First boot setup", 0x5EEAD4);
    lv_obj_set_pos(subtitle, 8, 38);

    lv_obj_t *name_label = create_label(s_onboarding_sheet, "Node name", 0x8EA0AE);
    lv_obj_set_pos(name_label, 8, 68);

    s_onboarding_name_textarea = create_textarea(s_onboarding_sheet, "onboarding name textarea");
    if (!s_onboarding_name_textarea) {
        d1l_ui_modal_hide(s_onboarding_sheet);
        return;
    }
    lv_obj_set_size(s_onboarding_name_textarea, 424, 48);
    lv_obj_set_pos(s_onboarding_name_textarea, 8, 92);
    lv_textarea_set_one_line(s_onboarding_name_textarea, true);
    lv_textarea_set_max_length(s_onboarding_name_textarea, D1L_NODE_NAME_LEN - 1U);
    lv_textarea_set_text(s_onboarding_name_textarea, d1l_settings_current()->node_name);
    lv_obj_set_style_radius(s_onboarding_name_textarea, 8, 0);
    lv_obj_set_style_bg_color(s_onboarding_name_textarea, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_color(s_onboarding_name_textarea, lv_color_hex(0x263241), 0);
    lv_obj_set_style_text_color(s_onboarding_name_textarea, lv_color_hex(0xF4F7FB), 0);

    lv_obj_t *preset = create_label(s_onboarding_sheet,
                                    "Canada/USA preset confirmed  910.525 BW62.5 SF7 CR5",
                                    0xE5EDF5);
    lv_label_set_long_mode(preset, LV_LABEL_LONG_DOT);
    lv_obj_set_width(preset, 424);
    lv_obj_set_pos(preset, 8, 152);

    lv_obj_t *role = create_label(s_onboarding_sheet,
                                  "Role Desk Companion  Wi-Fi off  BLE off  Observer off",
                                  0x8EA0AE);
    lv_label_set_long_mode(role, LV_LABEL_LONG_DOT);
    lv_obj_set_width(role, 424);
    lv_obj_set_pos(role, 8, 180);

    create_button(s_onboarding_sheet, "Start", 8, 210, 116, 40, onboarding_start_event_cb, NULL);
    create_button(s_onboarding_sheet, "Use Defaults", 136, 210, 146, 40,
                  onboarding_defaults_event_cb, NULL);

    s_onboarding_keyboard = create_keyboard(s_onboarding_sheet, "onboarding keyboard");
    if (!s_onboarding_keyboard) {
        d1l_ui_modal_hide(s_onboarding_sheet);
        return;
    }
    d1l_ui_keyboard_configure_input(s_onboarding_keyboard, s_onboarding_name_textarea,
                                    8, 258, 424, 160);
    lv_obj_add_event_cb(s_onboarding_keyboard, onboarding_keyboard_event_cb, LV_EVENT_READY, NULL);

    if (d1l_settings_current()->onboarding_complete) {
        s_onboarding_visible = false;
        d1l_ui_modal_hide(s_onboarding_sheet);
    } else {
        s_onboarding_visible = true;
    }
}

static void create_lock_overlay(lv_obj_t *screen)
{
    s_lock_overlay = create_object(screen, "lock overlay");
    if (!s_lock_overlay) {
        return;
    }
    lv_obj_set_size(s_lock_overlay, 480, 480);
    lv_obj_set_pos(s_lock_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_lock_overlay, lv_color_hex(0x02060A), 0);
    lv_obj_set_style_border_width(s_lock_overlay, 0, 0);
    lv_obj_clear_flag(s_lock_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_lock_overlay, unlock_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *title = create_label(s_lock_overlay, "MeshCore DeskOS", 0xF4F7FB);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -24);
    lv_obj_t *hint = create_label(s_lock_overlay, "Tap to unlock", 0x5EEAD4);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 24);
    s_lock_visible = false;
    lv_obj_add_flag(s_lock_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void ui_task(void *arg)
{
    (void)arg;
    while (true) {
        uint32_t wait_ms = lv_timer_handler();
        d1l_health_monitor_sample_lvgl();
        process_pending_tab_switch();
        process_pending_content_refresh();
        process_pending_scroll_probe();
        process_pending_compose_probe();
        if (d1l_ui_phase1_tab_switch_pending()) {
            wait_ms = D1L_UI_TIMER_MIN_SLEEP_MS;
        }
        if (wait_ms < D1L_UI_TIMER_MIN_SLEEP_MS) {
            wait_ms = D1L_UI_TIMER_MIN_SLEEP_MS;
        } else if (wait_ms > D1L_UI_TIMER_MAX_SLEEP_MS) {
            wait_ms = D1L_UI_TIMER_MAX_SLEEP_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(wait_ms));
    }
}

static void touch_poll_task(void *arg)
{
    (void)arg;
    while (true) {
        d1l_board_touch_state_t sample = {0};
        if (d1l_board_touch_read(&sample) != ESP_OK) {
            sample.pressed = false;
            sample.coordinate_valid = false;
        }
        publish_touch_state(&sample);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

esp_err_t d1l_ui_phase1_show_home(void)
{
    set_map_interactive_touch_authorized(false);
    d1l_ui_map_viewport_release();
    d1l_ui_modal_reset();
    s_screen = create_screen_object("root screen");
    if (!s_screen) {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x071018), 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    create_top_bar(s_screen);

    s_content = create_object(s_screen, "content root");
    if (!s_content) {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_set_size(s_content, 480, 424);
    lv_obj_set_pos(s_content, 0, 56);
    d1l_ui_screen_configure_content_root(s_content, true);

    create_dock(s_screen);
    create_sheet(s_screen);
    create_compose_sheet(s_screen);
    create_public_history_sheet(s_screen);
    create_public_search_sheet(s_screen);
    create_message_detail_sheet(s_screen);
    create_dm_thread_sheet(s_screen);
    create_radio_settings_sheet(s_screen);
    create_storage_sheet(s_screen);
    create_wifi_sheet(s_screen);
    create_ble_sheet(s_screen);
    create_display_sheet(s_screen);
    create_diagnostics_sheet(s_screen);
    create_map_location_sheet(s_screen);
    create_map_options_sheet(s_screen);
    create_contact_detail_sheet(s_screen);
    create_contact_options_sheet(s_screen);
    create_contact_forget_sheet(s_screen);
    create_contact_export_sheet(s_screen);
    create_node_detail_sheet(s_screen);
    create_route_detail_sheet(s_screen);
    create_route_trace_sheet(s_screen);
    create_packet_detail_sheet(s_screen);
    create_packet_search_sheet(s_screen);
    create_mesh_roles_sheet(s_screen);
    create_toast(s_screen);
    create_lock_overlay(s_screen);
    create_onboarding_sheet(s_screen);

    render_active_tab();
    lv_timer_create(map_viewport_timer_cb, D1L_UI_MAP_VIEWPORT_REFRESH_MS, NULL);
    lv_timer_create(refresh_timer_cb, 2000, NULL);
    lv_scr_load(s_screen);
    return ESP_OK;
}

esp_err_t d1l_ui_phase1_start(void)
{
    if (s_started) {
        return ESP_OK;
    }
    if (!s_scroll_probe_done_sem) {
        s_scroll_probe_done_sem = xSemaphoreCreateBinary();
        if (!s_scroll_probe_done_sem) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (!s_compose_probe_done_sem) {
        s_compose_probe_done_sem = xSemaphoreCreateBinary();
        if (!s_compose_probe_done_sem) {
            return ESP_ERR_NO_MEM;
        }
    }

    lv_init();
    d1l_health_monitor_set_lvgl_ready(true);
    ESP_RETURN_ON_ERROR(init_capture_buffers(), TAG, "capture buffer init failed");

    const size_t buffer_pixels = D1L_UI_CAPTURE_WIDTH * D1L_UI_CAPTURE_HEIGHT;
#if CONFIG_LCD_LVGL_DIRECT_MODE
    void *fb1 = NULL;
    void *fb2 = NULL;
    bsp_lcd_get_frame_buffer(&fb1, &fb2);
    s_buf1 = (lv_color_t *)fb1;
    s_buf2 = (lv_color_t *)fb2;
#else
    s_buf1 = heap_caps_malloc(buffer_pixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_buf2 = heap_caps_malloc(buffer_pixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
    if (!s_buf1 || !s_buf2) {
        return ESP_ERR_NO_MEM;
    }

    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, s_buf2, buffer_pixels);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = D1L_UI_CAPTURE_WIDTH;
    s_disp_drv.ver_res = D1L_UI_CAPTURE_HEIGHT;
    s_disp_drv.flush_cb = flush_cb;
    s_disp_drv.draw_buf = &s_draw_buf;
#if CONFIG_LCD_LVGL_DIRECT_MODE
    s_disp_drv.direct_mode = 1;
    bsp_lcd_flush_is_last_register(lcd_flush_is_last_cb);
    bsp_lcd_direct_mode_register(lcd_direct_mode_copy_cb);
#endif
    s_bsp_flush_ready_registered = (bsp_lcd_set_cb(lcd_flush_ready_cb, &s_disp_drv) == ESP_OK);
    if (!s_bsp_flush_ready_registered) {
        ESP_LOGW(TAG, "BSP LCD flush callback registration failed; falling back to immediate ready");
    }
    lv_disp_drv_register(&s_disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read_cb;
    lv_indev_drv_register(&indev_drv);

    const esp_timer_create_args_t tick_args = {
        .callback = &lv_tick_task,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, 5000));

    ESP_RETURN_ON_ERROR(d1l_ui_phase1_show_home(), TAG, "home screen failed");
    xTaskCreatePinnedToCore(touch_poll_task, "d1l_touch", D1L_TOUCH_TASK_STACK_BYTES, NULL, 4,
                            &s_touch_task_handle, D1L_UI_TASK_CORE);
    xTaskCreatePinnedToCore(ui_task, "d1l_ui", D1L_UI_TASK_STACK_BYTES, NULL, 5,
                            &s_ui_task_handle, D1L_UI_TASK_CORE);
    d1l_health_monitor_register_ui_task(s_ui_task_handle);
    d1l_app_model_get()->ui_ready = true;
    s_started = true;
    return ESP_OK;
}
