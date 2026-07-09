#include "ui_home.h"

#include <string.h>

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
    const char *state = snapshot->storage_sd_state;
    if (!state || state[0] == '\0') {
        return "fallback";
    }
    if (strcmp(state, "mount_pending") == 0 || strcmp(state, "checking") == 0) {
        return "mounting";
    }
    if (strcmp(state, "mount_required") == 0) {
        return "mount";
    }
    if (strcmp(state, "no_card") == 0) {
        return "no card";
    }
    if (strcmp(state, "not_fat32_or_unmountable") == 0) {
        return "needs FAT32";
    }
    if (strcmp(state, "creating_deskos_files") == 0) {
        return "preparing";
    }
    if (strcmp(state, "deskos_manifest_invalid") == 0) {
        return "repair";
    }
    if (strcmp(state, "protocol_pending") == 0 ||
        strcmp(state, "bridge_unavailable") == 0) {
        return "offline";
    }
    if (strcmp(state, "error") == 0) {
        return "SD error";
    }
    if (strcmp(state, "fat32_ready") == 0) {
        return "FAT32 ready";
    }
    if (strcmp(state, "bridge_reported") == 0) {
        return "detected";
    }
    return state;
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
