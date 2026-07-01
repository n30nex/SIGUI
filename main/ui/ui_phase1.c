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
#include "freertos/task.h"
#include "lvgl.h"

#include "app/app_model.h"
#include "app/settings_model.h"
#include "bsp_lcd.h"
#include "d1l_config.h"
#include "diagnostics/health_monitor.h"
#include "hal/indicator_board.h"
#include "sdkconfig.h"

static const char *TAG = "d1l_ui";
static lv_disp_draw_buf_t s_draw_buf;
static lv_color_t *s_buf1;
static lv_color_t *s_buf2;
static TaskHandle_t s_ui_task_handle;
static TaskHandle_t s_touch_task_handle;
static portMUX_TYPE s_touch_lock = portMUX_INITIALIZER_UNLOCKED;
static d1l_board_touch_state_t s_touch_state;
static bool s_touch_state_ready = false;
static bool s_started = false;
static lv_obj_t *s_screen;
static lv_obj_t *s_content;
static lv_obj_t *s_status_label;
static lv_obj_t *s_identity_label;
static lv_obj_t *s_toast;
static lv_obj_t *s_sheet;
static lv_obj_t *s_compose_sheet;
static lv_obj_t *s_compose_title;
static lv_obj_t *s_compose_textarea;
static lv_obj_t *s_compose_keyboard;
static lv_obj_t *s_public_history_sheet;
static lv_obj_t *s_public_search_sheet;
static lv_obj_t *s_public_search_textarea;
static lv_obj_t *s_public_search_keyboard;
static lv_obj_t *s_dm_thread_sheet;
static lv_obj_t *s_radio_settings_sheet;
static lv_obj_t *s_storage_sheet;
static lv_obj_t *s_contact_detail_sheet;
static lv_obj_t *s_contact_edit_sheet;
static lv_obj_t *s_contact_edit_title;
static lv_obj_t *s_contact_edit_textarea;
static lv_obj_t *s_contact_edit_keyboard;
static lv_obj_t *s_contact_export_sheet;
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
static uint32_t s_toast_until;
static d1l_app_snapshot_t s_snapshot;
static bool s_compose_dm;
static d1l_contact_entry_t s_compose_contact;
static d1l_app_radio_profile_edit_t s_radio_edit;
static d1l_contact_entry_t s_contact_detail_contact;
static d1l_contact_entry_t s_contact_export_contact;
static d1l_route_entry_t s_route_detail_route;
static d1l_contact_entry_t s_route_trace_contact;
static d1l_packet_log_entry_t s_packet_detail_packet;
static d1l_packet_log_entry_t s_packet_filtered_packets[D1L_APP_SNAPSHOT_PACKET_PREVIEW];
static d1l_route_entry_t s_route_trace_entries[D1L_ROUTE_STORE_CAPACITY];
static d1l_message_entry_t s_public_history_entries[D1L_MESSAGE_STORE_CAPACITY];
static d1l_dm_entry_t s_dm_thread_entries[D1L_DM_STORE_CAPACITY];
static bool s_dm_thread_unread[D1L_DM_STORE_CAPACITY];
static bool s_tab_switch_pending = false;
static d1l_ui_tab_t s_pending_tab = D1L_UI_TAB_HOME;
static char s_public_search_text[D1L_MESSAGE_TEXT_LEN];
static char s_dm_thread_fingerprint[D1L_NODE_FINGERPRINT_LEN];
static char s_dm_thread_alias[D1L_CONTACT_ALIAS_LEN];
static char s_contact_export_uri[D1L_CONTACT_EXPORT_URI_LEN];
static char s_packet_search_text[D1L_PACKET_LOG_QUERY_TEXT_LEN];
static const char s_contact_action_favorite[] = "favorite";
static const char s_contact_action_mute[] = "mute";
static const uint32_t D1L_UI_TIMER_MIN_SLEEP_MS = 20U;
static const uint32_t D1L_UI_TIMER_MAX_SLEEP_MS = 50U;

#if CONFIG_FREERTOS_UNICORE
#define D1L_UI_TASK_CORE 0
#else
#define D1L_UI_TASK_CORE 1
#endif

static void render_active_tab(void);
static void render_contact_detail_sheet(void);
static void open_dm_compose_event_cb(lv_event_t *event);
static void open_public_history_event_cb(lv_event_t *event);
static void open_public_search_event_cb(lv_event_t *event);
static void open_dm_thread_event_cb(lv_event_t *event);
static void open_contact_detail_event_cb(lv_event_t *event);
static void open_contact_edit_event_cb(lv_event_t *event);
static void open_route_trace_event_cb(lv_event_t *event);
static void open_route_detail_event_cb(lv_event_t *event);
static void open_packet_detail_event_cb(lv_event_t *event);
static void open_packet_search_event_cb(lv_event_t *event);
static void open_mesh_roles_event_cb(lv_event_t *event);
static void open_radio_settings_event_cb(lv_event_t *event);
static void open_storage_sheet_event_cb(lv_event_t *event);

typedef enum {
    D1L_UI_TAB_HOME = 0,
    D1L_UI_TAB_MESSAGES,
    D1L_UI_TAB_NODES,
    D1L_UI_TAB_MAP,
    D1L_UI_TAB_PACKETS,
    D1L_UI_TAB_SETTINGS,
} d1l_ui_tab_t;

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

static d1l_ui_tab_t s_active_tab = D1L_UI_TAB_HOME;
static d1l_packet_filter_mode_t s_packet_filter_mode = D1L_PACKET_FILTER_ALL;

static void render_dm_thread_sheet(void);
static void render_public_history_sheet(void);
static void render_radio_settings_sheet(void);
static void render_storage_sheet(void);
static void create_contact_edit_sheet(lv_obj_t *screen);

static void lv_tick_task(void *arg)
{
    (void)arg;
    lv_tick_inc(5);
}

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_err_t ret = bsp_lcd_flush(area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_map);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "flush failed: %s", esp_err_to_name(ret));
    }
    lv_disp_flush_ready(drv);
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
    char text[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    lv_label_set_text(label, text);
}

static lv_obj_t *create_label(lv_obj_t *parent, const char *text, uint32_t color)
{
    if (!parent || !text) {
        return NULL;
    }
    lv_obj_t *label = lv_label_create(parent);
    if (!label) {
        ESP_LOGE(TAG, "label allocation failed");
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
    lv_obj_t *panel = lv_obj_create(parent);
    if (!panel) {
        ESP_LOGE(TAG, "panel allocation failed");
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
    lv_obj_t *label = lv_label_create(button);
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

static void hide_sheet(void)
{
    if (s_sheet) {
        lv_obj_add_flag(s_sheet, LV_OBJ_FLAG_HIDDEN);
    }
}

static void hide_compose_sheet(void)
{
    if (s_compose_sheet) {
        lv_obj_add_flag(s_compose_sheet, LV_OBJ_FLAG_HIDDEN);
    }
    s_compose_dm = false;
    memset(&s_compose_contact, 0, sizeof(s_compose_contact));
}

static void hide_public_history_sheet(void)
{
    if (s_public_history_sheet) {
        lv_obj_add_flag(s_public_history_sheet, LV_OBJ_FLAG_HIDDEN);
    }
}

static void hide_public_search_sheet(void)
{
    if (s_public_search_sheet) {
        lv_obj_add_flag(s_public_search_sheet, LV_OBJ_FLAG_HIDDEN);
    }
}

static void hide_dm_thread_sheet(void)
{
    if (s_dm_thread_sheet) {
        lv_obj_add_flag(s_dm_thread_sheet, LV_OBJ_FLAG_HIDDEN);
    }
    s_dm_thread_fingerprint[0] = '\0';
    s_dm_thread_alias[0] = '\0';
}

static void hide_radio_settings_sheet(void)
{
    if (s_radio_settings_sheet) {
        lv_obj_add_flag(s_radio_settings_sheet, LV_OBJ_FLAG_HIDDEN);
    }
}

static void hide_storage_sheet(void)
{
    if (s_storage_sheet) {
        lv_obj_add_flag(s_storage_sheet, LV_OBJ_FLAG_HIDDEN);
    }
}

static void hide_contact_edit_sheet(void)
{
    if (s_contact_edit_sheet) {
        lv_obj_add_flag(s_contact_edit_sheet, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_contact_edit_textarea) {
        lv_textarea_set_text(s_contact_edit_textarea, "");
    }
}

static void hide_contact_detail_sheet(void)
{
    if (s_contact_detail_sheet) {
        lv_obj_add_flag(s_contact_detail_sheet, LV_OBJ_FLAG_HIDDEN);
    }
    hide_contact_edit_sheet();
    memset(&s_contact_detail_contact, 0, sizeof(s_contact_detail_contact));
}

static void hide_contact_export_sheet(void)
{
    if (s_contact_export_sheet) {
        lv_obj_add_flag(s_contact_export_sheet, LV_OBJ_FLAG_HIDDEN);
    }
    memset(&s_contact_export_contact, 0, sizeof(s_contact_export_contact));
    s_contact_export_uri[0] = '\0';
}

static void hide_route_detail_sheet(void)
{
    if (s_route_detail_sheet) {
        lv_obj_add_flag(s_route_detail_sheet, LV_OBJ_FLAG_HIDDEN);
    }
    memset(&s_route_detail_route, 0, sizeof(s_route_detail_route));
}

static void hide_route_trace_sheet(void)
{
    if (s_route_trace_sheet) {
        lv_obj_add_flag(s_route_trace_sheet, LV_OBJ_FLAG_HIDDEN);
    }
    memset(&s_route_trace_contact, 0, sizeof(s_route_trace_contact));
}

static void hide_packet_detail_sheet(void)
{
    if (s_packet_detail_sheet) {
        lv_obj_add_flag(s_packet_detail_sheet, LV_OBJ_FLAG_HIDDEN);
    }
    memset(&s_packet_detail_packet, 0, sizeof(s_packet_detail_packet));
}

static void hide_packet_search_sheet(void)
{
    if (s_packet_search_sheet) {
        lv_obj_add_flag(s_packet_search_sheet, LV_OBJ_FLAG_HIDDEN);
    }
}

static void hide_mesh_roles_sheet(void)
{
    if (s_mesh_roles_sheet) {
        lv_obj_add_flag(s_mesh_roles_sheet, LV_OBJ_FLAG_HIDDEN);
    }
}

static void hide_onboarding_sheet(void)
{
    if (s_onboarding_sheet) {
        lv_obj_add_flag(s_onboarding_sheet, LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_onboarding_visibility(const d1l_app_snapshot_t *snapshot)
{
    if (!snapshot || !s_onboarding_sheet) {
        return;
    }
    if (snapshot->onboarding_complete) {
        hide_onboarding_sheet();
        return;
    }
    lv_obj_clear_flag(s_onboarding_sheet, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_onboarding_sheet);
}

static void update_chrome(const d1l_app_snapshot_t *snapshot)
{
    if (!snapshot || !s_status_label || !s_identity_label) {
        return;
    }
    label_set_fmt(s_status_label, "%s  RX:%lu TX:%lu",
                  snapshot->mesh_state,
                  (unsigned long)snapshot->rx_packets,
                  (unsigned long)snapshot->tx_packets);
    label_set_fmt(s_identity_label, "%s %.8s",
                  snapshot->identity_ready ? "ID" : "NO-ID",
                  snapshot->identity_fingerprint[0] ? snapshot->identity_fingerprint : "--------");
}

static lv_obj_t *render_metric_card(lv_obj_t *parent, int x, int y, const char *title,
                                    const char *value, const char *detail, uint32_t accent)
{
    lv_obj_t *card = create_panel(parent, x, y, 204, 104);
    lv_obj_t *title_label = create_label(card, title, 0x8EA0AE);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *value_label = create_label(card, value, accent);
    lv_obj_set_style_text_font(value_label, &lv_font_montserrat_24, 0);
    lv_obj_align(value_label, LV_ALIGN_TOP_LEFT, 0, 26);

    lv_obj_t *detail_label = create_label(card, detail, 0xD7E1EA);
    lv_label_set_long_mode(detail_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(detail_label, 176);
    lv_obj_align(detail_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
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

static void render_home(const d1l_app_snapshot_t *snapshot)
{
    char value[32];
    char detail[64];
    char snr[16];

    snprintf(value, sizeof(value), "%s", snapshot->radio_ready ? "Ready" : "Starting");
    snprintf(detail, sizeof(detail), "path %u byte  packets %lu",
             snapshot->path_hash_bytes, (unsigned long)snapshot->packet_total_written);
    render_metric_card(s_content, 18, 16, "Mesh", value, detail,
                       snapshot->radio_ready ? 0x5EEAD4 : 0xFBBF24);

    snprintf(value, sizeof(value), "%s", snapshot->identity_ready ? "Stored" : "Needed");
    snprintf(detail, sizeof(detail), "%.16s", snapshot->identity_fingerprint[0] ?
             snapshot->identity_fingerprint : "not generated");
    render_metric_card(s_content, 238, 16, "Identity", value, detail,
                       snapshot->identity_ready ? 0xA7F3D0 : 0xFBBF24);

    snprintf(value, sizeof(value), "%lu", (unsigned long)snapshot->rx_packets);
    format_snr_tenths(snr, sizeof(snr), snapshot->signal_summary.latest_snr_tenths);
    snprintf(detail, sizeof(detail), "rssi %d snr %s rooms %lu",
             snapshot->signal_summary.latest_rssi_dbm, snr,
             (unsigned long)snapshot->signal_summary.room_server_count);
    render_metric_card(s_content, 18, 136, "RF Packets", value, detail, 0x93C5FD);

    snprintf(value, sizeof(value), "%luK", (unsigned long)(snapshot->heap_free / 1024U));
    snprintf(detail, sizeof(detail), "store %s  up %lum",
             snapshot->storage_backend ? snapshot->storage_backend : "nvs",
             (unsigned long)(snapshot->uptime_ms / 60000U));
    render_metric_card(s_content, 238, 136, "System", value, detail, 0xC4B5FD);
}

static void render_storage_line(lv_obj_t *parent, int y, const d1l_app_snapshot_t *snapshot)
{
    lv_obj_t *line = create_label(parent, "", 0x8EA0AE);
    label_set_fmt(line, "Storage %s  SD %s",
                  snapshot->storage_backend ? snapshot->storage_backend : "nvs",
                  snapshot->storage_sd_state ? snapshot->storage_sd_state : "unknown");
    lv_label_set_long_mode(line, LV_LABEL_LONG_DOT);
    lv_obj_set_width(line, 126);
    lv_obj_set_pos(line, 0, y);
}

static void render_health_line(lv_obj_t *parent, int y, const d1l_app_snapshot_t *snapshot)
{
    lv_obj_t *line = create_label(parent, "", 0x8EA0AE);
    label_set_fmt(line, "reset %s  heap %luK/%luK  ui stk %lu",
                  snapshot->reset_reason ? snapshot->reset_reason : "UNKNOWN",
                  (unsigned long)(snapshot->heap_free / 1024U),
                  (unsigned long)(snapshot->heap_min_free / 1024U),
                  (unsigned long)snapshot->ui_task_stack_free_words);
    lv_label_set_long_mode(line, LV_LABEL_LONG_DOT);
    lv_obj_set_width(line, 390);
    lv_obj_set_pos(line, 0, y);
}

static void render_message_row(lv_obj_t *parent, int y, const d1l_message_entry_t *entry)
{
    lv_obj_t *row = create_panel(parent, 18, y, 424, 54);
    lv_obj_set_style_pad_all(row, 8, 0);
    const bool unread = entry->direction[0] == 'r' && entry->seq > s_snapshot.last_public_read_seq;
    lv_obj_t *author = create_label(row, entry->author,
                                    unread ? 0xFBBF24 :
                                    (entry->direction[0] == 't' ? 0x93C5FD : 0x5EEAD4));
    lv_obj_align(author, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *state = create_label(row,
                                   unread ? "new" :
                                   (entry->direction[0] == 't' ? "queued" : "received"),
                                   0x8EA0AE);
    lv_obj_align(state, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_t *text = create_label(row, entry->text, 0xE5EDF5);
    lv_label_set_long_mode(text, LV_LABEL_LONG_DOT);
    lv_obj_set_width(text, 392);
    lv_obj_align(text, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static void render_dm_row(lv_obj_t *parent, int y, const d1l_dm_entry_t *entry, bool unread)
{
    lv_obj_t *row = create_panel(parent, 18, y, 424, 54);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, open_dm_thread_event_cb, LV_EVENT_CLICKED, (void *)entry);
    lv_obj_set_style_pad_all(row, 8, 0);
    lv_obj_t *alias = create_label(row, entry->contact_alias,
                                    unread ? 0xFBBF24 :
                                    (entry->direction[0] == 't' ? 0xC4B5FD : 0xA7F3D0));
    lv_label_set_long_mode(alias, LV_LABEL_LONG_DOT);
    lv_obj_set_width(alias, 190);
    lv_obj_align(alias, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *state = create_label(row,
                                   unread ? "new" :
                                   (entry->acked ? "acked" :
                                    (entry->direction[0] == 't' ? "sent" : "received")),
                                   0x8EA0AE);
    lv_obj_align(state, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_t *text = create_label(row, entry->text, 0xE5EDF5);
    lv_label_set_long_mode(text, LV_LABEL_LONG_DOT);
    lv_obj_set_width(text, 392);
    lv_obj_align(text, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static void render_packet_row(lv_obj_t *parent, int y, const d1l_packet_log_entry_t *entry)
{
    lv_obj_t *row = create_panel(parent, 18, y, 424, 48);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, open_packet_detail_event_cb, LV_EVENT_CLICKED, (void *)entry);
    lv_obj_set_style_pad_all(row, 8, 0);
    lv_obj_t *kind = create_label(row, entry->kind, 0x5EEAD4);
    lv_obj_align(kind, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *meta = create_label(row, "", 0x8EA0AE);
    label_set_fmt(meta, "#%lu %s rssi %d", (unsigned long)entry->seq,
                  entry->direction, entry->rssi_dbm);
    lv_obj_align(meta, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_t *note = create_label(row, entry->note[0] ? entry->note : "-", 0xE5EDF5);
    lv_label_set_long_mode(note, LV_LABEL_LONG_DOT);
    lv_obj_set_width(note, 392);
    lv_obj_align(note, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static void render_route_row(lv_obj_t *parent, int y, const d1l_route_entry_t *entry)
{
    lv_obj_t *row = create_panel(parent, 18, y, 424, 48);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, open_route_detail_event_cb, LV_EVENT_CLICKED, (void *)entry);
    lv_obj_set_style_pad_all(row, 8, 0);
    lv_obj_t *label = create_label(row, entry->label[0] ? entry->label : entry->target, 0xA7F3D0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, 176);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *meta = create_label(row, "", 0x8EA0AE);
    label_set_fmt(meta, "#%lu %s %s", (unsigned long)entry->seq, entry->direction, entry->route);
    lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
    lv_obj_set_width(meta, 190);
    lv_obj_align(meta, LV_ALIGN_TOP_RIGHT, 0, 0);
    const int snr_abs = entry->last_snr_tenths < 0 ? -entry->last_snr_tenths : entry->last_snr_tenths;
    lv_obj_t *signal = create_label(row, "", 0xE5EDF5);
    label_set_fmt(signal, "%s  rssi %d  snr %s%d.%d  hops %u",
                  entry->kind, entry->last_rssi_dbm,
                  entry->last_snr_tenths < 0 ? "-" : "", snr_abs / 10, snr_abs % 10,
                  entry->path_hops);
    lv_label_set_long_mode(signal, LV_LABEL_LONG_DOT);
    lv_obj_set_width(signal, 392);
    lv_obj_align(signal, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static void render_node_row(lv_obj_t *parent, int y, const d1l_node_entry_t *entry)
{
    lv_obj_t *row = create_panel(parent, 18, y, 424, 56);
    lv_obj_set_style_pad_all(row, 8, 0);
    lv_obj_t *name = create_label(row, entry->name, 0xF4F7FB);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(name, 190);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *type = create_label(row, entry->type, 0x5EEAD4);
    lv_obj_align(type, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_t *meta = create_label(row, "", 0x8EA0AE);
    const int snr_abs = entry->snr_tenths < 0 ? -entry->snr_tenths : entry->snr_tenths;
    label_set_fmt(meta, "%.8s  %s  rssi %d  snr %s%d.%d",
                  entry->fingerprint, entry->public_key_hex[0] ? "key" : "no key",
                  entry->rssi_dbm,
                  entry->snr_tenths < 0 ? "-" : "", snr_abs / 10, snr_abs % 10);
    lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
    lv_obj_set_width(meta, 392);
    lv_obj_align(meta, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static void render_contact_row(lv_obj_t *parent, int y, const d1l_contact_entry_t *entry)
{
    lv_obj_t *row = create_panel(parent, 18, y, 424, 48);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, open_contact_detail_event_cb, LV_EVENT_CLICKED, (void *)entry);
    lv_obj_set_style_pad_all(row, 8, 0);
    lv_obj_t *alias = create_label(row, entry->alias, 0xF4F7FB);
    lv_label_set_long_mode(alias, LV_LABEL_LONG_DOT);
    lv_obj_set_width(alias, 166);
    lv_obj_align(alias, LV_ALIGN_TOP_LEFT, 0, 0);
    if (entry->public_key_hex[0] != '\0') {
        create_button(row, "DM", 350, -1, 48, 34, open_dm_compose_event_cb, (void *)entry);
    } else {
        lv_obj_t *type = create_label(row, entry->type, 0xA7F3D0);
        lv_obj_align(type, LV_ALIGN_TOP_RIGHT, 0, 0);
    }
    lv_obj_t *meta = create_label(row, "", 0x8EA0AE);
    label_set_fmt(meta, "%.8s  %s  %s  rssi %d", entry->fingerprint,
                  entry->public_key_hex[0] ? "key" : "no key",
                  entry->out_path_valid ? "path" : "flood", entry->last_rssi_dbm);
    lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
    lv_obj_set_width(meta, 320);
    lv_obj_align(meta, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static void public_test_event_cb(lv_event_t *event)
{
    (void)event;
    show_toast("Public test", d1l_app_model_send_public_test());
}

static void mark_messages_read_event_cb(lv_event_t *event)
{
    (void)event;
    esp_err_t ret = d1l_app_model_mark_messages_read();
    show_toast("Read", ret);
    if (ret == ESP_OK) {
        render_active_tab();
    }
}

static void open_compose_event_cb(lv_event_t *event)
{
    (void)event;
    hide_sheet();
    hide_public_history_sheet();
    hide_public_search_sheet();
    hide_dm_thread_sheet();
    hide_radio_settings_sheet();
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
        lv_label_set_text(s_compose_title, "Public");
    }
    if (s_compose_sheet) {
        lv_obj_clear_flag(s_compose_sheet, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_compose_sheet);
    }
    if (s_compose_textarea && s_compose_keyboard) {
        lv_textarea_set_text(s_compose_textarea, "");
        lv_textarea_set_placeholder_text(s_compose_textarea, "Public message");
        lv_keyboard_set_textarea(s_compose_keyboard, s_compose_textarea);
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
    hide_dm_thread_sheet();
    hide_radio_settings_sheet();
    hide_contact_detail_sheet();
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
        lv_obj_clear_flag(s_compose_sheet, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_compose_sheet);
    }
    if (s_compose_textarea && s_compose_keyboard) {
        lv_textarea_set_text(s_compose_textarea, "");
        lv_textarea_set_placeholder_text(s_compose_textarea, "Direct message");
        lv_keyboard_set_textarea(s_compose_keyboard, s_compose_textarea);
    }
}

static void open_dm_compose_event_cb(lv_event_t *event)
{
    const d1l_contact_entry_t *entry = (const d1l_contact_entry_t *)lv_event_get_user_data(event);
    open_dm_compose_for_contact(entry);
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
    lv_obj_t *qr = lv_qrcode_create(s_contact_export_sheet, 166,
                                    lv_color_hex(0x02060A), lv_color_hex(0xF8FAFC));
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
        lv_obj_clear_flag(s_contact_export_sheet, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_contact_export_sheet);
    }
}

static void show_contact_detail_after_edit(void)
{
    render_active_tab();
    render_contact_detail_sheet();
    if (s_contact_detail_sheet) {
        lv_obj_clear_flag(s_contact_detail_sheet, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_contact_detail_sheet);
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
            lv_obj_add_flag(s_contact_detail_sheet, LV_OBJ_FLAG_HIDDEN);
        }
        memset(&s_contact_detail_contact, 0, sizeof(s_contact_detail_contact));
        render_active_tab();
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
        lv_obj_add_flag(s_contact_detail_sheet, LV_OBJ_FLAG_HIDDEN);
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
    lv_obj_clear_flag(s_contact_edit_sheet, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_contact_edit_sheet);
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

    lv_obj_t *list = lv_obj_create(s_route_trace_sheet);
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
        lv_obj_t *row = lv_obj_create(list);
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
                                  "Local evidence only; active RF ping/trace is pending",
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
        lv_obj_clear_flag(s_route_trace_sheet, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_route_trace_sheet);
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
        render_active_tab();
        render_contact_detail_sheet();
        if (s_contact_detail_sheet) {
            lv_obj_clear_flag(s_contact_detail_sheet, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(s_contact_detail_sheet);
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
    render_contact_detail_sheet();
    if (s_contact_detail_sheet) {
        lv_obj_clear_flag(s_contact_detail_sheet, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_contact_detail_sheet);
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
    hide_dm_thread_sheet();
    hide_radio_settings_sheet();
    hide_contact_detail_sheet();
    hide_contact_export_sheet();
    hide_packet_detail_sheet();
    hide_packet_search_sheet();
    hide_mesh_roles_sheet();
    render_route_detail_sheet();
    if (s_route_detail_sheet) {
        lv_obj_clear_flag(s_route_detail_sheet, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_route_detail_sheet);
    }
}

static void close_packet_detail_event_cb(lv_event_t *event)
{
    (void)event;
    hide_packet_detail_sheet();
}

static void render_packet_detail_sheet(void)
{
    if (!s_packet_detail_sheet) {
        return;
    }
    lv_obj_clean(s_packet_detail_sheet);

    const d1l_packet_log_entry_t *entry = &s_packet_detail_packet;
    lv_obj_t *title = create_label(s_packet_detail_sheet, entry->kind[0] ? entry->kind : "packet", 0xF4F7FB);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, 260);
    lv_obj_set_pos(title, 8, 4);

    create_button(s_packet_detail_sheet, "Close", 316, 0, 76, 40, close_packet_detail_event_cb, NULL);

    lv_obj_t *meta = create_label(s_packet_detail_sheet, "", 0x8EA0AE);
    label_set_fmt(meta, "#%lu  %s  payload %u  raw %u",
                  (unsigned long)entry->seq,
                  entry->direction[0] ? entry->direction : "-",
                  entry->payload_len, entry->raw_len);
    lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
    lv_obj_set_width(meta, 392);
    lv_obj_set_pos(meta, 8, 50);

    const int snr_abs = entry->snr_tenths < 0 ? -entry->snr_tenths : entry->snr_tenths;
    lv_obj_t *signal = create_label(s_packet_detail_sheet, "", 0x8EA0AE);
    label_set_fmt(signal, "rssi %d  snr %s%d.%d",
                  entry->rssi_dbm,
                  entry->snr_tenths < 0 ? "-" : "", snr_abs / 10, snr_abs % 10);
    lv_label_set_long_mode(signal, LV_LABEL_LONG_DOT);
    lv_obj_set_width(signal, 392);
    lv_obj_set_pos(signal, 8, 84);

    lv_obj_t *path = create_label(s_packet_detail_sheet, "", 0x8EA0AE);
    label_set_fmt(path, "hash %u byte  hops %u  uptime %lums",
                  entry->path_hash_bytes, entry->path_hops, (unsigned long)entry->uptime_ms);
    lv_label_set_long_mode(path, LV_LABEL_LONG_DOT);
    lv_obj_set_width(path, 392);
    lv_obj_set_pos(path, 8, 118);

    lv_obj_t *note = create_label(s_packet_detail_sheet, entry->note[0] ? entry->note : "-", 0xE5EDF5);
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(note, 392);
    lv_obj_set_pos(note, 8, 156);

    lv_obj_t *raw_title = create_label(s_packet_detail_sheet, "Raw Hex", 0x5EEAD4);
    lv_obj_set_pos(raw_title, 8, 190);

    lv_obj_t *raw = create_label(s_packet_detail_sheet, entry->raw_hex[0] ? entry->raw_hex : "-", 0x93C5FD);
    lv_label_set_long_mode(raw, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(raw, 392);
    lv_obj_set_pos(raw, 8, 214);
}

static void open_packet_detail_event_cb(lv_event_t *event)
{
    const d1l_packet_log_entry_t *entry = (const d1l_packet_log_entry_t *)lv_event_get_user_data(event);
    if (!entry || entry->seq == 0) {
        show_toast("Packet", ESP_ERR_INVALID_STATE);
        return;
    }
    s_packet_detail_packet = *entry;
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
    hide_packet_search_sheet();
    hide_mesh_roles_sheet();
    render_packet_detail_sheet();
    if (s_packet_detail_sheet) {
        lv_obj_clear_flag(s_packet_detail_sheet, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_packet_detail_sheet);
    }
}

static void packet_filter_event_cb(lv_event_t *event)
{
    s_packet_filter_mode = (d1l_packet_filter_mode_t)(uintptr_t)lv_event_get_user_data(event);
    render_active_tab();
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
    hide_packet_search_sheet();
    render_active_tab();
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
    hide_packet_search_sheet();
    render_active_tab();
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
    lv_obj_clear_flag(s_packet_search_sheet, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_packet_search_sheet);
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
        lv_obj_clear_flag(s_mesh_roles_sheet, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_mesh_roles_sheet);
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
        hide_compose_sheet();
        render_active_tab();
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
        render_active_tab();
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

    const size_t history_count =
        d1l_app_model_query_public_messages(s_public_history_entries,
                                            D1L_MESSAGE_STORE_CAPACITY,
                                            s_public_search_text);
    lv_obj_t *meta = create_label(s_public_history_sheet, "", 0x8EA0AE);
    if (s_public_search_text[0]) {
        label_set_fmt(meta, "retained %u/%u  find %.18s",
                      (unsigned)history_count, (unsigned)s_snapshot.message_count,
                      s_public_search_text);
    } else {
        label_set_fmt(meta, "retained %u/%u rows",
                      (unsigned)history_count, (unsigned)s_snapshot.message_count);
    }
    lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
    lv_obj_set_width(meta, 392);
    lv_obj_set_pos(meta, 8, 42);

    lv_obj_t *list = lv_obj_create(s_public_history_sheet);
    lv_obj_set_size(list, 424, 206);
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
}

static void show_public_history_sheet(void)
{
    render_public_history_sheet();
    if (s_public_history_sheet) {
        lv_obj_clear_flag(s_public_history_sheet, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_public_history_sheet);
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
    const void *user_data = lv_event_get_user_data(event);
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
    lv_obj_clear_flag(s_public_search_sheet, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_public_search_sheet);
}

static void open_public_history_event_cb(lv_event_t *event)
{
    (void)event;
    hide_sheet();
    hide_compose_sheet();
    hide_public_search_sheet();
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
        render_active_tab();
        render_dm_thread_sheet();
        if (s_dm_thread_sheet) {
            lv_obj_clear_flag(s_dm_thread_sheet, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(s_dm_thread_sheet);
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

    const size_t thread_count =
        d1l_app_model_copy_dm_thread(s_dm_thread_fingerprint, s_dm_thread_entries,
                                     s_dm_thread_unread, D1L_DM_STORE_CAPACITY);

    lv_obj_t *meta = create_label(s_dm_thread_sheet, "", 0x8EA0AE);
    label_set_fmt(meta, "%.16s  thread %u/%u stored", s_dm_thread_fingerprint,
                  (unsigned)thread_count, (unsigned)s_snapshot.dm_count);
    lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
    lv_obj_set_width(meta, 392);
    lv_obj_set_pos(meta, 8, 42);

    lv_obj_t *list = lv_obj_create(s_dm_thread_sheet);
    lv_obj_set_size(list, 424, 196);
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
    if (thread_count == 0) {
        lv_obj_t *empty = create_label(list, "No retained rows for this contact", 0x8EA0AE);
        lv_obj_align(empty, LV_ALIGN_CENTER, 0, 0);
    } else {
        lv_obj_scroll_to_y(list, LV_COORD_MAX, LV_ANIM_OFF);
    }
}

static void open_dm_thread_event_cb(lv_event_t *event)
{
    const d1l_dm_entry_t *entry = (const d1l_dm_entry_t *)lv_event_get_user_data(event);
    if (!entry || entry->contact_fingerprint[0] == '\0') {
        show_toast("DM", ESP_ERR_INVALID_STATE);
        return;
    }
    snprintf(s_dm_thread_fingerprint, sizeof(s_dm_thread_fingerprint), "%s",
             entry->contact_fingerprint);
    snprintf(s_dm_thread_alias, sizeof(s_dm_thread_alias), "%s",
             entry->contact_alias[0] ? entry->contact_alias : entry->contact_fingerprint);
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
        lv_obj_clear_flag(s_dm_thread_sheet, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_dm_thread_sheet);
    }
}

static void render_messages(const d1l_app_snapshot_t *snapshot)
{
    lv_obj_t *header = create_panel(s_content, 18, 16, 424, 70);
    lv_obj_t *title = create_label(header, "Messages", 0xF4F7FB);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, 144);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *meta = create_label(header, "", 0x8EA0AE);
    label_set_fmt(meta, "public %u new %lu  dm %u new %lu muted %lu",
                  (unsigned)snapshot->message_count,
                  (unsigned long)snapshot->public_unread_count,
                  (unsigned)snapshot->dm_count,
                  (unsigned long)snapshot->dm_unread_count,
                  (unsigned long)snapshot->muted_dm_unread_count);
    lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
    lv_obj_set_width(meta, 392);
    lv_obj_align(meta, LV_ALIGN_BOTTOM_LEFT, 2, 0);
    create_button(header, "Read", 142, 10, 54, 44, mark_messages_read_event_cb, NULL);
    create_button(header, "Compose", 202, 10, 82, 44, open_compose_event_cb, NULL);
    create_button(header, "History", 292, 10, 76, 44, open_public_history_event_cb, NULL);
    create_button(header, "Test", 376, 10, 48, 44, public_test_event_cb, NULL);

    int y = 98;
    if (snapshot->recent_message_count > 0) {
        lv_obj_t *public_label = create_label(s_content, "Public", 0x8EA0AE);
        lv_obj_set_pos(public_label, 26, y);
        y += 22;
    }
    for (size_t i = 0; i < snapshot->recent_message_count && y <= 188; ++i) {
        render_message_row(s_content, y, &snapshot->recent_messages[i]);
        y += 60;
    }
    if (snapshot->recent_dm_count > 0) {
        lv_obj_t *dm_label = create_label(s_content, "DM", 0x8EA0AE);
        lv_obj_set_pos(dm_label, 26, y + 2);
        y += 26;
    }
    for (size_t i = 0; i < snapshot->recent_dm_count && y <= 302; ++i) {
        render_dm_row(s_content, y, &snapshot->recent_dms[i], snapshot->recent_dm_unread[i]);
        y += 60;
    }
    if (snapshot->recent_message_count == 0 && snapshot->recent_dm_count == 0) {
        lv_obj_t *empty = create_label(s_content, "No stored messages", 0x8EA0AE);
        lv_obj_align(empty, LV_ALIGN_TOP_MID, 0, 130);
    }
}

static void render_nodes(const d1l_app_snapshot_t *snapshot)
{
    char value[32];
    char detail[64];
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
    for (size_t i = 0; i < snapshot->recent_node_count && y <= 300; ++i) {
        render_node_row(s_content, y, &snapshot->recent_nodes[i]);
        y += 62;
    }
    if (snapshot->recent_node_count == 0) {
        lv_obj_t *empty = create_label(s_content, "No heard nodes yet", 0x8EA0AE);
        lv_obj_align(empty, LV_ALIGN_TOP_MID, 0, 154);
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

    snprintf(value, sizeof(value), "%s", snapshot->map_tile_cache_ready ? "SD Ready" : "Offline");
    snprintf(detail, sizeof(detail), "%s",
             snapshot->map_tile_backend ? snapshot->map_tile_backend : "unavailable");
    render_metric_card(s_content, 18, 48, "Tile Cache", value, detail,
                       snapshot->map_tile_cache_ready ? 0x5EEAD4 : 0xFBBF24);

    snprintf(value, sizeof(value), "%s",
             snapshot->map_tile_download_supported ? "Ready" : "Pending");
    snprintf(detail, sizeof(detail), "%s",
             snapshot->map_tile_download_state ? snapshot->map_tile_download_state :
             "wifi_runtime_pending");
    render_metric_card(s_content, 238, 48, "Downloads", value, detail, 0x93C5FD);

    lv_obj_t *cache = create_panel(s_content, 18, 168, 424, 82);
    create_label(cache, "Offline Cache", 0xF4F7FB);
    lv_obj_t *policy = create_label(cache, "", 0x8EA0AE);
    label_set_fmt(policy, "policy %s",
                  snapshot->map_tile_cache_policy ? snapshot->map_tile_cache_policy :
                  "sd_offline_cache_when_ready");
    lv_label_set_long_mode(policy, LV_LABEL_LONG_DOT);
    lv_obj_set_width(policy, 390);
    lv_obj_set_pos(policy, 0, 28);
    lv_obj_t *path = create_label(cache, "", 0x8EA0AE);
    label_set_fmt(path, "path %s",
                  snapshot->map_tile_cache_path_template ?
                  snapshot->map_tile_cache_path_template : "map/tiles/z{z}/x{x}/y{y}.tile");
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
                  "Wi-Fi runtime plus user opt-in; no background network download");
    lv_label_set_long_mode(download, LV_LABEL_LONG_DOT);
    lv_obj_set_width(download, 390);
    lv_obj_set_pos(download, 0, 72);
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
    lv_obj_t *button = create_button(parent, label, x, 128, 58, 34, packet_filter_event_cb,
                                     (void *)(uintptr_t)mode);
    if (s_packet_filter_mode == mode) {
        lv_obj_set_style_bg_color(button, lv_color_hex(0x12362F), 0);
        lv_obj_set_style_border_color(button, lv_color_hex(0x5EEAD4), 0);
        lv_obj_set_style_border_width(button, 1, 0);
    }
    return button;
}

static void render_packets(const d1l_app_snapshot_t *snapshot)
{
    char value[32];
    char detail[64];
    char snr[16];
    snprintf(value, sizeof(value), "%d", snapshot->signal_summary.latest_rssi_dbm);
    format_snr_tenths(snr, sizeof(snr), snapshot->signal_summary.latest_snr_tenths);
    snprintf(detail, sizeof(detail), "snr %s avg %d", snr,
             snapshot->signal_summary.avg_rssi_dbm);
    render_metric_card(s_content, 18, 16, "Signal", value, detail, 0x93C5FD);
    snprintf(value, sizeof(value), "%lu", (unsigned long)snapshot->signal_summary.room_server_count);
    snprintf(detail, sizeof(detail), "repeaters %lu samples %lu",
             (unsigned long)snapshot->signal_summary.repeater_candidate_count,
             (unsigned long)snapshot->signal_summary.sample_count);
    lv_obj_t *roles_card = render_metric_card(s_content, 238, 16, "Mesh Roles", value, detail, 0xA7F3D0);
    lv_obj_add_flag(roles_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(roles_card, open_mesh_roles_event_cb, LV_EVENT_CLICKED, NULL);

    render_packet_filter_button(s_content, "All", 18, D1L_PACKET_FILTER_ALL);
    render_packet_filter_button(s_content, "RX", 82, D1L_PACKET_FILTER_RX);
    render_packet_filter_button(s_content, "TX", 146, D1L_PACKET_FILTER_TX);
    render_packet_filter_button(s_content, "Text", 210, D1L_PACKET_FILTER_TEXT);
    create_button(s_content, "Search", 286, 128, 74, 34, open_packet_search_event_cb, NULL);
    if (s_packet_search_text[0]) {
        lv_obj_t *search = create_label(s_content, "", 0xFBBF24);
        label_set_fmt(search, "find %.18s", s_packet_search_text);
        lv_label_set_long_mode(search, LV_LABEL_LONG_DOT);
        lv_obj_set_width(search, 84);
        lv_obj_set_pos(search, 366, 135);
    }

    size_t packet_rows = d1l_packet_log_query(s_packet_filtered_packets,
                                              D1L_APP_SNAPSHOT_PACKET_PREVIEW,
                                              packet_filter_direction(),
                                              packet_filter_kind(),
                                              s_packet_search_text);
    int y = 174;
    if (snapshot->recent_route_count > 0 && packet_rows > 3) {
        packet_rows = 2;
    }
    for (size_t i = 0; i < packet_rows && y <= 286; ++i) {
        render_packet_row(s_content, y, &s_packet_filtered_packets[i]);
        y += 56;
    }
    if (packet_rows == 0 && snapshot->recent_packet_count > 0) {
        lv_obj_t *empty = create_label(s_content, "No packets match filter", 0x8EA0AE);
        lv_obj_set_pos(empty, 26, y + 8);
        y += 34;
    }
    if (snapshot->recent_route_count > 0) {
        lv_obj_t *routes = create_label(s_content, "Routes", 0x8EA0AE);
        lv_obj_set_pos(routes, 26, y + 2);
        y += 24;
        for (size_t i = 0; i < snapshot->recent_route_count && y <= 304; ++i) {
            render_route_row(s_content, y, &snapshot->recent_routes[i]);
            y += 54;
        }
    }
    if (snapshot->recent_packet_count == 0 && snapshot->recent_route_count == 0) {
        lv_obj_t *empty = create_label(s_content, "Packet log is empty", 0x8EA0AE);
        lv_obj_align(empty, LV_ALIGN_CENTER, 0, -20);
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
        render_active_tab();
        render_radio_settings_sheet();
        if (s_radio_settings_sheet) {
            lv_obj_clear_flag(s_radio_settings_sheet, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(s_radio_settings_sheet);
        }
        show_toast_text("Radio saved - reboot/apply required", true);
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

    lv_obj_t *warning = create_label(s_radio_settings_sheet,
                                     "Saved profile applies after reboot; live RF unchanged",
                                     0xFBBF24);
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
    hide_contact_detail_sheet();
    hide_contact_export_sheet();
    hide_route_detail_sheet();
    hide_route_trace_sheet();
    hide_packet_detail_sheet();
    hide_packet_search_sheet();
    hide_mesh_roles_sheet();
    render_radio_settings_sheet();
    if (s_radio_settings_sheet) {
        lv_obj_clear_flag(s_radio_settings_sheet, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_radio_settings_sheet);
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

    lv_obj_t *title = create_label(s_storage_sheet, "Storage Setup", 0xF4F7FB);
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

    lv_obj_t *card = create_label(s_storage_sheet, "", 0xE5EDF5);
    label_set_fmt(card, "card %s  mounted %s  root %s",
                  s_snapshot.storage_sd_present ? "present" : "absent",
                  s_snapshot.storage_sd_mounted ? "yes" : "no",
                  s_snapshot.storage_sd_data_root_ready ? "ready" : "pending");
    lv_obj_set_pos(card, 8, 78);

    lv_obj_t *space = create_label(s_storage_sheet, "", 0x8EA0AE);
    label_set_fmt(space, "fs %s  free %luK / %luK",
                  s_snapshot.storage_sd_filesystem ? s_snapshot.storage_sd_filesystem : "unknown",
                  (unsigned long)s_snapshot.storage_free_kb,
                  (unsigned long)s_snapshot.storage_capacity_kb);
    lv_obj_set_pos(space, 8, 106);

    lv_obj_t *backend = create_label(s_storage_sheet, "", 0x8EA0AE);
    label_set_fmt(backend, "messages %s  packets %s  routes %s",
                  s_snapshot.message_store_backend ? s_snapshot.message_store_backend : "nvs",
                  s_snapshot.packet_log_backend ? s_snapshot.packet_log_backend : "nvs",
                  s_snapshot.route_store_backend ? s_snapshot.route_store_backend : "nvs");
    lv_label_set_long_mode(backend, LV_LABEL_LONG_DOT);
    lv_obj_set_width(backend, 408);
    lv_obj_set_pos(backend, 8, 134);

    lv_obj_t *action = create_label(s_storage_sheet, "", 0xFBBF24);
    label_set_fmt(action, "setup %s  format %s",
                  s_snapshot.storage_setup_action ? s_snapshot.storage_setup_action : "not_available",
                  s_snapshot.storage_format_action ? s_snapshot.storage_format_action : "not_available");
    lv_label_set_long_mode(action, LV_LABEL_LONG_DOT);
    lv_obj_set_width(action, 408);
    lv_obj_set_pos(action, 8, 168);

    lv_obj_t *note = create_label(s_storage_sheet,
                                  s_snapshot.storage_note ? s_snapshot.storage_note :
                                  "Onboard NVS remains the fallback data store.",
                                  0x8EA0AE);
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(note, 408);
    lv_obj_set_pos(note, 8, 202);

    lv_obj_t *safety = create_label(s_storage_sheet,
                                    "No automatic format. Formatting will require explicit confirmation.",
                                    0xFBBF24);
    lv_label_set_long_mode(safety, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(safety, 408);
    lv_obj_set_pos(safety, 8, 260);
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
    hide_contact_detail_sheet();
    hide_contact_export_sheet();
    hide_route_detail_sheet();
    hide_route_trace_sheet();
    hide_packet_detail_sheet();
    hide_packet_search_sheet();
    hide_mesh_roles_sheet();
    render_storage_sheet();
    if (s_storage_sheet) {
        lv_obj_clear_flag(s_storage_sheet, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_storage_sheet);
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
        hide_contact_detail_sheet();
        hide_contact_export_sheet();
        hide_route_detail_sheet();
        hide_route_trace_sheet();
        hide_packet_detail_sheet();
        hide_packet_search_sheet();
        hide_mesh_roles_sheet();
        lv_obj_clear_flag(s_sheet, LV_OBJ_FLAG_HIDDEN);
    }
}

static void close_sheet_event_cb(lv_event_t *event)
{
    (void)event;
    hide_sheet();
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

    lv_obj_t *radio = create_panel(s_content, 18, 16, 424, 82);
    create_label(radio, profile_line, 0xF4F7FB);
    lv_obj_t *path = create_label(radio, "", 0x8EA0AE);
    label_set_fmt(path, "TX %d dBm  RX boost %s  TCXO %s",
                  snapshot->radio_tx_power_dbm,
                  snapshot->radio_rx_boost ? "on" : "off",
                  snapshot->radio_tcxo ? snapshot->radio_tcxo : "NONE");
    lv_obj_align(path, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_t *identity = create_panel(s_content, 18, 114, 424, 82);
    create_label(identity, snapshot->node_name, 0xF4F7FB);
    lv_obj_t *id = create_label(identity, "", 0x8EA0AE);
    label_set_fmt(id, "identity %.16s", snapshot->identity_fingerprint[0] ?
                  snapshot->identity_fingerprint : "not generated");
    lv_obj_align(id, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_t *companion = create_panel(s_content, 18, 212, 424, 104);
    create_label(companion, "Companion", 0xF4F7FB);
    lv_obj_t *wireless = create_label(companion, "", 0x8EA0AE);
    label_set_fmt(wireless, "Wi-Fi %s  BLE %s",
                  snapshot->wifi_state ? snapshot->wifi_state : "off",
                  snapshot->ble_state ? snapshot->ble_state : "off");
    lv_obj_set_pos(wireless, 0, 30);
    lv_obj_t *policy = create_label(companion, "", 0x8EA0AE);
    label_set_fmt(policy, "USB ready  %s",
                  snapshot->coexistence_policy ? snapshot->coexistence_policy : "offline first");
    lv_label_set_long_mode(policy, LV_LABEL_LONG_DOT);
    lv_obj_set_width(policy, 390);
    lv_obj_set_pos(policy, 0, 54);
    render_health_line(companion, 76, snapshot);

    create_button(s_content, "Radio", 18, 332, 130, 48, open_radio_settings_event_cb, NULL);
    create_button(s_content, "Advert", 160, 332, 130, 48, open_sheet_event_cb, NULL);
    lv_obj_t *storage = create_panel(s_content, 302, 332, 140, 48);
    lv_obj_set_style_pad_all(storage, 8, 0);
    lv_obj_add_flag(storage, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(storage, open_storage_sheet_event_cb, LV_EVENT_CLICKED, NULL);
    create_label(storage, "Storage", 0xF4F7FB);
    render_storage_line(storage, 22, snapshot);
}

static void render_active_tab(void)
{
    if (!s_content) {
        return;
    }
    d1l_app_model_snapshot(&s_snapshot);
    update_chrome(&s_snapshot);
    lv_obj_clean(s_content);
    switch (s_active_tab) {
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
}

static void dock_event_cb(lv_event_t *event)
{
    s_pending_tab = (d1l_ui_tab_t)(uintptr_t)lv_event_get_user_data(event);
    s_tab_switch_pending = true;
}

static void process_pending_tab_switch(void)
{
    if (!s_tab_switch_pending) {
        return;
    }
    s_tab_switch_pending = false;
    s_active_tab = s_pending_tab;
    hide_sheet();
    hide_public_history_sheet();
    hide_public_search_sheet();
    hide_compose_sheet();
    hide_dm_thread_sheet();
    hide_radio_settings_sheet();
    hide_storage_sheet();
    hide_contact_detail_sheet();
    hide_contact_export_sheet();
    hide_route_detail_sheet();
    hide_route_trace_sheet();
    hide_packet_detail_sheet();
    hide_packet_search_sheet();
    hide_mesh_roles_sheet();
    render_active_tab();
}

static void lock_event_cb(lv_event_t *event)
{
    (void)event;
    if (s_lock_overlay) {
        lv_obj_clear_flag(s_lock_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

static void unlock_event_cb(lv_event_t *event)
{
    (void)event;
    if (s_lock_overlay) {
        lv_obj_add_flag(s_lock_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

static void refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    d1l_app_model_snapshot(&s_snapshot);
    update_chrome(&s_snapshot);
    update_onboarding_visibility(&s_snapshot);
    if (s_toast && s_toast_until != 0 && lv_tick_get() > s_toast_until) {
        lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
        s_toast_until = 0;
    }
}

static void create_top_bar(lv_obj_t *screen)
{
    lv_obj_t *bar = lv_obj_create(screen);
    lv_obj_set_size(bar, 480, 56);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x071018), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 8, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = create_label(bar, "MeshCore DeskOS", 0xF4F7FB);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 2, 0);

    s_status_label = create_label(bar, "starting", 0x5EEAD4);
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_status_label, 150);
    lv_obj_align(s_status_label, LV_ALIGN_TOP_RIGHT, -48, 1);

    s_identity_label = create_label(bar, "ID --------", 0x8EA0AE);
    lv_label_set_long_mode(s_identity_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_identity_label, 150);
    lv_obj_align(s_identity_label, LV_ALIGN_BOTTOM_RIGHT, -48, -1);

    create_button(bar, "Lock", 404, 6, 64, 40, lock_event_cb, NULL);
}

static void create_dock(lv_obj_t *screen)
{
    lv_obj_t *dock = lv_obj_create(screen);
    lv_obj_set_size(dock, 480, 62);
    lv_obj_set_pos(dock, 0, 418);
    lv_obj_set_style_bg_color(dock, lv_color_hex(0x09131D), 0);
    lv_obj_set_style_border_width(dock, 0, 0);
    lv_obj_set_style_pad_all(dock, 5, 0);
    lv_obj_clear_flag(dock, LV_OBJ_FLAG_SCROLLABLE);

    const char *labels[] = {"Home", "Msg", "Nodes", "Map", "Pkts", "Set"};
    for (int i = 0; i < 6; ++i) {
        create_button(dock, labels[i], 4 + i * 80, 5, 72, 50, dock_event_cb,
                      (void *)(uintptr_t)i);
    }
}

static void create_toast(lv_obj_t *screen)
{
    s_toast = lv_label_create(screen);
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
    s_sheet = lv_obj_create(screen);
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
    lv_obj_add_flag(s_sheet, LV_OBJ_FLAG_HIDDEN);
}

static void create_compose_sheet(lv_obj_t *screen)
{
    s_compose_sheet = lv_obj_create(screen);
    lv_obj_set_size(s_compose_sheet, 448, 344);
    lv_obj_set_pos(s_compose_sheet, 16, 64);
    lv_obj_set_style_radius(s_compose_sheet, 8, 0);
    lv_obj_set_style_bg_color(s_compose_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_color(s_compose_sheet, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(s_compose_sheet, 1, 0);
    lv_obj_set_style_pad_all(s_compose_sheet, 12, 0);
    lv_obj_clear_flag(s_compose_sheet, LV_OBJ_FLAG_SCROLLABLE);

    s_compose_title = create_label(s_compose_sheet, "Public", 0xF4F7FB);
    lv_obj_set_style_text_font(s_compose_title, &lv_font_montserrat_24, 0);
    lv_label_set_long_mode(s_compose_title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_compose_title, 204);
    lv_obj_set_pos(s_compose_title, 8, 4);

    create_button(s_compose_sheet, "Send", 226, 0, 64, 40, send_compose_event_cb, NULL);
    create_button(s_compose_sheet, "Clear", 298, 0, 64, 40, clear_compose_event_cb, NULL);
    create_button(s_compose_sheet, "Close", 370, 0, 58, 40, close_compose_event_cb, NULL);

    s_compose_textarea = lv_textarea_create(s_compose_sheet);
    lv_obj_set_size(s_compose_textarea, 424, 68);
    lv_obj_set_pos(s_compose_textarea, 0, 50);
    lv_textarea_set_placeholder_text(s_compose_textarea, "Public message");
    lv_textarea_set_max_length(s_compose_textarea, 80);
    lv_textarea_set_one_line(s_compose_textarea, false);
    lv_textarea_set_text(s_compose_textarea, "");
    lv_obj_set_style_radius(s_compose_textarea, 8, 0);
    lv_obj_set_style_bg_color(s_compose_textarea, lv_color_hex(0x071018), 0);
    lv_obj_set_style_border_color(s_compose_textarea, lv_color_hex(0x263241), 0);
    lv_obj_set_style_text_color(s_compose_textarea, lv_color_hex(0xF4F7FB), 0);
    lv_obj_set_style_text_color(s_compose_textarea, lv_color_hex(0x8EA0AE), LV_PART_TEXTAREA_PLACEHOLDER);

    s_compose_keyboard = lv_keyboard_create(s_compose_sheet);
    lv_obj_set_size(s_compose_keyboard, 424, 202);
    lv_obj_set_pos(s_compose_keyboard, 0, 128);
    lv_keyboard_set_textarea(s_compose_keyboard, s_compose_textarea);
    lv_obj_add_event_cb(s_compose_keyboard, compose_keyboard_event_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_compose_keyboard, compose_keyboard_event_cb, LV_EVENT_CANCEL, NULL);

    lv_obj_add_flag(s_compose_sheet, LV_OBJ_FLAG_HIDDEN);
}

static void create_public_history_sheet(lv_obj_t *screen)
{
    s_public_history_sheet = lv_obj_create(screen);
    lv_obj_set_size(s_public_history_sheet, 448, 304);
    lv_obj_set_pos(s_public_history_sheet, 16, 88);
    lv_obj_set_style_radius(s_public_history_sheet, 8, 0);
    lv_obj_set_style_bg_color(s_public_history_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_color(s_public_history_sheet, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(s_public_history_sheet, 1, 0);
    lv_obj_set_style_pad_all(s_public_history_sheet, 12, 0);
    lv_obj_clear_flag(s_public_history_sheet, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_public_history_sheet, LV_OBJ_FLAG_HIDDEN);
}

static void create_public_search_sheet(lv_obj_t *screen)
{
    s_public_search_sheet = lv_obj_create(screen);
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

    s_public_search_textarea = lv_textarea_create(s_public_search_sheet);
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

    s_public_search_keyboard = lv_keyboard_create(s_public_search_sheet);
    lv_obj_set_size(s_public_search_keyboard, 424, 194);
    lv_obj_set_pos(s_public_search_keyboard, 0, 114);
    lv_keyboard_set_textarea(s_public_search_keyboard, s_public_search_textarea);
    lv_obj_add_event_cb(s_public_search_keyboard, public_search_keyboard_event_cb,
                        LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_public_search_keyboard, public_search_keyboard_event_cb,
                        LV_EVENT_CANCEL, NULL);

    lv_obj_add_flag(s_public_search_sheet, LV_OBJ_FLAG_HIDDEN);
}

static void create_dm_thread_sheet(lv_obj_t *screen)
{
    s_dm_thread_sheet = lv_obj_create(screen);
    lv_obj_set_size(s_dm_thread_sheet, 448, 300);
    lv_obj_set_pos(s_dm_thread_sheet, 16, 92);
    lv_obj_set_style_radius(s_dm_thread_sheet, 8, 0);
    lv_obj_set_style_bg_color(s_dm_thread_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_color(s_dm_thread_sheet, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(s_dm_thread_sheet, 1, 0);
    lv_obj_set_style_pad_all(s_dm_thread_sheet, 12, 0);
    lv_obj_clear_flag(s_dm_thread_sheet, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_dm_thread_sheet, LV_OBJ_FLAG_HIDDEN);
}

static void create_radio_settings_sheet(lv_obj_t *screen)
{
    s_radio_settings_sheet = lv_obj_create(screen);
    lv_obj_set_size(s_radio_settings_sheet, 448, 320);
    lv_obj_set_pos(s_radio_settings_sheet, 16, 82);
    lv_obj_set_style_radius(s_radio_settings_sheet, 8, 0);
    lv_obj_set_style_bg_color(s_radio_settings_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_color(s_radio_settings_sheet, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(s_radio_settings_sheet, 1, 0);
    lv_obj_set_style_pad_all(s_radio_settings_sheet, 12, 0);
    lv_obj_clear_flag(s_radio_settings_sheet, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_radio_settings_sheet, LV_OBJ_FLAG_HIDDEN);
}

static void create_storage_sheet(lv_obj_t *screen)
{
    s_storage_sheet = lv_obj_create(screen);
    lv_obj_set_size(s_storage_sheet, 448, 320);
    lv_obj_set_pos(s_storage_sheet, 16, 82);
    lv_obj_set_style_radius(s_storage_sheet, 8, 0);
    lv_obj_set_style_bg_color(s_storage_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_color(s_storage_sheet, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(s_storage_sheet, 1, 0);
    lv_obj_set_style_pad_all(s_storage_sheet, 12, 0);
    lv_obj_clear_flag(s_storage_sheet, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_storage_sheet, LV_OBJ_FLAG_HIDDEN);
}

static void create_contact_detail_sheet(lv_obj_t *screen)
{
    s_contact_detail_sheet = lv_obj_create(screen);
    lv_obj_set_size(s_contact_detail_sheet, 448, 276);
    lv_obj_set_pos(s_contact_detail_sheet, 16, 116);
    lv_obj_set_style_radius(s_contact_detail_sheet, 8, 0);
    lv_obj_set_style_bg_color(s_contact_detail_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_color(s_contact_detail_sheet, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(s_contact_detail_sheet, 1, 0);
    lv_obj_set_style_pad_all(s_contact_detail_sheet, 12, 0);
    lv_obj_clear_flag(s_contact_detail_sheet, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_contact_detail_sheet, LV_OBJ_FLAG_HIDDEN);
}

static void create_contact_edit_sheet(lv_obj_t *screen)
{
    if (!screen || s_contact_edit_sheet) {
        return;
    }
    s_contact_edit_sheet = lv_obj_create(screen);
    if (!s_contact_edit_sheet) {
        ESP_LOGE(TAG, "contact edit sheet allocation failed");
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

    s_contact_edit_textarea = lv_textarea_create(s_contact_edit_sheet);
    if (!s_contact_edit_textarea) {
        ESP_LOGE(TAG, "contact edit textarea allocation failed");
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

    s_contact_edit_keyboard = lv_keyboard_create(s_contact_edit_sheet);
    if (!s_contact_edit_keyboard) {
        ESP_LOGE(TAG, "contact edit keyboard allocation failed");
        goto fail;
    }
    lv_obj_set_size(s_contact_edit_keyboard, 424, 178);
    lv_obj_set_pos(s_contact_edit_keyboard, 0, 138);
    lv_keyboard_set_textarea(s_contact_edit_keyboard, s_contact_edit_textarea);
    lv_obj_add_event_cb(s_contact_edit_keyboard, contact_edit_keyboard_event_cb,
                        LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_contact_edit_keyboard, contact_edit_keyboard_event_cb,
                        LV_EVENT_CANCEL, NULL);

    lv_obj_add_flag(s_contact_edit_sheet, LV_OBJ_FLAG_HIDDEN);
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
    s_contact_export_sheet = lv_obj_create(screen);
    lv_obj_set_size(s_contact_export_sheet, 448, 320);
    lv_obj_set_pos(s_contact_export_sheet, 16, 82);
    lv_obj_set_style_radius(s_contact_export_sheet, 8, 0);
    lv_obj_set_style_bg_color(s_contact_export_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_color(s_contact_export_sheet, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(s_contact_export_sheet, 1, 0);
    lv_obj_set_style_pad_all(s_contact_export_sheet, 12, 0);
    lv_obj_clear_flag(s_contact_export_sheet, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_contact_export_sheet, LV_OBJ_FLAG_HIDDEN);
}

static void create_route_detail_sheet(lv_obj_t *screen)
{
    s_route_detail_sheet = lv_obj_create(screen);
    lv_obj_set_size(s_route_detail_sheet, 448, 250);
    lv_obj_set_pos(s_route_detail_sheet, 16, 132);
    lv_obj_set_style_radius(s_route_detail_sheet, 8, 0);
    lv_obj_set_style_bg_color(s_route_detail_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_color(s_route_detail_sheet, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(s_route_detail_sheet, 1, 0);
    lv_obj_set_style_pad_all(s_route_detail_sheet, 12, 0);
    lv_obj_clear_flag(s_route_detail_sheet, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_route_detail_sheet, LV_OBJ_FLAG_HIDDEN);
}

static void create_route_trace_sheet(lv_obj_t *screen)
{
    s_route_trace_sheet = lv_obj_create(screen);
    lv_obj_set_size(s_route_trace_sheet, 448, 326);
    lv_obj_set_pos(s_route_trace_sheet, 16, 76);
    lv_obj_set_style_radius(s_route_trace_sheet, 8, 0);
    lv_obj_set_style_bg_color(s_route_trace_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_color(s_route_trace_sheet, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(s_route_trace_sheet, 1, 0);
    lv_obj_set_style_pad_all(s_route_trace_sheet, 12, 0);
    lv_obj_clear_flag(s_route_trace_sheet, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_route_trace_sheet, LV_OBJ_FLAG_HIDDEN);
}

static void create_packet_detail_sheet(lv_obj_t *screen)
{
    s_packet_detail_sheet = lv_obj_create(screen);
    lv_obj_set_size(s_packet_detail_sheet, 448, 286);
    lv_obj_set_pos(s_packet_detail_sheet, 16, 100);
    lv_obj_set_style_radius(s_packet_detail_sheet, 8, 0);
    lv_obj_set_style_bg_color(s_packet_detail_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_color(s_packet_detail_sheet, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(s_packet_detail_sheet, 1, 0);
    lv_obj_set_style_pad_all(s_packet_detail_sheet, 12, 0);
    lv_obj_clear_flag(s_packet_detail_sheet, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_packet_detail_sheet, LV_OBJ_FLAG_HIDDEN);
}

static void create_packet_search_sheet(lv_obj_t *screen)
{
    s_packet_search_sheet = lv_obj_create(screen);
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

    s_packet_search_textarea = lv_textarea_create(s_packet_search_sheet);
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

    s_packet_search_keyboard = lv_keyboard_create(s_packet_search_sheet);
    lv_obj_set_size(s_packet_search_keyboard, 424, 194);
    lv_obj_set_pos(s_packet_search_keyboard, 0, 114);
    lv_keyboard_set_textarea(s_packet_search_keyboard, s_packet_search_textarea);
    lv_obj_add_event_cb(s_packet_search_keyboard, packet_search_keyboard_event_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_packet_search_keyboard, packet_search_keyboard_event_cb, LV_EVENT_CANCEL, NULL);

    lv_obj_add_flag(s_packet_search_sheet, LV_OBJ_FLAG_HIDDEN);
}

static void create_mesh_roles_sheet(lv_obj_t *screen)
{
    s_mesh_roles_sheet = lv_obj_create(screen);
    lv_obj_set_size(s_mesh_roles_sheet, 448, 320);
    lv_obj_set_pos(s_mesh_roles_sheet, 16, 82);
    lv_obj_set_style_radius(s_mesh_roles_sheet, 8, 0);
    lv_obj_set_style_bg_color(s_mesh_roles_sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_color(s_mesh_roles_sheet, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(s_mesh_roles_sheet, 1, 0);
    lv_obj_set_style_pad_all(s_mesh_roles_sheet, 12, 0);
    lv_obj_set_scrollbar_mode(s_mesh_roles_sheet, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(s_mesh_roles_sheet, LV_OBJ_FLAG_HIDDEN);
}

static void create_onboarding_sheet(lv_obj_t *screen)
{
    s_onboarding_sheet = lv_obj_create(screen);
    lv_obj_set_size(s_onboarding_sheet, 456, 430);
    lv_obj_set_pos(s_onboarding_sheet, 12, 25);
    lv_obj_set_style_radius(s_onboarding_sheet, 8, 0);
    lv_obj_set_style_bg_color(s_onboarding_sheet, lv_color_hex(0x071018), 0);
    lv_obj_set_style_border_color(s_onboarding_sheet, lv_color_hex(0x5EEAD4), 0);
    lv_obj_set_style_border_width(s_onboarding_sheet, 1, 0);
    lv_obj_set_style_pad_all(s_onboarding_sheet, 12, 0);
    lv_obj_clear_flag(s_onboarding_sheet, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = create_label(s_onboarding_sheet, "MeshCore DeskOS D1L", 0xF4F7FB);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(title, 8, 4);

    lv_obj_t *subtitle = create_label(s_onboarding_sheet, "First boot setup", 0x5EEAD4);
    lv_obj_set_pos(subtitle, 8, 38);

    lv_obj_t *name_label = create_label(s_onboarding_sheet, "Node name", 0x8EA0AE);
    lv_obj_set_pos(name_label, 8, 68);

    s_onboarding_name_textarea = lv_textarea_create(s_onboarding_sheet);
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

    s_onboarding_keyboard = lv_keyboard_create(s_onboarding_sheet);
    lv_obj_set_size(s_onboarding_keyboard, 424, 160);
    lv_obj_set_pos(s_onboarding_keyboard, 8, 258);
    lv_keyboard_set_textarea(s_onboarding_keyboard, s_onboarding_name_textarea);
    lv_obj_add_event_cb(s_onboarding_keyboard, onboarding_keyboard_event_cb, LV_EVENT_READY, NULL);

    if (d1l_settings_current()->onboarding_complete) {
        lv_obj_add_flag(s_onboarding_sheet, LV_OBJ_FLAG_HIDDEN);
    }
}

static void create_lock_overlay(lv_obj_t *screen)
{
    s_lock_overlay = lv_obj_create(screen);
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
    lv_obj_add_flag(s_lock_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void ui_task(void *arg)
{
    (void)arg;
    while (true) {
        uint32_t wait_ms = lv_timer_handler();
        process_pending_tab_switch();
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
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x071018), 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    create_top_bar(s_screen);

    s_content = lv_obj_create(s_screen);
    lv_obj_set_size(s_content, 480, 362);
    lv_obj_set_pos(s_content, 0, 56);
    lv_obj_set_style_bg_color(s_content, lv_color_hex(0x071018), 0);
    lv_obj_set_style_border_width(s_content, 0, 0);
    lv_obj_set_style_pad_all(s_content, 0, 0);
    lv_obj_clear_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);

    create_dock(s_screen);
    create_sheet(s_screen);
    create_compose_sheet(s_screen);
    create_public_history_sheet(s_screen);
    create_public_search_sheet(s_screen);
    create_dm_thread_sheet(s_screen);
    create_radio_settings_sheet(s_screen);
    create_storage_sheet(s_screen);
    create_contact_detail_sheet(s_screen);
    create_contact_export_sheet(s_screen);
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

    lv_init();
    d1l_health_monitor_set_lvgl_ready(true);
    const size_t buffer_pixels = 480 * 40;
    s_buf1 = heap_caps_malloc(buffer_pixels * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    s_buf2 = heap_caps_malloc(buffer_pixels * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_buf1 || !s_buf2) {
        s_buf1 = heap_caps_malloc(buffer_pixels * sizeof(lv_color_t), MALLOC_CAP_8BIT);
        s_buf2 = heap_caps_malloc(buffer_pixels * sizeof(lv_color_t), MALLOC_CAP_8BIT);
    }
    if (!s_buf1 || !s_buf2) {
        return ESP_ERR_NO_MEM;
    }

    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, s_buf2, buffer_pixels);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 480;
    disp_drv.ver_res = 480;
    disp_drv.flush_cb = flush_cb;
    disp_drv.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&disp_drv);

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
    xTaskCreatePinnedToCore(touch_poll_task, "d1l_touch", 4096, NULL, 4,
                            &s_touch_task_handle, D1L_UI_TASK_CORE);
    xTaskCreatePinnedToCore(ui_task, "d1l_ui", 4096, NULL, 5,
                            &s_ui_task_handle, D1L_UI_TASK_CORE);
    d1l_health_monitor_register_ui_task(s_ui_task_handle);
    d1l_app_model_get()->ui_ready = true;
    s_started = true;
    return ESP_OK;
}
