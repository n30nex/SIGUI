#include "ui_phase1.h"

#include <ctype.h>
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
#include "ui_modal.h"
#include "ui_navigation.h"
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
static bool s_started = false;
static lv_obj_t *s_screen;
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
static lv_obj_t *s_map_tiles_sheet;
static lv_obj_t *s_map_tiles_url_textarea;
static lv_obj_t *s_map_tiles_attribution_textarea;
static lv_obj_t *s_map_tiles_zoom_label;
static lv_obj_t *s_map_tiles_keyboard;
static lv_obj_t *s_contact_detail_sheet;
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
static int32_t s_map_location_lat_e7 = 436532000L;
static int32_t s_map_location_lon_e7 = -793832000L;
static uint8_t s_map_tile_provider_zoom = D1L_MAP_TILE_DEFAULT_ZOOM;
static bool s_map_location_prompt_seen;
static d1l_contact_entry_t s_contact_detail_contact;
static d1l_contact_entry_t s_contact_export_contact;
static d1l_node_view_t s_node_detail_node;
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
static const uint32_t D1L_UI_SCROLL_PROBE_TIMEOUT_MS = 1500U;
static const uint32_t D1L_UI_COMPOSE_PROBE_TIMEOUT_MS = 1500U;
static const size_t D1L_PACKET_UI_INITIAL_ROWS = 100U;
static const size_t D1L_PACKET_UI_LOAD_OLDER_STEP = 100U;
static size_t s_public_history_limit = D1L_PUBLIC_HISTORY_UI_INITIAL_ROWS;
static size_t s_dm_thread_limit = D1L_DM_THREAD_UI_INITIAL_ROWS;
static size_t s_packet_row_limit = 100U;
static size_t s_packet_skip_newest;
static size_t s_packet_total_matches;
static bool s_packet_sd_history_page;
static SemaphoreHandle_t s_scroll_probe_done_sem;
static portMUX_TYPE s_scroll_probe_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_scroll_probe_pending;
static char s_scroll_probe_surface[24];
static d1l_ui_scroll_probe_result_t s_scroll_probe_result;
static SemaphoreHandle_t s_compose_probe_done_sem;
static portMUX_TYPE s_compose_probe_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_compose_probe_pending;
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
static void render_node_detail_sheet(void);
static void open_dm_compose_event_cb(lv_event_t *event);
static void open_public_history_event_cb(lv_event_t *event);
static void open_public_search_event_cb(lv_event_t *event);
static void open_messages_public_event_cb(lv_event_t *event);
static void open_messages_dm_event_cb(lv_event_t *event);
static void open_home_dm_preview_event_cb(lv_event_t *event);
static void open_dm_thread_event_cb(lv_event_t *event);
static void open_contact_detail_event_cb(lv_event_t *event);
static void open_node_detail_event_cb(lv_event_t *event);
static void node_detail_dm_event_cb(lv_event_t *event);
static void open_node_dm_event_cb(lv_event_t *event);
static void open_contact_edit_event_cb(lv_event_t *event);
static void open_route_trace_event_cb(lv_event_t *event);
static void open_route_detail_event_cb(lv_event_t *event);
static void open_message_detail_event_cb(lv_event_t *event);
static void open_packet_detail_event_cb(lv_event_t *event);
static void open_packet_search_event_cb(lv_event_t *event);
static void open_mesh_roles_event_cb(lv_event_t *event);
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
} d1l_ui_content_generation_t;

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

static bool s_content_refresh_pending = false;
static d1l_ui_content_generation_t s_rendered_content_generation;
static bool s_rendered_content_generation_valid = false;
static d1l_packet_filter_mode_t s_packet_filter_mode = D1L_PACKET_FILTER_ALL;
static bool s_packets_paused;
static bool s_packet_detail_advanced;
static bool s_message_detail_advanced;

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
    };
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
        left->packet_count == right->packet_count;
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

static bool scroll_probe_pending(void)
{
    bool pending;

    portENTER_CRITICAL(&s_scroll_probe_lock);
    pending = s_scroll_probe_pending;
    portEXIT_CRITICAL(&s_scroll_probe_lock);
    return pending;
}

static bool compose_probe_pending(void)
{
    bool pending;

    portENTER_CRITICAL(&s_compose_probe_lock);
    pending = s_compose_probe_pending;
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
static void render_map_tiles_sheet(void);
static void create_contact_edit_sheet(lv_obj_t *screen);
static void hide_node_detail_sheet(void);
static void request_tab_event_cb(lv_event_t *event);
static void open_map_location_sheet_event_cb(lv_event_t *event);
static void open_map_tiles_sheet_event_cb(lv_event_t *event);
static const char *home_sd_state(const d1l_app_snapshot_t *snapshot);
static const char *packet_filter_direction(void);
static const char *packet_filter_kind(void);
static void message_detail_mode_event_cb(lv_event_t *event);
static void map_location_keyboard_event_cb(lv_event_t *event);
static void map_tiles_keyboard_event_cb(lv_event_t *event);
static void process_pending_content_refresh(void);
static void process_pending_scroll_probe(void);
static void process_pending_compose_probe(void);
static bool object_is_visible(lv_obj_t *obj);

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
    char normalized[16] = {0};
    if (!name || !out_target || out_target_len == 0) {
        return false;
    }
    size_t len = 0;
    while (*name && len + 1U < sizeof(normalized)) {
        char ch = (char)tolower((unsigned char)*name++);
        normalized[len++] = (ch == '-' || ch == ' ') ? '_' : ch;
    }
    normalized[len] = '\0';
    if (*name || len == 0) {
        return false;
    }

    const char *target = NULL;
    if (strcmp(normalized, "public") == 0 ||
        strcmp(normalized, "public_short") == 0) {
        target = "public";
    } else if (strcmp(normalized, "public_long") == 0) {
        target = "public_long";
    } else if (strcmp(normalized, "dm") == 0 ||
               strcmp(normalized, "direct") == 0 ||
               strcmp(normalized, "dm_short") == 0 ||
               strcmp(normalized, "direct_short") == 0) {
        target = "dm";
    } else if (strcmp(normalized, "dm_long") == 0 ||
               strcmp(normalized, "direct_long") == 0) {
        target = "dm_long";
    } else if (strcmp(normalized, "public_search") == 0) {
        target = "public_search";
    } else if (strcmp(normalized, "packet_search") == 0) {
        target = "packet_search";
    } else if (strcmp(normalized, "contact_edit") == 0) {
        target = "contact_edit";
    } else if (strcmp(normalized, "onboarding") == 0 ||
               strcmp(normalized, "onboarding_name") == 0) {
        target = "onboarding";
    } else if (strcmp(normalized, "map_location") == 0) {
        target = "map_location";
    } else if (strcmp(normalized, "map_provider") == 0 ||
               strcmp(normalized, "map_tiles") == 0) {
        target = "map_provider";
    } else if (strcmp(normalized, "wifi") == 0 ||
               strcmp(normalized, "wifi_ssid") == 0) {
        target = "wifi_ssid";
    } else if (strcmp(normalized, "wifi_password") == 0 ||
               strcmp(normalized, "wifi_pass") == 0) {
        target = "wifi_password";
    } else {
        return false;
    }

    snprintf(out_target, out_target_len, "%s", target);
    return true;
}

static void configure_content_scroll_root(lv_obj_t *root)
{
    if (!root) {
        return;
    }
    lv_obj_add_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(root, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(root, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x071018), 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_pad_bottom(root, 12, 0);
}

static void configure_home_content_root(lv_obj_t *root)
{
    if (!root) {
        return;
    }
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(root, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(root, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x071018), 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_pad_bottom(root, 0, 0);
}

static void configure_content_for_active_tab(void)
{
    const d1l_ui_chrome_layout_t layout =
        d1l_ui_chrome_layout_for_screen(d1l_ui_navigation_active());
    if (!layout.content_scrollable) {
        configure_home_content_root(s_content);
    } else {
        configure_content_scroll_root(s_content);
    }
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

static const char *d1l_compose_kb_map_lc[] = {
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", "\n",
    "z", "x", "c", "v", "b", "n", "m", ".", "?", "\n",
    "1#", "ABC", ",", "-", " ", LV_SYMBOL_BACKSPACE, LV_SYMBOL_OK, ""
};

static const lv_btnmatrix_ctrl_t d1l_compose_kb_ctrl_lc[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1,
    LV_KEYBOARD_CTRL_BTN_FLAGS | 2,
    LV_KEYBOARD_CTRL_BTN_FLAGS | 2,
    1,
    1,
    6,
    LV_KEYBOARD_CTRL_BTN_FLAGS | 2,
    LV_KEYBOARD_CTRL_BTN_FLAGS | 2
};

static const char *d1l_compose_kb_map_uc[] = {
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
    "A", "S", "D", "F", "G", "H", "J", "K", "L", "\n",
    "Z", "X", "C", "V", "B", "N", "M", ".", "?", "\n",
    "1#", "abc", ",", "-", " ", LV_SYMBOL_BACKSPACE, LV_SYMBOL_OK, ""
};

static const lv_btnmatrix_ctrl_t d1l_compose_kb_ctrl_uc[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1,
    LV_KEYBOARD_CTRL_BTN_FLAGS | 2,
    LV_KEYBOARD_CTRL_BTN_FLAGS | 2,
    1,
    1,
    6,
    LV_KEYBOARD_CTRL_BTN_FLAGS | 2,
    LV_KEYBOARD_CTRL_BTN_FLAGS | 2
};

static const char *d1l_compose_kb_map_spec[] = {
    "abc", "1", "2", "3", "4", "5", "6", "7", "8", "9", "\n",
    "0", "+", "-", "/", "*", "=", "%", "!", "?", "#", "\n",
    "@", "&", "(", ")", ":", ";", "\"", "'", ".", ",", "\n",
    "ABC", "_", " ", "/", LV_SYMBOL_BACKSPACE, LV_SYMBOL_OK, ""
};

static const lv_btnmatrix_ctrl_t d1l_compose_kb_ctrl_spec[] = {
    LV_KEYBOARD_CTRL_BTN_FLAGS | 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    LV_KEYBOARD_CTRL_BTN_FLAGS | 2,
    1,
    6,
    1,
    LV_KEYBOARD_CTRL_BTN_FLAGS | 2,
    LV_KEYBOARD_CTRL_BTN_FLAGS | 2
};

static void configure_compose_keyboard(lv_obj_t *keyboard)
{
    if (!keyboard) {
        return;
    }
    lv_keyboard_set_popovers(keyboard, false);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER,
                        d1l_compose_kb_map_lc, d1l_compose_kb_ctrl_lc);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_TEXT_UPPER,
                        d1l_compose_kb_map_uc, d1l_compose_kb_ctrl_uc);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_SPECIAL_1,
                        d1l_compose_kb_map_spec, d1l_compose_kb_ctrl_spec);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_SPECIAL_2,
                        d1l_compose_kb_map_spec, d1l_compose_kb_ctrl_spec);
    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_set_style_text_font(keyboard, &lv_font_montserrat_14, LV_PART_ITEMS);
    lv_obj_set_style_pad_all(keyboard, 4, 0);
    lv_obj_set_style_pad_row(keyboard, 6, 0);
    lv_obj_set_style_pad_column(keyboard, 4, 0);
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
    if (hidden) {
        lv_obj_add_flag(s_dock, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(s_dock, LV_OBJ_FLAG_HIDDEN);
    }
    request_full_screen_repaint();
}

static void restore_dock_for_active_tab(void)
{
    const d1l_ui_chrome_layout_t layout =
        d1l_ui_chrome_layout_for_screen(d1l_ui_navigation_active());
    set_dock_hidden(!layout.dock_visible);
}

static void layout_content_for_active_tab(void)
{
    if (s_content) {
        const d1l_ui_chrome_layout_t layout =
            d1l_ui_chrome_layout_for_screen(d1l_ui_navigation_active());
        lv_obj_set_pos(s_content, 0, layout.content_y);
        lv_obj_set_size(s_content, 480, layout.content_height);
    }
    restore_dock_for_active_tab();
}

static void hide_sheet(void)
{
    d1l_ui_modal_hide(s_sheet);
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
}

static void hide_dm_thread_sheet(void)
{
    d1l_ui_modal_hide(s_dm_thread_sheet);
    s_dm_thread_fingerprint[0] = '\0';
    s_dm_thread_alias[0] = '\0';
    s_dm_thread_limit = D1L_DM_THREAD_UI_INITIAL_ROWS;
}

static void hide_radio_settings_sheet(void)
{
    d1l_ui_modal_hide(s_radio_settings_sheet);
}

static void hide_storage_sheet(void)
{
    d1l_ui_modal_hide(s_storage_sheet);
}

static void hide_wifi_sheet(void)
{
    d1l_ui_modal_hide(s_wifi_sheet);
    restore_dock_for_active_tab();
}

static void hide_ble_sheet(void)
{
    d1l_ui_modal_hide(s_ble_sheet);
}

static void hide_display_sheet(void)
{
    d1l_ui_modal_hide(s_display_sheet);
}

static void hide_diagnostics_sheet(void)
{
    d1l_ui_modal_hide(s_diagnostics_sheet);
}

static void hide_map_location_sheet(void)
{
    d1l_ui_modal_hide(s_map_location_sheet);
    restore_dock_for_active_tab();
}

static void hide_map_tiles_sheet(void)
{
    d1l_ui_modal_hide(s_map_tiles_sheet);
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

static void hide_contact_detail_sheet(void)
{
    d1l_ui_modal_hide(s_contact_detail_sheet);
    hide_contact_edit_sheet();
    hide_node_detail_sheet();
    memset(&s_contact_detail_contact, 0, sizeof(s_contact_detail_contact));
}

static void hide_contact_export_sheet(void)
{
    d1l_ui_modal_hide(s_contact_export_sheet);
    memset(&s_contact_export_contact, 0, sizeof(s_contact_export_contact));
    s_contact_export_uri[0] = '\0';
}

static void hide_node_detail_sheet(void)
{
    d1l_ui_modal_hide(s_node_detail_sheet);
    memset(&s_node_detail_node, 0, sizeof(s_node_detail_node));
}

static void hide_route_detail_sheet(void)
{
    d1l_ui_modal_hide(s_route_detail_sheet);
    memset(&s_route_detail_route, 0, sizeof(s_route_detail_route));
}

static void hide_route_trace_sheet(void)
{
    d1l_ui_modal_hide(s_route_trace_sheet);
    memset(&s_route_trace_contact, 0, sizeof(s_route_trace_contact));
}

static void hide_packet_detail_sheet(void)
{
    d1l_ui_modal_hide(s_packet_detail_sheet);
    memset(&s_packet_detail_packet, 0, sizeof(s_packet_detail_packet));
}

static void hide_packet_search_sheet(void)
{
    d1l_ui_modal_hide(s_packet_search_sheet);
    restore_dock_for_active_tab();
}

static void hide_mesh_roles_sheet(void)
{
    d1l_ui_modal_hide(s_mesh_roles_sheet);
}

static void hide_onboarding_sheet(void)
{
    d1l_ui_modal_hide(s_onboarding_sheet);
    s_onboarding_visible = false;
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
    if (s_onboarding_probe_suppressed && object_is_visible(s_compose_sheet)) {
        hide_onboarding_sheet();
        return;
    }
    s_onboarding_visible = true;
    d1l_ui_modal_show(s_onboarding_sheet);
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
                  home_sd_state(snapshot));
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

static void format_coord_e7(char *dest, size_t dest_size, int32_t value)
{
    int64_t scaled = value;
    const char *sign = "";
    if (scaled < 0) {
        sign = "-";
        scaled = -scaled;
    }
    snprintf(dest, dest_size, "%s%lld.%07lld", sign,
             (long long)(scaled / 10000000LL),
             (long long)(scaled % 10000000LL));
}

static void map_location_from_snapshot(const d1l_app_snapshot_t *snapshot)
{
    if (snapshot && snapshot->map_location_set) {
        s_map_location_lat_e7 = snapshot->map_lat_e7;
        s_map_location_lon_e7 = snapshot->map_lon_e7;
        return;
    }
    s_map_location_lat_e7 = 436532000L;
    s_map_location_lon_e7 = -793832000L;
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
           !node_role_is_managed_service(view->role);
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

static const char *home_sd_state(const d1l_app_snapshot_t *snapshot)
{
    if (!snapshot) {
        return "unknown";
    }
    if (snapshot->storage_data_enabled || snapshot->storage_sd_data_root_ready) {
        return "ready";
    }
    if (snapshot->storage_setup_required) {
        return "setup";
    }
    if (snapshot->storage_sd_state && snapshot->storage_sd_state[0]) {
        return snapshot->storage_sd_state;
    }
    return "fallback";
}

static lv_obj_t *render_home_status_icon(lv_obj_t *parent,
                                         int x,
                                         int y,
                                         int w,
                                         const char *icon,
                                         const char *title,
                                         uint32_t accent,
                                         lv_event_cb_t cb,
                                         void *user_data)
{
    lv_obj_t *chip = create_panel(parent, x, y, w, 56);
    if (!chip) {
        return NULL;
    }
    lv_obj_set_style_pad_all(chip, 4, 0);
    if (cb) {
        lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(chip, cb, LV_EVENT_CLICKED, user_data);
    }
    lv_obj_t *icon_label = create_label(chip, icon, accent);
    obj_set_style_text_font_if(icon_label, &lv_font_montserrat_24);
    label_set_dot_width(icon_label, w - 8);
    lv_obj_set_style_text_align(icon_label, LV_TEXT_ALIGN_CENTER, 0);
    obj_set_pos_if(icon_label, 4, 4);

    lv_obj_t *label = create_label(chip, title, accent);
    label_set_dot_width(label, w - 8);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    obj_set_pos_if(label, 4, 38);
    return chip;
}

static lv_obj_t *render_home_launcher_tile(lv_obj_t *parent,
                                           int x,
                                           int y,
                                           const char *icon,
                                           const char *title,
                                           const char *detail,
                                           uint32_t accent,
                                           lv_event_cb_t cb,
                                           void *user_data)
{
    lv_obj_t *tile = create_panel(parent, x, y, 110, 118);
    if (!tile) {
        return NULL;
    }
    lv_obj_set_style_radius(tile, 4, 0);
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x0F1712), 0);
    lv_obj_set_style_border_color(tile, lv_color_hex(0x1F372E), 0);
    lv_obj_set_style_pad_all(tile, 6, 0);
    if (cb) {
        lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(tile, cb, LV_EVENT_CLICKED, user_data);
    }

    lv_obj_t *icon_label = create_label(tile, icon, accent);
    obj_set_style_text_font_if(icon_label, &lv_font_montserrat_24);
    label_set_dot_width(icon_label, 98);
    lv_obj_set_style_text_align(icon_label, LV_TEXT_ALIGN_CENTER, 0);
    obj_set_pos_if(icon_label, 0, 16);

    lv_obj_t *title_label = create_label(tile, title, 0xF4F7FB);
    label_set_dot_width(title_label, 98);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
    obj_set_pos_if(title_label, 0, 62);

    lv_obj_t *detail_label = create_label(tile, detail, 0x8EA0AE);
    label_set_dot_width(detail_label, 98);
    lv_obj_set_style_text_align(detail_label, LV_TEXT_ALIGN_CENTER, 0);
    obj_set_pos_if(detail_label, 0, 88);
    return tile;
}

static void format_age_label(char *dest, size_t dest_size, uint32_t age_sec)
{
    if (!dest || dest_size == 0) {
        return;
    }
    if (age_sec >= 3600U) {
        snprintf(dest, dest_size, "%luh", (unsigned long)(age_sec / 3600U));
    } else if (age_sec >= 60U) {
        snprintf(dest, dest_size, "%lum", (unsigned long)(age_sec / 60U));
    } else {
        snprintf(dest, dest_size, "%lus", (unsigned long)age_sec);
    }
}

static void render_home_message_preview(lv_obj_t *parent,
                                        int y,
                                        const d1l_home_message_preview_t *entry)
{
    lv_obj_t *row = create_panel(parent, 18, y, 424, 76);
    if (!row || !entry) {
        return;
    }
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row,
                        entry->is_dm ? open_home_dm_preview_event_cb : open_public_history_event_cb,
                        LV_EVENT_CLICKED,
                        entry->is_dm ? (void *)entry : NULL);
    lv_obj_set_style_pad_all(row, 8, 0);

    char age[12];
    char meta[96];
    char snr[16];
    format_age_label(age, sizeof(age), entry->age_sec);
    format_snr_tenths(snr, sizeof(snr), entry->snr_tenths);

    lv_obj_t *sender = create_label(row, entry->sender[0] ? entry->sender : "Message",
                                    entry->unread ? 0xFBBF24 :
                                    (entry->is_dm ? 0xA7F3D0 : 0x5EEAD4));
    label_set_dot_width(sender, 240);
    lv_obj_set_pos(sender, 8, 4);
    lv_obj_t *badge = create_label(row, entry->status[0] ? entry->status : (entry->is_dm ? "DM" : "Public"),
                                   entry->is_dm ? 0xC4B5FD : 0x8EA0AE);
    label_set_dot_width(badge, 118);
    lv_obj_set_pos(badge, 292, 4);
    lv_obj_t *text = create_label(row, entry->text[0] ? entry->text : "No text", 0xE5EDF5);
    label_set_dot_width(text, 392);
    lv_obj_set_pos(text, 8, 28);
    snprintf(meta, sizeof(meta), "%s ago  rssi %d  snr %s  hops %u",
             age, entry->rssi_dbm, snr, entry->path_hops);
    lv_obj_t *details = create_label(row, meta, 0x8EA0AE);
    label_set_dot_width(details, 392);
    lv_obj_set_pos(details, 8, 52);
}

static void render_home_repeater_preview(lv_obj_t *parent,
                                         int y,
                                         const d1l_home_repeater_preview_t *entry)
{
    char snr[16];
    char age[12];
    lv_obj_t *row = create_panel(parent, 18, y, 424, 50);
    if (!row || !entry) {
        return;
    }
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, request_tab_event_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)D1L_UI_TAB_NODES);
    lv_obj_set_style_pad_all(row, 8, 0);
    lv_obj_t *name = create_label(row, entry->label[0] ? entry->label : "Repeater", 0xFBBF24);
    label_set_dot_width(name, 210);
    obj_align_if(name, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *role = create_label(row, entry->kind[0] ? entry->kind : "repeater", 0x8EA0AE);
    obj_align_if(role, LV_ALIGN_TOP_RIGHT, 0, 0);
    format_snr_tenths(snr, sizeof(snr), entry->snr_tenths);
    format_age_label(age, sizeof(age), entry->age_sec);
    lv_obj_t *meta = create_label(row, "", 0xE5EDF5);
    label_set_fmt(meta, "%s ago  route %s  rssi %d  snr %s  hops %u",
                  age,
                  entry->route[0] ? entry->route : "unknown",
                  entry->rssi_dbm, snr, entry->path_hops);
    label_set_dot_width(meta, 392);
    obj_align_if(meta, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static void render_home(const d1l_app_snapshot_t *snapshot)
{
    char value[32];
    char detail[64];

    snprintf(value, sizeof(value), "%lu", (unsigned long)snapshot->public_unread_count);
    snprintf(detail, sizeof(detail), "%s new", value);
    render_home_launcher_tile(s_content, 8, 6, LV_SYMBOL_ENVELOPE, "Chats", detail,
                              snapshot->public_unread_count ? 0xFBBF24 : 0x00C2FF,
                              open_messages_public_event_cb, NULL);

    snprintf(value, sizeof(value), "%lu", (unsigned long)snapshot->dm_unread_count);
    snprintf(detail, sizeof(detail), "%s new", value);
    render_home_launcher_tile(s_content, 126, 6, LV_SYMBOL_FILE, "DMs", detail,
                              snapshot->dm_unread_count ? 0xFBBF24 : 0xA7F3D0,
                              open_messages_dm_event_cb, NULL);

    snprintf(detail, sizeof(detail), "%lu seen", (unsigned long)snapshot->recent_room_count);
    render_home_launcher_tile(s_content, 244, 6, LV_SYMBOL_DIRECTORY, "Rooms", detail,
                              snapshot->recent_room_count ? 0x5EEAD4 : 0x8EA0AE,
                              open_mesh_roles_event_cb, NULL);

    snprintf(detail, sizeof(detail), "%lu saved", (unsigned long)snapshot->contact_count);
    render_home_launcher_tile(s_content, 362, 6, LV_SYMBOL_CALL, "Contacts", detail,
                              snapshot->contact_count ? 0x5EEAD4 : 0x8EA0AE,
                              request_tab_event_cb, (void *)(uintptr_t)D1L_UI_TAB_NODES);

    snprintf(detail, sizeof(detail), "%lu heard", (unsigned long)snapshot->recent_repeater_count);
    render_home_launcher_tile(s_content, 8, 130, LV_SYMBOL_REFRESH, "Repeaters", detail,
                              snapshot->recent_repeater_count ? 0xFBBF24 : 0x8EA0AE,
                              open_mesh_roles_event_cb, NULL);

    render_home_launcher_tile(s_content, 126, 130, LV_SYMBOL_BELL, "Advertise", "manual",
                              0x00C2FF, open_sheet_event_cb, NULL);

    render_home_launcher_tile(s_content, 244, 130, LV_SYMBOL_GPS, "Map",
                              snapshot->map_tile_cache_ready ? "tiles ready" : "offline",
                              snapshot->map_tile_cache_ready ? 0x5EEAD4 : 0x00C2FF,
                              request_tab_event_cb, (void *)(uintptr_t)D1L_UI_TAB_MAP);

    render_home_launcher_tile(s_content, 362, 130, LV_SYMBOL_KEYBOARD, "Terminal", "diagnose",
                              0xC4B5FD, open_diagnostics_sheet_event_cb, NULL);

    snprintf(detail, sizeof(detail), "%lu rows", (unsigned long)snapshot->packet_count);
    render_home_launcher_tile(s_content, 8, 254, LV_SYMBOL_LIST, "Packets", detail,
                              snapshot->packet_count ? 0x5EEAD4 : 0x8EA0AE,
                              request_tab_event_cb, (void *)(uintptr_t)D1L_UI_TAB_PACKETS);

    render_home_launcher_tile(s_content, 126, 254, LV_SYMBOL_SETTINGS, "Settings", "setup",
                              0x00C2FF, request_tab_event_cb,
                              (void *)(uintptr_t)D1L_UI_TAB_SETTINGS);

    render_home_launcher_tile(s_content, 244, 254, LV_SYMBOL_HOME, "Setup", home_sd_state(snapshot),
                              snapshot->storage_data_enabled ? 0x5EEAD4 :
                              (snapshot->storage_setup_required ? 0xFBBF24 : 0x8EA0AE),
                              open_storage_sheet_event_cb, NULL);

    if (snapshot->signal_summary.sample_count > 0U) {
        snprintf(detail, sizeof(detail), "%d dBm", snapshot->signal_summary.latest_rssi_dbm);
    } else {
        snprintf(detail, sizeof(detail), "waiting");
    }
    render_home_launcher_tile(s_content, 362, 254, LV_SYMBOL_VOLUME_MAX, "Signal", detail,
                              snapshot->signal_summary.sample_count ? 0x5EEAD4 : 0x8EA0AE,
                              open_mesh_roles_event_cb, NULL);

    render_home_status_icon(s_content, 8, 386, 110, LV_SYMBOL_REFRESH, "Time",
                            snapshot->time_available ? 0x5EEAD4 : 0x00C2FF, NULL, NULL);
    render_home_status_icon(s_content, 126, 386, 110, LV_SYMBOL_WIFI, "Wi-Fi",
                            snapshot->wifi_enabled ? 0x5EEAD4 : 0x00C2FF,
                            open_wifi_sheet_event_cb, NULL);
    render_home_status_icon(s_content, 244, 386, 110, LV_SYMBOL_BLUETOOTH, "BLE",
                            snapshot->ble_companion_enabled ? 0xA7F3D0 : 0xC4B5FD,
                            open_ble_sheet_event_cb, NULL);
    render_home_status_icon(s_content, 362, 386, 110, LV_SYMBOL_SD_CARD, "SD",
                            snapshot->storage_data_enabled ? 0x5EEAD4 :
                            0xFBBF24,
                            open_storage_sheet_event_cb, NULL);
}

static void render_storage_line(lv_obj_t *parent, int y, const d1l_app_snapshot_t *snapshot)
{
    lv_obj_t *line = create_label(parent, "", 0x8EA0AE);
    label_set_fmt(line, "Storage %s  SD %s",
                  snapshot->storage_backend ? snapshot->storage_backend : "nvs",
                  snapshot->storage_sd_state ? snapshot->storage_sd_state : "unknown");
    label_set_dot_width(line, 126);
    obj_set_pos_if(line, 0, y);
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
    if (entry->public_key_hex[0] != '\0') {
        create_button(row, "DM", 350, -1, 48, 34, open_dm_compose_event_cb, (void *)entry);
    } else {
        lv_obj_t *type = create_label(row, entry->type, 0xA7F3D0);
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
    hide_map_tiles_sheet();
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
        set_dock_hidden(true);
        d1l_ui_modal_show(s_compose_sheet);
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
    if (!entry || entry->public_key_hex[0] == '\0') {
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
    hide_map_tiles_sheet();
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
        set_dock_hidden(true);
        d1l_ui_modal_show(s_compose_sheet);
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
    hide_contact_export_sheet();
}

static void contact_detail_dm_event_cb(lv_event_t *event)
{
    (void)event;
    open_dm_compose_for_contact(&s_contact_detail_contact);
}

static void close_contact_export_event_cb(lv_event_t *event)
{
    (void)event;
    hide_contact_export_sheet();
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
    lv_obj_set_pos(title, 8, 4);

    create_button(s_contact_export_sheet, "Close", 340, 0, 76, 40, close_contact_export_event_cb, NULL);

    lv_obj_t *subtitle = create_label(s_contact_export_sheet, "", 0x8EA0AE);
    label_set_fmt(subtitle, "MeshCore QR  %.16s  type %u", entry->fingerprint,
                  (unsigned)d1l_contact_store_meshcore_type_id(entry->type));
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_DOT);
    lv_obj_set_width(subtitle, 408);
    lv_obj_set_pos(subtitle, 8, 42);

    if (s_contact_export_uri[0] == '\0') {
        lv_obj_t *missing = create_label(s_contact_export_sheet,
                                         "No retained public key for QR export", 0xF87171);
        lv_obj_set_pos(missing, 8, 96);
        return;
    }

#if LV_USE_QRCODE
    lv_obj_t *qr = create_qrcode(s_contact_export_sheet, 166,
                                 lv_color_hex(0x02060A), lv_color_hex(0xF8FAFC),
                                 "contact export qr");
    if (!qr) {
        lv_obj_t *qr_error = create_label(s_contact_export_sheet, "QR allocation failed", 0xF87171);
        lv_obj_set_pos(qr_error, 24, 132);
        return;
    }
    lv_obj_set_pos(qr, 12, 74);
    lv_obj_set_style_border_width(qr, 5, 0);
    lv_obj_set_style_border_color(qr, lv_color_hex(0xF8FAFC), 0);
    if (lv_qrcode_update(qr, s_contact_export_uri, strlen(s_contact_export_uri)) != LV_RES_OK) {
        lv_obj_del(qr);
        lv_obj_t *qr_error = create_label(s_contact_export_sheet, "QR payload too long", 0xF87171);
        lv_obj_set_pos(qr_error, 24, 132);
    }
#else
    lv_obj_t *qr_disabled = create_label(s_contact_export_sheet, "QR renderer disabled in LVGL", 0xFBBF24);
    lv_obj_set_pos(qr_disabled, 24, 132);
#endif

    lv_obj_t *name = create_label(s_contact_export_sheet,
                                  entry->alias[0] ? entry->alias : entry->fingerprint,
                                  0xE5EDF5);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(name, 220);
    lv_obj_set_pos(name, 200, 84);

    lv_obj_t *key = create_label(s_contact_export_sheet, "public key retained", 0x5EEAD4);
    lv_obj_set_pos(key, 200, 118);

    lv_obj_t *uri_title = create_label(s_contact_export_sheet, "URI", 0x8EA0AE);
    lv_obj_set_pos(uri_title, 200, 152);
    lv_obj_t *uri = create_label(s_contact_export_sheet, s_contact_export_uri, 0x93C5FD);
    lv_label_set_long_mode(uri, LV_LABEL_LONG_DOT);
    lv_obj_set_width(uri, 220);
    lv_obj_set_pos(uri, 200, 176);

    lv_obj_t *note = create_label(s_contact_export_sheet,
                                  "Scan with a MeshCore client or copy from serial",
                                  0x8EA0AE);
    lv_label_set_long_mode(note, LV_LABEL_LONG_DOT);
    lv_obj_set_width(note, 392);
    lv_obj_set_pos(note, 8, 262);
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
        d1l_ui_modal_show(s_contact_export_sheet);
    }
}

static void show_contact_detail_after_edit(void)
{
    request_content_refresh();
    render_contact_detail_sheet();
    if (s_contact_detail_sheet) {
        d1l_ui_modal_show(s_contact_detail_sheet);
    }
}

static void close_contact_edit_event_cb(lv_event_t *event)
{
    (void)event;
    hide_contact_edit_sheet();
    show_contact_detail_after_edit();
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
        show_contact_detail_after_edit();
    }
    show_toast("Rename", ret);
}

static void forget_contact_edit_event_cb(lv_event_t *event)
{
    (void)event;
    if (s_contact_detail_contact.fingerprint[0] == '\0') {
        show_toast("Contact", ESP_ERR_INVALID_STATE);
        return;
    }
    d1l_contact_entry_t removed = {0};
    esp_err_t ret = d1l_app_model_delete_contact(s_contact_detail_contact.fingerprint, &removed);
    if (ret == ESP_OK) {
        hide_contact_edit_sheet();
        hide_contact_export_sheet();
        if (s_contact_detail_sheet) {
            d1l_ui_modal_hide(s_contact_detail_sheet);
        }
        memset(&s_contact_detail_contact, 0, sizeof(s_contact_detail_contact));
        request_content_refresh();
    }
    show_toast("Forget", ret);
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
    if (s_contact_detail_sheet) {
        d1l_ui_modal_hide(s_contact_detail_sheet);
    }
    if (s_contact_edit_title) {
        char title[48];
        snprintf(title, sizeof(title), "Edit %.32s",
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
    d1l_ui_modal_show(s_contact_edit_sheet);
    set_dock_hidden(true);
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

    lv_obj_t *title = create_label(s_route_trace_sheet, alias, 0xF4F7FB);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, 250);
    lv_obj_set_pos(title, 8, 4);

    create_button(s_route_trace_sheet, "Ping", 238, 0, 70, 40, route_trace_probe_event_cb, NULL);
    create_button(s_route_trace_sheet, "Close", 316, 0, 76, 40, close_route_trace_event_cb, NULL);

    lv_obj_t *meta = create_label(s_route_trace_sheet, "", 0x8EA0AE);
    label_set_fmt(meta, "Trace %.16s  rows %u/%u",
                  contact->fingerprint, (unsigned)route_count,
                  (unsigned)D1L_ROUTE_STORE_CAPACITY);
    lv_obj_set_pos(meta, 8, 42);

    lv_obj_t *contact_path = create_label(s_route_trace_sheet, "", 0x8EA0AE);
    label_set_fmt(contact_path, "%s  contact path %s hops %u",
                  contact->public_key_hex[0] ? "key retained" : "no key",
                  contact->out_path_valid ? "known" : "unknown",
                  contact->out_path_valid ? contact->path_hops : 0);
    lv_label_set_long_mode(contact_path, LV_LABEL_LONG_DOT);
    lv_obj_set_width(contact_path, 392);
    lv_obj_set_pos(contact_path, 8, 74);

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
    lv_obj_set_width(best_label, 392);
    lv_obj_set_pos(best_label, 8, 104);

    lv_obj_t *list = create_object(s_route_trace_sheet, "route trace list");
    if (!list) {
        return;
    }
    lv_obj_set_size(list, 400, 126);
    lv_obj_set_pos(list, 8, 134);
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
        lv_obj_set_size(row, 360, 50);
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
        lv_obj_set_width(line1, 330);
        lv_obj_set_pos(line1, 10, 6);

        const int snr_abs = entry->last_snr_tenths < 0 ? -entry->last_snr_tenths : entry->last_snr_tenths;
        lv_obj_t *line2 = create_label(row, "", 0x8EA0AE);
        label_set_fmt(line2, "rssi %d snr %s%d.%d hops %u conf %u seen %lu",
                      entry->last_rssi_dbm,
                      entry->last_snr_tenths < 0 ? "-" : "", snr_abs / 10, snr_abs % 10,
                      entry->path_hops, entry->confidence, (unsigned long)entry->seen_count);
        lv_label_set_long_mode(line2, LV_LABEL_LONG_DOT);
        lv_obj_set_width(line2, 330);
        lv_obj_set_pos(line2, 10, 28);
        y += 58;
    }

    lv_obj_t *note = create_label(s_route_trace_sheet,
                                  "DM-only trace probe; no Public RF",
                                  0x8EA0AE);
    lv_label_set_long_mode(note, LV_LABEL_LONG_DOT);
    lv_obj_set_width(note, 392);
    lv_obj_set_pos(note, 8, 270);
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
    hide_contact_detail_sheet();
    hide_contact_export_sheet();
    hide_route_detail_sheet();
    hide_packet_detail_sheet();
    hide_packet_search_sheet();
    hide_mesh_roles_sheet();
    render_route_trace_sheet();
    if (s_route_trace_sheet) {
        d1l_ui_modal_show(s_route_trace_sheet);
    }
}

static void render_contact_detail_sheet(void)
{
    if (!s_contact_detail_sheet) {
        return;
    }
    lv_obj_clean(s_contact_detail_sheet);

    const d1l_contact_entry_t *entry = &s_contact_detail_contact;
    lv_obj_t *title = create_label(s_contact_detail_sheet,
                                   entry->alias[0] ? entry->alias : entry->fingerprint,
                                   0xF4F7FB);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, 250);
    lv_obj_set_pos(title, 8, 4);

    lv_obj_t *flags = create_label(s_contact_detail_sheet, "", 0x8EA0AE);
    label_set_fmt(flags, "%s  %s  %s", entry->type[0] ? entry->type : "node",
                  entry->favorite ? "favorite" : "normal",
                  entry->muted ? "muted" : "audible");
    lv_obj_set_pos(flags, 8, 42);

    lv_obj_t *fingerprint = create_label(s_contact_detail_sheet, "", 0xE5EDF5);
    label_set_fmt(fingerprint, "fp %.16s", entry->fingerprint);
    lv_obj_set_pos(fingerprint, 8, 78);

    lv_obj_t *key = create_label(s_contact_detail_sheet, "", 0x8EA0AE);
    label_set_fmt(key, "%s  route %s  hops %u",
                  entry->public_key_hex[0] ? "public key retained" : "no public key",
                  entry->out_path_valid ? "direct" : "flood",
                  entry->out_path_valid ? entry->path_hops : 0);
    lv_label_set_long_mode(key, LV_LABEL_LONG_DOT);
    lv_obj_set_width(key, 392);
    lv_obj_set_pos(key, 8, 112);

    const int snr_abs = entry->last_snr_tenths < 0 ? -entry->last_snr_tenths : entry->last_snr_tenths;
    lv_obj_t *signal = create_label(s_contact_detail_sheet, "", 0x8EA0AE);
    label_set_fmt(signal, "rssi %d  snr %s%d.%d  heard %.18s",
                  entry->last_rssi_dbm,
                  entry->last_snr_tenths < 0 ? "-" : "", snr_abs / 10, snr_abs % 10,
                  entry->heard_name[0] ? entry->heard_name : "-");
    lv_label_set_long_mode(signal, LV_LABEL_LONG_DOT);
    lv_obj_set_width(signal, 392);
    lv_obj_set_pos(signal, 8, 146);

    create_button(s_contact_detail_sheet, "DM", 8, 198, 42, 42, contact_detail_dm_event_cb, NULL);
    create_button(s_contact_detail_sheet, "Trace", 58, 198, 62, 42, open_route_trace_event_cb, NULL);
    create_button(s_contact_detail_sheet, "Edit", 128, 198, 54, 42, open_contact_edit_event_cb, NULL);
    create_button(s_contact_detail_sheet, "Export", 190, 198, 68, 42,
                  contact_detail_export_event_cb, NULL);
    create_button(s_contact_detail_sheet, entry->favorite ? "Unfav" : "Fav", 266, 198, 54, 42,
                  open_contact_detail_event_cb, (void *)s_contact_action_favorite);
    create_button(s_contact_detail_sheet, entry->muted ? "Unmute" : "Mute", 328, 198, 60, 42,
                  open_contact_detail_event_cb, (void *)s_contact_action_mute);
    create_button(s_contact_detail_sheet, "Close", 8, 244, 78, 32, close_contact_detail_event_cb, NULL);
}

static void update_contact_detail_flags(bool favorite, bool muted)
{
    d1l_contact_entry_t updated = {0};
    esp_err_t ret = d1l_app_model_set_contact_flags(s_contact_detail_contact.fingerprint,
                                                    favorite, muted, &updated);
    if (ret == ESP_OK) {
        s_contact_detail_contact = updated;
        request_content_refresh();
        render_contact_detail_sheet();
        if (s_contact_detail_sheet) {
            d1l_ui_modal_show(s_contact_detail_sheet);
        }
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
    hide_node_detail_sheet();
    render_contact_detail_sheet();
    if (s_contact_detail_sheet) {
        d1l_ui_modal_show(s_contact_detail_sheet);
    }
}

static void close_node_detail_event_cb(lv_event_t *event)
{
    (void)event;
    hide_node_detail_sheet();
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

    lv_obj_t *heard = create_label(s_node_detail_sheet, "", 0x8EA0AE);
    label_set_fmt(heard, "Last heard %lums  first %lums  count %lu",
                  (unsigned long)entry->last_heard_ms,
                  (unsigned long)entry->first_heard_ms,
                  (unsigned long)entry->heard_count);
    lv_label_set_long_mode(heard, LV_LABEL_LONG_DOT);
    lv_obj_set_width(heard, 392);
    lv_obj_set_pos(heard, 8, 252);
}

static void open_node_detail_event_cb(lv_event_t *event)
{
    const d1l_node_view_t *view = (const d1l_node_view_t *)lv_event_get_user_data(event);
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
    render_node_detail_sheet();
    if (s_node_detail_sheet) {
        d1l_ui_modal_show(s_node_detail_sheet);
    }
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
        d1l_ui_modal_show(s_route_detail_sheet);
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

static void render_message_detail_sheet(void)
{
    if (!s_message_detail_sheet) {
        return;
    }
    lv_obj_clean(s_message_detail_sheet);

    const d1l_message_entry_t *entry = &s_message_detail_message;
    lv_obj_t *title = create_label(s_message_detail_sheet, "Message Detail", 0xF4F7FB);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, 158);
    lv_obj_set_pos(title, 8, 4);

    create_button(s_message_detail_sheet,
                  s_message_detail_advanced ? "Normal" : "Advanced",
                  174, 0, 90, 40, message_detail_mode_event_cb, NULL);
    if (entry->direction[0] != 't') {
        create_button(s_message_detail_sheet, "Reply", 274, 0, 62, 40, reply_message_detail_event_cb, NULL);
    }
    create_button(s_message_detail_sheet, "Close", 344, 0, 66, 40, close_message_detail_event_cb, NULL);

    lv_obj_t *sender_title = create_label(s_message_detail_sheet, "Sender", 0x5EEAD4);
    lv_obj_set_pos(sender_title, 8, 50);
    lv_obj_t *sender = create_label(s_message_detail_sheet, "", 0xE5EDF5);
    label_set_fmt(sender, "%s  %s",
                  entry->author[0] ? entry->author : "unknown",
                  message_delivery_label(entry));
    lv_label_set_long_mode(sender, LV_LABEL_LONG_DOT);
    lv_obj_set_width(sender, 392);
    lv_obj_set_pos(sender, 8, 72);

    lv_obj_t *message_title = create_label(s_message_detail_sheet, "Message", 0x5EEAD4);
    lv_obj_set_pos(message_title, 8, 106);
    lv_obj_t *message = create_label(s_message_detail_sheet, entry->text[0] ? entry->text : "-", 0xF4F7FB);
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(message, 392);
    lv_obj_set_pos(message, 8, 128);

    const int snr_abs = entry->snr_tenths < 0 ? -entry->snr_tenths : entry->snr_tenths;
    lv_obj_t *signal = create_label(s_message_detail_sheet, "", 0x8EA0AE);
    label_set_fmt(signal, "Signal rssi %d  snr %s%d.%d",
                  entry->rssi_dbm,
                  entry->snr_tenths < 0 ? "-" : "", snr_abs / 10, snr_abs % 10);
    lv_label_set_long_mode(signal, LV_LABEL_LONG_DOT);
    lv_obj_set_width(signal, 392);
    lv_obj_set_pos(signal, 8, 190);

    lv_obj_t *path = create_label(s_message_detail_sheet, "", 0x8EA0AE);
    label_set_fmt(path, "Path %u hop%s",
                  entry->path_hops, entry->path_hops == 1 ? "" : "s");
    lv_label_set_long_mode(path, LV_LABEL_LONG_DOT);
    lv_obj_set_width(path, 392);
    lv_obj_set_pos(path, 8, 224);

    if (!s_message_detail_advanced) {
        lv_obj_t *hint = create_label(s_message_detail_sheet,
                                      "Advanced shows route details",
                                      0x8EA0AE);
        lv_label_set_long_mode(hint, LV_LABEL_LONG_DOT);
        lv_obj_set_width(hint, 392);
        lv_obj_set_pos(hint, 8, 258);
        return;
    }

    lv_obj_t *advanced = create_label(s_message_detail_sheet, "Advanced", 0xFBBF24);
    lv_obj_set_pos(advanced, 8, 258);
    lv_obj_t *seq = create_label(s_message_detail_sheet, "", 0x8EA0AE);
    label_set_fmt(seq, "seq %lu  uptime %lums  direction %s",
                  (unsigned long)entry->seq,
                  (unsigned long)entry->uptime_ms,
                  entry->direction[0] ? entry->direction : "?");
    lv_label_set_long_mode(seq, LV_LABEL_LONG_DOT);
    lv_obj_set_width(seq, 392);
    lv_obj_set_pos(seq, 8, 282);
    lv_obj_t *hash = create_label(s_message_detail_sheet, "", 0x8EA0AE);
    label_set_fmt(hash, "path hash %u byte  delivered %s",
                  entry->path_hash_bytes, entry->delivered ? "yes" : "no");
    lv_label_set_long_mode(hash, LV_LABEL_LONG_DOT);
    lv_obj_set_width(hash, 392);
    lv_obj_set_pos(hash, 8, 304);
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
    hide_map_tiles_sheet();
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
        d1l_ui_modal_show(s_message_detail_sheet);
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
        d1l_ui_modal_show(s_packet_detail_sheet);
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
    d1l_ui_modal_show(s_packet_search_sheet);
    set_dock_hidden(true);
}

static void close_mesh_roles_event_cb(lv_event_t *event)
{
    (void)event;
    hide_mesh_roles_sheet();
}

static void render_room_server_role_row(lv_obj_t *parent, int y, const d1l_mesh_room_server_t *entry)
{
    char snr[16];
    lv_obj_t *row = create_panel(parent, 0, y, 424, 44);
    lv_obj_set_style_pad_all(row, 8, 0);
    lv_obj_t *name = create_label(row, entry->name[0] ? entry->name : entry->fingerprint, 0xA7F3D0);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(name, 180);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *role = create_label(row, entry->type[0] ? entry->type : "room", 0x8EA0AE);
    lv_obj_align(role, LV_ALIGN_TOP_RIGHT, 0, 0);
    format_snr_tenths(snr, sizeof(snr), entry->snr_tenths);
    lv_obj_t *meta = create_label(row, "", 0xE5EDF5);
    label_set_fmt(meta, "%.8s  rssi %d  snr %s  hops %u  heard %lu",
                  entry->fingerprint, entry->rssi_dbm, snr, entry->path_hops,
                  (unsigned long)entry->heard_count);
    lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
    lv_obj_set_width(meta, 392);
    lv_obj_align(meta, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static void render_repeater_role_row(lv_obj_t *parent, int y,
                                     const d1l_mesh_repeater_candidate_t *entry)
{
    lv_obj_t *row = create_panel(parent, 0, y, 424, 44);
    lv_obj_set_style_pad_all(row, 8, 0);
    lv_obj_t *label = create_label(row, entry->label[0] ? entry->label : entry->target, 0xFBBF24);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, 180);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *source = create_label(row, entry->source[0] ? entry->source : "path", 0x8EA0AE);
    lv_obj_align(source, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_t *meta = create_label(row, "", 0xE5EDF5);
    label_set_fmt(meta, "%s  %s  hops %u  conf %u  rssi %d",
                  entry->kind[0] ? entry->kind : "mesh",
                  entry->route[0] ? entry->route : "unknown",
                  entry->path_hops, entry->confidence, entry->rssi_dbm);
    lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
    lv_obj_set_width(meta, 392);
    lv_obj_align(meta, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static void render_mesh_roles_sheet(void)
{
    if (!s_mesh_roles_sheet) {
        return;
    }
    d1l_app_model_snapshot(&s_snapshot);
    update_chrome(&s_snapshot);
    lv_obj_clean(s_mesh_roles_sheet);

    lv_obj_t *title = create_label(s_mesh_roles_sheet, "Mesh Roles", 0xF4F7FB);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, 210);
    lv_obj_set_pos(title, 8, 4);
    create_button(s_mesh_roles_sheet, "Close", 316, 0, 76, 40, close_mesh_roles_event_cb, NULL);

    lv_obj_t *meta = create_label(s_mesh_roles_sheet, "", 0x8EA0AE);
    label_set_fmt(meta, "rooms %lu  repeaters %lu  samples %lu",
                  (unsigned long)s_snapshot.signal_summary.room_server_count,
                  (unsigned long)s_snapshot.signal_summary.repeater_candidate_count,
                  (unsigned long)s_snapshot.signal_summary.sample_count);
    lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
    lv_obj_set_width(meta, 392);
    lv_obj_set_pos(meta, 8, 44);

    int y = 78;
    lv_obj_t *rooms = create_label(s_mesh_roles_sheet, "Room Servers", 0x8EA0AE);
    lv_obj_set_pos(rooms, 8, y);
    y += 24;
    for (size_t i = 0; i < s_snapshot.recent_room_count; ++i) {
        render_room_server_role_row(s_mesh_roles_sheet, y, &s_snapshot.recent_rooms[i]);
        y += 50;
    }
    if (s_snapshot.recent_room_count == 0) {
        lv_obj_t *empty = create_label(s_mesh_roles_sheet, "No room servers heard", 0x8EA0AE);
        lv_obj_set_pos(empty, 8, y);
        y += 28;
    }

    y += 8;
    lv_obj_t *repeaters = create_label(s_mesh_roles_sheet, "Repeater Candidates", 0x8EA0AE);
    lv_obj_set_pos(repeaters, 8, y);
    y += 24;
    for (size_t i = 0; i < s_snapshot.recent_repeater_count; ++i) {
        render_repeater_role_row(s_mesh_roles_sheet, y, &s_snapshot.recent_repeaters[i]);
        y += 50;
    }
    if (s_snapshot.recent_repeater_count == 0) {
        lv_obj_t *empty = create_label(s_mesh_roles_sheet, "No path-hop candidates", 0x8EA0AE);
        lv_obj_set_pos(empty, 8, y);
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
    render_mesh_roles_sheet();
    if (s_mesh_roles_sheet) {
        d1l_ui_modal_show(s_mesh_roles_sheet);
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
        d1l_ui_modal_show(s_public_history_sheet);
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
        hide_public_search_sheet();
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
    d1l_ui_modal_show(s_public_search_sheet);
    set_dock_hidden(true);
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
        d1l_ui_modal_show(s_dm_thread_sheet);
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

static void read_dm_thread_event_cb(lv_event_t *event)
{
    (void)event;
    esp_err_t ret = d1l_app_model_mark_dm_thread_read(s_dm_thread_fingerprint);
    show_toast("Thread read", ret);
    if (ret == ESP_OK) {
        request_content_refresh();
        render_dm_thread_sheet();
        if (s_dm_thread_sheet) {
            d1l_ui_modal_show(s_dm_thread_sheet);
        }
    }
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
    lv_obj_t *title = create_label(s_dm_thread_sheet, alias, 0xF4F7FB);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, 178);
    lv_obj_set_pos(title, 8, 4);

    create_button(s_dm_thread_sheet, "Reply", 198, 0, 64, 40, reply_dm_thread_event_cb, NULL);
    create_button(s_dm_thread_sheet, "Read", 270, 0, 56, 40, read_dm_thread_event_cb, NULL);
    create_button(s_dm_thread_sheet, "Close", 334, 0, 76, 40, close_dm_thread_event_cb, NULL);

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

    lv_obj_t *meta = create_label(s_dm_thread_sheet, "", 0x8EA0AE);
    label_set_fmt(meta, "%.16s  showing %u/%u", s_dm_thread_fingerprint,
                  (unsigned)thread_count, (unsigned)total_matches);
    lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
    lv_obj_set_width(meta, 392);
    lv_obj_set_pos(meta, 8, 42);

    lv_obj_t *list = create_object(s_dm_thread_sheet, "dm thread list");
    if (!list) {
        return;
    }
    lv_obj_set_size(list, 424, 176);
    lv_obj_set_pos(list, 0, 68);
    lv_obj_set_style_radius(list, 6, 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    int y = 0;
    for (size_t i = 0; i < thread_count; ++i) {
        const d1l_dm_entry_t *entry = &s_dm_thread_entries[i];
        const bool unread = s_dm_thread_unread[i];
        lv_obj_t *row = create_panel(list, 0, y, 404, 48);
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
        y += 54;
    }
    if (thread_count < 3) {
        const char *notes[] = {
            "Thread keeps delivery, ACK, and PATH state together.",
            "Older retained rows appear here after RF traffic or SD restore.",
            "Use Reply for a direct MeshCore DM when contact keys exist.",
        };
        for (size_t i = 0; i < sizeof(notes) / sizeof(notes[0]); ++i) {
            lv_obj_t *note = create_panel(list, 0, y, 404, 48);
            lv_obj_set_style_pad_all(note, 8, 0);
            lv_obj_t *text = create_label(note, notes[i], 0x8EA0AE);
            lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(text, 372);
            lv_obj_align(text, LV_ALIGN_LEFT_MID, 0, 0);
            y += 54;
        }
    }
    if (thread_count > 0) {
        lv_obj_scroll_to_y(list, LV_COORD_MAX, LV_ANIM_OFF);
    }
    if (thread_count < total_matches && s_dm_thread_limit < D1L_DM_STORE_CAPACITY) {
        create_button(s_dm_thread_sheet, "Load Older", 8, 260, 126, 36,
                      dm_thread_load_older_event_cb, NULL);
    }
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
        d1l_ui_modal_show(s_dm_thread_sheet);
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

static void render_messages(const d1l_app_snapshot_t *snapshot)
{
    lv_obj_t *header = create_panel(s_content, 18, 16, 424, 108);
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

    create_button(s_content, "Public", 18, 136, 96, 40, open_messages_public_event_cb, NULL);
    create_button(s_content, "DMs", 122, 136, 80, 40, open_messages_dm_event_cb, NULL);

    lv_obj_t *mode_label = create_label(s_content,
                                        s_messages_show_dms ? "DM Conversations" : "Public Channel",
                                        s_messages_show_dms ? 0xA7F3D0 : 0x5EEAD4);
    lv_obj_set_pos(mode_label, 26, 190);

    int y = 218;
    if (s_messages_show_dms) {
        for (size_t i = 0; i < snapshot->recent_dm_count; ++i) {
            render_dm_row(s_content, y, &snapshot->recent_dms[i], snapshot->recent_dm_unread[i]);
            y += 80;
        }
        if (snapshot->recent_dm_count == 0) {
            lv_obj_t *empty = create_label(s_content, "No direct messages", 0x8EA0AE);
            lv_obj_set_pos(empty, 26, y);
        }
    } else {
        for (size_t i = 0; i < snapshot->recent_message_count; ++i) {
            render_message_row(s_content, y, &snapshot->recent_messages[i]);
            y += 80;
        }
        if (snapshot->recent_message_count == 0) {
            lv_obj_t *empty = create_label(s_content, "No Public messages", 0x8EA0AE);
            lv_obj_set_pos(empty, 26, y);
        }
    }
}

static void render_nodes(const d1l_app_snapshot_t *snapshot)
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
    render_metric_card(s_content, 18, 16, "Heard Nodes", value, detail, 0x5EEAD4);
    snprintf(value, sizeof(value), "%u", (unsigned)snapshot->contact_count);
    snprintf(detail, sizeof(detail), "contacts  writes %lu",
             (unsigned long)snapshot->contact_total_written);
    render_metric_card(s_content, 238, 16, "Contacts", value, detail, 0xA7F3D0);

    int y = 136;
    if (snapshot->recent_contact_count > 0) {
        for (size_t i = 0; i < snapshot->recent_contact_count && y <= 190; ++i) {
            render_contact_row(s_content, y, &snapshot->recent_contacts[i]);
            y += 54;
        }
        lv_obj_t *heard = create_label(s_content, "Heard", 0x8EA0AE);
        lv_obj_set_pos(heard, 26, y + 4);
        y += 28;
    }
    lv_obj_t *all_heard = create_label(s_content, "All Heard", 0x8EA0AE);
    lv_obj_set_pos(all_heard, 26, y + 4);
    y += 28;
    for (size_t i = 0; i < node_rows; ++i) {
        render_node_row(s_content, y, &s_node_rows[i]);
        y += 62;
    }
    if (node_rows == 0) {
        lv_obj_t *empty = create_label(s_content, "No heard nodes yet", 0x8EA0AE);
        lv_obj_align(empty, LV_ALIGN_TOP_MID, 0, 154);
        const char *notes[] = {
            "Listening for signed adverts from nearby MeshCore nodes.",
            "Contacts appear separately when a retained public key exists.",
            "Signal, route, and repeater summaries update from RX packets.",
            "Scroll proof keeps this empty-state layout validated too.",
        };
        int note_y = 206;
        for (size_t i = 0; i < sizeof(notes) / sizeof(notes[0]); ++i) {
            lv_obj_t *note = create_panel(s_content, 18, note_y, 424, 52);
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
    s_map_location_prompt_seen = true;
    hide_map_location_sheet();
}

static void map_location_textarea_event_cb(lv_event_t *event)
{
    if (!s_map_location_keyboard) {
        return;
    }
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
        lv_obj_t *textarea = lv_event_get_target(event);
        if (textarea == s_map_lat_textarea || textarea == s_map_lon_textarea) {
            lv_keyboard_set_textarea(s_map_location_keyboard, textarea);
        }
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
    s_map_location_prompt_seen = true;
    if (ret == ESP_OK) {
        hide_map_location_sheet();
        hide_map_tiles_sheet();
        request_content_refresh();
        show_toast_text("D1L pin saved", true);
    } else {
        show_toast_text("Location rejected", false);
    }
}

static void map_location_clear_event_cb(lv_event_t *event)
{
    (void)event;
    esp_err_t ret = d1l_app_model_clear_map_location();
    s_map_location_prompt_seen = true;
    if (ret == ESP_OK) {
        map_location_from_snapshot(NULL);
        hide_map_location_sheet();
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
    lv_obj_clean(s_map_location_sheet);
    s_map_lat_textarea = NULL;
    s_map_lon_textarea = NULL;
    s_map_location_keyboard = NULL;

    char lat[20];
    char lon[20];
    format_coord_e7(lat, sizeof(lat), s_map_location_lat_e7);
    format_coord_e7(lon, sizeof(lon), s_map_location_lon_e7);

    lv_obj_t *title = create_label(s_map_location_sheet, "Set D1L Location", 0xF4F7FB);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, 214);
    lv_obj_set_pos(title, 16, 10);
    create_button(s_map_location_sheet, "Save", 238, 10, 62, 40,
                  map_location_save_event_cb, NULL);
    create_button(s_map_location_sheet, "Clear", 308, 10, 70, 40,
                  map_location_clear_event_cb, NULL);
    create_button(s_map_location_sheet, "Skip", 386, 10, 70, 40,
                  close_map_location_sheet_event_cb, NULL);

    lv_obj_t *needed = create_label(s_map_location_sheet,
                                    "Map needs your D1L location", 0x5EEAD4);
    lv_obj_set_pos(needed, 16, 58);
    lv_obj_t *hint = create_label(s_map_location_sheet,
                                  "Enter decimal degrees", 0x8EA0AE);
    lv_obj_set_pos(hint, 16, 80);

    lv_obj_t *lat_label = create_label(s_map_location_sheet, "Latitude", 0xF4F7FB);
    lv_obj_set_pos(lat_label, 16, 108);
    s_map_lat_textarea = create_textarea(s_map_location_sheet, "map latitude textarea");
    if (!s_map_lat_textarea) {
        return;
    }
    lv_obj_set_size(s_map_lat_textarea, 448, 44);
    lv_obj_set_pos(s_map_lat_textarea, 16, 130);
    lv_textarea_set_one_line(s_map_lat_textarea, true);
    lv_textarea_set_max_length(s_map_lat_textarea, 14);
    lv_textarea_set_placeholder_text(s_map_lat_textarea, "43.6532000");
    lv_textarea_set_text(s_map_lat_textarea, lat);
    lv_obj_set_style_radius(s_map_lat_textarea, 8, 0);
    lv_obj_set_style_bg_color(s_map_lat_textarea, lv_color_hex(0x071018), 0);
    lv_obj_set_style_border_color(s_map_lat_textarea, lv_color_hex(0x263241), 0);
    lv_obj_set_style_text_color(s_map_lat_textarea, lv_color_hex(0xF4F7FB), 0);
    lv_obj_set_style_text_color(s_map_lat_textarea, lv_color_hex(0x8EA0AE),
                                LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_add_event_cb(s_map_lat_textarea, map_location_textarea_event_cb,
                        LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_map_lat_textarea, map_location_textarea_event_cb,
                        LV_EVENT_CLICKED, NULL);

    lv_obj_t *lon_label = create_label(s_map_location_sheet, "Longitude", 0xF4F7FB);
    lv_obj_set_pos(lon_label, 16, 184);
    s_map_lon_textarea = create_textarea(s_map_location_sheet, "map longitude textarea");
    if (!s_map_lon_textarea) {
        return;
    }
    lv_obj_set_size(s_map_lon_textarea, 448, 44);
    lv_obj_set_pos(s_map_lon_textarea, 16, 206);
    lv_textarea_set_one_line(s_map_lon_textarea, true);
    lv_textarea_set_max_length(s_map_lon_textarea, 15);
    lv_textarea_set_placeholder_text(s_map_lon_textarea, "-79.3832000");
    lv_textarea_set_text(s_map_lon_textarea, lon);
    lv_obj_set_style_radius(s_map_lon_textarea, 8, 0);
    lv_obj_set_style_bg_color(s_map_lon_textarea, lv_color_hex(0x071018), 0);
    lv_obj_set_style_border_color(s_map_lon_textarea, lv_color_hex(0x263241), 0);
    lv_obj_set_style_text_color(s_map_lon_textarea, lv_color_hex(0xF4F7FB), 0);
    lv_obj_set_style_text_color(s_map_lon_textarea, lv_color_hex(0x8EA0AE),
                                LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_add_event_cb(s_map_lon_textarea, map_location_textarea_event_cb,
                        LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_map_lon_textarea, map_location_textarea_event_cb,
                        LV_EVENT_CLICKED, NULL);

    s_map_location_keyboard = create_keyboard(s_map_location_sheet,
                                              "map location keyboard");
    if (!s_map_location_keyboard) {
        return;
    }
    lv_obj_set_size(s_map_location_keyboard, 448, 164);
    lv_obj_set_align(s_map_location_keyboard, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(s_map_location_keyboard, 16, 248);
    lv_keyboard_set_textarea(s_map_location_keyboard, s_map_lat_textarea);
    lv_obj_add_event_cb(s_map_location_keyboard, map_location_keyboard_event_cb,
                        LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_map_location_keyboard, map_location_keyboard_event_cb,
                        LV_EVENT_CANCEL, NULL);
}

static void open_map_location_sheet_event_cb(lv_event_t *event)
{
    (void)event;
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
    hide_display_sheet();
    hide_diagnostics_sheet();
    hide_map_location_sheet();
    hide_map_tiles_sheet();
    hide_contact_detail_sheet();
    hide_contact_export_sheet();
    hide_route_detail_sheet();
    hide_route_trace_sheet();
    hide_packet_detail_sheet();
    hide_packet_search_sheet();
    hide_mesh_roles_sheet();
    render_map_location_sheet();
    if (s_map_location_sheet) {
        set_dock_hidden(true);
        d1l_ui_modal_show(s_map_location_sheet);
    }
}

static void close_map_tiles_sheet_event_cb(lv_event_t *event)
{
    (void)event;
    hide_map_tiles_sheet();
}

static void map_tiles_from_snapshot(const d1l_app_snapshot_t *snapshot)
{
    const uint8_t zoom = snapshot && snapshot->map_tile_zoom ?
        snapshot->map_tile_zoom : D1L_MAP_TILE_DEFAULT_ZOOM;
    s_map_tile_provider_zoom = zoom > D1L_MAP_TILE_ZOOM_MAX ?
        D1L_MAP_TILE_DEFAULT_ZOOM : zoom;
}

static void update_map_tiles_zoom_label(void)
{
    if (s_map_tiles_zoom_label) {
        label_set_fmt(s_map_tiles_zoom_label, "Zoom %u", (unsigned)s_map_tile_provider_zoom);
    }
}

static void map_tiles_textarea_event_cb(lv_event_t *event)
{
    if (!s_map_tiles_keyboard) {
        return;
    }
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
        lv_obj_t *target = lv_event_get_target(event);
        if (target == s_map_tiles_url_textarea ||
            target == s_map_tiles_attribution_textarea) {
            lv_keyboard_set_textarea(s_map_tiles_keyboard, target);
        }
    }
}

static void map_tiles_keyboard_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        if (s_map_tiles_keyboard) {
            lv_keyboard_set_textarea(s_map_tiles_keyboard, NULL);
        }
    }
}

static void map_tiles_zoom_event_cb(lv_event_t *event)
{
    const intptr_t delta = (intptr_t)lv_event_get_user_data(event);
    int next = (int)s_map_tile_provider_zoom + (int)delta;
    if (next < 1) {
        next = 1;
    } else if (next > (int)D1L_MAP_TILE_ZOOM_MAX) {
        next = (int)D1L_MAP_TILE_ZOOM_MAX;
    }
    s_map_tile_provider_zoom = (uint8_t)next;
    update_map_tiles_zoom_label();
}

static void map_tiles_save_event_cb(lv_event_t *event)
{
    (void)event;
    const char *url = s_map_tiles_url_textarea ?
        lv_textarea_get_text(s_map_tiles_url_textarea) : "";
    const char *attribution = s_map_tiles_attribution_textarea ?
        lv_textarea_get_text(s_map_tiles_attribution_textarea) : "";
    esp_err_t ret = d1l_app_model_save_map_tile_provider(url, attribution,
                                                         s_map_tile_provider_zoom);
    if (ret == ESP_OK) {
        d1l_app_model_snapshot(&s_snapshot);
        render_map_tiles_sheet();
        show_toast_text("Tile provider saved", true);
    } else {
        show_toast_text("Provider must be allowed HTTPS with attribution", false);
    }
}

static void map_tiles_clear_event_cb(lv_event_t *event)
{
    (void)event;
    esp_err_t ret = d1l_app_model_clear_map_tile_provider();
    if (ret == ESP_OK) {
        d1l_app_model_snapshot(&s_snapshot);
        map_tiles_from_snapshot(&s_snapshot);
        render_map_tiles_sheet();
        show_toast_text("Tile provider cleared", true);
    } else {
        show_toast_text("Provider clear failed", false);
    }
}

static void map_tiles_download_event_cb(lv_event_t *event)
{
    (void)event;
    const char *url = s_map_tiles_url_textarea ?
        lv_textarea_get_text(s_map_tiles_url_textarea) : "";
    const char *attribution = s_map_tiles_attribution_textarea ?
        lv_textarea_get_text(s_map_tiles_attribution_textarea) : "";
    esp_err_t ret = d1l_app_model_save_map_tile_provider(url, attribution,
                                                         s_map_tile_provider_zoom);
    if (ret != ESP_OK) {
        show_toast_text("Provider must be allowed HTTPS with attribution", false);
        return;
    }

    d1l_map_tile_download_result_t result = {0};
    ret = d1l_app_model_download_center_map_tile(&result);
    d1l_app_model_snapshot(&s_snapshot);
    render_map_tiles_sheet();
    if (ret == ESP_OK) {
        char msg[72];
        snprintf(msg, sizeof(msg), "Center tile saved %u bytes", (unsigned)result.bytes);
        show_toast_text(msg, true);
    } else {
        char msg[96];
        snprintf(msg, sizeof(msg), "Tile download needs setup: %s",
                 result.step[0] ? result.step : esp_err_to_name(ret));
        show_toast_text(msg, false);
    }
}

static void render_map_tiles_sheet(void)
{
    if (!s_map_tiles_sheet) {
        return;
    }
    lv_obj_clean(s_map_tiles_sheet);
    s_map_tiles_url_textarea = NULL;
    s_map_tiles_attribution_textarea = NULL;
    s_map_tiles_zoom_label = NULL;
    s_map_tiles_keyboard = NULL;

    lv_obj_t *title = create_label(s_map_tiles_sheet, "Map Tiles", 0xF4F7FB);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, 190);
    lv_obj_set_pos(title, 16, 10);
    create_button(s_map_tiles_sheet, "Close", 392, 10, 72, 40,
                  close_map_tiles_sheet_event_cb, NULL);

    lv_obj_t *status = create_label(s_map_tiles_sheet, "", 0x8EA0AE);
    label_set_fmt(status, "Cache %s  Wi-Fi %s  Provider %s",
                  s_snapshot.map_tile_cache_ready ? "ready" : "needs SD",
                  s_snapshot.wifi_connected ? "connected" : "needed",
                  s_snapshot.map_tile_provider_saved ? "saved" : "needed");
    label_set_dot_width(status, 448);
    lv_obj_set_pos(status, 16, 54);

    lv_obj_t *url_label = create_label(s_map_tiles_sheet, "Allowed provider template", 0x5EEAD4);
    lv_obj_set_pos(url_label, 16, 82);
    s_map_tiles_url_textarea = create_textarea(s_map_tiles_sheet, "map tile provider textarea");
    if (s_map_tiles_url_textarea) {
        lv_obj_set_size(s_map_tiles_url_textarea, 448, 40);
        lv_obj_set_pos(s_map_tiles_url_textarea, 16, 104);
        lv_textarea_set_one_line(s_map_tiles_url_textarea, true);
        lv_textarea_set_max_length(s_map_tiles_url_textarea, D1L_MAP_TILE_URL_TEMPLATE_MAX);
        lv_textarea_set_placeholder_text(s_map_tiles_url_textarea,
                                         "https://provider.example/{z}/{x}/{y}.png");
        lv_textarea_set_text(s_map_tiles_url_textarea, s_snapshot.map_tile_url_template);
        lv_obj_set_style_radius(s_map_tiles_url_textarea, 8, 0);
        lv_obj_set_style_bg_color(s_map_tiles_url_textarea, lv_color_hex(0x071018), 0);
        lv_obj_set_style_border_color(s_map_tiles_url_textarea, lv_color_hex(0x263241), 0);
        lv_obj_set_style_text_color(s_map_tiles_url_textarea, lv_color_hex(0xF4F7FB), 0);
        lv_obj_add_event_cb(s_map_tiles_url_textarea, map_tiles_textarea_event_cb,
                            LV_EVENT_FOCUSED, NULL);
        lv_obj_add_event_cb(s_map_tiles_url_textarea, map_tiles_textarea_event_cb,
                            LV_EVENT_CLICKED, NULL);
    }

    lv_obj_t *attrib_label = create_label(s_map_tiles_sheet, "Attribution", 0x5EEAD4);
    lv_obj_set_pos(attrib_label, 16, 150);
    s_map_tiles_attribution_textarea = create_textarea(s_map_tiles_sheet,
                                                       "map tile attribution textarea");
    if (s_map_tiles_attribution_textarea) {
        lv_obj_set_size(s_map_tiles_attribution_textarea, 448, 40);
        lv_obj_set_pos(s_map_tiles_attribution_textarea, 16, 172);
        lv_textarea_set_one_line(s_map_tiles_attribution_textarea, true);
        lv_textarea_set_max_length(s_map_tiles_attribution_textarea,
                                   D1L_MAP_TILE_ATTRIBUTION_MAX);
        lv_textarea_set_placeholder_text(s_map_tiles_attribution_textarea,
                                         "Provider attribution");
        lv_textarea_set_text(s_map_tiles_attribution_textarea,
                             s_snapshot.map_tile_attribution);
        lv_obj_set_style_radius(s_map_tiles_attribution_textarea, 8, 0);
        lv_obj_set_style_bg_color(s_map_tiles_attribution_textarea, lv_color_hex(0x071018), 0);
        lv_obj_set_style_border_color(s_map_tiles_attribution_textarea,
                                      lv_color_hex(0x263241), 0);
        lv_obj_set_style_text_color(s_map_tiles_attribution_textarea,
                                    lv_color_hex(0xF4F7FB), 0);
        lv_obj_add_event_cb(s_map_tiles_attribution_textarea,
                            map_tiles_textarea_event_cb, LV_EVENT_FOCUSED, NULL);
        lv_obj_add_event_cb(s_map_tiles_attribution_textarea,
                            map_tiles_textarea_event_cb, LV_EVENT_CLICKED, NULL);
    }

    s_map_tiles_zoom_label = create_label(s_map_tiles_sheet, "", 0xE5EDF5);
    lv_obj_set_pos(s_map_tiles_zoom_label, 16, 224);
    update_map_tiles_zoom_label();
    create_button(s_map_tiles_sheet, "Z-", 116, 216, 54, 36,
                  map_tiles_zoom_event_cb, (void *)(intptr_t)-1);
    create_button(s_map_tiles_sheet, "Z+", 178, 216, 54, 36,
                  map_tiles_zoom_event_cb, (void *)(intptr_t)1);
    create_button(s_map_tiles_sheet, "Save", 244, 216, 62, 36,
                  map_tiles_save_event_cb, NULL);
    create_button(s_map_tiles_sheet, "Clear", 314, 216, 66, 36,
                  map_tiles_clear_event_cb, NULL);
    const bool tile_download_supported = s_snapshot.map_tile_download_supported;
    if (tile_download_supported) {
        create_button(s_map_tiles_sheet, "Download", 16, 262, 112, 38,
                      map_tiles_download_event_cb, NULL);
    } else {
        lv_obj_t *unavailable = create_label(s_map_tiles_sheet, "Live download unavailable",
                                             0xFBBF24);
        lv_label_set_long_mode(unavailable, LV_LABEL_LONG_DOT);
        lv_obj_set_width(unavailable, 190);
        lv_obj_set_pos(unavailable, 16, 270);
    }

    lv_obj_t *download = create_label(s_map_tiles_sheet,
                                      tile_download_supported ?
                                      "Downloads one center tile for your saved D1L location." :
                                      "Tile render pending; use serial canaries for SD cache proof.",
                                      0xE5EDF5);
    lv_label_set_long_mode(download, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(download, tile_download_supported ? 320 : 238);
    lv_obj_set_pos(download, tile_download_supported ? 142 : 218, 260);
    lv_obj_t *policy = create_label(s_map_tiles_sheet,
                                    "No public OSM bulk tile servers. Visible attribution is required.",
                                    0xFBBF24);
    lv_label_set_long_mode(policy, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(policy, 448);
    lv_obj_set_pos(policy, 16, 304);

    s_map_tiles_keyboard = create_keyboard(s_map_tiles_sheet, "map tile keyboard");
    if (s_map_tiles_keyboard) {
        lv_obj_set_size(s_map_tiles_keyboard, 448, 82);
        lv_obj_set_align(s_map_tiles_keyboard, LV_ALIGN_TOP_LEFT);
        lv_obj_set_pos(s_map_tiles_keyboard, 16, 330);
        if (s_map_tiles_url_textarea) {
            lv_keyboard_set_textarea(s_map_tiles_keyboard, s_map_tiles_url_textarea);
        }
        lv_obj_add_event_cb(s_map_tiles_keyboard, map_tiles_keyboard_event_cb,
                            LV_EVENT_READY, NULL);
        lv_obj_add_event_cb(s_map_tiles_keyboard, map_tiles_keyboard_event_cb,
                            LV_EVENT_CANCEL, NULL);
    }
}

static void open_map_tiles_sheet_event_cb(lv_event_t *event)
{
    (void)event;
    d1l_app_model_snapshot(&s_snapshot);
    map_tiles_from_snapshot(&s_snapshot);
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
    render_map_tiles_sheet();
    if (s_map_tiles_sheet) {
        set_dock_hidden(true);
        d1l_ui_modal_show(s_map_tiles_sheet);
    }
}

static void render_map(const d1l_app_snapshot_t *snapshot)
{
    char value[32];
    char detail[96];
    char lat[20];
    char lon[20];
    lv_obj_t *title = create_label(s_content, "Map", 0xF4F7FB);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(title, 18, 8);
    create_button(s_content, "Tiles", 238, 8, 72, 40,
                  open_map_tiles_sheet_event_cb, NULL);
    create_button(s_content, snapshot->map_location_set ? "Move Pin" : "Set Pin",
                  318, 8, 124, 40, open_map_location_sheet_event_cb, NULL);

    snprintf(value, sizeof(value), "%s", snapshot->map_tile_cache_ready ? "SD Ready" : "Offline");
    snprintf(detail, sizeof(detail), "%s",
             snapshot->map_tile_backend ? snapshot->map_tile_backend : "unavailable");
    render_metric_card(s_content, 18, 48, "Tile Cache", value, detail,
                       snapshot->map_tile_cache_ready ? 0x5EEAD4 : 0xFBBF24);

    snprintf(value, sizeof(value), "%s",
             snapshot->map_tile_download_supported ? "Ready" :
             snapshot->map_tile_render_supported ? "Setup" : "Unavailable");
    snprintf(detail, sizeof(detail), "%s",
             snapshot->map_tile_download_state ? snapshot->map_tile_download_state :
             "provider_required");
    render_metric_card(s_content, 238, 48, "Downloads", value, detail, 0x93C5FD);

    lv_obj_t *cache = create_panel(s_content, 18, 168, 424, 82);
    create_label(cache, snapshot->map_tile_cache_ready ? "Offline Cache" : "No Offline Tiles", 0xF4F7FB);
    lv_obj_t *policy = create_label(cache, "", 0x8EA0AE);
    if (snapshot->map_tile_cache_ready) {
        label_set_fmt(policy, "policy %s",
                      snapshot->map_tile_cache_policy ? snapshot->map_tile_cache_policy :
                      "sd_offline_cache_when_ready");
    } else {
        label_set_fmt(policy, "Connect Wi-Fi and download allowed tiles for your area.");
    }
    lv_label_set_long_mode(policy, LV_LABEL_LONG_DOT);
    lv_obj_set_width(policy, 390);
    lv_obj_set_pos(policy, 0, 28);
    lv_obj_t *path = create_label(cache, "", 0x8EA0AE);
    if (snapshot->map_tile_cache_ready) {
        label_set_fmt(path, "path %s",
                      snapshot->map_tile_cache_path_template ?
                      snapshot->map_tile_cache_path_template : "map/tiles/z{z}/x{x}/y{y}.tile");
    } else {
        label_set_fmt(path, "Allowed provider and visible attribution required.");
    }
    lv_label_set_long_mode(path, LV_LABEL_LONG_DOT);
    lv_obj_set_width(path, 390);
    lv_obj_set_pos(path, 0, 50);

    lv_obj_t *center = create_panel(s_content, 18, 266, 424, 94);
    create_label(center, "Center", 0xF4F7FB);
    lv_obj_t *routes_label = create_label(center, "Routes", 0x8EA0AE);
    lv_obj_set_pos(routes_label, 320, 0);
    lv_obj_t *location = create_label(center, "", 0x8EA0AE);
    if (snapshot->map_location_set) {
        format_coord_e7(lat, sizeof(lat), snapshot->map_lat_e7);
        format_coord_e7(lon, sizeof(lon), snapshot->map_lon_e7);
        label_set_fmt(location, "Manual Location  %s, %s", lat, lon);
    } else {
        label_set_fmt(location, "Manual Location unset");
    }
    lv_label_set_long_mode(location, LV_LABEL_LONG_DOT);
    lv_obj_set_width(location, 390);
    lv_obj_set_pos(location, 0, 28);
    lv_obj_t *counts = create_label(center, "", 0x8EA0AE);
    label_set_fmt(counts, "Routes %u  nodes %u  latest RSSI %d",
                  (unsigned)snapshot->route_count,
                  (unsigned)snapshot->node_count,
                  snapshot->signal_summary.latest_rssi_dbm);
    lv_label_set_long_mode(counts, LV_LABEL_LONG_DOT);
    lv_obj_set_width(counts, 390);
    lv_obj_set_pos(counts, 0, 50);
    lv_obj_t *download = create_label(center, "", 0x8EA0AE);
    label_set_fmt(download, "%s",
                  snapshot->map_tile_download_requires ?
                  snapshot->map_tile_download_requires :
                  "Connect Wi-Fi, choose an allowed provider, download only your area");
    lv_label_set_long_mode(download, LV_LABEL_LONG_DOT);
    lv_obj_set_width(download, 390);
    lv_obj_set_pos(download, 0, 72);

    if (!snapshot->map_location_set && !s_map_location_prompt_seen) {
        map_location_from_snapshot(snapshot);
        render_map_location_sheet();
        if (s_map_location_sheet) {
            set_dock_hidden(true);
            d1l_ui_modal_show(s_map_location_sheet);
        }
    }
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

static void render_packets(const d1l_app_snapshot_t *snapshot)
{
    char snr[16];
    format_snr_tenths(snr, sizeof(snr), snapshot->signal_summary.latest_snr_tenths);
    lv_obj_t *title = create_label(s_content, "Packets", 0xF4F7FB);
    obj_set_style_text_font_if(title, &lv_font_montserrat_24);
    obj_set_pos_if(title, 18, 16);

    lv_obj_t *terminal = create_panel(s_content, 18, 52, 424, 50);
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

    render_packet_filter_button(s_content, "All", 18, D1L_PACKET_FILTER_ALL);
    render_packet_filter_button(s_content, "RX", 82, D1L_PACKET_FILTER_RX);
    render_packet_filter_button(s_content, "TX", 146, D1L_PACKET_FILTER_TX);
    render_packet_filter_button(s_content, "Text", 210, D1L_PACKET_FILTER_TEXT);
    create_button(s_content, "Search", 278, 112, 74, 34, open_packet_search_event_cb, NULL);
    create_button(s_content, s_packets_paused ? "Resume" : "Pause", 362, 112, 80, 34,
                  packet_pause_event_cb, NULL);
    if (s_packet_search_text[0]) {
        lv_obj_t *search = create_label(s_content, "", 0xFBBF24);
        label_set_fmt(search, "find %.18s", s_packet_search_text);
        lv_label_set_long_mode(search, LV_LABEL_LONG_DOT);
        lv_obj_set_width(search, 408);
        lv_obj_set_pos(search, 26, 152);
    }

    lv_obj_t *feed_title = create_label(s_content, "Packet Feed", 0x8EA0AE);
    obj_set_pos_if(feed_title, 26, s_packet_search_text[0] ? 176 : 156);

    size_t packet_rows = refresh_packet_terminal_rows();
    int y = s_packet_search_text[0] ? 204 : 184;
    lv_obj_t *feed_count = create_label(s_content, "", 0x8EA0AE);
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
        render_packet_row(s_content, y, &s_packet_filtered_packets[i]);
        y += 52;
    }
    if (packet_rows == 0 && snapshot->recent_packet_count > 0) {
        lv_obj_t *empty = create_label(s_content, "No packets match filter", 0x8EA0AE);
        lv_obj_set_pos(empty, 26, y + 8);
        y += 34;
    } else if (packet_rows == 0) {
        lv_obj_t *empty = create_label(s_content, "Packet log is empty", 0x8EA0AE);
        lv_obj_set_pos(empty, 26, y + 8);
        y += 34;
    }

    if (packet_feed_can_load_older(packet_rows)) {
        create_button(s_content, "Load Older", 18, y + 4, 128, 40,
                      packet_load_older_event_cb, NULL);
        if (packet_feed_can_load_newer()) {
            create_button(s_content, "Newer", 154, y + 4, 92, 40,
                          packet_load_newer_event_cb, NULL);
        }
        y += 54;
    } else if (packet_feed_can_load_newer()) {
        create_button(s_content, "Newer", 18, y + 4, 92, 40,
                      packet_load_newer_event_cb, NULL);
        y += 54;
    }

    y += 10;
    lv_obj_t *roles = create_button(s_content, "Mesh Roles", 18, y, 130, 36,
                                    open_mesh_roles_event_cb, NULL);
    if (roles) {
        lv_obj_set_style_bg_color(roles, lv_color_hex(0x12362F), 0);
    }
    y += 48;
    if (snapshot->recent_route_count > 0) {
        lv_obj_t *routes = create_label(s_content, "Routes", 0x8EA0AE);
        lv_obj_set_pos(routes, 26, y);
        y += 24;
        for (size_t i = 0; i < snapshot->recent_route_count; ++i) {
            render_route_row(s_content, y, &snapshot->recent_routes[i]);
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
            d1l_ui_modal_show(s_radio_settings_sheet);
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
    hide_map_tiles_sheet();
    hide_contact_detail_sheet();
    hide_contact_export_sheet();
    hide_route_detail_sheet();
    hide_route_trace_sheet();
    hide_packet_detail_sheet();
    hide_packet_search_sheet();
    hide_mesh_roles_sheet();
    render_radio_settings_sheet();
    if (s_radio_settings_sheet) {
        d1l_ui_modal_show(s_radio_settings_sheet);
    }
}

static void close_storage_sheet_event_cb(lv_event_t *event)
{
    (void)event;
    hide_storage_sheet();
}

static void render_storage_sheet(void)
{
    if (!s_storage_sheet) {
        return;
    }
    d1l_app_model_snapshot(&s_snapshot);
    lv_obj_clean(s_storage_sheet);

    lv_obj_t *title = create_label(s_storage_sheet, "SD Card", 0xF4F7FB);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(title, 8, 4);
    create_button(s_storage_sheet, "Close", 340, 0, 76, 40, close_storage_sheet_event_cb, NULL);

    lv_obj_t *state = create_label(s_storage_sheet, "", 0x5EEAD4);
    label_set_fmt(state, "SD %s via %s",
                  s_snapshot.storage_sd_state ? s_snapshot.storage_sd_state : "unknown",
                  s_snapshot.storage_sd_interface ? s_snapshot.storage_sd_interface : "unknown");
    lv_label_set_long_mode(state, LV_LABEL_LONG_DOT);
    lv_obj_set_width(state, 408);
    lv_obj_set_pos(state, 8, 48);
    render_storage_line(s_storage_sheet, 70, &s_snapshot);

    lv_obj_t *card = create_label(s_storage_sheet, "", 0xE5EDF5);
    label_set_fmt(card, "card %s  mounted %s  root %s",
                  s_snapshot.storage_sd_present ? "present" : "absent",
                  s_snapshot.storage_sd_mounted ? "yes" : "no",
                  s_snapshot.storage_sd_data_root_ready ? "ready" : "pending");
    lv_obj_set_pos(card, 8, 96);

    lv_obj_t *space = create_label(s_storage_sheet, "", 0x8EA0AE);
    label_set_fmt(space, "fs %s  free %luK / %luK",
                  s_snapshot.storage_sd_filesystem ? s_snapshot.storage_sd_filesystem : "unknown",
                  (unsigned long)s_snapshot.storage_free_kb,
                  (unsigned long)s_snapshot.storage_capacity_kb);
    lv_obj_set_pos(space, 8, 124);

    lv_obj_t *backend = create_label(s_storage_sheet, "", 0x8EA0AE);
    label_set_fmt(backend, "messages %s  packets %s  routes %s",
                  s_snapshot.message_store_backend ? s_snapshot.message_store_backend : "nvs",
                  s_snapshot.packet_log_backend ? s_snapshot.packet_log_backend : "nvs",
                  s_snapshot.route_store_backend ? s_snapshot.route_store_backend : "nvs");
    lv_label_set_long_mode(backend, LV_LABEL_LONG_DOT);
    lv_obj_set_width(backend, 408);
    lv_obj_set_pos(backend, 8, 152);

    lv_obj_t *action = create_label(s_storage_sheet, "", 0xFBBF24);
    const bool prepare_fat32_action = s_snapshot.storage_setup_action &&
        strcmp(s_snapshot.storage_setup_action, "prepare_fat32_on_computer") == 0;
    label_set_fmt(action, "next step: %s%s",
                  s_snapshot.storage_setup_action ? s_snapshot.storage_setup_action : "not_available",
                  prepare_fat32_action ? " (prepare FAT32 on computer)" : "");
    lv_label_set_long_mode(action, LV_LABEL_LONG_DOT);
    lv_obj_set_width(action, 408);
    lv_obj_set_pos(action, 8, 184);

    lv_obj_t *note = create_label(s_storage_sheet,
                                  s_snapshot.storage_note ? s_snapshot.storage_note :
                                  "Onboard NVS remains the fallback data store.",
                                  0x8EA0AE);
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(note, 408);
    lv_obj_set_pos(note, 8, 216);

    lv_obj_t *safety = create_label(s_storage_sheet,
                                    "Use a FAT32 card prepared on your computer. DeskOS creates its folders automatically and never formats cards on-device.",
                                    0xFBBF24);
    lv_label_set_long_mode(safety, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(safety, 408);
    lv_obj_set_pos(safety, 8, 268);
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
    hide_map_tiles_sheet();
    hide_contact_detail_sheet();
    hide_contact_export_sheet();
    hide_route_detail_sheet();
    hide_route_trace_sheet();
    hide_packet_detail_sheet();
    hide_packet_search_sheet();
    hide_mesh_roles_sheet();
    render_storage_sheet();
    if (s_storage_sheet) {
        d1l_ui_modal_show(s_storage_sheet);
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
    if (!s_wifi_keyboard) {
        return;
    }
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
        lv_obj_t *textarea = lv_event_get_target(event);
        if (textarea == s_wifi_ssid_textarea || textarea == s_wifi_password_textarea) {
            lv_keyboard_set_textarea(s_wifi_keyboard, textarea);
        }
    }
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
        close_map_location_sheet_event_cb(event);
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
        lv_obj_set_size(s_wifi_keyboard, 448, 82);
        lv_obj_set_align(s_wifi_keyboard, LV_ALIGN_TOP_LEFT);
        lv_obj_set_pos(s_wifi_keyboard, 16, 330);
        if (s_wifi_ssid_textarea) {
            lv_keyboard_set_textarea(s_wifi_keyboard, s_wifi_ssid_textarea);
        }
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
    hide_map_tiles_sheet();
    hide_contact_detail_sheet();
    hide_contact_export_sheet();
    hide_route_detail_sheet();
    hide_route_trace_sheet();
    hide_packet_detail_sheet();
    hide_packet_search_sheet();
    hide_mesh_roles_sheet();
    render_wifi_sheet();
    if (s_wifi_sheet) {
        set_dock_hidden(true);
        d1l_ui_modal_show(s_wifi_sheet);
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
    hide_map_tiles_sheet();
    hide_contact_detail_sheet();
    hide_contact_export_sheet();
    hide_route_detail_sheet();
    hide_route_trace_sheet();
    hide_packet_detail_sheet();
    hide_packet_search_sheet();
    hide_mesh_roles_sheet();
    render_ble_sheet();
    if (s_ble_sheet) {
        d1l_ui_modal_show(s_ble_sheet);
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
    hide_map_tiles_sheet();
    hide_contact_detail_sheet();
    hide_contact_export_sheet();
    hide_route_detail_sheet();
    hide_route_trace_sheet();
    hide_packet_detail_sheet();
    hide_packet_search_sheet();
    hide_mesh_roles_sheet();
    render_display_sheet();
    if (s_display_sheet) {
        d1l_ui_modal_show(s_display_sheet);
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
    hide_map_tiles_sheet();
    hide_contact_detail_sheet();
    hide_contact_export_sheet();
    hide_route_detail_sheet();
    hide_route_trace_sheet();
    hide_packet_detail_sheet();
    hide_packet_search_sheet();
    hide_mesh_roles_sheet();
    render_diagnostics_sheet();
    if (s_diagnostics_sheet) {
        d1l_ui_modal_show(s_diagnostics_sheet);
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
        hide_map_tiles_sheet();
        hide_contact_detail_sheet();
        hide_contact_export_sheet();
        hide_route_detail_sheet();
        hide_route_trace_sheet();
        hide_packet_detail_sheet();
        hide_packet_search_sheet();
        hide_mesh_roles_sheet();
        d1l_ui_modal_show(s_sheet);
    }
}

static void close_sheet_event_cb(lv_event_t *event)
{
    (void)event;
    hide_sheet();
}

static lv_obj_t *render_settings_tile(lv_obj_t *parent, int x, int y, const char *title,
                                      const char *value, const char *detail,
                                      uint32_t accent, lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *tile = create_panel(parent, x, y, 204, 54);
    if (!tile) {
        return NULL;
    }
    lv_obj_set_style_pad_all(tile, 8, 0);
    if (cb) {
        lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(tile, cb, LV_EVENT_CLICKED, user_data);
    }

    lv_obj_t *title_label = create_label(tile, title, accent);
    label_set_dot_width(title_label, 92);
    lv_obj_set_pos(title_label, 0, 0);

    lv_obj_t *value_label = create_label(tile, value, 0xF4F7FB);
    label_set_dot_width(value_label, 96);
    lv_obj_set_pos(value_label, 96, 0);

    lv_obj_t *detail_label = create_label(tile, detail, 0x8EA0AE);
    label_set_dot_width(detail_label, 188);
    lv_obj_set_pos(detail_label, 0, 28);
    return tile;
}

static void render_settings(const d1l_app_snapshot_t *snapshot)
{
    d1l_app_radio_profile_edit_t profile = {
        .frequency_hz = snapshot->radio_frequency_hz,
        .bandwidth_tenths_khz = snapshot->radio_bandwidth_tenths_khz,
        .spreading_factor = snapshot->radio_spreading_factor,
        .coding_rate = snapshot->radio_coding_rate,
        .tx_power_dbm = snapshot->radio_tx_power_dbm,
        .rx_boost = snapshot->radio_rx_boost,
    };
    char profile_line[80];
    format_radio_profile_line(profile_line, sizeof(profile_line), &profile);

    lv_obj_t *title = create_label(s_content, "Settings", 0xF4F7FB);
    obj_set_style_text_font_if(title, &lv_font_montserrat_24);
    obj_set_pos_if(title, 18, 16);
    lv_obj_t *subtitle = create_label(s_content, "Setup Dashboard", 0x8EA0AE);
    label_set_dot_width(subtitle, 260);
    obj_set_pos_if(subtitle, 18, 44);

    char storage_value[48];
    char storage_detail[128];
    char wifi_value[64];
    char wifi_detail[128];
    char ble_value[48];
    char radio_detail[128];
    char map_value[48];
    char map_detail[128];
    char identity_value[48];
    char identity_detail[128];
    char about_detail[128];

    snprintf(storage_value, sizeof(storage_value), "%s",
             snapshot->storage_data_enabled ? "Ready" :
             (snapshot->storage_setup_required ? "Needs FAT32" : "NVS fallback"));
    snprintf(storage_detail, sizeof(storage_detail), "SD %s; FAT32 only, no format",
             home_sd_state(snapshot));
    render_settings_tile(s_content, 18, 70, "SD Card", storage_value, storage_detail,
                         snapshot->storage_data_enabled ? 0x5EEAD4 : 0xFBBF24,
                         open_storage_sheet_event_cb, NULL);

    if (snapshot->wifi_connected && snapshot->wifi_ip[0]) {
        snprintf(wifi_value, sizeof(wifi_value), "Connected %s", snapshot->wifi_ip);
    } else {
        snprintf(wifi_value, sizeof(wifi_value), "%s",
                 snapshot->wifi_state ? snapshot->wifi_state : "off");
    }
    snprintf(wifi_detail, sizeof(wifi_detail), "%s; mesh stays offline",
             snapshot->wifi_build_enabled ? "Scan/connect for tiles" : "Not in this firmware");
    render_settings_tile(s_content, 238, 70, "Wi-Fi", wifi_value, wifi_detail,
                         snapshot->wifi_connected ? 0x5EEAD4 :
                         (snapshot->wifi_enabled ? 0xFBBF24 : 0x8EA0AE),
                         open_wifi_sheet_event_cb, NULL);

    snprintf(ble_value, sizeof(ble_value), "%s",
             snapshot->ble_state ? snapshot->ble_state :
             (snapshot->ble_build_enabled ? "off" : "unavailable"));
    render_settings_tile(s_content, 18, 128, "BLE", ble_value,
                         "Companion pairing gated",
                         snapshot->ble_companion_enabled ? 0xA7F3D0 : 0x8EA0AE,
                         open_ble_sheet_event_cb, NULL);

    snprintf(radio_detail, sizeof(radio_detail), "%s; TX %d dBm, RX boost %s",
             profile_line,
             snapshot->radio_tx_power_dbm,
             snapshot->radio_rx_boost ? "on" : "off");
    render_settings_tile(s_content, 238, 128, "Radio", "Mesh profile", radio_detail,
                         0x93C5FD, open_radio_settings_event_cb, NULL);

    snprintf(map_value, sizeof(map_value), "%s",
             snapshot->map_tile_cache_ready ? "Tiles ready" : "Setup");
    snprintf(map_detail, sizeof(map_detail), "%s",
             snapshot->map_tile_cache_ready ? "Offline cache ready" :
             "Wi-Fi and allowed provider");
    render_settings_tile(s_content, 18, 186, "Map Tiles", map_value, map_detail,
                         snapshot->map_tile_cache_ready ? 0x5EEAD4 : 0xFBBF24,
                         open_map_tiles_sheet_event_cb, NULL);

    render_settings_tile(s_content, 238, 186, "Display", "Backlight",
                         "Brightness, night, contrast",
                         0xA7F3D0, open_display_sheet_event_cb, NULL);

    snprintf(identity_value, sizeof(identity_value), "%s",
             snapshot->identity_ready ? "Ready" : "Not set");
    snprintf(identity_detail, sizeof(identity_detail), "Name %s; fingerprint %.16s",
             snapshot->node_name[0] ? snapshot->node_name : "D1L Desk",
             snapshot->identity_fingerprint[0] ? snapshot->identity_fingerprint : "not generated");
    render_settings_tile(s_content, 18, 244, "Identity", identity_value, identity_detail,
                         snapshot->identity_ready ? 0x5EEAD4 : 0x8EA0AE, NULL, NULL);

    render_settings_tile(s_content, 238, 244, "Diagnostics", "Health",
                         "Crashlog, exports, soak",
                         0xC4B5FD, open_diagnostics_sheet_event_cb, NULL);

    snprintf(about_detail, sizeof(about_detail), "%s %s",
             D1L_FIRMWARE_NAME, D1L_FIRMWARE_VERSION);
    render_settings_tile(s_content, 18, 302, "About",
                         snapshot->node_name[0] ? snapshot->node_name : "DeskOS D1L",
                         about_detail, 0x8EA0AE, NULL, NULL);

    render_settings_tile(s_content, 238, 302, "Advanced", "Hidden tools",
                         "Raw data and adverts",
                         0xFCA5A5, open_sheet_event_cb, NULL);
}

static void render_active_tab(void)
{
    if (!s_content) {
        return;
    }
    d1l_app_model_snapshot(&s_snapshot);
    update_chrome(&s_snapshot);
    layout_content_for_active_tab();
    lv_obj_clean(s_content);
    configure_content_for_active_tab();
    lv_obj_scroll_to_y(s_content, 0, LV_ANIM_OFF);
    switch (d1l_ui_navigation_active()) {
    case D1L_UI_TAB_HOME:
        render_home(&s_snapshot);
        break;
    case D1L_UI_TAB_MESSAGES:
        render_messages(&s_snapshot);
        break;
    case D1L_UI_TAB_NODES:
        render_nodes(&s_snapshot);
        break;
    case D1L_UI_TAB_MAP:
        render_map(&s_snapshot);
        break;
    case D1L_UI_TAB_PACKETS:
        render_packets(&s_snapshot);
        break;
    case D1L_UI_TAB_SETTINGS:
        render_settings(&s_snapshot);
        break;
    }
    update_onboarding_visibility(&s_snapshot);
    remember_rendered_content_generation(&s_snapshot);
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
    request_tab_switch(tab);
    return ESP_OK;
}

esp_err_t d1l_ui_phase1_scroll_probe(const char *surface,
                                      d1l_ui_scroll_probe_result_t *result)
{
    char canonical[24] = {0};
    d1l_ui_tab_t tab = D1L_UI_TAB_HOME;
    if (!result || !scroll_surface_from_name(surface, canonical, sizeof(canonical), &tab)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_started || !s_content || !s_scroll_probe_done_sem) {
        return ESP_ERR_INVALID_STATE;
    }

    while (xSemaphoreTake(s_scroll_probe_done_sem, 0) == pdTRUE) {
    }

    portENTER_CRITICAL(&s_scroll_probe_lock);
    if (s_scroll_probe_pending) {
        portEXIT_CRITICAL(&s_scroll_probe_lock);
        return ESP_ERR_INVALID_STATE;
    }
    memset(&s_scroll_probe_result, 0, sizeof(s_scroll_probe_result));
    snprintf(s_scroll_probe_surface, sizeof(s_scroll_probe_surface), "%s", canonical);
    s_scroll_probe_pending = true;
    portEXIT_CRITICAL(&s_scroll_probe_lock);

    if (xSemaphoreTake(s_scroll_probe_done_sem,
                       pdMS_TO_TICKS(D1L_UI_SCROLL_PROBE_TIMEOUT_MS)) != pdTRUE) {
        portENTER_CRITICAL(&s_scroll_probe_lock);
        s_scroll_probe_pending = false;
        portEXIT_CRITICAL(&s_scroll_probe_lock);
        return ESP_ERR_TIMEOUT;
    }

    portENTER_CRITICAL(&s_scroll_probe_lock);
    *result = s_scroll_probe_result;
    portEXIT_CRITICAL(&s_scroll_probe_lock);
    return ESP_OK;
}

esp_err_t d1l_ui_phase1_compose_probe(const char *target,
                                       d1l_ui_compose_probe_result_t *result)
{
    char canonical[16] = {0};
    if (!result || !compose_probe_target_from_name(target, canonical, sizeof(canonical))) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_started || !s_content || !s_compose_probe_done_sem) {
        return ESP_ERR_INVALID_STATE;
    }

    while (xSemaphoreTake(s_compose_probe_done_sem, 0) == pdTRUE) {
    }

    portENTER_CRITICAL(&s_compose_probe_lock);
    if (s_compose_probe_pending) {
        portEXIT_CRITICAL(&s_compose_probe_lock);
        return ESP_ERR_INVALID_STATE;
    }
    memset(&s_compose_probe_result, 0, sizeof(s_compose_probe_result));
    snprintf(s_compose_probe_target, sizeof(s_compose_probe_target), "%s", canonical);
    s_compose_probe_pending = true;
    portEXIT_CRITICAL(&s_compose_probe_lock);

    if (xSemaphoreTake(s_compose_probe_done_sem,
                       pdMS_TO_TICKS(D1L_UI_COMPOSE_PROBE_TIMEOUT_MS)) != pdTRUE) {
        portENTER_CRITICAL(&s_compose_probe_lock);
        s_compose_probe_pending = false;
        portEXIT_CRITICAL(&s_compose_probe_lock);
        return ESP_ERR_TIMEOUT;
    }

    portENTER_CRITICAL(&s_compose_probe_lock);
    *result = s_compose_probe_result;
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
    if (!s_capture_shadow_ready) {
        xSemaphoreGive(s_capture_lock);
        return ESP_ERR_INVALID_STATE;
    }
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
    s_capture_active = false;
    fill_capture_status(out_status);
    xSemaphoreGive(s_capture_lock);
    return ESP_OK;
}

static void request_tab_event_cb(lv_event_t *event)
{
    request_tab_switch((d1l_ui_tab_t)(uintptr_t)lv_event_get_user_data(event));
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
    hide_map_tiles_sheet();
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
    if (strcmp(surface, "storage") == 0) {
        open_storage_sheet_event_cb(NULL);
        return scroll_probe_find_target(s_storage_sheet);
    }
    if (strcmp(surface, "wifi") == 0) {
        open_wifi_sheet_event_cb(NULL);
        return scroll_probe_find_target(s_wifi_sheet);
    }
    return scroll_probe_find_target(s_content);
}

static void run_scroll_probe_on_ui_task(const char *surface,
                                        d1l_ui_scroll_probe_result_t *result)
{
    d1l_ui_tab_t tab = D1L_UI_TAB_HOME;
    char canonical[24] = {0};
    if (!scroll_surface_from_name(surface, canonical, sizeof(canonical), &tab)) {
        return;
    }

    result->surface_supported = true;
    snprintf(result->surface, sizeof(result->surface), "%s", canonical);
    snprintf(result->tab, sizeof(result->tab), "%s", tab_name(tab));
    request_tab_switch(tab);
    process_pending_tab_switch();

    lv_obj_t *target = scroll_probe_open_surface(canonical);
    force_ui_layout_repaint();
    measure_scroll_probe_target(target, result);
}

static bool begin_pending_scroll_probe(char *surface, size_t surface_len)
{
    bool pending;

    portENTER_CRITICAL(&s_scroll_probe_lock);
    pending = s_scroll_probe_pending;
    if (pending && surface && surface_len > 0) {
        snprintf(surface, surface_len, "%s", s_scroll_probe_surface);
        s_scroll_probe_pending = false;
    }
    portEXIT_CRITICAL(&s_scroll_probe_lock);
    return pending;
}

static void finish_pending_scroll_probe(const d1l_ui_scroll_probe_result_t *result)
{
    portENTER_CRITICAL(&s_scroll_probe_lock);
    s_scroll_probe_result = *result;
    portEXIT_CRITICAL(&s_scroll_probe_lock);
    if (s_scroll_probe_done_sem) {
        (void)xSemaphoreGive(s_scroll_probe_done_sem);
    }
}

static void process_pending_scroll_probe(void)
{
    char surface[24] = {0};
    if (!begin_pending_scroll_probe(surface, sizeof(surface))) {
        return;
    }

    d1l_ui_scroll_probe_result_t result = {0};
    run_scroll_probe_on_ui_task(surface, &result);
    finish_pending_scroll_probe(&result);
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

static bool compose_probe_target_is_dm(const char *target)
{
    return target && strncmp(target, "dm", 2) == 0;
}

static bool compose_probe_target_is_compose(const char *target)
{
    return target &&
        (strcmp(target, "public") == 0 ||
         strcmp(target, "public_long") == 0 ||
         strcmp(target, "dm") == 0 ||
         strcmp(target, "dm_long") == 0);
}

static bool compose_probe_target_is_onboarding(const char *target)
{
    return target && strcmp(target, "onboarding") == 0;
}

static d1l_ui_tab_t compose_probe_tab_for_target(const char *target)
{
    if (!target) {
        return D1L_UI_TAB_HOME;
    }
    if (compose_probe_target_is_compose(target) ||
        strcmp(target, "public_search") == 0) {
        return D1L_UI_TAB_MESSAGES;
    }
    if (strcmp(target, "packet_search") == 0) {
        return D1L_UI_TAB_PACKETS;
    }
    if (strcmp(target, "contact_edit") == 0) {
        return D1L_UI_TAB_NODES;
    }
    if (strcmp(target, "map_location") == 0 ||
        strcmp(target, "map_provider") == 0) {
        return D1L_UI_TAB_MAP;
    }
    if (strcmp(target, "wifi_ssid") == 0 ||
        strcmp(target, "wifi_password") == 0) {
        return D1L_UI_TAB_SETTINGS;
    }
    return D1L_UI_TAB_HOME;
}

static bool compose_probe_target_requires_hidden_dock(const char *target)
{
    return compose_probe_target_is_compose(target) ||
        compose_probe_target_is_onboarding(target) ||
        strcmp(target, "public_search") == 0 ||
        strcmp(target, "packet_search") == 0 ||
        strcmp(target, "contact_edit") == 0 ||
        strcmp(target, "map_location") == 0 ||
        strcmp(target, "map_provider") == 0 ||
        strcmp(target, "wifi_ssid") == 0 ||
        strcmp(target, "wifi_password") == 0;
}

static int32_t compose_probe_min_keyboard_width(const char *target)
{
    if (strcmp(target, "public_search") == 0 ||
        strcmp(target, "packet_search") == 0 ||
        strcmp(target, "contact_edit") == 0 ||
        compose_probe_target_is_onboarding(target)) {
        return 400;
    }
    return 440;
}

static int32_t compose_probe_min_keyboard_height(const char *target)
{
    if (compose_probe_target_is_compose(target)) {
        return 250;
    }
    if (strcmp(target, "public_search") == 0 ||
        strcmp(target, "packet_search") == 0) {
        return 180;
    }
    if (strcmp(target, "contact_edit") == 0) {
        return 170;
    }
    if (compose_probe_target_is_onboarding(target) ||
        strcmp(target, "map_location") == 0) {
        return 150;
    }
    return 80;
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
    hide_map_tiles_sheet();
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
    if (compose_probe_target_is_compose(target)) {
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
    } else if (compose_probe_target_is_onboarding(target)) {
        *sheet = s_onboarding_sheet;
        *textarea = s_onboarding_name_textarea;
        *keyboard = s_onboarding_keyboard;
    } else if (strcmp(target, "map_location") == 0) {
        *sheet = s_map_location_sheet;
        *textarea = s_map_lat_textarea;
        *keyboard = s_map_location_keyboard;
    } else if (strcmp(target, "map_provider") == 0) {
        *sheet = s_map_tiles_sheet;
        *textarea = s_map_tiles_url_textarea;
        *keyboard = s_map_tiles_keyboard;
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
    s_messages_show_dms = compose_probe_target_is_dm(target);
    render_active_tab();
    hide_onboarding_sheet();

    if (compose_probe_target_is_dm(target)) {
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
    if (compose_probe_target_is_compose(target)) {
        open_compose_probe_on_ui_task(target);
        return;
    }

    hide_keyboard_probe_sheets(true);
    s_onboarding_probe_suppressed = !compose_probe_target_is_onboarding(target);
    request_tab_switch(compose_probe_tab_for_target(target));
    process_pending_tab_switch();
    if (!compose_probe_target_is_onboarding(target)) {
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
    } else if (compose_probe_target_is_onboarding(target)) {
        s_onboarding_probe_suppressed = false;
        if (s_onboarding_sheet) {
            s_onboarding_visible = true;
            d1l_ui_modal_show(s_onboarding_sheet);
            set_dock_hidden(true);
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
        }
    } else if (strcmp(target, "map_provider") == 0) {
        open_map_tiles_sheet_event_cb(NULL);
        if (s_map_tiles_url_textarea) {
            lv_textarea_set_text(s_map_tiles_url_textarea,
                                 "https://tiles.example/{z}/{x}/{y}.png");
        }
        if (s_map_tiles_keyboard) {
            lv_keyboard_set_textarea(s_map_tiles_keyboard, s_map_tiles_url_textarea);
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
    if (!compose_probe_target_from_name(target, canonical, sizeof(canonical))) {
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
        result->onboarding_visible == compose_probe_target_is_onboarding(canonical);
    const bool dock_state_ok =
        !compose_probe_target_requires_hidden_dock(canonical) || result->dock_hidden;
    const bool dm_state_ok =
        !compose_probe_target_is_compose(canonical) ||
        result->dm_mode == compose_probe_target_is_dm(canonical);

    result->ok = result->target_supported &&
        result->sheet_visible &&
        result->textarea_visible &&
        result->keyboard_visible &&
        onboarding_state_ok &&
        dock_state_ok &&
        dm_state_ok &&
        result->keyboard_w >= compose_probe_min_keyboard_width(canonical) &&
        result->keyboard_h >= compose_probe_min_keyboard_height(canonical) &&
        sheet_inside_screen &&
        keyboard_inside_sheet &&
        keyboard_inside_screen &&
        textarea_inside_sheet;
}

static bool begin_pending_compose_probe(char *target, size_t target_len)
{
    bool pending;

    portENTER_CRITICAL(&s_compose_probe_lock);
    pending = s_compose_probe_pending;
    if (pending && target && target_len > 0) {
        snprintf(target, target_len, "%s", s_compose_probe_target);
        s_compose_probe_pending = false;
    }
    portEXIT_CRITICAL(&s_compose_probe_lock);
    return pending;
}

static void finish_pending_compose_probe(const d1l_ui_compose_probe_result_t *result)
{
    portENTER_CRITICAL(&s_compose_probe_lock);
    s_compose_probe_result = *result;
    portEXIT_CRITICAL(&s_compose_probe_lock);
    if (s_compose_probe_done_sem) {
        (void)xSemaphoreGive(s_compose_probe_done_sem);
    }
}

static void process_pending_compose_probe(void)
{
    char target[16] = {0};
    if (!begin_pending_compose_probe(target, sizeof(target))) {
        return;
    }

    d1l_ui_compose_probe_result_t result = {0};
    run_compose_probe_on_ui_task(target, &result);
    finish_pending_compose_probe(&result);
}

static void lock_event_cb(lv_event_t *event)
{
    (void)event;
    if (s_lock_overlay) {
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
    }
}

static void refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    d1l_app_model_snapshot(&s_snapshot);
    update_chrome(&s_snapshot);
    update_onboarding_visibility(&s_snapshot);
    if (content_generation_changed_from_rendered(&s_snapshot)) {
        request_content_refresh();
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

    const char *labels[] = {"Home", "Msg", "Nodes", "Map", "Pkts", "Set"};
    for (int i = 0; i < 6; ++i) {
        create_button(s_dock, labels[i], 4 + i * 80, 5, 72, 50, dock_event_cb,
                      (void *)(uintptr_t)i);
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
    configure_compose_keyboard(s_compose_keyboard);
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
    lv_obj_set_size(s_message_detail_sheet, 448, 328);
    lv_obj_set_pos(s_message_detail_sheet, 16, 82);
    lv_obj_set_style_radius(s_message_detail_sheet, 8, 0);
    lv_obj_set_style_bg_color(s_message_detail_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_color(s_message_detail_sheet, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(s_message_detail_sheet, 1, 0);
    lv_obj_set_style_pad_all(s_message_detail_sheet, 12, 0);
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
    lv_obj_set_size(s_public_search_keyboard, 424, 194);
    lv_obj_set_align(s_public_search_keyboard, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(s_public_search_keyboard, 0, 114);
    lv_keyboard_set_textarea(s_public_search_keyboard, s_public_search_textarea);
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
    lv_obj_set_size(s_dm_thread_sheet, 448, 328);
    lv_obj_set_pos(s_dm_thread_sheet, 16, 82);
    lv_obj_set_style_radius(s_dm_thread_sheet, 8, 0);
    lv_obj_set_style_bg_color(s_dm_thread_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_color(s_dm_thread_sheet, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(s_dm_thread_sheet, 1, 0);
    lv_obj_set_style_pad_all(s_dm_thread_sheet, 12, 0);
    configure_sheet_scroll(s_dm_thread_sheet);
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
    lv_obj_set_size(s_storage_sheet, 448, 320);
    lv_obj_set_pos(s_storage_sheet, 16, 82);
    lv_obj_set_style_radius(s_storage_sheet, 8, 0);
    lv_obj_set_style_bg_color(s_storage_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_color(s_storage_sheet, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(s_storage_sheet, 1, 0);
    lv_obj_set_style_pad_all(s_storage_sheet, 12, 0);
    configure_sheet_scroll(s_storage_sheet);
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
    lv_obj_set_style_bg_color(s_map_location_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_color(s_map_location_sheet, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(s_map_location_sheet, 1, 0);
    lv_obj_set_style_pad_all(s_map_location_sheet, 12, 0);
    lv_obj_clear_flag(s_map_location_sheet, LV_OBJ_FLAG_SCROLLABLE);
    d1l_ui_modal_hide(s_map_location_sheet);
}

static void create_map_tiles_sheet(lv_obj_t *screen)
{
    s_map_tiles_sheet = create_object(screen, "map tiles sheet");
    if (!s_map_tiles_sheet) {
        return;
    }
    lv_obj_set_size(s_map_tiles_sheet, 480, 424);
    lv_obj_set_pos(s_map_tiles_sheet, 0, 56);
    lv_obj_set_style_radius(s_map_tiles_sheet, 0, 0);
    lv_obj_set_style_bg_color(s_map_tiles_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_color(s_map_tiles_sheet, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(s_map_tiles_sheet, 1, 0);
    lv_obj_set_style_pad_all(s_map_tiles_sheet, 12, 0);
    lv_obj_clear_flag(s_map_tiles_sheet, LV_OBJ_FLAG_SCROLLABLE);
    d1l_ui_modal_hide(s_map_tiles_sheet);
}

static void create_contact_detail_sheet(lv_obj_t *screen)
{
    s_contact_detail_sheet = create_object(screen, "contact detail sheet");
    if (!s_contact_detail_sheet) {
        return;
    }
    lv_obj_set_size(s_contact_detail_sheet, 448, 276);
    lv_obj_set_pos(s_contact_detail_sheet, 16, 116);
    lv_obj_set_style_radius(s_contact_detail_sheet, 8, 0);
    lv_obj_set_style_bg_color(s_contact_detail_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_color(s_contact_detail_sheet, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(s_contact_detail_sheet, 1, 0);
    lv_obj_set_style_pad_all(s_contact_detail_sheet, 12, 0);
    lv_obj_clear_flag(s_contact_detail_sheet, LV_OBJ_FLAG_SCROLLABLE);
    d1l_ui_modal_hide(s_contact_detail_sheet);
}

static void create_node_detail_sheet(lv_obj_t *screen)
{
    s_node_detail_sheet = create_object(screen, "node detail sheet");
    if (!s_node_detail_sheet) {
        return;
    }
    lv_obj_set_size(s_node_detail_sheet, 448, 286);
    lv_obj_set_pos(s_node_detail_sheet, 16, 100);
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
    lv_obj_set_size(s_contact_edit_sheet, 448, 320);
    lv_obj_set_pos(s_contact_edit_sheet, 16, 82);
    lv_obj_set_style_radius(s_contact_edit_sheet, 8, 0);
    lv_obj_set_style_bg_color(s_contact_edit_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_color(s_contact_edit_sheet, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(s_contact_edit_sheet, 1, 0);
    lv_obj_set_style_pad_all(s_contact_edit_sheet, 12, 0);
    lv_obj_clear_flag(s_contact_edit_sheet, LV_OBJ_FLAG_SCROLLABLE);

    s_contact_edit_title = create_label(s_contact_edit_sheet, "Edit Contact", 0xF4F7FB);
    if (!s_contact_edit_title) {
        goto fail;
    }
    lv_obj_set_style_text_font(s_contact_edit_title, &lv_font_montserrat_24, 0);
    lv_label_set_long_mode(s_contact_edit_title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_contact_edit_title, 194);
    lv_obj_set_pos(s_contact_edit_title, 8, 4);

    create_button(s_contact_edit_sheet, "Save", 208, 0, 62, 40, save_contact_edit_event_cb, NULL);
    create_button(s_contact_edit_sheet, "Forget", 278, 0, 72, 40,
                  forget_contact_edit_event_cb, NULL);
    create_button(s_contact_edit_sheet, "Close", 358, 0, 66, 40,
                  close_contact_edit_event_cb, NULL);

    lv_obj_t *meta = create_label(s_contact_edit_sheet,
                                  "Alias only; retained history remains", 0x8EA0AE);
    if (!meta) {
        goto fail;
    }
    lv_obj_set_pos(meta, 8, 46);

    s_contact_edit_textarea = create_textarea(s_contact_edit_sheet, "contact edit textarea");
    if (!s_contact_edit_textarea) {
        goto fail;
    }
    lv_obj_set_size(s_contact_edit_textarea, 424, 48);
    lv_obj_set_pos(s_contact_edit_textarea, 0, 78);
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
    lv_obj_set_size(s_contact_edit_keyboard, 424, 178);
    lv_obj_set_align(s_contact_edit_keyboard, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(s_contact_edit_keyboard, 0, 138);
    lv_keyboard_set_textarea(s_contact_edit_keyboard, s_contact_edit_textarea);
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
    lv_obj_set_size(s_contact_export_sheet, 448, 320);
    lv_obj_set_pos(s_contact_export_sheet, 16, 82);
    lv_obj_set_style_radius(s_contact_export_sheet, 8, 0);
    lv_obj_set_style_bg_color(s_contact_export_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_color(s_contact_export_sheet, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(s_contact_export_sheet, 1, 0);
    lv_obj_set_style_pad_all(s_contact_export_sheet, 12, 0);
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
    lv_obj_set_size(s_route_trace_sheet, 448, 326);
    lv_obj_set_pos(s_route_trace_sheet, 16, 76);
    lv_obj_set_style_radius(s_route_trace_sheet, 8, 0);
    lv_obj_set_style_bg_color(s_route_trace_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_color(s_route_trace_sheet, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(s_route_trace_sheet, 1, 0);
    lv_obj_set_style_pad_all(s_route_trace_sheet, 12, 0);
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
    lv_obj_set_size(s_packet_search_keyboard, 424, 194);
    lv_obj_set_align(s_packet_search_keyboard, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(s_packet_search_keyboard, 0, 114);
    lv_keyboard_set_textarea(s_packet_search_keyboard, s_packet_search_textarea);
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
    lv_obj_set_size(s_mesh_roles_sheet, 448, 320);
    lv_obj_set_pos(s_mesh_roles_sheet, 16, 82);
    lv_obj_set_style_radius(s_mesh_roles_sheet, 8, 0);
    lv_obj_set_style_bg_color(s_mesh_roles_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_color(s_mesh_roles_sheet, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(s_mesh_roles_sheet, 1, 0);
    lv_obj_set_style_pad_all(s_mesh_roles_sheet, 12, 0);
    lv_obj_set_scrollbar_mode(s_mesh_roles_sheet, LV_SCROLLBAR_MODE_AUTO);
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
    lv_obj_set_size(s_onboarding_keyboard, 424, 160);
    lv_obj_set_align(s_onboarding_keyboard, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(s_onboarding_keyboard, 8, 258);
    lv_keyboard_set_textarea(s_onboarding_keyboard, s_onboarding_name_textarea);
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
    configure_content_scroll_root(s_content);

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
    create_map_tiles_sheet(s_screen);
    create_contact_detail_sheet(s_screen);
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
