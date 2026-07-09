#pragma once

#include <stdint.h>

#include "app/app_model.h"

typedef struct _lv_obj_t lv_obj_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;
} d1l_ui_home_box_t;

typedef enum {
    D1L_UI_HOME_LAUNCHER_CHATS = 0,
    D1L_UI_HOME_LAUNCHER_DMS,
    D1L_UI_HOME_LAUNCHER_ROOMS,
    D1L_UI_HOME_LAUNCHER_CONTACTS,
    D1L_UI_HOME_LAUNCHER_REPEATERS,
    D1L_UI_HOME_LAUNCHER_ADVERTISE,
    D1L_UI_HOME_LAUNCHER_MAP,
    D1L_UI_HOME_LAUNCHER_TERMINAL,
    D1L_UI_HOME_LAUNCHER_PACKETS,
    D1L_UI_HOME_LAUNCHER_SETTINGS,
    D1L_UI_HOME_LAUNCHER_SETUP,
    D1L_UI_HOME_LAUNCHER_SIGNAL,
    D1L_UI_HOME_LAUNCHER_COUNT,
} d1l_ui_home_launcher_slot_t;

typedef enum {
    D1L_UI_HOME_STATUS_TIME = 0,
    D1L_UI_HOME_STATUS_WIFI,
    D1L_UI_HOME_STATUS_BLE,
    D1L_UI_HOME_STATUS_SD,
    D1L_UI_HOME_STATUS_COUNT,
} d1l_ui_home_status_slot_t;

typedef enum {
    D1L_UI_HOME_ACTION_NONE = 0,
    D1L_UI_HOME_ACTION_MESSAGES_PUBLIC,
    D1L_UI_HOME_ACTION_MESSAGES_DM,
    D1L_UI_HOME_ACTION_MESH_ROLES,
    D1L_UI_HOME_ACTION_NODES,
    D1L_UI_HOME_ACTION_ADVERTISE,
    D1L_UI_HOME_ACTION_MAP,
    D1L_UI_HOME_ACTION_DIAGNOSTICS,
    D1L_UI_HOME_ACTION_PACKETS,
    D1L_UI_HOME_ACTION_SETTINGS,
    D1L_UI_HOME_ACTION_STORAGE,
    D1L_UI_HOME_ACTION_WIFI,
    D1L_UI_HOME_ACTION_BLE,
} d1l_ui_home_action_t;

typedef void (*d1l_ui_home_action_handler_t)(d1l_ui_home_action_t action);

const char *d1l_ui_home_sd_state(const d1l_app_snapshot_t *snapshot);
d1l_ui_home_box_t d1l_ui_home_launcher_box(d1l_ui_home_launcher_slot_t slot);
d1l_ui_home_box_t d1l_ui_home_status_box(d1l_ui_home_status_slot_t slot);
void d1l_ui_home_render(lv_obj_t *parent,
                        const d1l_app_snapshot_t *snapshot,
                        d1l_ui_home_action_handler_t action_handler);
