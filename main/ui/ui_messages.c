#include "ui_messages.h"

#include <stdio.h>
#include <string.h>

#include "lvgl.h"

static lv_obj_t *messages_create_label(lv_obj_t *parent, const char *text, uint32_t color)
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

static void messages_set_dot_width(lv_obj_t *label, lv_coord_t width)
{
    if (!label) {
        return;
    }
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, width);
}

static lv_obj_t *messages_create_panel(lv_obj_t *parent, int x, int y, int width, int height)
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

static void messages_dispatch_event_cb(lv_event_t *event)
{
    if (!event) {
        return;
    }
    d1l_ui_messages_action_binding_t *binding =
        (d1l_ui_messages_action_binding_t *)lv_event_get_user_data(event);
    if (!binding || !binding->controller) {
        return;
    }
    d1l_ui_messages_controller_t *controller = binding->controller;
    if (!controller->action_handler) {
        return;
    }

    d1l_ui_messages_action_event_t action_event = {
        .action = binding->action,
        .public_message = NULL,
        .dm_message = NULL,
    };
    if (binding->action == D1L_UI_MESSAGES_ACTION_OPEN_PUBLIC_MESSAGE) {
        if (binding->row_index >= controller->rendered.public_row_count) {
            return;
        }
        action_event.public_message = &controller->rendered.public_rows[binding->row_index];
    } else if (binding->action == D1L_UI_MESSAGES_ACTION_OPEN_DM_THREAD) {
        if (binding->row_index >= controller->rendered.dm_row_count) {
            return;
        }
        action_event.dm_message = &controller->rendered.dm_rows[binding->row_index];
    }
    controller->action_handler(&action_event, controller->action_context);
}

static lv_obj_t *messages_create_button(lv_obj_t *parent,
                                        const char *text,
                                        int x,
                                        int y,
                                        int width,
                                        int height,
                                        d1l_ui_messages_action_binding_t *binding)
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
    lv_obj_t *label = messages_create_label(button, text, 0xF4F7FB);
    if (label) {
        lv_obj_center(label);
    }
    if (binding) {
        lv_obj_add_event_cb(button, messages_dispatch_event_cb, LV_EVENT_CLICKED, binding);
    }
    return button;
}

static d1l_ui_messages_action_binding_t *messages_bind_control(
    d1l_ui_messages_controller_t *controller,
    size_t index,
    d1l_ui_messages_action_t action)
{
    if (!controller || index >= D1L_UI_MESSAGES_CONTROL_BINDING_COUNT) {
        return NULL;
    }
    d1l_ui_messages_action_binding_t *binding = &controller->controls[index];
    *binding = (d1l_ui_messages_action_binding_t) {
        .controller = controller,
        .action = action,
        .row_index = 0U,
    };
    return binding;
}

static const char *messages_dm_row_state(const d1l_dm_entry_t *entry, bool unread)
{
    if (!entry) {
        return "unknown";
    }
    if (entry->direction[0] == 'r') {
        if (entry->identity_digest_valid) {
            return d1l_dm_ack_state_name(entry->ack_state);
        }
        return unread ? "new legacy" : "legacy";
    }
    if (unread) {
        return "new";
    }
    if (entry->acked) {
        return "acked";
    }
    return entry->direction[0] == 't' ? "sent" : "received";
}

static void messages_format_snr(char *dest, size_t dest_size, int snr_tenths)
{
    if (!dest || dest_size == 0U) {
        return;
    }
    const int absolute = snr_tenths < 0 ? -snr_tenths : snr_tenths;
    snprintf(dest, dest_size, "%s%d.%d", snr_tenths < 0 ? "-" : "",
             absolute / 10, absolute % 10);
}

static void messages_render_public_row(d1l_ui_messages_controller_t *controller,
                                       lv_obj_t *parent,
                                       int y,
                                       size_t index)
{
    if (!controller || !parent || index >= controller->rendered.public_row_count) {
        return;
    }
    const d1l_message_entry_t *entry = &controller->rendered.public_rows[index];
    lv_obj_t *row = messages_create_panel(parent, 18, y, 424, 72);
    if (!row) {
        return;
    }
    d1l_ui_messages_action_binding_t *binding = &controller->public_rows[index];
    *binding = (d1l_ui_messages_action_binding_t) {
        .controller = controller,
        .action = D1L_UI_MESSAGES_ACTION_OPEN_PUBLIC_MESSAGE,
        .row_index = index,
    };
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, messages_dispatch_event_cb, LV_EVENT_CLICKED, binding);
    lv_obj_set_style_pad_all(row, 8, 0);

    const bool unread = entry->direction[0] == 'r' &&
                        entry->seq > controller->rendered.last_public_read_seq;
    char snr[16];
    char meta[96];
    messages_format_snr(snr, sizeof(snr), entry->snr_tenths);
    lv_obj_t *author = messages_create_label(row,
                                             entry->author[0] ? entry->author : "unknown",
                                             unread ? 0xFBBF24 :
                                             (entry->direction[0] == 't' ? 0x93C5FD : 0x5EEAD4));
    messages_set_dot_width(author, 250);
    if (author) {
        lv_obj_set_pos(author, 8, 4);
    }
    lv_obj_t *state = messages_create_label(row,
                                            unread ? "new" :
                                            (entry->direction[0] == 't' ? "queued" : "received"),
                                            0x8EA0AE);
    messages_set_dot_width(state, 118);
    if (state) {
        lv_obj_set_pos(state, 292, 4);
    }
    lv_obj_t *text = messages_create_label(row, entry->text[0] ? entry->text : "-", 0xE5EDF5);
    messages_set_dot_width(text, 392);
    if (text) {
        lv_obj_set_pos(text, 8, 28);
    }
    snprintf(meta, sizeof(meta), "rssi %d  snr %s  hops %u",
             entry->rssi_dbm, snr, entry->path_hops);
    lv_obj_t *details = messages_create_label(row, meta, 0x8EA0AE);
    messages_set_dot_width(details, 392);
    if (details) {
        lv_obj_set_pos(details, 8, 50);
    }
}

static void messages_render_dm_row(d1l_ui_messages_controller_t *controller,
                                   lv_obj_t *parent,
                                   int y,
                                   size_t index)
{
    if (!controller || !parent || index >= controller->rendered.dm_row_count) {
        return;
    }
    const d1l_dm_entry_t *entry = &controller->rendered.dm_rows[index];
    const bool unread = controller->rendered.dm_row_unread[index];
    lv_obj_t *row = messages_create_panel(parent, 18, y, 424, 72);
    if (!row) {
        return;
    }
    d1l_ui_messages_action_binding_t *binding = &controller->dm_rows[index];
    *binding = (d1l_ui_messages_action_binding_t) {
        .controller = controller,
        .action = D1L_UI_MESSAGES_ACTION_OPEN_DM_THREAD,
        .row_index = index,
    };
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, messages_dispatch_event_cb, LV_EVENT_CLICKED, binding);
    lv_obj_set_style_pad_all(row, 8, 0);

    char snr[16];
    char meta[96];
    messages_format_snr(snr, sizeof(snr), entry->snr_tenths);
    lv_obj_t *alias = messages_create_label(
        row, entry->contact_alias[0] ? entry->contact_alias : entry->contact_fingerprint,
        unread ? 0xFBBF24 : (entry->direction[0] == 't' ? 0xC4B5FD : 0xA7F3D0));
    messages_set_dot_width(alias, 250);
    if (alias) {
        lv_obj_set_pos(alias, 8, 4);
    }
    lv_obj_t *state = messages_create_label(row, messages_dm_row_state(entry, unread), 0x8EA0AE);
    messages_set_dot_width(state, 118);
    if (state) {
        lv_obj_set_pos(state, 292, 4);
    }
    lv_obj_t *text = messages_create_label(row, entry->text[0] ? entry->text : "-", 0xE5EDF5);
    messages_set_dot_width(text, 392);
    if (text) {
        lv_obj_set_pos(text, 8, 28);
    }
    snprintf(meta, sizeof(meta), "rssi %d  snr %s  hops %u",
             entry->rssi_dbm, snr, entry->path_hops);
    lv_obj_t *details = messages_create_label(row, meta, 0x8EA0AE);
    messages_set_dot_width(details, 392);
    if (details) {
        lv_obj_set_pos(details, 8, 50);
    }
}

void d1l_ui_messages_render(d1l_ui_messages_controller_t *controller,
                            lv_obj_t *parent,
                            const d1l_ui_messages_view_model_t *view_model,
                            d1l_ui_messages_action_handler_t action_handler,
                            void *action_context)
{
    if (!controller || !parent || !view_model || !action_handler) {
        return;
    }
    controller->rendered = *view_model;
    if (controller->rendered.public_row_count > D1L_APP_SNAPSHOT_MESSAGE_PREVIEW) {
        controller->rendered.public_row_count = D1L_APP_SNAPSHOT_MESSAGE_PREVIEW;
    }
    if (controller->rendered.dm_row_count > D1L_APP_SNAPSHOT_DM_PREVIEW) {
        controller->rendered.dm_row_count = D1L_APP_SNAPSHOT_DM_PREVIEW;
    }
    controller->action_handler = action_handler;
    controller->action_context = action_context;

    lv_obj_t *header = messages_create_panel(parent, 18, 16, 424, 108);
    if (!header) {
        return;
    }
    lv_obj_t *title = messages_create_label(header, "Messages", 0xF4F7FB);
    if (title) {
        lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
        messages_set_dot_width(title, 170);
        lv_obj_set_pos(title, 8, 4);
    }
    char summary[128];
    snprintf(summary, sizeof(summary), "public %u new %lu  dm %u new %lu muted %lu",
             (unsigned)controller->rendered.public_total,
             (unsigned long)controller->rendered.public_unread,
             (unsigned)controller->rendered.dm_total,
             (unsigned long)controller->rendered.dm_unread,
             (unsigned long)controller->rendered.muted_dm_unread);
    lv_obj_t *meta = messages_create_label(header, summary, 0x8EA0AE);
    messages_set_dot_width(meta, 392);
    if (meta) {
        lv_obj_set_pos(meta, 8, 34);
    }

    messages_create_button(header, "Read", 8, 62, 54, 40,
                           messages_bind_control(controller, 0U,
                                                 D1L_UI_MESSAGES_ACTION_MARK_READ));
    messages_create_button(header, "Compose", 70, 62, 88, 40,
                           messages_bind_control(controller, 1U,
                                                 D1L_UI_MESSAGES_ACTION_COMPOSE_PUBLIC));
    messages_create_button(header, "History", 166, 62, 80, 40,
                           messages_bind_control(controller, 2U,
                                                 D1L_UI_MESSAGES_ACTION_OPEN_HISTORY));
    messages_create_button(header, "Test", 254, 62, 64, 40,
                           messages_bind_control(controller, 3U,
                                                 D1L_UI_MESSAGES_ACTION_SEND_PUBLIC_TEST));
    messages_create_button(parent, "Public", 18, 136, 96, 40,
                           messages_bind_control(controller, 4U,
                                                 D1L_UI_MESSAGES_ACTION_SHOW_PUBLIC));
    messages_create_button(parent, "DMs", 122, 136, 80, 40,
                           messages_bind_control(controller, 5U,
                                                 D1L_UI_MESSAGES_ACTION_SHOW_DIRECT));

    const bool show_direct = controller->rendered.mode == D1L_UI_MESSAGES_MODE_DIRECT;
    lv_obj_t *mode_label = messages_create_label(parent,
                                                 show_direct ? "DM Conversations" : "Public Channel",
                                                 show_direct ? 0xA7F3D0 : 0x5EEAD4);
    if (mode_label) {
        lv_obj_set_pos(mode_label, 26, 190);
    }

    int y = 218;
    if (show_direct) {
        for (size_t i = 0; i < controller->rendered.dm_row_count; ++i) {
            messages_render_dm_row(controller, parent, y, i);
            y += 80;
        }
        if (controller->rendered.dm_row_count == 0U) {
            lv_obj_t *empty = messages_create_label(parent, "No direct messages", 0x8EA0AE);
            if (empty) {
                lv_obj_set_pos(empty, 26, y);
            }
        }
    } else {
        for (size_t i = 0; i < controller->rendered.public_row_count; ++i) {
            messages_render_public_row(controller, parent, y, i);
            y += 80;
        }
        if (controller->rendered.public_row_count == 0U) {
            lv_obj_t *empty = messages_create_label(parent, "No Public messages", 0x8EA0AE);
            if (empty) {
                lv_obj_set_pos(empty, 26, y);
            }
        }
    }
}

void d1l_ui_messages_deactivate(d1l_ui_messages_controller_t *controller)
{
    if (!controller) {
        return;
    }
    memset(&controller->rendered, 0, sizeof(controller->rendered));
    memset(controller->controls, 0, sizeof(controller->controls));
    memset(controller->public_rows, 0, sizeof(controller->public_rows));
    memset(controller->dm_rows, 0, sizeof(controller->dm_rows));
    controller->action_handler = NULL;
    controller->action_context = NULL;
}
