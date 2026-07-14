#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app/app_model.h"

typedef struct _lv_obj_t lv_obj_t;

typedef struct {
    size_t node_count;
    uint32_t node_total_written;
    uint32_t room_server_count;
    uint32_t repeater_candidate_count;
    size_t contact_count;
    uint32_t contact_total_written;
    d1l_contact_entry_t contact_rows[D1L_APP_SNAPSHOT_CONTACT_PREVIEW];
    bool contact_can_dm[D1L_APP_SNAPSHOT_CONTACT_PREVIEW];
    size_t contact_row_count;
    d1l_node_view_t node_rows[D1L_NODE_STORE_CAPACITY];
    bool node_can_dm[D1L_NODE_STORE_CAPACITY];
    size_t node_row_count;
} d1l_ui_nodes_view_model_t;

typedef enum {
    D1L_UI_NODES_ACTION_NONE = 0,
    D1L_UI_NODES_ACTION_OPEN_CONTACT,
    D1L_UI_NODES_ACTION_OPEN_CONTACT_DM,
    D1L_UI_NODES_ACTION_OPEN_NODE,
    D1L_UI_NODES_ACTION_OPEN_NODE_DM,
} d1l_ui_nodes_action_t;

typedef struct {
    d1l_ui_nodes_action_t action;
    const d1l_contact_entry_t *contact;
    const d1l_node_view_t *node;
} d1l_ui_nodes_action_event_t;

typedef void (*d1l_ui_nodes_action_handler_t)(
    const d1l_ui_nodes_action_event_t *event,
    void *context);

struct d1l_ui_nodes_controller;

typedef struct {
    struct d1l_ui_nodes_controller *controller;
    size_t row_index;
} d1l_ui_nodes_action_binding_t;

typedef struct d1l_ui_nodes_controller {
    d1l_ui_nodes_view_model_t rendered;
    d1l_ui_nodes_action_handler_t action_handler;
    void *action_context;
    d1l_ui_nodes_action_binding_t contact_rows[D1L_APP_SNAPSHOT_CONTACT_PREVIEW];
    d1l_ui_nodes_action_binding_t node_rows[D1L_NODE_STORE_CAPACITY];
} d1l_ui_nodes_controller_t;

void d1l_ui_nodes_render(d1l_ui_nodes_controller_t *controller,
                         lv_obj_t *parent,
                         const d1l_ui_nodes_view_model_t *view_model,
                         d1l_ui_nodes_action_handler_t action_handler,
                         void *action_context);
void d1l_ui_nodes_deactivate(d1l_ui_nodes_controller_t *controller);
