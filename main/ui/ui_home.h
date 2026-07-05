#pragma once

#include <stdint.h>

#include "app/app_model.h"

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

const char *d1l_ui_home_sd_state(const d1l_app_snapshot_t *snapshot);
d1l_ui_home_box_t d1l_ui_home_launcher_box(d1l_ui_home_launcher_slot_t slot);
d1l_ui_home_box_t d1l_ui_home_status_box(d1l_ui_home_status_slot_t slot);
