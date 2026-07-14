#include "ui_nodes.h"

#include <stdio.h>
#include <string.h>

#include "lvgl.h"

static lv_obj_t *nodes_create_label(lv_obj_t *parent, const char *text, uint32_t color)
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

static void nodes_set_dot_width(lv_obj_t *label, lv_coord_t width)
{
    if (!label) {
        return;
    }
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, width);
}

static lv_obj_t *nodes_create_panel(lv_obj_t *parent, int x, int y, int width, int height)
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

static lv_obj_t *nodes_create_button(lv_obj_t *parent,
                                     const char *text,
                                     int x,
                                     int y,
                                     int width,
                                     int height,
                                     lv_event_cb_t callback,
                                     void *user_data)
{
    if (!parent || !text) {
        return NULL;
    }
    lv_obj_t *button = lv_btn_create(parent);
    if (!button) {
        return NULL;
    }
    lv_obj_set_size(button, width, height);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x1E2A36), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x263545), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_t *label = nodes_create_label(button, text, 0xF4F7FB);
    if (label) {
        lv_obj_center(label);
    }
    if (callback) {
        lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    }
    return button;
}

static const char *nodes_role_badge_text(const char *role)
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

static uint32_t nodes_role_color(const char *role)
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

static lv_obj_t *nodes_render_role_badge(lv_obj_t *parent,
                                         const char *role,
                                         lv_coord_t x,
                                         lv_coord_t y,
                                         lv_coord_t width)
{
    lv_obj_t *badge = nodes_create_panel(parent, x, y, width, 24);
    if (!badge) {
        return NULL;
    }
    lv_obj_set_style_radius(badge, 6, 0);
    lv_obj_set_style_bg_color(badge, lv_color_hex(0x10202A), 0);
    lv_obj_set_style_border_color(badge, lv_color_hex(nodes_role_color(role)), 0);
    lv_obj_set_style_pad_all(badge, 0, 0);

    lv_obj_t *label = nodes_create_label(
        badge, nodes_role_badge_text(role), nodes_role_color(role));
    nodes_set_dot_width(label, width - 6);
    if (label) {
        lv_obj_center(label);
    }
    return badge;
}

static lv_obj_t *nodes_render_metric_card(lv_obj_t *parent,
                                          int x,
                                          int y,
                                          const char *title,
                                          const char *value,
                                          const char *detail,
                                          uint32_t accent)
{
    lv_obj_t *card = nodes_create_panel(parent, x, y, 204, 104);
    if (!card) {
        return NULL;
    }
    lv_obj_t *title_label = nodes_create_label(card, title, 0x8EA0AE);
    if (title_label) {
        lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 0);
    }
    lv_obj_t *value_label = nodes_create_label(card, value, accent);
    if (value_label) {
        lv_obj_set_style_text_font(value_label, &lv_font_montserrat_24, 0);
        lv_obj_align(value_label, LV_ALIGN_TOP_LEFT, 0, 26);
    }
    lv_obj_t *detail_label = nodes_create_label(card, detail, 0xD7E1EA);
    nodes_set_dot_width(detail_label, 176);
    if (detail_label) {
        lv_obj_align(detail_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }
    return card;
}

static void nodes_dispatch_contact_event(d1l_ui_nodes_action_binding_t *binding,
                                         d1l_ui_nodes_action_t action)
{
    if (!binding || !binding->controller) {
        return;
    }
    d1l_ui_nodes_controller_t *controller = binding->controller;
    if (!controller->action_handler ||
        binding->row_index >= controller->rendered.contact_row_count) {
        return;
    }
    if (action == D1L_UI_NODES_ACTION_OPEN_CONTACT_DM &&
        !controller->rendered.contact_can_dm[binding->row_index]) {
        return;
    }
    const d1l_ui_nodes_action_event_t action_event = {
        .action = action,
        .contact = &controller->rendered.contact_rows[binding->row_index],
        .node = NULL,
    };
    controller->action_handler(&action_event, controller->action_context);
}

static void nodes_dispatch_contact_open_event_cb(lv_event_t *event)
{
    nodes_dispatch_contact_event(
        event ? (d1l_ui_nodes_action_binding_t *)lv_event_get_user_data(event) : NULL,
        D1L_UI_NODES_ACTION_OPEN_CONTACT);
}

static void nodes_dispatch_contact_dm_event_cb(lv_event_t *event)
{
    nodes_dispatch_contact_event(
        event ? (d1l_ui_nodes_action_binding_t *)lv_event_get_user_data(event) : NULL,
        D1L_UI_NODES_ACTION_OPEN_CONTACT_DM);
}

static void nodes_dispatch_node_event(d1l_ui_nodes_action_binding_t *binding,
                                      d1l_ui_nodes_action_t action)
{
    if (!binding || !binding->controller) {
        return;
    }
    d1l_ui_nodes_controller_t *controller = binding->controller;
    if (!controller->action_handler ||
        binding->row_index >= controller->rendered.node_row_count) {
        return;
    }
    if (action == D1L_UI_NODES_ACTION_OPEN_NODE_DM &&
        !controller->rendered.node_can_dm[binding->row_index]) {
        return;
    }
    const d1l_ui_nodes_action_event_t action_event = {
        .action = action,
        .contact = NULL,
        .node = &controller->rendered.node_rows[binding->row_index],
    };
    controller->action_handler(&action_event, controller->action_context);
}

static void nodes_dispatch_node_open_event_cb(lv_event_t *event)
{
    nodes_dispatch_node_event(
        event ? (d1l_ui_nodes_action_binding_t *)lv_event_get_user_data(event) : NULL,
        D1L_UI_NODES_ACTION_OPEN_NODE);
}

static void nodes_dispatch_node_dm_event_cb(lv_event_t *event)
{
    nodes_dispatch_node_event(
        event ? (d1l_ui_nodes_action_binding_t *)lv_event_get_user_data(event) : NULL,
        D1L_UI_NODES_ACTION_OPEN_NODE_DM);
}

static void nodes_render_contact_row(d1l_ui_nodes_controller_t *controller,
                                     lv_obj_t *parent,
                                     int y,
                                     size_t index)
{
    if (!controller || !parent || index >= controller->rendered.contact_row_count) {
        return;
    }
    const d1l_contact_entry_t *entry = &controller->rendered.contact_rows[index];
    const bool can_dm = controller->rendered.contact_can_dm[index];
    lv_obj_t *row = nodes_create_panel(parent, 18, y, 424, 48);
    if (!row) {
        return;
    }
    d1l_ui_nodes_action_binding_t *binding = &controller->contact_rows[index];
    *binding = (d1l_ui_nodes_action_binding_t) {
        .controller = controller,
        .row_index = index,
    };
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, nodes_dispatch_contact_open_event_cb, LV_EVENT_CLICKED, binding);
    lv_obj_set_style_pad_all(row, 8, 0);

    lv_obj_t *alias = nodes_create_label(row, entry->alias, 0xF4F7FB);
    nodes_set_dot_width(alias, 166);
    if (alias) {
        lv_obj_align(alias, LV_ALIGN_TOP_LEFT, 0, 0);
    }
    if (can_dm) {
        nodes_create_button(row, "DM", 350, -1, 48, 34,
                            nodes_dispatch_contact_dm_event_cb, binding);
    } else {
        lv_obj_t *type = nodes_create_label(
            row, entry->type[0] ? entry->type : "unknown", 0xA7F3D0);
        if (type) {
            lv_obj_align(type, LV_ALIGN_TOP_RIGHT, 0, 0);
        }
    }
    char meta[128];
    snprintf(meta, sizeof(meta), "%.8s  %s  %s  rssi %d", entry->fingerprint,
             entry->public_key_hex[0] ? "key" : "no key",
             entry->out_path_valid ? "path" : "flood", entry->last_rssi_dbm);
    lv_obj_t *details = nodes_create_label(row, meta, 0x8EA0AE);
    nodes_set_dot_width(details, 320);
    if (details) {
        lv_obj_align(details, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }
}

static void nodes_render_node_row(d1l_ui_nodes_controller_t *controller,
                                  lv_obj_t *parent,
                                  int y,
                                  size_t index)
{
    if (!controller || !parent || index >= controller->rendered.node_row_count) {
        return;
    }
    const d1l_node_view_t *view = &controller->rendered.node_rows[index];
    const d1l_node_entry_t *entry = &view->node;
    const bool can_dm = controller->rendered.node_can_dm[index];
    lv_obj_t *row = nodes_create_panel(parent, 18, y, 424, 56);
    if (!row) {
        return;
    }
    d1l_ui_nodes_action_binding_t *binding = &controller->node_rows[index];
    *binding = (d1l_ui_nodes_action_binding_t) {
        .controller = controller,
        .row_index = index,
    };
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, nodes_dispatch_node_open_event_cb, LV_EVENT_CLICKED, binding);
    lv_obj_set_style_pad_all(row, 8, 0);

    lv_obj_t *name = nodes_create_label(
        row,
        view->display_name[0] ? view->display_name :
        (entry->name[0] ? entry->name : entry->fingerprint),
        0xF4F7FB);
    nodes_set_dot_width(name, 240);
    if (name) {
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, 0);
    }
    nodes_render_role_badge(row, view->role, can_dm ? 278 : 336, 0, can_dm ? 60 : 68);
    if (can_dm) {
        nodes_create_button(row, "DM", 350, -1, 52, 34,
                            nodes_dispatch_node_dm_event_cb, binding);
    }
    const int snr_abs = entry->snr_tenths < 0 ? -entry->snr_tenths : entry->snr_tenths;
    char meta[128];
    snprintf(meta, sizeof(meta), "%.8s  %s  %s  rssi %d  snr %s%d.%d",
             entry->fingerprint, view->keyed ? "key" : "no key",
             view->reachable ? "reachable" : "quiet", entry->rssi_dbm,
             entry->snr_tenths < 0 ? "-" : "", snr_abs / 10, snr_abs % 10);
    lv_obj_t *details = nodes_create_label(row, meta, 0x8EA0AE);
    nodes_set_dot_width(details, 392);
    if (details) {
        lv_obj_align(details, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }
}

void d1l_ui_nodes_render(d1l_ui_nodes_controller_t *controller,
                         lv_obj_t *parent,
                         const d1l_ui_nodes_view_model_t *view_model,
                         d1l_ui_nodes_action_handler_t action_handler,
                         void *action_context)
{
    if (!controller || !parent || !view_model || !action_handler) {
        return;
    }
    if (view_model != &controller->rendered) {
        controller->rendered = *view_model;
    }
    if (controller->rendered.contact_row_count > D1L_APP_SNAPSHOT_CONTACT_PREVIEW) {
        controller->rendered.contact_row_count = D1L_APP_SNAPSHOT_CONTACT_PREVIEW;
    }
    if (controller->rendered.node_row_count > D1L_NODE_STORE_CAPACITY) {
        controller->rendered.node_row_count = D1L_NODE_STORE_CAPACITY;
    }
    memset(controller->contact_rows, 0, sizeof(controller->contact_rows));
    memset(controller->node_rows, 0, sizeof(controller->node_rows));
    controller->action_handler = action_handler;
    controller->action_context = action_context;

    char value[32];
    char detail[64];
    snprintf(value, sizeof(value), "%u", (unsigned)controller->rendered.node_count);
    snprintf(detail, sizeof(detail), "rooms %lu  rpt %lu  writes %lu",
             (unsigned long)controller->rendered.room_server_count,
             (unsigned long)controller->rendered.repeater_candidate_count,
             (unsigned long)controller->rendered.node_total_written);
    nodes_render_metric_card(parent, 18, 16, "Heard Nodes", value, detail, 0x5EEAD4);
    snprintf(value, sizeof(value), "%u", (unsigned)controller->rendered.contact_count);
    snprintf(detail, sizeof(detail), "contacts  writes %lu",
             (unsigned long)controller->rendered.contact_total_written);
    nodes_render_metric_card(parent, 238, 16, "Contacts", value, detail, 0xA7F3D0);

    int y = 136;
    if (controller->rendered.contact_row_count > 0U) {
        for (size_t i = 0; i < controller->rendered.contact_row_count && y <= 190; ++i) {
            nodes_render_contact_row(controller, parent, y, i);
            y += 54;
        }
        lv_obj_t *heard = nodes_create_label(parent, "Heard", 0x8EA0AE);
        if (heard) {
            lv_obj_set_pos(heard, 26, y + 4);
        }
        y += 28;
    }
    lv_obj_t *all_heard = nodes_create_label(parent, "All Heard", 0x8EA0AE);
    if (all_heard) {
        lv_obj_set_pos(all_heard, 26, y + 4);
    }
    y += 28;
    for (size_t i = 0; i < controller->rendered.node_row_count; ++i) {
        nodes_render_node_row(controller, parent, y, i);
        y += 62;
    }
    if (controller->rendered.node_row_count == 0U) {
        lv_obj_t *empty = nodes_create_label(parent, "No heard nodes yet", 0x8EA0AE);
        if (empty) {
            lv_obj_align(empty, LV_ALIGN_TOP_MID, 0, 154);
        }
        static const char *const notes[] = {
            "Listening for signed adverts from nearby MeshCore nodes.",
            "Contacts appear separately when a retained public key exists.",
            "Signal, route, and repeater summaries update from RX packets.",
            "Scroll proof keeps this empty-state layout validated too.",
        };
        int note_y = 206;
        for (size_t i = 0; i < sizeof(notes) / sizeof(notes[0]); ++i) {
            lv_obj_t *note = nodes_create_panel(parent, 18, note_y, 424, 52);
            if (!note) {
                continue;
            }
            lv_obj_set_style_pad_all(note, 8, 0);
            lv_obj_t *text = nodes_create_label(note, notes[i], 0x8EA0AE);
            if (text) {
                lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
                lv_obj_set_width(text, 392);
                lv_obj_align(text, LV_ALIGN_LEFT_MID, 0, 0);
            }
            note_y += 58;
        }
    }
}

void d1l_ui_nodes_deactivate(d1l_ui_nodes_controller_t *controller)
{
    if (!controller) {
        return;
    }
    memset(&controller->rendered, 0, sizeof(controller->rendered));
    memset(controller->contact_rows, 0, sizeof(controller->contact_rows));
    memset(controller->node_rows, 0, sizeof(controller->node_rows));
    controller->action_handler = NULL;
    controller->action_context = NULL;
}
