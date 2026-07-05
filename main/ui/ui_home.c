#include "ui_home.h"

static const d1l_ui_home_box_t k_launcher_boxes[D1L_UI_HOME_LAUNCHER_COUNT] = {
    [D1L_UI_HOME_LAUNCHER_CHATS] = {4, 0, 116, 132},
    [D1L_UI_HOME_LAUNCHER_DMS] = {122, 0, 116, 132},
    [D1L_UI_HOME_LAUNCHER_ROOMS] = {240, 0, 116, 132},
    [D1L_UI_HOME_LAUNCHER_CONTACTS] = {358, 0, 116, 132},
    [D1L_UI_HOME_LAUNCHER_REPEATERS] = {4, 140, 116, 132},
    [D1L_UI_HOME_LAUNCHER_ADVERTISE] = {122, 140, 116, 132},
    [D1L_UI_HOME_LAUNCHER_MAP] = {240, 140, 116, 132},
    [D1L_UI_HOME_LAUNCHER_TERMINAL] = {358, 140, 116, 132},
    [D1L_UI_HOME_LAUNCHER_PACKETS] = {4, 280, 116, 132},
    [D1L_UI_HOME_LAUNCHER_SETTINGS] = {122, 280, 116, 132},
    [D1L_UI_HOME_LAUNCHER_SETUP] = {240, 280, 116, 132},
    [D1L_UI_HOME_LAUNCHER_SIGNAL] = {358, 280, 116, 132},
};

static const d1l_ui_home_box_t k_status_boxes[D1L_UI_HOME_STATUS_COUNT] = {
    [D1L_UI_HOME_STATUS_TIME] = {4, 416, 116, 48},
    [D1L_UI_HOME_STATUS_WIFI] = {122, 416, 116, 48},
    [D1L_UI_HOME_STATUS_BLE] = {240, 416, 116, 48},
    [D1L_UI_HOME_STATUS_SD] = {358, 416, 116, 48},
};

const char *d1l_ui_home_sd_state(const d1l_app_snapshot_t *snapshot)
{
    if (!snapshot) {
        return "unknown";
    }
    if (snapshot->storage_data_enabled || snapshot->storage_sd_data_root_ready) {
        return "ready";
    }
    if (snapshot->storage_setup_required) {
        return "setup";
    }
    if (snapshot->storage_sd_state && snapshot->storage_sd_state[0]) {
        return snapshot->storage_sd_state;
    }
    return "fallback";
}

d1l_ui_home_box_t d1l_ui_home_launcher_box(d1l_ui_home_launcher_slot_t slot)
{
    const int index = (int)slot;
    if (index < 0 || index >= D1L_UI_HOME_LAUNCHER_COUNT) {
        return (d1l_ui_home_box_t){0, 0, 0, 0};
    }
    return k_launcher_boxes[index];
}

d1l_ui_home_box_t d1l_ui_home_status_box(d1l_ui_home_status_slot_t slot)
{
    const int index = (int)slot;
    if (index < 0 || index >= D1L_UI_HOME_STATUS_COUNT) {
        return (d1l_ui_home_box_t){0, 0, 0, 0};
    }
    return k_status_boxes[index];
}
