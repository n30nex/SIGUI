#include "ui_settings.h"

#include <stdio.h>
#include <string.h>

#include "d1l_config.h"
#include "lvgl.h"

static d1l_ui_settings_action_handler_t s_action_handler;

static const d1l_ui_settings_action_t k_action_values[D1L_UI_SETTINGS_ACTION_COUNT] = {
    [D1L_UI_SETTINGS_ACTION_NONE] = D1L_UI_SETTINGS_ACTION_NONE,
    [D1L_UI_SETTINGS_ACTION_STORAGE] = D1L_UI_SETTINGS_ACTION_STORAGE,
    [D1L_UI_SETTINGS_ACTION_WIFI] = D1L_UI_SETTINGS_ACTION_WIFI,
    [D1L_UI_SETTINGS_ACTION_BLE] = D1L_UI_SETTINGS_ACTION_BLE,
    [D1L_UI_SETTINGS_ACTION_RADIO] = D1L_UI_SETTINGS_ACTION_RADIO,
    [D1L_UI_SETTINGS_ACTION_MAP_TILES] = D1L_UI_SETTINGS_ACTION_MAP_TILES,
    [D1L_UI_SETTINGS_ACTION_DISPLAY] = D1L_UI_SETTINGS_ACTION_DISPLAY,
    [D1L_UI_SETTINGS_ACTION_DIAGNOSTICS] = D1L_UI_SETTINGS_ACTION_DIAGNOSTICS,
    [D1L_UI_SETTINGS_ACTION_ADVANCED] = D1L_UI_SETTINGS_ACTION_ADVANCED,
    [D1L_UI_SETTINGS_ACTION_PACKETS] = D1L_UI_SETTINGS_ACTION_PACKETS,
};

typedef enum {
    SETTINGS_CATEGORY_NONE = -1,
    SETTINGS_CATEGORY_TOOLS = 0,
    SETTINGS_CATEGORY_CONNECTIONS,
    SETTINGS_CATEGORY_STORAGE_MAPS,
    SETTINGS_CATEGORY_DEVICE,
    SETTINGS_CATEGORY_SUPPORT,
    SETTINGS_CATEGORY_ADVANCED,
    SETTINGS_CATEGORY_COUNT,
} settings_category_t;

typedef struct {
    const char *title;
    const char *status;
    uint32_t accent;
    d1l_ui_settings_action_t action;
    bool warning;
} settings_menu_item_t;

static const settings_category_t k_category_values[SETTINGS_CATEGORY_COUNT] = {
    [SETTINGS_CATEGORY_TOOLS] = SETTINGS_CATEGORY_TOOLS,
    [SETTINGS_CATEGORY_CONNECTIONS] = SETTINGS_CATEGORY_CONNECTIONS,
    [SETTINGS_CATEGORY_STORAGE_MAPS] = SETTINGS_CATEGORY_STORAGE_MAPS,
    [SETTINGS_CATEGORY_DEVICE] = SETTINGS_CATEGORY_DEVICE,
    [SETTINGS_CATEGORY_SUPPORT] = SETTINGS_CATEGORY_SUPPORT,
    [SETTINGS_CATEGORY_ADVANCED] = SETTINGS_CATEGORY_ADVANCED,
};

static settings_category_t s_expanded_category = SETTINGS_CATEGORY_NONE;
static lv_obj_t *s_category_children[SETTINGS_CATEGORY_COUNT];
static lv_obj_t *s_category_chevrons[SETTINGS_CATEGORY_COUNT];
static lv_obj_t *s_menu;

static bool settings_storage_text_equals(const char *value, const char *expected)
{
    return value && expected && strcmp(value, expected) == 0;
}

static bool settings_storage_needs_attention(const d1l_app_snapshot_t *snapshot)
{
    if (!snapshot) {
        return false;
    }
    return snapshot->storage_retained_sd_degraded ||
           settings_storage_text_equals(snapshot->storage_sd_state, "error") ||
           settings_storage_text_equals(snapshot->storage_sd_state, "bridge_reported") ||
           settings_storage_text_equals(snapshot->storage_setup_action,
                                        "inspect_rp2040_sd_cmd0_firmware_path") ||
           settings_storage_text_equals(snapshot->storage_setup_action,
                                        "inspect_rp2040_sd_mount_error_firmware_path");
}

static lv_obj_t *settings_create_label(lv_obj_t *parent, const char *text, uint32_t color)
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

static void settings_set_dot_width(lv_obj_t *label, lv_coord_t width)
{
    if (!label) {
        return;
    }
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, width);
}

static lv_obj_t *settings_create_container(lv_obj_t *parent, int width)
{
    if (!parent) {
        return NULL;
    }
    lv_obj_t *container = lv_obj_create(parent);
    if (!container) {
        return NULL;
    }
    lv_obj_set_width(container, width);
    lv_obj_set_height(container, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    return container;
}

static lv_obj_t *settings_create_row(lv_obj_t *parent, int width, int height, bool warning)
{
    if (!parent) {
        return NULL;
    }
    lv_obj_t *row = lv_obj_create(parent);
    if (!row) {
        return NULL;
    }
    lv_obj_set_size(row, width, height);
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(warning ? 0x231317 : 0x111923), 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(warning ? 0x321820 : 0x182533),
                              LV_STATE_PRESSED);
    lv_obj_set_style_border_color(row, lv_color_hex(warning ? 0x7F1D1D : 0x263241), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

static void settings_action_event_cb(lv_event_t *event)
{
    if (!event || !s_action_handler) {
        return;
    }
    const d1l_ui_settings_action_t *action_value =
        (const d1l_ui_settings_action_t *)lv_event_get_user_data(event);
    if (!action_value) {
        return;
    }
    const d1l_ui_settings_action_t action = *action_value;
    if (action > D1L_UI_SETTINGS_ACTION_NONE && action < D1L_UI_SETTINGS_ACTION_COUNT) {
        s_action_handler(action);
    }
}

static void settings_bind_action(lv_obj_t *object, d1l_ui_settings_action_t action)
{
    if (!object || !s_action_handler || action <= D1L_UI_SETTINGS_ACTION_NONE ||
        action >= D1L_UI_SETTINGS_ACTION_COUNT) {
        return;
    }
    lv_obj_add_flag(object, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(object, settings_action_event_cb, LV_EVENT_CLICKED,
                        (void *)&k_action_values[action]);
}

static void settings_apply_category_state(void)
{
    for (int index = 0; index < SETTINGS_CATEGORY_COUNT; ++index) {
        const bool expanded = s_expanded_category == (settings_category_t)index;
        if (s_category_children[index]) {
            if (expanded) {
                lv_obj_clear_flag(s_category_children[index], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(s_category_children[index], LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (s_category_chevrons[index]) {
            lv_label_set_text(s_category_chevrons[index], expanded ? "v" : ">");
        }
    }

    if (s_menu) {
        lv_obj_update_layout(s_menu);
    }
}

static void settings_category_event_cb(lv_event_t *event)
{
    if (!event) {
        return;
    }
    const settings_category_t *category_value =
        (const settings_category_t *)lv_event_get_user_data(event);
    if (!category_value || *category_value < SETTINGS_CATEGORY_TOOLS ||
        *category_value >= SETTINGS_CATEGORY_COUNT) {
        return;
    }
    s_expanded_category = s_expanded_category == *category_value
        ? SETTINGS_CATEGORY_NONE
        : *category_value;
    settings_apply_category_state();
}

static lv_obj_t *render_menu_item(lv_obj_t *parent, const settings_menu_item_t *item)
{
    if (!parent || !item) {
        return NULL;
    }
    lv_obj_t *row = settings_create_row(parent, 444, 54, item->warning);
    if (!row) {
        return NULL;
    }
    settings_bind_action(row, item->action);

    lv_obj_t *title = settings_create_label(row, item->title, item->accent);
    settings_set_dot_width(title, 190);
    if (title) {
        lv_obj_set_pos(title, 28, 17);
    }

    const bool actionable = item->action > D1L_UI_SETTINGS_ACTION_NONE &&
        item->action < D1L_UI_SETTINGS_ACTION_COUNT;
    lv_obj_t *status = settings_create_label(row, item->status,
                                              item->warning ? 0xFCA5A5 : 0xAAB8C4);
    settings_set_dot_width(status, actionable ? 176 : 202);
    if (status) {
        lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(status, actionable ? 220 : 218, 17);
    }

    if (actionable) {
        lv_obj_t *chevron = settings_create_label(row, ">",
                                                   item->warning ? 0xFCA5A5 : 0x8EA0AE);
        if (chevron) {
            lv_obj_set_pos(chevron, 418, 17);
        }
    }
    return row;
}

static void render_category(lv_obj_t *menu,
                            settings_category_t category,
                            const char *title,
                            const char *summary,
                            const settings_menu_item_t *items,
                            size_t item_count,
                            bool warning)
{
    if (!menu || category < SETTINGS_CATEGORY_TOOLS || category >= SETTINGS_CATEGORY_COUNT ||
        !title || !summary || !items || item_count == 0) {
        return;
    }
    lv_obj_t *group = settings_create_container(menu, 444);
    if (!group) {
        return;
    }
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(group, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(group, 4, 0);

    lv_obj_t *category_row = settings_create_row(group, 444, 48, warning);
    if (!category_row) {
        return;
    }
    lv_obj_add_flag(category_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(category_row, settings_category_event_cb, LV_EVENT_CLICKED,
                        (void *)&k_category_values[category]);

    lv_obj_t *title_label = settings_create_label(category_row, title,
                                                   warning ? 0xFCA5A5 : 0xF4F7FB);
    settings_set_dot_width(title_label, 360);
    if (title_label) {
        lv_obj_set_pos(title_label, 16, 4);
    }
    lv_obj_t *summary_label = settings_create_label(category_row, summary,
                                                     warning ? 0xD98993 : 0x8EA0AE);
    settings_set_dot_width(summary_label, 376);
    if (summary_label) {
        lv_obj_set_pos(summary_label, 16, 25);
    }
    lv_obj_t *category_chevron = settings_create_label(category_row, ">",
                                                        warning ? 0xFCA5A5 : 0xAAB8C4);
    if (category_chevron) {
        lv_obj_set_pos(category_chevron, 418, 14);
    }
    s_category_chevrons[category] = category_chevron;

    lv_obj_t *children = settings_create_container(group, 444);
    if (!children) {
        return;
    }
    lv_obj_set_flex_flow(children, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(children, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(children, 4, 0);
    for (size_t index = 0; index < item_count; ++index) {
        render_menu_item(children, &items[index]);
    }
    s_category_children[category] = children;
}

void d1l_ui_settings_render(lv_obj_t *parent,
                            const d1l_app_snapshot_t *snapshot,
                            d1l_ui_settings_action_handler_t action_handler)
{
    if (!parent || !snapshot) {
        return;
    }
    s_action_handler = action_handler;

    for (int index = 0; index < SETTINGS_CATEGORY_COUNT; ++index) {
        s_category_children[index] = NULL;
        s_category_chevrons[index] = NULL;
    }
    s_menu = NULL;

    lv_obj_t *title = settings_create_label(parent, "More", 0xF4F7FB);
    if (title) {
        lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
        lv_obj_set_pos(title, 18, 8);
    }
    lv_obj_t *subtitle = settings_create_label(parent, "Settings and tools", 0x8EA0AE);
    settings_set_dot_width(subtitle, 260);
    if (subtitle) {
        lv_obj_set_pos(subtitle, 18, 36);
    }

    char packet_status[32];
    char about_status[48];
    snprintf(packet_status, sizeof(packet_status), "%lu saved",
             (unsigned long)snapshot->packet_count);
    snprintf(about_status, sizeof(about_status), "Version %s", D1L_FIRMWARE_VERSION);

    const char *wifi_status;
    if (!snapshot->wifi_build_enabled) {
        wifi_status = "Unavailable";
    } else if (snapshot->wifi_connected) {
        wifi_status = "Connected";
    } else if (snapshot->wifi_connecting) {
        wifi_status = "Connecting";
    } else {
        wifi_status = snapshot->wifi_enabled ? "On" : "Off";
    }

    const char *ble_status = !snapshot->ble_build_enabled ||
        !snapshot->ble_transport_supported
        ? "Unavailable"
        : (snapshot->ble_companion_enabled ? "On" : "Off");
    const char *radio_status = snapshot->radio_apply_pending
        ? "Applying"
        : ((snapshot->radio_ready || snapshot->radio_applied) ? "Ready" : "Needs setup");
    const bool storage_needs_attention = settings_storage_needs_attention(snapshot);
    const bool storage_ready = snapshot->storage_data_enabled ||
                               snapshot->storage_sd_data_root_ready;
    const char *storage_status = storage_needs_attention
        ? "Needs attention"
        : (storage_ready
            ? "Ready"
            : (snapshot->storage_setup_required
                ? "Needs setup"
                : (snapshot->storage_sd_present ? "Detected" : "Internal storage")));
    const char *map_status = snapshot->map_tile_cache_ready
        ? "Ready"
        : ((snapshot->map_tile_download_supported || snapshot->map_tile_sideload_supported)
            ? "Not set up"
            : "Unavailable");
    const char *storage_category_summary = storage_needs_attention
        ? "SD needs attention"
        : "SD card and offline maps";

    const settings_menu_item_t tools[] = {
        {"Packets", packet_status, 0x93C5FD, D1L_UI_SETTINGS_ACTION_PACKETS, false},
        {"Diagnostics", "Health & reports", 0xC4B5FD,
         D1L_UI_SETTINGS_ACTION_DIAGNOSTICS, false},
    };
    const settings_menu_item_t connections[] = {
        {"Wi-Fi", wifi_status,
         snapshot->wifi_connected ? 0x5EEAD4 : 0xF4F7FB,
         D1L_UI_SETTINGS_ACTION_WIFI, false},
        {"Bluetooth", ble_status,
         snapshot->ble_companion_enabled ? 0x5EEAD4 : 0xF4F7FB,
         D1L_UI_SETTINGS_ACTION_BLE, false},
        {"Radio", radio_status,
         snapshot->radio_ready ? 0x5EEAD4 : 0xF4F7FB,
         D1L_UI_SETTINGS_ACTION_RADIO, false},
    };
    const settings_menu_item_t storage_maps[] = {
        {"SD Card", storage_status,
         storage_needs_attention ? 0xF87171 :
            (storage_ready ? 0x5EEAD4 : 0xF4F7FB),
         D1L_UI_SETTINGS_ACTION_STORAGE, storage_needs_attention},
        {"Offline Maps", map_status,
         snapshot->map_tile_cache_ready ? 0x5EEAD4 : 0xF4F7FB,
         D1L_UI_SETTINGS_ACTION_MAP_TILES, false},
    };
    const settings_menu_item_t device[] = {
        {"Display", "Brightness & theme", 0xA7F3D0,
         D1L_UI_SETTINGS_ACTION_DISPLAY, false},
        {"Identity", snapshot->identity_ready ? "Ready" : "Not set", 0xF4F7FB,
         D1L_UI_SETTINGS_ACTION_NONE, false},
    };
    const settings_menu_item_t support[] = {
        {"About", about_status, 0xF4F7FB, D1L_UI_SETTINGS_ACTION_NONE, false},
    };
    const settings_menu_item_t advanced[] = {
        {"Mesh advertise", "Broadcast presence", 0xFCA5A5,
         D1L_UI_SETTINGS_ACTION_ADVANCED, true},
    };

    s_menu = settings_create_container(parent, 444);
    if (!s_menu) {
        return;
    }
    lv_obj_set_pos(s_menu, 18, 54);
    lv_obj_set_flex_flow(s_menu, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_menu, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_menu, 4, 0);

    render_category(s_menu, SETTINGS_CATEGORY_TOOLS, "Tools",
                    "Packets and diagnostics", tools, 2, false);
    render_category(s_menu, SETTINGS_CATEGORY_CONNECTIONS, "Connections",
                    "Wi-Fi, Bluetooth, and radio", connections, 3, false);
    render_category(s_menu, SETTINGS_CATEGORY_STORAGE_MAPS, "Storage & maps",
                    storage_category_summary, storage_maps, 2,
                    storage_needs_attention);
    render_category(s_menu, SETTINGS_CATEGORY_DEVICE, "Device",
                    "Display and identity", device, 2, false);
    render_category(s_menu, SETTINGS_CATEGORY_SUPPORT, "Support",
                    "About this device", support, 1, false);
    render_category(s_menu, SETTINGS_CATEGORY_ADVANCED, "Advanced",
                    "Developer options", advanced, 1, true);
    settings_apply_category_state();
}
