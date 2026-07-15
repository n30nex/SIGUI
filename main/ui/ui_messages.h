#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mesh/dm_store.h"
#include "mesh/message_store.h"

typedef struct _lv_obj_t lv_obj_t;

#define D1L_UI_MESSAGES_PUBLIC_PREVIEW_ROWS 5U
#define D1L_UI_MESSAGES_DM_PREVIEW_ROWS 5U

typedef enum {
    D1L_UI_MESSAGES_MODE_ROOT = 0,
    D1L_UI_MESSAGES_MODE_PUBLIC,
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
    d1l_message_entry_t public_rows[D1L_UI_MESSAGES_PUBLIC_PREVIEW_ROWS];
    size_t public_row_count;
    d1l_dm_entry_t dm_rows[D1L_UI_MESSAGES_DM_PREVIEW_ROWS];
    bool dm_row_unread[D1L_UI_MESSAGES_DM_PREVIEW_ROWS];
    uint32_t dm_row_unread_count[D1L_UI_MESSAGES_DM_PREVIEW_ROWS];
    bool dm_row_muted[D1L_UI_MESSAGES_DM_PREVIEW_ROWS];
    size_t dm_row_count;
} d1l_ui_messages_view_model_t;

typedef enum {
    D1L_UI_MESSAGES_ACTION_NONE = 0,
    D1L_UI_MESSAGES_ACTION_MARK_PUBLIC_READ,
    D1L_UI_MESSAGES_ACTION_COMPOSE_PUBLIC,
    D1L_UI_MESSAGES_ACTION_OPEN_HISTORY,
    D1L_UI_MESSAGES_ACTION_SHOW_ROOT,
    D1L_UI_MESSAGES_ACTION_SHOW_PUBLIC,
    D1L_UI_MESSAGES_ACTION_SHOW_DIRECT,
    D1L_UI_MESSAGES_ACTION_OPEN_PUBLIC_MESSAGE,
    D1L_UI_MESSAGES_ACTION_OPEN_DM_THREAD,
    D1L_UI_MESSAGES_ACTION_CLOSE_DM_THREAD,
    D1L_UI_MESSAGES_ACTION_LOAD_OLDER_DM_THREAD,
    D1L_UI_MESSAGES_ACTION_REPLY_DM_THREAD,
    D1L_UI_MESSAGES_ACTION_TOGGLE_DM_DETAILS,
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
    uint32_t generation;
} d1l_ui_messages_action_binding_t;

#define D1L_UI_MESSAGES_CONTROL_BINDING_COUNT 6U
#define D1L_UI_MESSAGES_THREAD_CONTROL_BINDING_COUNT 3U
#define D1L_UI_MESSAGES_THREAD_INITIAL_ROWS 5U
#define D1L_UI_MESSAGES_THREAD_LOAD_OLDER_STEP 5U
#define D1L_UI_MESSAGES_CONTROLLER_MAX_BYTES 16384U

typedef size_t (*d1l_ui_messages_thread_loader_t)(
    const char *contact_fingerprint,
    d1l_dm_entry_t *out_entries,
    bool *out_unread,
    size_t max_entries,
    size_t skip_newest,
    size_t *out_total_matches,
    void *context);

typedef struct d1l_ui_messages_controller {
    lv_obj_t *thread_sheet;
    d1l_ui_messages_view_model_t rendered;
    d1l_ui_messages_action_handler_t action_handler;
    void *action_context;
    d1l_ui_messages_action_binding_t controls[D1L_UI_MESSAGES_CONTROL_BINDING_COUNT];
    d1l_ui_messages_action_binding_t
        public_rows[D1L_UI_MESSAGES_PUBLIC_PREVIEW_ROWS];
    d1l_ui_messages_action_binding_t dm_rows[D1L_UI_MESSAGES_DM_PREVIEW_ROWS];
    d1l_ui_messages_action_binding_t
        thread_controls[D1L_UI_MESSAGES_THREAD_CONTROL_BINDING_COUNT];
    d1l_ui_messages_action_binding_t thread_rows[D1L_DM_STORE_CAPACITY];
    d1l_dm_entry_t thread_entries[D1L_DM_STORE_CAPACITY];
    bool thread_unread[D1L_DM_STORE_CAPACITY];
    char thread_fingerprint[D1L_NODE_FINGERPRINT_LEN];
    char thread_alias[D1L_CONTACT_ALIAS_LEN];
    size_t thread_limit;
    size_t thread_row_count;
    size_t thread_total_matches;
    uint64_t expanded_delivery_session_id;
    uint32_t expanded_row_seq;
    uint32_t generation;
    bool expanded_row_valid;
} d1l_ui_messages_controller_t;

bool d1l_ui_messages_create(d1l_ui_messages_controller_t *controller,
                            lv_obj_t *parent);
void d1l_ui_messages_render(d1l_ui_messages_controller_t *controller,
                            lv_obj_t *parent,
                            const d1l_ui_messages_view_model_t *view_model,
                            d1l_ui_messages_action_handler_t action_handler,
                            void *action_context);
bool d1l_ui_messages_select_thread(d1l_ui_messages_controller_t *controller,
                                   const char *fingerprint,
                                   const char *alias);
bool d1l_ui_messages_render_thread(
    d1l_ui_messages_controller_t *controller,
    d1l_ui_messages_thread_loader_t loader,
    void *loader_context,
    d1l_ui_messages_action_handler_t action_handler,
    void *action_context);
bool d1l_ui_messages_expand_thread(d1l_ui_messages_controller_t *controller);
bool d1l_ui_messages_toggle_thread_details(
    d1l_ui_messages_controller_t *controller,
    const d1l_dm_entry_t *entry);
bool d1l_ui_messages_thread_active(
    const d1l_ui_messages_controller_t *controller);
const char *d1l_ui_messages_thread_fingerprint(
    const d1l_ui_messages_controller_t *controller);
lv_obj_t *d1l_ui_messages_thread_sheet(
    const d1l_ui_messages_controller_t *controller);
void d1l_ui_messages_hide_thread(d1l_ui_messages_controller_t *controller);
const char *d1l_ui_messages_delivery_label(const d1l_dm_entry_t *entry,
                                           bool unread);
void d1l_ui_messages_deactivate(d1l_ui_messages_controller_t *controller);
