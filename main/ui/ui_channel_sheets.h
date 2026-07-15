#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mesh/channel_store.h"

typedef struct _lv_obj_t lv_obj_t;

#define D1L_UI_CHANNEL_SHEETS_BINDING_COUNT 19U
#define D1L_UI_CHANNEL_SHEETS_CONTROLLER_MAX_BYTES 1024U

typedef enum {
    D1L_UI_CHANNEL_ACTION_NONE = 0,
    D1L_UI_CHANNEL_ACTION_CANCEL_CREATE,
    D1L_UI_CHANNEL_ACTION_SUBMIT_CREATE,
    D1L_UI_CHANNEL_ACTION_CANCEL_IMPORT,
    D1L_UI_CHANNEL_ACTION_SUBMIT_IMPORT,
    D1L_UI_CHANNEL_ACTION_CLOSE_OPTIONS,
    D1L_UI_CHANNEL_ACTION_OPEN_EDIT,
    D1L_UI_CHANNEL_ACTION_CANCEL_EDIT,
    D1L_UI_CHANNEL_ACTION_SUBMIT_EDIT,
    D1L_UI_CHANNEL_ACTION_TOGGLE_ENABLED,
    D1L_UI_CHANNEL_ACTION_MAKE_DEFAULT,
    D1L_UI_CHANNEL_ACTION_OPEN_EXPORT,
    D1L_UI_CHANNEL_ACTION_CLOSE_EXPORT,
    D1L_UI_CHANNEL_ACTION_OPEN_REMOVE,
    D1L_UI_CHANNEL_ACTION_CANCEL_REMOVE,
    D1L_UI_CHANNEL_ACTION_CONFIRM_REMOVE,
} d1l_ui_channel_action_t;

/* text is owned by the controller and is valid only for the synchronous
 * handler call. channel is always redacted d1l_channel_info_t metadata. */
typedef struct {
    d1l_ui_channel_action_t action;
    const d1l_channel_info_t *channel;
    const char *text;
} d1l_ui_channel_action_event_t;

typedef void (*d1l_ui_channel_action_handler_t)(
    const d1l_ui_channel_action_event_t *event,
    void *context);

struct d1l_ui_channel_sheets_controller;

typedef struct {
    struct d1l_ui_channel_sheets_controller *controller;
    d1l_ui_channel_action_t action;
    uint32_t generation;
} d1l_ui_channel_binding_t;

typedef struct d1l_ui_channel_sheets_controller {
    lv_obj_t *parent;
    lv_obj_t *create_sheet;
    lv_obj_t *create_textarea;
    lv_obj_t *create_keyboard;
    lv_obj_t *import_sheet;
    lv_obj_t *import_textarea;
    lv_obj_t *import_keyboard;
    lv_obj_t *options_sheet;
    lv_obj_t *edit_sheet;
    lv_obj_t *edit_textarea;
    lv_obj_t *edit_keyboard;
    lv_obj_t *export_sheet;
    lv_obj_t *remove_sheet;
    d1l_channel_info_t selected;
    char export_uri[D1L_CHANNEL_SHARE_URI_LEN];
    d1l_ui_channel_action_handler_t action_handler;
    void *action_context;
    d1l_ui_channel_binding_t
        bindings[D1L_UI_CHANNEL_SHEETS_BINDING_COUNT];
    uint32_t generation;
} d1l_ui_channel_sheets_controller_t;

bool d1l_ui_channel_sheets_create(
    d1l_ui_channel_sheets_controller_t *controller,
    lv_obj_t *parent);
bool d1l_ui_channel_sheets_set_channel(
    d1l_ui_channel_sheets_controller_t *controller,
    const d1l_channel_info_t *channel);
const d1l_channel_info_t *d1l_ui_channel_sheets_channel(
    const d1l_ui_channel_sheets_controller_t *controller);

/* This is the controller's only secret-bearing input. It is bounded, never
 * exposed through a getter, and volatile-zeroed on every failure and after
 * the one-shot QR renderer consumes it. */
bool d1l_ui_channel_sheets_set_export_uri(
    d1l_ui_channel_sheets_controller_t *controller,
    const char *uri);
void d1l_ui_channel_sheets_clear_export_uri(
    d1l_ui_channel_sheets_controller_t *controller);

const char *d1l_ui_channel_sheets_create_text(
    const d1l_ui_channel_sheets_controller_t *controller);
const char *d1l_ui_channel_sheets_import_text(
    const d1l_ui_channel_sheets_controller_t *controller);
const char *d1l_ui_channel_sheets_edit_text(
    const d1l_ui_channel_sheets_controller_t *controller);

bool d1l_ui_channel_sheets_render_create(
    d1l_ui_channel_sheets_controller_t *controller,
    d1l_ui_channel_action_handler_t action_handler,
    void *action_context);
bool d1l_ui_channel_sheets_render_import(
    d1l_ui_channel_sheets_controller_t *controller,
    d1l_ui_channel_action_handler_t action_handler,
    void *action_context);
bool d1l_ui_channel_sheets_render_options(
    d1l_ui_channel_sheets_controller_t *controller,
    d1l_ui_channel_action_handler_t action_handler,
    void *action_context);
bool d1l_ui_channel_sheets_render_edit(
    d1l_ui_channel_sheets_controller_t *controller,
    d1l_ui_channel_action_handler_t action_handler,
    void *action_context);
bool d1l_ui_channel_sheets_render_export(
    d1l_ui_channel_sheets_controller_t *controller,
    d1l_ui_channel_action_handler_t action_handler,
    void *action_context);
bool d1l_ui_channel_sheets_render_remove(
    d1l_ui_channel_sheets_controller_t *controller,
    d1l_ui_channel_action_handler_t action_handler,
    void *action_context);

void d1l_ui_channel_sheets_hide_all(
    d1l_ui_channel_sheets_controller_t *controller,
    bool clear_channel);
void d1l_ui_channel_sheets_hide_create(
    d1l_ui_channel_sheets_controller_t *controller);
void d1l_ui_channel_sheets_hide_import(
    d1l_ui_channel_sheets_controller_t *controller);
void d1l_ui_channel_sheets_hide_options(
    d1l_ui_channel_sheets_controller_t *controller);
void d1l_ui_channel_sheets_hide_edit(
    d1l_ui_channel_sheets_controller_t *controller);
void d1l_ui_channel_sheets_hide_export(
    d1l_ui_channel_sheets_controller_t *controller);
void d1l_ui_channel_sheets_hide_remove(
    d1l_ui_channel_sheets_controller_t *controller);

lv_obj_t *d1l_ui_channel_sheets_create_sheet(
    const d1l_ui_channel_sheets_controller_t *controller);
lv_obj_t *d1l_ui_channel_sheets_create_textarea(
    const d1l_ui_channel_sheets_controller_t *controller);
lv_obj_t *d1l_ui_channel_sheets_create_keyboard(
    const d1l_ui_channel_sheets_controller_t *controller);
lv_obj_t *d1l_ui_channel_sheets_import_sheet(
    const d1l_ui_channel_sheets_controller_t *controller);
lv_obj_t *d1l_ui_channel_sheets_import_textarea(
    const d1l_ui_channel_sheets_controller_t *controller);
lv_obj_t *d1l_ui_channel_sheets_import_keyboard(
    const d1l_ui_channel_sheets_controller_t *controller);
lv_obj_t *d1l_ui_channel_sheets_options(
    const d1l_ui_channel_sheets_controller_t *controller);
lv_obj_t *d1l_ui_channel_sheets_edit(
    const d1l_ui_channel_sheets_controller_t *controller);
lv_obj_t *d1l_ui_channel_sheets_edit_textarea(
    const d1l_ui_channel_sheets_controller_t *controller);
lv_obj_t *d1l_ui_channel_sheets_edit_keyboard(
    const d1l_ui_channel_sheets_controller_t *controller);
lv_obj_t *d1l_ui_channel_sheets_export(
    const d1l_ui_channel_sheets_controller_t *controller);
lv_obj_t *d1l_ui_channel_sheets_remove(
    const d1l_ui_channel_sheets_controller_t *controller);
