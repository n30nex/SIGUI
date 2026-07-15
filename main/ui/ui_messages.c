#include "ui_messages.h"

#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "ui_modal.h"

_Static_assert(sizeof(d1l_ui_messages_controller_t) <=
                   D1L_UI_MESSAGES_CONTROLLER_MAX_BYTES,
               "Messages controller exceeded its persistent-owner size budget");

static bool messages_object_is_valid(const lv_obj_t *object)
{
    return object && lv_obj_is_valid(object);
}

static void messages_advance_generation(d1l_ui_messages_controller_t *controller)
{
    controller->generation++;
    if (controller->generation == 0U) {
        controller->generation = 1U;
    }
}

static void messages_deactivate_actions(d1l_ui_messages_controller_t *controller)
{
    if (!controller) {
        return;
    }
    messages_advance_generation(controller);
    memset(controller->controls, 0, sizeof(controller->controls));
    memset(controller->public_rows, 0, sizeof(controller->public_rows));
    memset(controller->dm_rows, 0, sizeof(controller->dm_rows));
    memset(controller->thread_controls, 0, sizeof(controller->thread_controls));
    memset(controller->thread_rows, 0, sizeof(controller->thread_rows));
    controller->action_handler = NULL;
    controller->action_context = NULL;
}

static void messages_begin_actions(
    d1l_ui_messages_controller_t *controller,
    d1l_ui_messages_action_handler_t action_handler,
    void *action_context)
{
    messages_deactivate_actions(controller);
    controller->action_handler = action_handler;
    controller->action_context = action_context;
}

static lv_obj_t *messages_create_thread_sheet(lv_obj_t *parent)
{
    if (!messages_object_is_valid(parent)) {
        return NULL;
    }
    lv_obj_t *sheet = lv_obj_create(parent);
    if (!sheet) {
        return NULL;
    }
    lv_obj_set_size(sheet, 480, 424);
    lv_obj_set_pos(sheet, 0, 56);
    lv_obj_set_style_radius(sheet, 0, 0);
    lv_obj_set_style_bg_color(sheet, lv_color_hex(0x111923), 0);
    lv_obj_set_style_border_width(sheet, 0, 0);
    lv_obj_set_style_pad_all(sheet, 0, 0);
    lv_obj_clear_flag(sheet, LV_OBJ_FLAG_SCROLLABLE);
    d1l_ui_modal_hide(sheet);
    return sheet;
}

bool d1l_ui_messages_create(d1l_ui_messages_controller_t *controller,
                            lv_obj_t *parent)
{
    if (!controller) {
        return false;
    }
    messages_deactivate_actions(controller);
    if (messages_object_is_valid(controller->thread_sheet)) {
        d1l_ui_modal_hide(controller->thread_sheet);
        lv_obj_del(controller->thread_sheet);
    }
    memset(controller, 0, sizeof(*controller));
    controller->thread_limit = D1L_UI_MESSAGES_THREAD_INITIAL_ROWS;
    controller->thread_sheet = messages_create_thread_sheet(parent);
    return controller->thread_sheet != NULL;
}

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
    if (!binding || !binding->controller || binding->generation == 0U ||
        binding->generation != binding->controller->generation) {
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
    } else if (binding->action == D1L_UI_MESSAGES_ACTION_TOGGLE_DM_DETAILS) {
        if (binding->row_index >= controller->thread_row_count) {
            return;
        }
        action_event.dm_message = &controller->thread_entries[binding->row_index];
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
    if (!label) {
        lv_obj_del(button);
        return NULL;
    }
    lv_obj_center(label);
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
        .generation = controller->generation,
    };
    return binding;
}

static const char *messages_inbound_state(const d1l_dm_entry_t *entry,
                                          bool unread)
{
    if (unread) {
        return "New";
    }
    if (!entry->identity_digest_valid) {
        return "Received (legacy)";
    }
    switch (entry->ack_state) {
    case D1L_DM_ACK_STATE_PENDING:
        return "Received / ACK queued";
    case D1L_DM_ACK_STATE_SENT:
        return "Received / ACK sent";
    case D1L_DM_ACK_STATE_RETRYABLE:
        if (entry->ack_dispatch_count == 0U &&
            entry->ack_last_error == ESP_OK) {
            return "Received / ACK needed";
        }
        return "Received / ACK retry";
    case D1L_DM_ACK_STATE_TERMINAL:
        return "Received / ACK failed";
    case D1L_DM_ACK_STATE_LEGACY_UNVERIFIED:
    default:
        return "Received";
    }
}

const char *d1l_ui_messages_delivery_label(const d1l_dm_entry_t *entry,
                                           bool unread)
{
    if (!entry) {
        return "Status unavailable";
    }
    if (entry->direction[0] != 't') {
        return messages_inbound_state(entry, unread);
    }
    switch (entry->delivery_state) {
    case D1L_DM_DELIVERY_QUEUED:
        return "Queued";
    case D1L_DM_DELIVERY_WAITING_RADIO:
        return "Waiting for radio";
    case D1L_DM_DELIVERY_TX_ACTIVE:
        return "Sending";
    case D1L_DM_DELIVERY_TX_DONE:
        return "Sent over RF";
    case D1L_DM_DELIVERY_AWAITING_ACK:
        return "Sent over RF / awaiting delivery";
    case D1L_DM_DELIVERY_ACKNOWLEDGED:
        return "Delivered";
    case D1L_DM_DELIVERY_RETRY_WAIT:
        return "Retry scheduled";
    case D1L_DM_DELIVERY_RETRY_TX:
        return "Retrying";
    case D1L_DM_DELIVERY_FAILED_RADIO:
    case D1L_DM_DELIVERY_FAILED_TIMEOUT:
    case D1L_DM_DELIVERY_FAILED_QUEUE:
        return "Failed";
    case D1L_DM_DELIVERY_INTERRUPTED_BY_REBOOT:
        return "Interrupted by reboot";
    case D1L_DM_DELIVERY_CANCELLED:
        return "Cancelled";
    case D1L_DM_DELIVERY_NOT_APPLICABLE:
    default:
        return "Status unavailable";
    }
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
        .generation = controller->generation,
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
        .generation = controller->generation,
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
    messages_set_dot_width(alias, 200);
    if (alias) {
        lv_obj_set_pos(alias, 8, 4);
    }
    const char *list_state;
    if (entry->direction[0] != 't') {
        list_state = unread ? "New" : "Received";
    } else {
        switch (entry->delivery_state) {
        case D1L_DM_DELIVERY_QUEUED:
            list_state = "Queued";
            break;
        case D1L_DM_DELIVERY_WAITING_RADIO:
            list_state = "Waiting radio";
            break;
        case D1L_DM_DELIVERY_TX_ACTIVE:
            list_state = "Sending";
            break;
        case D1L_DM_DELIVERY_TX_DONE:
            list_state = "Sent RF";
            break;
        case D1L_DM_DELIVERY_AWAITING_ACK:
            list_state = "Awaiting ACK";
            break;
        case D1L_DM_DELIVERY_ACKNOWLEDGED:
            list_state = "Delivered";
            break;
        case D1L_DM_DELIVERY_RETRY_WAIT:
            list_state = "Retry waiting";
            break;
        case D1L_DM_DELIVERY_RETRY_TX:
            list_state = "Retrying";
            break;
        case D1L_DM_DELIVERY_FAILED_RADIO:
        case D1L_DM_DELIVERY_FAILED_TIMEOUT:
        case D1L_DM_DELIVERY_FAILED_QUEUE:
            list_state = "Failed";
            break;
        case D1L_DM_DELIVERY_INTERRUPTED_BY_REBOOT:
            list_state = "Interrupted";
            break;
        case D1L_DM_DELIVERY_CANCELLED:
            list_state = "Cancelled";
            break;
        case D1L_DM_DELIVERY_NOT_APPLICABLE:
        default:
            list_state = "Status unknown";
            break;
        }
    }
    lv_obj_t *state = messages_create_label(row, list_state, 0x8EA0AE);
    messages_set_dot_width(state, 168);
    if (state) {
        lv_obj_set_pos(state, 232, 4);
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
    if (!controller || !messages_object_is_valid(parent) || !view_model ||
        !action_handler) {
        return;
    }
    messages_begin_actions(controller, action_handler, action_context);
    controller->rendered = *view_model;
    if (controller->rendered.public_row_count > D1L_UI_MESSAGES_PUBLIC_PREVIEW_ROWS) {
        controller->rendered.public_row_count = D1L_UI_MESSAGES_PUBLIC_PREVIEW_ROWS;
    }
    if (controller->rendered.dm_row_count > D1L_UI_MESSAGES_DM_PREVIEW_ROWS) {
        controller->rendered.dm_row_count = D1L_UI_MESSAGES_DM_PREVIEW_ROWS;
    }
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

static d1l_ui_messages_action_binding_t *messages_bind_thread_control(
    d1l_ui_messages_controller_t *controller,
    size_t index,
    d1l_ui_messages_action_t action)
{
    if (!controller || index >= D1L_UI_MESSAGES_THREAD_CONTROL_BINDING_COUNT) {
        return NULL;
    }
    d1l_ui_messages_action_binding_t *binding =
        &controller->thread_controls[index];
    *binding = (d1l_ui_messages_action_binding_t) {
        .controller = controller,
        .action = action,
        .row_index = 0U,
        .generation = controller->generation,
    };
    return binding;
}

static lv_obj_t *messages_create_thread_body(lv_obj_t *sheet)
{
    if (!messages_object_is_valid(sheet)) {
        return NULL;
    }
    lv_obj_t *body = lv_obj_create(sheet);
    if (!body) {
        return NULL;
    }
    lv_obj_set_size(body, 448, 292);
    lv_obj_set_pos(body, 16, 60);
    lv_obj_set_style_radius(body, 8, 0);
    lv_obj_set_style_bg_color(body, lv_color_hex(0x071018), 0);
    lv_obj_set_style_border_color(body, lv_color_hex(0x263241), 0);
    lv_obj_set_style_border_width(body, 1, 0);
    lv_obj_set_style_pad_all(body, 12, 0);
    lv_obj_set_style_pad_row(body, 10, 0);
    lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    return body;
}

static lv_obj_t *messages_create_wrapped_label(lv_obj_t *parent,
                                                const char *text,
                                                uint32_t color)
{
    lv_obj_t *label = messages_create_label(parent, text ? text : "", color);
    if (!label) {
        return NULL;
    }
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_height(label, LV_SIZE_CONTENT);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    return label;
}

static bool messages_thread_entry_is_expanded(
    const d1l_ui_messages_controller_t *controller,
    const d1l_dm_entry_t *entry)
{
    if (!controller || !entry || !controller->expanded_row_valid) {
        return false;
    }
    if (entry->direction[0] == 't' && entry->delivery_session_id != 0U) {
        return controller->expanded_delivery_session_id ==
            entry->delivery_session_id;
    }
    return controller->expanded_delivery_session_id == 0U &&
        controller->expanded_row_seq == entry->seq;
}

static bool messages_delivery_failed_or_interrupted(
    d1l_dm_delivery_state_t state)
{
    return state == D1L_DM_DELIVERY_FAILED_RADIO ||
        state == D1L_DM_DELIVERY_FAILED_TIMEOUT ||
        state == D1L_DM_DELIVERY_FAILED_QUEUE ||
        state == D1L_DM_DELIVERY_INTERRUPTED_BY_REBOOT;
}

static bool messages_render_thread_bubble(
    d1l_ui_messages_controller_t *controller,
    lv_obj_t *body,
    size_t index)
{
    if (!controller || !messages_object_is_valid(body) ||
        index >= controller->thread_row_count || index >= D1L_DM_STORE_CAPACITY) {
        return false;
    }
    const d1l_dm_entry_t *entry = &controller->thread_entries[index];
    const bool outgoing = entry->direction[0] == 't';
    const bool unread = !outgoing && controller->thread_unread[index];
    const bool expanded = messages_thread_entry_is_expanded(controller, entry);
    lv_obj_t *row = lv_obj_create(body);
    if (!row) {
        return false;
    }
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        row, outgoing ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *bubble = lv_obj_create(row);
    if (!bubble) {
        return false;
    }
    lv_obj_set_width(bubble, 352);
    lv_obj_set_height(bubble, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(bubble, 10, 0);
    lv_obj_set_style_bg_color(
        bubble, lv_color_hex(outgoing ? 0x19263A : 0x122D2A), 0);
    lv_obj_set_style_border_color(
        bubble, lv_color_hex(outgoing ? 0x3B5B86 : 0x28635A), 0);
    lv_obj_set_style_border_width(bubble, 1, 0);
    lv_obj_set_style_pad_all(bubble, 10, 0);
    lv_obj_set_style_pad_row(bubble, 6, 0);
    lv_obj_set_flex_flow(bubble, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bubble, LV_OBJ_FLAG_CLICKABLE);

    d1l_ui_messages_action_binding_t *binding = &controller->thread_rows[index];
    *binding = (d1l_ui_messages_action_binding_t) {
        .controller = controller,
        .action = D1L_UI_MESSAGES_ACTION_TOGGLE_DM_DETAILS,
        .row_index = index,
        .generation = controller->generation,
    };
    lv_obj_add_event_cb(
        bubble, messages_dispatch_event_cb, LV_EVENT_CLICKED, binding);

    const char *who = outgoing ? "You" :
        (controller->thread_alias[0] ? controller->thread_alias :
         controller->thread_fingerprint);
    char heading[160];
    snprintf(heading, sizeof(heading), "%s  |  %s  |  details %s",
             who, d1l_ui_messages_delivery_label(entry, unread),
             expanded ? "v" : ">");
    if (!messages_create_wrapped_label(
            bubble, heading, unread ? 0xFBBF24 :
            (outgoing ? 0xC4B5FD : 0xA7F3D0))) {
        return false;
    }
    if (!messages_create_wrapped_label(
            bubble, entry->text[0] ? entry->text : "-", 0xF4F7FB)) {
        return false;
    }
    if (!expanded) {
        return true;
    }

    char snr[16];
    messages_format_snr(snr, sizeof(snr), entry->snr_tenths);
    char technical[256];
    snprintf(technical, sizeof(technical),
             "state %s | reason %s | error %d | revision %lu | session %llu",
             d1l_dm_delivery_state_name(entry->delivery_state),
             d1l_dm_delivery_reason_name(entry->delivery_reason),
             (int)entry->delivery_last_error,
             (unsigned long)entry->delivery_revision,
             (unsigned long long)entry->delivery_session_id);
    if (!messages_create_wrapped_label(bubble, technical, 0x8EA0AE)) {
        return false;
    }
    if (outgoing) {
        snprintf(technical, sizeof(technical),
                 "seq %lu | attempt %u | retries %u | expected ACK %08lX | "
                 "rssi %d | snr %s | path bytes %u | hops %u",
                 (unsigned long)entry->seq,
                 (unsigned)entry->attempt,
                 (unsigned)entry->delivery_retry_count,
                 (unsigned long)entry->ack_hash,
                 entry->rssi_dbm, snr,
                 (unsigned)entry->path_hash_bytes,
                 (unsigned)entry->path_hops);
    } else {
        snprintf(technical, sizeof(technical),
                 "seq %lu | ACK dispatch %s | count %u | error %d | hash %08lX | "
                 "identity %s | rssi %d | snr %s | path bytes %u | hops %u",
                 (unsigned long)entry->seq,
                 d1l_dm_ack_state_name(entry->ack_state),
                 (unsigned)entry->ack_dispatch_count,
                 (int)entry->ack_last_error,
                 (unsigned long)entry->ack_hash,
                 entry->identity_digest_valid ? "retained" : "legacy",
                 entry->rssi_dbm, snr,
                 (unsigned)entry->path_hash_bytes,
                 (unsigned)entry->path_hops);
    }
    if (!messages_create_wrapped_label(bubble, technical, 0x8EA0AE)) {
        return false;
    }
    if (outgoing && messages_delivery_failed_or_interrupted(
            entry->delivery_state) &&
        !messages_create_wrapped_label(
            bubble,
            "Retry control is not exposed yet; Reply sends a new message.",
            0xFBBF24)) {
        return false;
    }
    return true;
}

bool d1l_ui_messages_select_thread(d1l_ui_messages_controller_t *controller,
                                   const char *fingerprint,
                                   const char *alias)
{
    if (!controller || !fingerprint || fingerprint[0] == '\0' ||
        !memchr(fingerprint, '\0', sizeof(controller->thread_fingerprint))) {
        return false;
    }
    const char *display = alias && alias[0] ? alias : fingerprint;
    if (!memchr(display, '\0', sizeof(controller->thread_alias))) {
        return false;
    }
    snprintf(controller->thread_fingerprint,
             sizeof(controller->thread_fingerprint), "%s", fingerprint);
    snprintf(controller->thread_alias,
             sizeof(controller->thread_alias), "%s", display);
    controller->thread_limit = D1L_UI_MESSAGES_THREAD_INITIAL_ROWS;
    controller->thread_row_count = 0U;
    controller->thread_total_matches = 0U;
    controller->expanded_delivery_session_id = 0U;
    controller->expanded_row_seq = 0U;
    controller->expanded_row_valid = false;
    memset(controller->thread_entries, 0, sizeof(controller->thread_entries));
    memset(controller->thread_unread, 0, sizeof(controller->thread_unread));
    return true;
}

bool d1l_ui_messages_render_thread(
    d1l_ui_messages_controller_t *controller,
    d1l_ui_messages_thread_loader_t loader,
    void *loader_context,
    d1l_ui_messages_action_handler_t action_handler,
    void *action_context)
{
    if (!controller || !messages_object_is_valid(controller->thread_sheet) ||
        !loader || !action_handler || controller->thread_fingerprint[0] == '\0') {
        return false;
    }
    if (controller->thread_limit < D1L_UI_MESSAGES_THREAD_INITIAL_ROWS) {
        controller->thread_limit = D1L_UI_MESSAGES_THREAD_INITIAL_ROWS;
    }
    if (controller->thread_limit > D1L_DM_STORE_CAPACITY) {
        controller->thread_limit = D1L_DM_STORE_CAPACITY;
    }
    messages_begin_actions(controller, action_handler, action_context);
    controller->thread_total_matches = 0U;
    controller->thread_row_count = loader(
        controller->thread_fingerprint,
        controller->thread_entries,
        controller->thread_unread,
        controller->thread_limit,
        0U,
        &controller->thread_total_matches,
        loader_context);
    if (controller->thread_row_count > controller->thread_limit) {
        controller->thread_row_count = controller->thread_limit;
    }
    if (controller->thread_row_count > D1L_DM_STORE_CAPACITY) {
        controller->thread_row_count = D1L_DM_STORE_CAPACITY;
    }

    lv_obj_t *sheet = controller->thread_sheet;
    lv_obj_clean(sheet);
    bool complete = true;
    complete = messages_create_button(
        sheet, "Back", 12, 6, 72, 44,
        messages_bind_thread_control(
            controller, 0U, D1L_UI_MESSAGES_ACTION_CLOSE_DM_THREAD)) != NULL &&
        complete;
    lv_obj_t *title = messages_create_label(
        sheet, controller->thread_alias[0] ? controller->thread_alias :
        controller->thread_fingerprint, 0xF4F7FB);
    if (title) {
        lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
        messages_set_dot_width(title, 364);
        lv_obj_set_pos(title, 100, 10);
    } else {
        complete = false;
    }
    lv_obj_t *body = messages_create_thread_body(sheet);
    if (!body) {
        complete = false;
    } else {
        char meta_text[96];
        snprintf(meta_text, sizeof(meta_text), "%.16s  showing %u/%u",
                 controller->thread_fingerprint,
                 (unsigned)controller->thread_row_count,
                 (unsigned)controller->thread_total_matches);
        complete = messages_create_wrapped_label(body, meta_text, 0x8EA0AE) != NULL &&
            complete;
        if (controller->thread_row_count < controller->thread_total_matches &&
            controller->thread_limit < D1L_DM_STORE_CAPACITY) {
            lv_obj_t *older = messages_create_button(
                body, "Load Older", 0, 0, 424, 48,
                messages_bind_thread_control(
                    controller, 1U,
                    D1L_UI_MESSAGES_ACTION_LOAD_OLDER_DM_THREAD));
            if (older) {
                lv_obj_set_width(older, LV_PCT(100));
            } else {
                complete = false;
            }
        }
        if (controller->thread_row_count == 0U) {
            complete = messages_create_wrapped_label(
                body, "No retained messages in this conversation.",
                0x8EA0AE) != NULL && complete;
        }
        for (size_t i = 0U; i < controller->thread_row_count; ++i) {
            complete = messages_render_thread_bubble(controller, body, i) &&
                complete;
        }
        if (controller->thread_row_count > 0U) {
            lv_obj_update_layout(body);
            lv_obj_scroll_to_y(body, LV_COORD_MAX, LV_ANIM_OFF);
        }
    }
    complete = messages_create_button(
        sheet, "Reply", 16, 360, 448, 52,
        messages_bind_thread_control(
            controller, 2U, D1L_UI_MESSAGES_ACTION_REPLY_DM_THREAD)) != NULL &&
        complete;
    if (!complete) {
        messages_deactivate_actions(controller);
        lv_obj_clean(sheet);
        d1l_ui_modal_hide(sheet);
    }
    return complete;
}

bool d1l_ui_messages_expand_thread(d1l_ui_messages_controller_t *controller)
{
    if (!controller || controller->thread_fingerprint[0] == '\0' ||
        controller->thread_limit >= D1L_DM_STORE_CAPACITY ||
        controller->thread_row_count >= controller->thread_total_matches) {
        return false;
    }
    controller->thread_limit += D1L_UI_MESSAGES_THREAD_LOAD_OLDER_STEP;
    if (controller->thread_limit > D1L_DM_STORE_CAPACITY) {
        controller->thread_limit = D1L_DM_STORE_CAPACITY;
    }
    return true;
}

bool d1l_ui_messages_toggle_thread_details(
    d1l_ui_messages_controller_t *controller,
    const d1l_dm_entry_t *entry)
{
    if (!controller || !entry || controller->thread_fingerprint[0] == '\0') {
        return false;
    }
    const uint64_t session_id =
        entry->direction[0] == 't' ? entry->delivery_session_id : 0U;
    const uint32_t row_seq = entry->seq;
    const bool already_expanded = controller->expanded_row_valid &&
        (session_id != 0U ?
            controller->expanded_delivery_session_id == session_id :
            controller->expanded_delivery_session_id == 0U &&
            controller->expanded_row_seq == row_seq);
    controller->expanded_delivery_session_id = session_id;
    controller->expanded_row_seq = row_seq;
    controller->expanded_row_valid = !already_expanded;
    return true;
}

bool d1l_ui_messages_thread_active(
    const d1l_ui_messages_controller_t *controller)
{
    return controller && controller->thread_fingerprint[0] != '\0' &&
        messages_object_is_valid(controller->thread_sheet);
}

const char *d1l_ui_messages_thread_fingerprint(
    const d1l_ui_messages_controller_t *controller)
{
    return d1l_ui_messages_thread_active(controller) ?
        controller->thread_fingerprint : NULL;
}

lv_obj_t *d1l_ui_messages_thread_sheet(
    const d1l_ui_messages_controller_t *controller)
{
    return controller && messages_object_is_valid(controller->thread_sheet) ?
        controller->thread_sheet : NULL;
}

void d1l_ui_messages_hide_thread(d1l_ui_messages_controller_t *controller)
{
    if (!controller) {
        return;
    }
    if (controller->thread_fingerprint[0] != '\0') {
        messages_deactivate_actions(controller);
    }
    if (messages_object_is_valid(controller->thread_sheet)) {
        d1l_ui_modal_hide(controller->thread_sheet);
    }
    controller->thread_fingerprint[0] = '\0';
    controller->thread_alias[0] = '\0';
    controller->thread_limit = D1L_UI_MESSAGES_THREAD_INITIAL_ROWS;
    controller->thread_row_count = 0U;
    controller->thread_total_matches = 0U;
    controller->expanded_delivery_session_id = 0U;
    controller->expanded_row_seq = 0U;
    controller->expanded_row_valid = false;
    memset(controller->thread_entries, 0, sizeof(controller->thread_entries));
    memset(controller->thread_unread, 0, sizeof(controller->thread_unread));
}

void d1l_ui_messages_deactivate(d1l_ui_messages_controller_t *controller)
{
    if (!controller) {
        return;
    }
    const bool thread_was_active = controller->thread_fingerprint[0] != '\0';
    d1l_ui_messages_hide_thread(controller);
    if (!thread_was_active) {
        messages_deactivate_actions(controller);
    }
    memset(&controller->rendered, 0, sizeof(controller->rendered));
}
