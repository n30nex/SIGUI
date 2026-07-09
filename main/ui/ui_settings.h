#pragma once

#include "app/app_model.h"

typedef struct _lv_obj_t lv_obj_t;

typedef enum {
    D1L_UI_SETTINGS_ACTION_NONE = 0,
    D1L_UI_SETTINGS_ACTION_STORAGE,
    D1L_UI_SETTINGS_ACTION_WIFI,
    D1L_UI_SETTINGS_ACTION_BLE,
    D1L_UI_SETTINGS_ACTION_RADIO,
    D1L_UI_SETTINGS_ACTION_MAP_TILES,
    D1L_UI_SETTINGS_ACTION_DISPLAY,
    D1L_UI_SETTINGS_ACTION_DIAGNOSTICS,
    D1L_UI_SETTINGS_ACTION_ADVANCED,
    D1L_UI_SETTINGS_ACTION_COUNT,
} d1l_ui_settings_action_t;

typedef void (*d1l_ui_settings_action_handler_t)(d1l_ui_settings_action_t action);

void d1l_ui_settings_render(lv_obj_t *parent,
                            const d1l_app_snapshot_t *snapshot,
                            d1l_ui_settings_action_handler_t action_handler);
