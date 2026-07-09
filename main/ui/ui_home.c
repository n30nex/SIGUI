#include "ui_home.h"

#include <stdio.h>
#include <string.h>

#include "lvgl.h"

static d1l_ui_home_action_handler_t s_action_handler;

static const d1l_ui_home_action_t k_action_values[D1L_UI_HOME_ACTION_BLE + 1] = {
    [D1L_UI_HOME_ACTION_NONE] = D1L_UI_HOME_ACTION_NONE,
    [D1L_UI_HOME_ACTION_MESSAGES_PUBLIC] = D1L_UI_HOME_ACTION_MESSAGES_PUBLIC,
    [D1L_UI_HOME_ACTION_MESSAGES_DM] = D1L_UI_HOME_ACTION_MESSAGES_DM,
    [D1L_UI_HOME_ACTION_MESH_ROLES] = D1L_UI_HOME_ACTION_MESH_ROLES,
    [D1L_UI_HOME_ACTION_NODES] = D1L_UI_HOME_ACTION_NODES,
    [D1L_UI_HOME_ACTION_ADVERTISE] = D1L_UI_HOME_ACTION_ADVERTISE,
    [D1L_UI_HOME_ACTION_MAP] = D1L_UI_HOME_ACTION_MAP,
    [D1L_UI_HOME_ACTION_DIAGNOSTICS] = D1L_UI_HOME_ACTION_DIAGNOSTICS,
    [D1L_UI_HOME_ACTION_PACKETS] = D1L_UI_HOME_ACTION_PACKETS,
    [D1L_UI_HOME_ACTION_SETTINGS] = D1L_UI_HOME_ACTION_SETTINGS,
    [D1L_UI_HOME_ACTION_STORAGE] = D1L_UI_HOME_ACTION_STORAGE,
    [D1L_UI_HOME_ACTION_WIFI] = D1L_UI_HOME_ACTION_WIFI,
    [D1L_UI_HOME_ACTION_BLE] = D1L_UI_HOME_ACTION_BLE,
};

static const d1l_ui_home_box_t k_launcher_boxes[D1L_UI_HOME_LAUNCHER_COUNT] = {
    [D1L_UI_HOME_LAUNCHER_CHATS] = {4, 0, 116, 132},
    [D1L_UI_HOME_LAUNCHER_DMS] = {122, 0, 116, 132},
    [D1L_UI_HOME_LAUNCHER_ROOMS] = {240, 0, 116, 132},
    [D1L_UI_HOME_LAUNCHER_CONTACTS] = {358, 0, 116, 132},
    [D1L_UI_HOME_LAUNCHER_REPEATERS] = {4, 140, 116, 132},
    [D1L_UI_HOME_LAUNCHER_ADVERTISE] = {122, 140, 116, 132},
    [D1L_UI_HOME_LAUNCHER_MAP] = {240, 140, 116, 132},
    [D1L_UI_HOME_LAUNCHER_TERMINAL] = {358, 140, 116, 132},
    [D1L_UI_HOME_LAUNCHER_PACKETS] = {4, 280, 116, 132},
    [D1L_UI_HOME_LAUNCHER_SETTINGS] = {122, 280, 116, 132},
    [D1L_UI_HOME_LAUNCHER_SETUP] = {240, 280, 116, 132},
    [D1L_UI_HOME_LAUNCHER_SIGNAL] = {358, 280, 116, 132},
};

static const d1l_ui_home_box_t k_status_boxes[D1L_UI_HOME_STATUS_COUNT] = {
    [D1L_UI_HOME_STATUS_TIME] = {4, 416, 116, 48},
    [D1L_UI_HOME_STATUS_WIFI] = {122, 416, 116, 48},
    [D1L_UI_HOME_STATUS_BLE] = {240, 416, 116, 48},
    [D1L_UI_HOME_STATUS_SD] = {358, 416, 116, 48},
};

const char *d1l_ui_home_sd_state(const d1l_app_snapshot_t *snapshot)
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
    const char *state = snapshot->storage_sd_state;
    if (!state || state[0] == '\0') {
        return "fallback";
    }
    if (strcmp(state, "mount_pending") == 0 || strcmp(state, "checking") == 0) {
        return "mounting";
    }
    if (strcmp(state, "mount_required") == 0) {
        return "mount";
    }
    if (strcmp(state, "no_card") == 0) {
        return "no card";
    }
    if (strcmp(state, "not_fat32_or_unmountable") == 0) {
        return "needs FAT32";
    }
    if (strcmp(state, "creating_deskos_files") == 0) {
        return "preparing";
    }
    if (strcmp(state, "deskos_manifest_invalid") == 0) {
        return "repair";
    }
    if (strcmp(state, "protocol_pending") == 0 ||
        strcmp(state, "bridge_unavailable") == 0) {
        return "offline";
    }
    if (strcmp(state, "error") == 0) {
        return "SD error";
    }
    if (strcmp(state, "fat32_ready") == 0) {
        return "FAT32 ready";
    }
    if (strcmp(state, "bridge_reported") == 0) {
        return "detected";
    }
    return state;
}

d1l_ui_home_box_t d1l_ui_home_launcher_box(d1l_ui_home_launcher_slot_t slot)
{
    const int index = (int)slot;
    if (index < 0 || index >= D1L_UI_HOME_LAUNCHER_COUNT) {
        return (d1l_ui_home_box_t){0, 0, 0, 0};
    }
    return k_launcher_boxes[index];
}

d1l_ui_home_box_t d1l_ui_home_status_box(d1l_ui_home_status_slot_t slot)
{
    const int index = (int)slot;
    if (index < 0 || index >= D1L_UI_HOME_STATUS_COUNT) {
        return (d1l_ui_home_box_t){0, 0, 0, 0};
    }
    return k_status_boxes[index];
}

static lv_obj_t *home_create_label(lv_obj_t *parent, const char *text, uint32_t color)
{
    if (!parent || !text) {
        return NULL;
    }
    lv_obj_t *label = lv_label_create(parent);
    if (!label) {
        return NULL;
    }
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

static void home_set_dot_width(lv_obj_t *label, lv_coord_t width)
{
    if (!label) {
        return;
    }
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, width);
}

static lv_obj_t *home_create_panel(lv_obj_t *parent, d1l_ui_home_box_t box)
{
    if (!parent) {
        return NULL;
    }
    lv_obj_t *panel = lv_obj_create(parent);
    if (!panel) {
        return NULL;
    }
    lv_obj_set_size(panel, box.width, box.height);
    lv_obj_set_pos(panel, box.x, box.y);
    lv_obj_set_style_radius(panel, 4, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x0F1712), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}

static void home_action_event_cb(lv_event_t *event)
{
    if (!event || !s_action_handler) {
        return;
    }
    const d1l_ui_home_action_t *action_value =
        (const d1l_ui_home_action_t *)lv_event_get_user_data(event);
    if (!action_value) {
        return;
    }
    const d1l_ui_home_action_t action = *action_value;
    if (action > D1L_UI_HOME_ACTION_NONE && action <= D1L_UI_HOME_ACTION_BLE) {
        s_action_handler(action);
    }
}

static void home_bind_action(lv_obj_t *object, d1l_ui_home_action_t action)
{
    if (!object || !s_action_handler || action <= D1L_UI_HOME_ACTION_NONE ||
        action > D1L_UI_HOME_ACTION_BLE) {
        return;
    }
    lv_obj_add_flag(object, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(object, home_action_event_cb, LV_EVENT_CLICKED,
                        (void *)&k_action_values[action]);
}

static void render_launcher_tile(lv_obj_t *parent,
                                 d1l_ui_home_launcher_slot_t slot,
                                 const char *icon,
                                 const char *title,
                                 const char *detail,
                                 uint32_t accent,
                                 d1l_ui_home_action_t action)
{
    lv_obj_t *tile = home_create_panel(parent, d1l_ui_home_launcher_box(slot));
    if (!tile) {
        return;
    }
    lv_obj_set_style_border_color(tile, lv_color_hex(0x1F372E), 0);
    lv_obj_set_style_pad_all(tile, 6, 0);
    home_bind_action(tile, action);

    lv_obj_t *icon_label = home_create_label(tile, icon, accent);
    if (icon_label) {
        lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_24, 0);
        home_set_dot_width(icon_label, 104);
        lv_obj_set_style_text_align(icon_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(icon_label, 6, 16);
    }

    lv_obj_t *title_label = home_create_label(tile, title, 0xF4F7FB);
    if (title_label) {
        home_set_dot_width(title_label, 104);
        lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(title_label, 6, 72);
    }

    lv_obj_t *detail_label = home_create_label(tile, detail, 0x8EA0AE);
    if (detail_label) {
        home_set_dot_width(detail_label, 104);
        lv_obj_set_style_text_align(detail_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(detail_label, 6, 102);
    }
}

static void render_status_icon(lv_obj_t *parent,
                               d1l_ui_home_status_slot_t slot,
                               const char *icon,
                               const char *title,
                               uint32_t accent,
                               d1l_ui_home_action_t action)
{
    lv_obj_t *chip = home_create_panel(parent, d1l_ui_home_status_box(slot));
    if (!chip) {
        return;
    }
    lv_obj_set_style_border_color(chip, lv_color_hex(accent), 0);
    lv_obj_set_style_pad_all(chip, 2, 0);
    home_bind_action(chip, action);

    lv_obj_t *icon_label = home_create_label(chip, icon, accent);
    if (icon_label) {
        lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_24, 0);
        home_set_dot_width(icon_label, 108);
        lv_obj_set_style_text_align(icon_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(icon_label, 4, 2);
    }

    lv_obj_t *title_label = home_create_label(chip, title, accent);
    if (title_label) {
        home_set_dot_width(title_label, 108);
        lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(title_label, 4, 31);
    }
}

void d1l_ui_home_render(lv_obj_t *parent,
                        const d1l_app_snapshot_t *snapshot,
                        d1l_ui_home_action_handler_t action_handler)
{
    if (!parent || !snapshot) {
        return;
    }
    s_action_handler = action_handler;

    char detail[64];
    snprintf(detail, sizeof(detail), "%lu new", (unsigned long)snapshot->public_unread_count);
    render_launcher_tile(parent, D1L_UI_HOME_LAUNCHER_CHATS, LV_SYMBOL_ENVELOPE,
                         "Chats", detail,
                         snapshot->public_unread_count ? 0xFBBF24 : 0x00C2FF,
                         D1L_UI_HOME_ACTION_MESSAGES_PUBLIC);

    snprintf(detail, sizeof(detail), "%lu new", (unsigned long)snapshot->dm_unread_count);
    render_launcher_tile(parent, D1L_UI_HOME_LAUNCHER_DMS, LV_SYMBOL_FILE, "DMs", detail,
                         snapshot->dm_unread_count ? 0xFBBF24 : 0xA7F3D0,
                         D1L_UI_HOME_ACTION_MESSAGES_DM);

    snprintf(detail, sizeof(detail), "%lu seen", (unsigned long)snapshot->recent_room_count);
    render_launcher_tile(parent, D1L_UI_HOME_LAUNCHER_ROOMS, LV_SYMBOL_DIRECTORY,
                         "Rooms", detail,
                         snapshot->recent_room_count ? 0x5EEAD4 : 0x8EA0AE,
                         D1L_UI_HOME_ACTION_MESH_ROLES);

    snprintf(detail, sizeof(detail), "%lu saved", (unsigned long)snapshot->contact_count);
    render_launcher_tile(parent, D1L_UI_HOME_LAUNCHER_CONTACTS, LV_SYMBOL_CALL,
                         "Contacts", detail,
                         snapshot->contact_count ? 0x5EEAD4 : 0x8EA0AE,
                         D1L_UI_HOME_ACTION_NODES);

    snprintf(detail, sizeof(detail), "%lu heard",
             (unsigned long)snapshot->recent_repeater_count);
    render_launcher_tile(parent, D1L_UI_HOME_LAUNCHER_REPEATERS, LV_SYMBOL_REFRESH,
                         "Repeaters", detail,
                         snapshot->recent_repeater_count ? 0xFBBF24 : 0x8EA0AE,
                         D1L_UI_HOME_ACTION_MESH_ROLES);
    render_launcher_tile(parent, D1L_UI_HOME_LAUNCHER_ADVERTISE, LV_SYMBOL_BELL,
                         "Advertise", "manual", 0x00C2FF,
                         D1L_UI_HOME_ACTION_ADVERTISE);
    render_launcher_tile(parent, D1L_UI_HOME_LAUNCHER_MAP, LV_SYMBOL_GPS, "Map",
                         snapshot->map_tile_cache_ready ? "tiles ready" : "offline",
                         snapshot->map_tile_cache_ready ? 0x5EEAD4 : 0x00C2FF,
                         D1L_UI_HOME_ACTION_MAP);
    render_launcher_tile(parent, D1L_UI_HOME_LAUNCHER_TERMINAL, LV_SYMBOL_KEYBOARD,
                         "Terminal", "diagnose", 0xC4B5FD,
                         D1L_UI_HOME_ACTION_DIAGNOSTICS);

    snprintf(detail, sizeof(detail), "%lu rows", (unsigned long)snapshot->packet_count);
    render_launcher_tile(parent, D1L_UI_HOME_LAUNCHER_PACKETS, LV_SYMBOL_LIST,
                         "Packets", detail,
                         snapshot->packet_count ? 0x5EEAD4 : 0x8EA0AE,
                         D1L_UI_HOME_ACTION_PACKETS);
    render_launcher_tile(parent, D1L_UI_HOME_LAUNCHER_SETTINGS, LV_SYMBOL_SETTINGS,
                         "Settings", "setup", 0x00C2FF,
                         D1L_UI_HOME_ACTION_SETTINGS);
    render_launcher_tile(parent, D1L_UI_HOME_LAUNCHER_SETUP, LV_SYMBOL_HOME, "Setup",
                         d1l_ui_home_sd_state(snapshot),
                         snapshot->storage_data_enabled ? 0x5EEAD4 :
                         (snapshot->storage_setup_required ? 0xFBBF24 : 0x8EA0AE),
                         D1L_UI_HOME_ACTION_STORAGE);

    if (snapshot->signal_summary.sample_count > 0U) {
        snprintf(detail, sizeof(detail), "%d dBm", snapshot->signal_summary.latest_rssi_dbm);
    } else {
        snprintf(detail, sizeof(detail), "waiting");
    }
    render_launcher_tile(parent, D1L_UI_HOME_LAUNCHER_SIGNAL, LV_SYMBOL_VOLUME_MAX,
                         "Signal", detail,
                         snapshot->signal_summary.sample_count ? 0x5EEAD4 : 0x8EA0AE,
                         D1L_UI_HOME_ACTION_MESH_ROLES);

    render_status_icon(parent, D1L_UI_HOME_STATUS_TIME, LV_SYMBOL_REFRESH, "Time",
                       snapshot->time_available ? 0x5EEAD4 : 0x00C2FF,
                       D1L_UI_HOME_ACTION_NONE);
    render_status_icon(parent, D1L_UI_HOME_STATUS_WIFI, LV_SYMBOL_WIFI, "Wi-Fi",
                       snapshot->wifi_enabled ? 0x5EEAD4 : 0x00C2FF,
                       D1L_UI_HOME_ACTION_WIFI);
    render_status_icon(parent, D1L_UI_HOME_STATUS_BLE, LV_SYMBOL_BLUETOOTH, "BLE",
                       snapshot->ble_companion_enabled ? 0xA7F3D0 : 0xC4B5FD,
                       D1L_UI_HOME_ACTION_BLE);
    render_status_icon(parent, D1L_UI_HOME_STATUS_SD, LV_SYMBOL_SD_CARD, "SD",
                       snapshot->storage_data_enabled ? 0x5EEAD4 : 0xFBBF24,
                       D1L_UI_HOME_ACTION_STORAGE);
}
