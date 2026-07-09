#include "ui_settings.h"

#include <stdio.h>

#include "d1l_config.h"
#include "lvgl.h"
#include "ui_home.h"

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
};

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

static lv_obj_t *settings_create_panel(lv_obj_t *parent, int x, int y, int width, int height)
{
    if (!parent) {
        return NULL;
    }
    lv_obj_t *panel = lv_obj_create(parent);
    if (!panel) {
        return NULL;
    }
    lv_obj_set_size(panel, width, height);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x263241), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_pad_all(panel, 12, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
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

static lv_obj_t *render_settings_tile(lv_obj_t *parent,
                                      int x,
                                      int y,
                                      const char *title,
                                      const char *value,
                                      const char *detail,
                                      uint32_t accent,
                                      d1l_ui_settings_action_t action)
{
    lv_obj_t *tile = settings_create_panel(parent, x, y, 204, 54);
    if (!tile) {
        return NULL;
    }
    lv_obj_set_style_pad_all(tile, 8, 0);
    settings_bind_action(tile, action);

    lv_obj_t *title_label = settings_create_label(tile, title, accent);
    settings_set_dot_width(title_label, 92);
    if (title_label) {
        lv_obj_set_pos(title_label, 0, 0);
    }

    lv_obj_t *value_label = settings_create_label(tile, value, 0xF4F7FB);
    settings_set_dot_width(value_label, 96);
    if (value_label) {
        lv_obj_set_pos(value_label, 96, 0);
    }

    lv_obj_t *detail_label = settings_create_label(tile, detail, 0x8EA0AE);
    settings_set_dot_width(detail_label, 188);
    if (detail_label) {
        lv_obj_set_pos(detail_label, 0, 28);
    }
    return tile;
}

static void format_radio_profile_line(char *dest,
                                      size_t dest_size,
                                      const d1l_app_snapshot_t *snapshot)
{
    if (!dest || dest_size == 0 || !snapshot) {
        return;
    }
    snprintf(dest, dest_size, "US/CAN %.3f  BW%.1f  SF%u  CR%u",
             ((double)snapshot->radio_frequency_hz) / 1000000.0,
             ((double)snapshot->radio_bandwidth_tenths_khz) / 10.0,
             snapshot->radio_spreading_factor,
             snapshot->radio_coding_rate);
}

void d1l_ui_settings_render(lv_obj_t *parent,
                            const d1l_app_snapshot_t *snapshot,
                            d1l_ui_settings_action_handler_t action_handler)
{
    if (!parent || !snapshot) {
        return;
    }
    s_action_handler = action_handler;

    char profile_line[80];
    format_radio_profile_line(profile_line, sizeof(profile_line), snapshot);

    lv_obj_t *title = settings_create_label(parent, "Settings", 0xF4F7FB);
    if (title) {
        lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
        lv_obj_set_pos(title, 18, 16);
    }
    lv_obj_t *subtitle = settings_create_label(parent, "Setup Dashboard", 0x8EA0AE);
    settings_set_dot_width(subtitle, 260);
    if (subtitle) {
        lv_obj_set_pos(subtitle, 18, 44);
    }

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
             d1l_ui_home_sd_state(snapshot));
    render_settings_tile(parent, 18, 70, "SD Card", storage_value, storage_detail,
                         snapshot->storage_data_enabled ? 0x5EEAD4 : 0xFBBF24,
                         D1L_UI_SETTINGS_ACTION_STORAGE);

    if (snapshot->wifi_connected && snapshot->wifi_ip[0]) {
        snprintf(wifi_value, sizeof(wifi_value), "Connected %s", snapshot->wifi_ip);
    } else {
        snprintf(wifi_value, sizeof(wifi_value), "%s",
                 snapshot->wifi_state ? snapshot->wifi_state : "off");
    }
    snprintf(wifi_detail, sizeof(wifi_detail), "%s; mesh stays offline",
             snapshot->wifi_build_enabled ? "Scan/connect for tiles" : "Not in this firmware");
    render_settings_tile(parent, 238, 70, "Wi-Fi", wifi_value, wifi_detail,
                         snapshot->wifi_connected ? 0x5EEAD4 :
                         (snapshot->wifi_enabled ? 0xFBBF24 : 0x8EA0AE),
                         D1L_UI_SETTINGS_ACTION_WIFI);

    snprintf(ble_value, sizeof(ble_value), "%s",
             snapshot->ble_state ? snapshot->ble_state :
             (snapshot->ble_build_enabled ? "off" : "unavailable"));
    render_settings_tile(parent, 18, 128, "BLE", ble_value,
                         "Companion pairing gated",
                         snapshot->ble_companion_enabled ? 0xA7F3D0 : 0x8EA0AE,
                         D1L_UI_SETTINGS_ACTION_BLE);

    snprintf(radio_detail, sizeof(radio_detail), "%s; TX %d dBm, RX boost %s",
             profile_line,
             snapshot->radio_tx_power_dbm,
             snapshot->radio_rx_boost ? "on" : "off");
    render_settings_tile(parent, 238, 128, "Radio", "Mesh profile", radio_detail,
                         0x93C5FD, D1L_UI_SETTINGS_ACTION_RADIO);

    snprintf(map_value, sizeof(map_value), "%s",
             snapshot->map_tile_cache_ready ? "Tiles ready" : "Setup");
    snprintf(map_detail, sizeof(map_detail), "%s",
             snapshot->map_tile_cache_ready ? "Offline cache ready" :
             "Wi-Fi and allowed provider");
    render_settings_tile(parent, 18, 186, "Map Tiles", map_value, map_detail,
                         snapshot->map_tile_cache_ready ? 0x5EEAD4 : 0xFBBF24,
                         D1L_UI_SETTINGS_ACTION_MAP_TILES);

    render_settings_tile(parent, 238, 186, "Display", "Backlight",
                         "Brightness, night, contrast",
                         0xA7F3D0, D1L_UI_SETTINGS_ACTION_DISPLAY);

    snprintf(identity_value, sizeof(identity_value), "%s",
             snapshot->identity_ready ? "Ready" : "Not set");
    snprintf(identity_detail, sizeof(identity_detail), "Name %s; fingerprint %.16s",
             snapshot->node_name[0] ? snapshot->node_name : "D1L Desk",
             snapshot->identity_fingerprint[0] ? snapshot->identity_fingerprint : "not generated");
    render_settings_tile(parent, 18, 244, "Identity", identity_value, identity_detail,
                         snapshot->identity_ready ? 0x5EEAD4 : 0x8EA0AE,
                         D1L_UI_SETTINGS_ACTION_NONE);

    render_settings_tile(parent, 238, 244, "Diagnostics", "Health",
                         "Crashlog, exports, soak",
                         0xC4B5FD, D1L_UI_SETTINGS_ACTION_DIAGNOSTICS);

    snprintf(about_detail, sizeof(about_detail), "%s %s",
             D1L_FIRMWARE_NAME, D1L_FIRMWARE_VERSION);
    render_settings_tile(parent, 18, 302, "About",
                         snapshot->node_name[0] ? snapshot->node_name : "DeskOS D1L",
                         about_detail, 0x8EA0AE, D1L_UI_SETTINGS_ACTION_NONE);

    render_settings_tile(parent, 238, 302, "Advanced", "Hidden tools",
                         "Raw data and adverts",
                         0xFCA5A5, D1L_UI_SETTINGS_ACTION_ADVANCED);
}
