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
#include "bsp_lcd.h"
#include "indev/indev.h"

static const char *TAG = "d1l_ui";
static lv_disp_draw_buf_t s_draw_buf;
static lv_color_t *s_buf1;
static lv_color_t *s_buf2;
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
static lv_obj_t *s_dm_thread_sheet;
static lv_obj_t *s_contact_detail_sheet;
static lv_obj_t *s_lock_overlay;
static uint32_t s_toast_until;
static d1l_app_snapshot_t s_snapshot;
static bool s_compose_dm;
static d1l_contact_entry_t s_compose_contact;
static d1l_contact_entry_t s_contact_detail_contact;
static char s_dm_thread_fingerprint[D1L_NODE_FINGERPRINT_LEN];
static char s_dm_thread_alias[D1L_CONTACT_ALIAS_LEN];
static const char s_contact_action_favorite[] = "favorite";
static const char s_contact_action_mute[] = "mute";

static void render_active_tab(void);
static void open_dm_compose_event_cb(lv_event_t *event);
static void open_dm_thread_event_cb(lv_event_t *event);
static void open_contact_detail_event_cb(lv_event_t *event);

typedef enum {
    D1L_UI_TAB_HOME = 0,
    D1L_UI_TAB_MESSAGES,
    D1L_UI_TAB_NODES,
    D1L_UI_TAB_PACKETS,
    D1L_UI_TAB_SETTINGS,
} d1l_ui_tab_t;

static d1l_ui_tab_t s_active_tab = D1L_UI_TAB_HOME;

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

static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    indev_data_t sample = {0};
    if (indev_get_major_value(&sample) == ESP_OK && sample.btn_val) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = sample.x;
        data->point.y = sample.y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
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
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

static lv_obj_t *create_panel(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *panel = lv_obj_create(parent);
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
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, w, h);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x1E2A36), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x263545), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(0xF4F7FB), 0);
    lv_obj_center(label);
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

static void hide_dm_thread_sheet(void)
{
    if (s_dm_thread_sheet) {
        lv_obj_add_flag(s_dm_thread_sheet, LV_OBJ_FLAG_HIDDEN);
    }
    s_dm_thread_fingerprint[0] = '\0';
    s_dm_thread_alias[0] = '\0';
}

static void hide_contact_detail_sheet(void)
{
    if (s_contact_detail_sheet) {
        lv_obj_add_flag(s_contact_detail_sheet, LV_OBJ_FLAG_HIDDEN);
    }
    memset(&s_contact_detail_contact, 0, sizeof(s_contact_detail_contact));
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

static void render_metric_card(lv_obj_t *parent, int x, int y, const char *title,
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
}

static void render_home(const d1l_app_snapshot_t *snapshot)
{
    char value[32];
    char detail[64];

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
    snprintf(detail, sizeof(detail), "adverts %lu  routes %lu",
             (unsigned long)snapshot->rx_adverts, (unsigned long)snapshot->route_count);
    render_metric_card(s_content, 18, 136, "RF Packets", value, detail, 0x93C5FD);

    snprintf(value, sizeof(value), "%luK", (unsigned long)(snapshot->heap_free / 1024U));
    snprintf(detail, sizeof(detail), "psram %luK  uptime %lum",
             (unsigned long)(snapshot->psram_free / 1024U),
             (unsigned long)(snapshot->uptime_ms / 60000U));
    render_metric_card(s_content, 238, 136, "System", value, detail, 0xC4B5FD);
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

static void render_dm_row(lv_obj_t *parent, int y, const d1l_dm_entry_t *entry)
{
    lv_obj_t *row = create_panel(parent, 18, y, 424, 54);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, open_dm_thread_event_cb, LV_EVENT_CLICKED, (void *)entry);
    lv_obj_set_style_pad_all(row, 8, 0);
    const bool unread = entry->direction[0] == 'r' && entry->seq > s_snapshot.last_dm_read_seq;
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
    hide_dm_thread_sheet();
    hide_contact_detail_sheet();
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
    hide_dm_thread_sheet();
    hide_contact_detail_sheet();
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
}

static void contact_detail_dm_event_cb(lv_event_t *event)
{
    (void)event;
    open_dm_compose_for_contact(&s_contact_detail_contact);
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

    create_button(s_contact_detail_sheet, "DM", 8, 198, 58, 42, contact_detail_dm_event_cb, NULL);
    create_button(s_contact_detail_sheet, entry->favorite ? "Unfav" : "Fav", 78, 198, 74, 42,
                  open_contact_detail_event_cb, (void *)s_contact_action_favorite);
    create_button(s_contact_detail_sheet, entry->muted ? "Unmute" : "Mute", 164, 198, 82, 42,
                  open_contact_detail_event_cb, (void *)s_contact_action_mute);
    create_button(s_contact_detail_sheet, "Close", 316, 198, 76, 42, close_contact_detail_event_cb, NULL);
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
    hide_dm_thread_sheet();
    hide_compose_sheet();
    render_contact_detail_sheet();
    if (s_contact_detail_sheet) {
        lv_obj_clear_flag(s_contact_detail_sheet, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_contact_detail_sheet);
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

static const char *dm_row_state(const d1l_dm_entry_t *entry)
{
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
    lv_obj_set_width(title, 206);
    lv_obj_set_pos(title, 8, 4);

    create_button(s_dm_thread_sheet, "Reply", 228, 0, 64, 40, reply_dm_thread_event_cb, NULL);
    create_button(s_dm_thread_sheet, "Close", 316, 0, 76, 40, close_dm_thread_event_cb, NULL);

    lv_obj_t *meta = create_label(s_dm_thread_sheet, "", 0x8EA0AE);
    label_set_fmt(meta, "%.16s  stored %u", s_dm_thread_fingerprint, (unsigned)s_snapshot.dm_count);
    lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
    lv_obj_set_width(meta, 392);
    lv_obj_set_pos(meta, 8, 42);

    int y = 68;
    size_t matches = 0;
    for (size_t i = 0; i < s_snapshot.recent_dm_count && y <= 246; ++i) {
        const d1l_dm_entry_t *entry = &s_snapshot.recent_dms[i];
        if (strcmp(entry->contact_fingerprint, s_dm_thread_fingerprint) != 0) {
            continue;
        }
        lv_obj_t *row = create_panel(s_dm_thread_sheet, 0, y, 424, 48);
        lv_obj_set_style_pad_all(row, 8, 0);
        const char *who = entry->direction[0] == 't' ? "You" : alias;
        lv_obj_t *sender = create_label(row, who, entry->direction[0] == 't' ? 0xC4B5FD : 0xA7F3D0);
        lv_label_set_long_mode(sender, LV_LABEL_LONG_DOT);
        lv_obj_set_width(sender, 190);
        lv_obj_align(sender, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_t *state = create_label(row, dm_row_state(entry), 0x8EA0AE);
        lv_obj_align(state, LV_ALIGN_TOP_RIGHT, 0, 0);
        lv_obj_t *text = create_label(row, entry->text, 0xE5EDF5);
        lv_label_set_long_mode(text, LV_LABEL_LONG_DOT);
        lv_obj_set_width(text, 392);
        lv_obj_align(text, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        y += 54;
        ++matches;
    }
    if (matches == 0) {
        lv_obj_t *empty = create_label(s_dm_thread_sheet, "No recent rows for this contact", 0x8EA0AE);
        lv_obj_align(empty, LV_ALIGN_CENTER, 0, 10);
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
    hide_compose_sheet();
    hide_contact_detail_sheet();
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
    create_button(header, "Read", 154, 10, 56, 44, mark_messages_read_event_cb, NULL);
    create_button(header, "Compose", 218, 10, 88, 44, open_compose_event_cb, NULL);
    create_button(header, "Test", 314, 10, 82, 44, public_test_event_cb, NULL);

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
        render_dm_row(s_content, y, &snapshot->recent_dms[i]);
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
    snprintf(detail, sizeof(detail), "adverts %lu  writes %lu",
             (unsigned long)snapshot->rx_adverts,
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

static void render_packets(const d1l_app_snapshot_t *snapshot)
{
    int y = 16;
    for (size_t i = 0; i < snapshot->recent_packet_count; ++i) {
        render_packet_row(s_content, y, &snapshot->recent_packets[i]);
        y += 56;
    }
    if (snapshot->recent_packet_count == 0) {
        lv_obj_t *empty = create_label(s_content, "Packet log is empty", 0x8EA0AE);
        lv_obj_align(empty, LV_ALIGN_CENTER, 0, -20);
    }
}

static void advert_zero_event_cb(lv_event_t *event)
{
    (void)event;
    hide_sheet();
    show_toast("Zero advert", d1l_app_model_request_advert(false));
}

static void advert_flood_event_cb(lv_event_t *event)
{
    (void)event;
    hide_sheet();
    show_toast("Flood advert", d1l_app_model_request_advert(true));
}

static void open_sheet_event_cb(lv_event_t *event)
{
    (void)event;
    if (s_sheet) {
        hide_dm_thread_sheet();
        hide_contact_detail_sheet();
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
    lv_obj_t *radio = create_panel(s_content, 18, 16, 424, 82);
    create_label(radio, "Canada/USA 910.525  BW62.5  SF7  CR5", 0xF4F7FB);
    lv_obj_t *path = create_label(radio, "", 0x8EA0AE);
    label_set_fmt(path, "path hash %u byte  companion %s",
                  snapshot->path_hash_bytes, snapshot->companion_ready ? "ready" : "waiting");
    lv_obj_align(path, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_t *identity = create_panel(s_content, 18, 114, 424, 82);
    create_label(identity, snapshot->node_name, 0xF4F7FB);
    lv_obj_t *id = create_label(identity, "", 0x8EA0AE);
    label_set_fmt(id, "identity %.16s", snapshot->identity_fingerprint[0] ?
                  snapshot->identity_fingerprint : "not generated");
    lv_obj_align(id, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    create_button(s_content, "Advert", 18, 218, 130, 52, open_sheet_event_cb, NULL);
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
    case D1L_UI_TAB_PACKETS:
        render_packets(&s_snapshot);
        break;
    case D1L_UI_TAB_SETTINGS:
        render_settings(&s_snapshot);
        break;
    }
}

static void dock_event_cb(lv_event_t *event)
{
    s_active_tab = (d1l_ui_tab_t)(uintptr_t)lv_event_get_user_data(event);
    hide_sheet();
    hide_compose_sheet();
    hide_dm_thread_sheet();
    hide_contact_detail_sheet();
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

    const char *labels[] = {"Home", "Msg", "Nodes", "Pkts", "Set"};
    for (int i = 0; i < 5; ++i) {
        create_button(dock, labels[i], 8 + i * 94, 5, 88, 50, dock_event_cb,
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
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
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
    create_dm_thread_sheet(s_screen);
    create_contact_detail_sheet(s_screen);
    create_toast(s_screen);
    create_lock_overlay(s_screen);

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
    xTaskCreate(ui_task, "d1l_ui", 4096, NULL, 5, NULL);
    d1l_app_model_get()->ui_ready = true;
    s_started = true;
    return ESP_OK;
}
