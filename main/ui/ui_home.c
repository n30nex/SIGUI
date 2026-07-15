#include "ui_home.h"

#include <string.h>

#include "lvgl.h"

static const d1l_ui_home_box_t k_destination_boxes[D1L_UI_HOME_DESTINATION_COUNT] = {
    [D1L_UI_HOME_DESTINATION_MESSAGES] = {12, 0, 222, 140},
    [D1L_UI_HOME_DESTINATION_NETWORK] = {246, 0, 222, 140},
    [D1L_UI_HOME_DESTINATION_MAP] = {12, 148, 222, 140},
    [D1L_UI_HOME_DESTINATION_MORE] = {246, 148, 222, 140},
};

static const d1l_ui_home_box_t k_device_box = {12, 296, 456, 116};


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
    if (!event) {
        return;
    }
    d1l_ui_home_action_binding_t *binding =
        (d1l_ui_home_action_binding_t *)lv_event_get_user_data(event);
    if (!binding || !binding->controller ||
        !binding->controller->action_handler) {
        return;
    }
    const d1l_ui_home_action_t action = binding->action;
    if (action > D1L_UI_HOME_ACTION_NONE && action <= D1L_UI_HOME_ACTION_MORE) {
        binding->controller->action_handler(
            action, binding->controller->action_context);
    }
}

static void home_bind_action(lv_obj_t *object,
                             d1l_ui_home_action_binding_t *binding,
                             d1l_ui_home_controller_t *controller,
                             d1l_ui_home_action_t action)
{
    if (!object || !binding || !controller || !controller->action_handler ||
        action <= D1L_UI_HOME_ACTION_NONE || action > D1L_UI_HOME_ACTION_MORE) {
        return;
    }
    binding->controller = controller;
    binding->action = action;
    lv_obj_add_flag(object, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(object, lv_color_hex(0x17241D), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(object, lv_color_hex(0x5EEAD4), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(object, home_action_event_cb, LV_EVENT_CLICKED, binding);
}

static void render_destination_card(lv_obj_t *parent,
                                    d1l_ui_home_destination_slot_t slot,
                                    const char *icon,
                                    const char *title,
                                    const char *detail,
                                    const char *status,
                                    uint32_t accent,
                                    d1l_ui_home_action_t action,
                                    d1l_ui_home_action_binding_t *binding,
                                    d1l_ui_home_controller_t *controller)
{
    lv_obj_t *card = home_create_panel(parent, d1l_ui_home_destination_box(slot));
    if (!card) {
        return;
    }
    lv_obj_set_style_border_color(card, lv_color_hex(0x1F372E), 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    home_bind_action(card, binding, controller, action);

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

static void render_device_status(lv_obj_t *parent,
                                 const d1l_ui_home_view_model_t *view_model,
                                 d1l_ui_home_action_binding_t *binding,
                                 d1l_ui_home_controller_t *controller)
{
    lv_obj_t *card = home_create_panel(parent, d1l_ui_home_device_box());
    if (!card) {
        return;
    }
    lv_obj_set_style_border_color(card, lv_color_hex(0x28463A), 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    home_bind_action(card, binding, controller, D1L_UI_HOME_ACTION_MORE);

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
        view_model->time_value,
        view_model->wifi_value,
        view_model->ble_value,
        view_model->sd_value,
    };
    const uint32_t colors[] = {
        view_model->time_value_color,
        view_model->wifi_value_color,
        view_model->ble_value_color,
        view_model->sd_value_color,
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

void d1l_ui_home_render(d1l_ui_home_controller_t *controller,
                        lv_obj_t *parent,
                        const d1l_ui_home_view_model_t *view_model,
                        d1l_ui_home_action_handler_t action_handler,
                        void *action_context)
{
    if (!controller || !parent || !view_model) {
        return;
    }
    controller->rendered = *view_model;
    controller->action_handler = action_handler;
    controller->action_context = action_context;
    memset(controller->bindings, 0, sizeof(controller->bindings));

    render_destination_card(parent, D1L_UI_HOME_DESTINATION_MESSAGES,
                            LV_SYMBOL_ENVELOPE, "Messages",
                            "Public, direct, and room conversations",
                            controller->rendered.messages_status,
                            controller->rendered.messages_status_color,
                            D1L_UI_HOME_ACTION_MESSAGES,
                            &controller->bindings[D1L_UI_HOME_DESTINATION_MESSAGES],
                            controller);

    render_destination_card(parent, D1L_UI_HOME_DESTINATION_NETWORK,
                            LV_SYMBOL_REFRESH, "Network",
                            "Contacts, nearby nodes, and routing",
                            controller->rendered.network_status,
                            controller->rendered.network_status_color,
                            D1L_UI_HOME_ACTION_NETWORK,
                            &controller->bindings[D1L_UI_HOME_DESTINATION_NETWORK],
                            controller);

    render_destination_card(parent, D1L_UI_HOME_DESTINATION_MAP, LV_SYMBOL_IMAGE, "Map",
                            "Saved center and a small local map area",
                            controller->rendered.map_status,
                            controller->rendered.map_status_color,
                            D1L_UI_HOME_ACTION_MAP,
                            &controller->bindings[D1L_UI_HOME_DESTINATION_MAP],
                            controller);

    render_destination_card(parent, D1L_UI_HOME_DESTINATION_MORE, LV_SYMBOL_SETTINGS,
                            "More", "Tools, device settings, and support",
                            controller->rendered.more_status,
                            controller->rendered.more_status_color,
                            D1L_UI_HOME_ACTION_MORE,
                            &controller->bindings[D1L_UI_HOME_DESTINATION_MORE],
                            controller);

    render_device_status(parent, &controller->rendered,
                         &controller->bindings[D1L_UI_HOME_DESTINATION_COUNT],
                         controller);
}

void d1l_ui_home_deactivate(d1l_ui_home_controller_t *controller)
{
    if (!controller) {
        return;
    }
    controller->action_handler = NULL;
    controller->action_context = NULL;
    memset(controller->bindings, 0, sizeof(controller->bindings));
}
