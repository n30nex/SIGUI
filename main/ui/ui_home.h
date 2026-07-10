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
    D1L_UI_HOME_DESTINATION_MESSAGES = 0,
    D1L_UI_HOME_DESTINATION_NETWORK,
    D1L_UI_HOME_DESTINATION_MAP,
    D1L_UI_HOME_DESTINATION_MORE,
    D1L_UI_HOME_DESTINATION_COUNT,
} d1l_ui_home_destination_slot_t;

typedef enum {
    D1L_UI_HOME_ACTION_NONE = 0,
    D1L_UI_HOME_ACTION_MESSAGES,
    D1L_UI_HOME_ACTION_NETWORK,
    D1L_UI_HOME_ACTION_MAP,
    D1L_UI_HOME_ACTION_MORE,
} d1l_ui_home_action_t;

typedef void (*d1l_ui_home_action_handler_t)(d1l_ui_home_action_t action);

const char *d1l_ui_home_sd_state(const d1l_app_snapshot_t *snapshot);
d1l_ui_home_box_t d1l_ui_home_destination_box(d1l_ui_home_destination_slot_t slot);
d1l_ui_home_box_t d1l_ui_home_device_box(void);
void d1l_ui_home_render(lv_obj_t *parent,
                        const d1l_app_snapshot_t *snapshot,
                        d1l_ui_home_action_handler_t action_handler);
