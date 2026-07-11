#include "ui_home.h"

#include <stdio.h>
#include <string.h>

#include "lvgl.h"

static d1l_ui_home_action_handler_t s_action_handler;

static const d1l_ui_home_action_t k_action_values[D1L_UI_HOME_ACTION_MORE + 1] = {
    [D1L_UI_HOME_ACTION_NONE] = D1L_UI_HOME_ACTION_NONE,
    [D1L_UI_HOME_ACTION_MESSAGES] = D1L_UI_HOME_ACTION_MESSAGES,
    [D1L_UI_HOME_ACTION_NETWORK] = D1L_UI_HOME_ACTION_NETWORK,
    [D1L_UI_HOME_ACTION_MAP] = D1L_UI_HOME_ACTION_MAP,
    [D1L_UI_HOME_ACTION_MORE] = D1L_UI_HOME_ACTION_MORE,
};

static const d1l_ui_home_box_t k_destination_boxes[D1L_UI_HOME_DESTINATION_COUNT] = {
    [D1L_UI_HOME_DESTINATION_MESSAGES] = {12, 0, 222, 140},
    [D1L_UI_HOME_DESTINATION_NETWORK] = {246, 0, 222, 140},
    [D1L_UI_HOME_DESTINATION_MAP] = {12, 148, 222, 140},
    [D1L_UI_HOME_DESTINATION_MORE] = {246, 148, 222, 140},
};

static const d1l_ui_home_box_t k_device_box = {12, 296, 456, 116};

static bool home_storage_text_equals(const char *value, const char *expected)
{
    return value && expected && strcmp(value, expected) == 0;
}

static bool home_storage_needs_attention(const d1l_app_snapshot_t *snapshot)
{
    if (!snapshot) {
        return false;
    }
    return snapshot->storage_retained_sd_degraded ||
           home_storage_text_equals(snapshot->storage_sd_state, "error") ||
           home_storage_text_equals(snapshot->storage_sd_state, "bridge_reported") ||
           home_storage_text_equals(snapshot->storage_setup_action,
                                    "inspect_rp2040_sd_cmd0_firmware_path") ||
           home_storage_text_equals(snapshot->storage_setup_action,
                                    "inspect_rp2040_sd_mount_error_firmware_path");
}

const char *d1l_ui_home_sd_state(const d1l_app_snapshot_t *snapshot)
{
    if (!snapshot) {
        return "unknown";
    }
    if (home_storage_needs_attention(snapshot)) {
        return "Needs attention";
    }
    const char *action = snapshot->storage_setup_action;
    if (home_storage_text_equals(action, "wait_for_storage_reconnect")) {
        return "reconnecting";
    }
    if (snapshot->storage_data_enabled || snapshot->storage_sd_data_root_ready) {
        return "ready";
    }
    if (snapshot->storage_setup_required) {
        return "setup";
    }
    if (home_storage_text_equals(action, "bridge_unavailable") ||
        home_storage_text_equals(action, "bridge_protocol_pending")) {
        return "offline";
    }
    if (home_storage_text_equals(action, "insert_card")) {
        return "no card";
    }
    if (home_storage_text_equals(action, "prepare_fat32_on_computer") ||
        home_storage_text_equals(action, "backup_reformat_fat32_on_computer")) {
        return "needs FAT32";
    }
    if (home_storage_text_equals(action, "run_storage_mount") ||
        home_storage_text_equals(action, "wait_for_storage_mount")) {
        return "mounting";
    }
    if (home_storage_text_equals(action, "retry_storage_mount") ||
        home_storage_text_equals(action, "use_nvs_fallback")) {
        return "setup";
    }
    if (home_storage_text_equals(action, "forced_nvs")) {
        return "internal";
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
    if (strcmp(state, "pending_bridge") == 0 ||
        strcmp(state, "protocol_pending") == 0 ||
        strcmp(state, "rp2040_unavailable") == 0 ||
        strcmp(state, "bridge_unavailable") == 0 ||
        strcmp(state, "unsupported") == 0) {
        return "offline";
    }
    if (strcmp(state, "fat32_ready") == 0) {
        return "FAT32 ready";
    }
    return "internal";
}

static const char *home_map_storage_state(const d1l_app_snapshot_t *snapshot)
{
    if (snapshot->storage_setup_action &&
        strcmp(snapshot->storage_setup_action, "insert_card") == 0) {
        return "Insert SD";
    }
    if (home_storage_needs_attention(snapshot)) {
        return "Check SD";
    }
    if (snapshot->storage_sd_needs_fat32) {
        return "Needs FAT32";
    }
    const char *state = d1l_ui_home_sd_state(snapshot);
    if (strcmp(state, "no card") == 0) {
        return "Insert SD";
    }
    return "SD starting";
}

d1l_ui_home_box_t d1l_ui_home_destination_box(d1l_ui_home_destination_slot_t slot)
{
    const int index = (int)slot;
    if (index < 0 || index >= D1L_UI_HOME_DESTINATION_COUNT) {
        return (d1l_ui_home_box_t){0, 0, 0, 0};
    }
    return k_destination_boxes[index];
}

d1l_ui_home_box_t d1l_ui_home_device_box(void)
{
    return k_device_box;
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
    lv_obj_set_style_radius(panel, 12, 0);
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
    if (action > D1L_UI_HOME_ACTION_NONE && action <= D1L_UI_HOME_ACTION_MORE) {
        s_action_handler(action);
    }
}

static void home_bind_action(lv_obj_t *object, d1l_ui_home_action_t action)
{
    if (!object || !s_action_handler || action <= D1L_UI_HOME_ACTION_NONE ||
        action > D1L_UI_HOME_ACTION_MORE) {
        return;
    }
    lv_obj_add_flag(object, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(object, lv_color_hex(0x17241D), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(object, lv_color_hex(0x5EEAD4), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(object, home_action_event_cb, LV_EVENT_CLICKED,
                        (void *)&k_action_values[action]);
}

static void render_destination_card(lv_obj_t *parent,
                                    d1l_ui_home_destination_slot_t slot,
                                    const char *icon,
                                    const char *title,
                                    const char *detail,
                                    const char *status,
                                    uint32_t accent,
                                    d1l_ui_home_action_t action)
{
    lv_obj_t *card = home_create_panel(parent, d1l_ui_home_destination_box(slot));
    if (!card) {
        return;
    }
    lv_obj_set_style_border_color(card, lv_color_hex(0x1F372E), 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    home_bind_action(card, action);

    lv_obj_t *icon_label = home_create_label(card, icon, accent);
    if (icon_label) {
        lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_24, 0);
        lv_obj_set_pos(icon_label, 14, 14);
    }

    lv_obj_t *title_label = home_create_label(card, title, 0xF4F7FB);
    if (title_label) {
        lv_obj_set_style_text_font(title_label, &lv_font_montserrat_24, 0);
        home_set_dot_width(title_label, 142);
        lv_obj_set_pos(title_label, 52, 16);
    }

    lv_obj_t *arrow_label = home_create_label(card, LV_SYMBOL_RIGHT, 0x8EA0AE);
    if (arrow_label) {
        lv_obj_set_pos(arrow_label, 192, 18);
    }

    lv_obj_t *detail_label = home_create_label(card, detail, 0xA7B4BE);
    if (detail_label) {
        lv_label_set_long_mode(detail_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_size(detail_label, 194, 40);
        lv_obj_set_pos(detail_label, 14, 57);
    }

    lv_obj_t *status_label = home_create_label(card, status, accent);
    if (status_label) {
        home_set_dot_width(status_label, 194);
        lv_obj_set_pos(status_label, 14, 108);
    }
}

static void render_device_status(lv_obj_t *parent, const d1l_app_snapshot_t *snapshot)
{
    lv_obj_t *card = home_create_panel(parent, d1l_ui_home_device_box());
    if (!card) {
        return;
    }
    lv_obj_set_style_border_color(card, lv_color_hex(0x28463A), 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    home_bind_action(card, D1L_UI_HOME_ACTION_MORE);

    lv_obj_t *title = home_create_label(card, "Device status", 0xF4F7FB);
    if (title) {
        lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
        lv_obj_set_pos(title, 14, 10);
    }
    lv_obj_t *hint = home_create_label(card, "Settings and support", 0x8EA0AE);
    if (hint) {
        lv_obj_set_pos(hint, 14, 34);
    }
    lv_obj_t *arrow = home_create_label(card, LV_SYMBOL_RIGHT, 0x8EA0AE);
    if (arrow) {
        lv_obj_set_pos(arrow, 426, 14);
    }

    const char *labels[] = {"Time", "Wi-Fi", "BLE", "SD"};
    const char *values[] = {
        snapshot->time_available && snapshot->time_label[0] != '\0' ?
        snapshot->time_label : "Syncing",
        snapshot->wifi_connected ? "Connected" :
        (snapshot->wifi_enabled ? "On" : "Off"),
        snapshot->ble_companion_enabled ? "On" : "Off",
        d1l_ui_home_sd_state(snapshot),
    };
    const uint32_t colors[] = {
        snapshot->time_available ? 0x5EEAD4 : 0x8EA0AE,
        snapshot->wifi_enabled ? 0x5EEAD4 : 0x8EA0AE,
        snapshot->ble_companion_enabled ? 0xA7F3D0 : 0x8EA0AE,
        home_storage_needs_attention(snapshot) ? 0xF87171 :
        (snapshot->storage_data_enabled ? 0x5EEAD4 :
            (snapshot->storage_setup_required ? 0xFBBF24 : 0x8EA0AE)),
    };

    for (int index = 0; index < 4; ++index) {
        const lv_coord_t x = (lv_coord_t)(14 + index * 110);
        lv_obj_t *label = home_create_label(card, labels[index], 0x8EA0AE);
        if (label) {
            home_set_dot_width(label, 100);
            lv_obj_set_pos(label, x, 66);
        }
        lv_obj_t *value = home_create_label(card, values[index], colors[index]);
        if (value) {
            home_set_dot_width(value, 100);
            lv_obj_set_pos(value, x, 88);
        }
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

    char status[64];
    const unsigned long unread_count =
        (unsigned long)(snapshot->public_unread_count + snapshot->dm_unread_count);
    if (unread_count > 0UL) {
        snprintf(status, sizeof(status), "%lu unread", unread_count);
    } else {
        snprintf(status, sizeof(status), "All caught up");
    }
    render_destination_card(parent, D1L_UI_HOME_DESTINATION_MESSAGES,
                            LV_SYMBOL_ENVELOPE, "Messages",
                            "Public, direct, and room conversations", status,
                            unread_count ? 0xFBBF24 : 0x5EEAD4,
                            D1L_UI_HOME_ACTION_MESSAGES);

    snprintf(status, sizeof(status), "%lu contacts | %lu nearby",
             (unsigned long)snapshot->contact_count,
             (unsigned long)snapshot->node_count);
    render_destination_card(parent, D1L_UI_HOME_DESTINATION_NETWORK,
                            LV_SYMBOL_REFRESH, "Network",
                            "Contacts, nearby nodes, and routing", status,
                            snapshot->contact_count ? 0x5EEAD4 : 0x8EA0AE,
                            D1L_UI_HOME_ACTION_NETWORK);

    const char *map_status = "Ready to open";
    uint32_t map_status_color = 0x5EEAD4;
    if (!snapshot->map_location_set) {
        map_status = "Set a location";
        map_status_color = 0xFBBF24;
    } else if (!snapshot->map_tile_cache_ready) {
        map_status = home_map_storage_state(snapshot);
        map_status_color = 0xFBBF24;
    } else if (!snapshot->wifi_connected) {
        map_status = "Needs Wi-Fi";
        map_status_color = 0xFBBF24;
    }
    render_destination_card(parent, D1L_UI_HOME_DESTINATION_MAP, LV_SYMBOL_IMAGE, "Map",
                            "Saved center and a small local map area",
                            map_status, map_status_color,
                            D1L_UI_HOME_ACTION_MAP);

    snprintf(status, sizeof(status), "%lu packet%s captured",
             (unsigned long)snapshot->packet_count,
             snapshot->packet_count == 1U ? "" : "s");
    render_destination_card(parent, D1L_UI_HOME_DESTINATION_MORE, LV_SYMBOL_SETTINGS,
                            "More", "Tools, device settings, and support", status,
                            snapshot->packet_count ? 0xC4B5FD : 0x8EA0AE,
                            D1L_UI_HOME_ACTION_MORE);

    render_device_status(parent, snapshot);
}
