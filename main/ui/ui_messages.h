#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app/app_model.h"

typedef struct _lv_obj_t lv_obj_t;

typedef enum {
    D1L_UI_MESSAGES_MODE_PUBLIC = 0,
    D1L_UI_MESSAGES_MODE_DIRECT,
} d1l_ui_messages_mode_t;

typedef struct {
    d1l_ui_messages_mode_t mode;
    size_t public_total;
    size_t dm_total;
    uint32_t public_unread;
    uint32_t dm_unread;
    uint32_t muted_dm_unread;
    uint32_t last_public_read_seq;
    d1l_message_entry_t public_rows[D1L_APP_SNAPSHOT_MESSAGE_PREVIEW];
    size_t public_row_count;
    d1l_dm_entry_t dm_rows[D1L_APP_SNAPSHOT_DM_PREVIEW];
    bool dm_row_unread[D1L_APP_SNAPSHOT_DM_PREVIEW];
    size_t dm_row_count;
} d1l_ui_messages_view_model_t;

typedef enum {
    D1L_UI_MESSAGES_ACTION_NONE = 0,
    D1L_UI_MESSAGES_ACTION_MARK_READ,
    D1L_UI_MESSAGES_ACTION_COMPOSE_PUBLIC,
    D1L_UI_MESSAGES_ACTION_OPEN_HISTORY,
    D1L_UI_MESSAGES_ACTION_SEND_PUBLIC_TEST,
    D1L_UI_MESSAGES_ACTION_SHOW_PUBLIC,
    D1L_UI_MESSAGES_ACTION_SHOW_DIRECT,
    D1L_UI_MESSAGES_ACTION_OPEN_PUBLIC_MESSAGE,
    D1L_UI_MESSAGES_ACTION_OPEN_DM_THREAD,
} d1l_ui_messages_action_t;

typedef struct {
    d1l_ui_messages_action_t action;
    const d1l_message_entry_t *public_message;
    const d1l_dm_entry_t *dm_message;
} d1l_ui_messages_action_event_t;

typedef void (*d1l_ui_messages_action_handler_t)(
    const d1l_ui_messages_action_event_t *event,
    void *context);

struct d1l_ui_messages_controller;

typedef struct {
    struct d1l_ui_messages_controller *controller;
    d1l_ui_messages_action_t action;
    size_t row_index;
} d1l_ui_messages_action_binding_t;

#define D1L_UI_MESSAGES_CONTROL_BINDING_COUNT 6U

typedef struct d1l_ui_messages_controller {
    d1l_ui_messages_view_model_t rendered;
    d1l_ui_messages_action_handler_t action_handler;
    void *action_context;
    d1l_ui_messages_action_binding_t controls[D1L_UI_MESSAGES_CONTROL_BINDING_COUNT];
    d1l_ui_messages_action_binding_t public_rows[D1L_APP_SNAPSHOT_MESSAGE_PREVIEW];
    d1l_ui_messages_action_binding_t dm_rows[D1L_APP_SNAPSHOT_DM_PREVIEW];
} d1l_ui_messages_controller_t;

void d1l_ui_messages_render(d1l_ui_messages_controller_t *controller,
                            lv_obj_t *parent,
                            const d1l_ui_messages_view_model_t *view_model,
                            d1l_ui_messages_action_handler_t action_handler,
                            void *action_context);
void d1l_ui_messages_deactivate(d1l_ui_messages_controller_t *controller);
