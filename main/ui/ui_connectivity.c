#include "ui_connectivity.h"

#include <stdio.h>
#include <string.h>

static const char *safe_text(const char *value, const char *fallback)
{
    return value && value[0] ? value : fallback;
}

void d1l_ui_connectivity_wifi_view(const d1l_ui_wifi_view_input_t *input,
                                   d1l_ui_wifi_view_model_t *out_view)
{
    if (!input || !out_view) {
        return;
    }
    memset(out_view, 0, sizeof(*out_view));
    snprintf(out_view->state_line, sizeof(out_view->state_line),
             "State %s  build %s", safe_text(input->state, "off"),
             input->build_enabled ? "enabled" : "not built");
    if (input->connected && input->ip && input->ip[0]) {
        snprintf(out_view->link_line, sizeof(out_view->link_line),
                 "IP %s  RSSI %d  ch %u", input->ip, input->rssi_dbm,
                 (unsigned)input->channel);
    } else {
        snprintf(out_view->link_line, sizeof(out_view->link_line),
                 "Last %s", safe_text(input->last_error, "none"));
    }
    snprintf(out_view->profile_line, sizeof(out_view->profile_line),
             "Profile %s  password %s",
             input->profile_saved ? safe_text(input->ssid, "saved") :
                                    "not saved",
             input->password_saved ? "saved" : "open/empty");
    snprintf(out_view->ssid, sizeof(out_view->ssid), "%s",
             safe_text(input->ssid, ""));
    if (!input->build_enabled) {
        snprintf(out_view->scan_line, sizeof(out_view->scan_line),
                 "Wi-Fi is unavailable in this build");
    } else if (input->scan_loaded) {
        snprintf(out_view->scan_line, sizeof(out_view->scan_line),
                 "Scan %s  %u/%u networks  strongest %s",
                 safe_text(input->scan_reason, "unknown"),
                 (unsigned)input->scan_returned_count,
                 (unsigned)input->scan_total_count,
                 input->scan_returned_count > 0U ?
                     safe_text(input->strongest_ssid, "hidden") : "none");
    } else {
        snprintf(out_view->scan_line, sizeof(out_view->scan_line),
                 "Scan to list nearby 2.4 GHz networks");
    }
    out_view->controls_available = input->build_enabled;
    snprintf(out_view->toggle_label, sizeof(out_view->toggle_label), "%s",
             input->build_enabled && input->enabled ? "Disable" : "Enable");
    snprintf(out_view->password_placeholder,
             sizeof(out_view->password_placeholder), "%s",
             input->password_saved ? "Saved; enter to replace" : "Optional");
    out_view->state_color = input->build_enabled && input->enabled ?
        0x5EEAD4U : 0xFBBF24U;
}

void d1l_ui_connectivity_ble_view(const d1l_ui_ble_view_input_t *input,
                                  d1l_ui_ble_view_model_t *out_view)
{
    if (!input || !out_view) {
        return;
    }
    memset(out_view, 0, sizeof(*out_view));
    snprintf(out_view->state_line, sizeof(out_view->state_line),
             "State %s  build %s", safe_text(input->state, "off"),
             input->build_enabled ? "enabled" : "not built");
    const bool runtime_available =
        input->build_enabled && input->transport_supported;
    out_view->controls_available = runtime_available;
    out_view->purpose = runtime_available ?
        "Companion BLE is available for measured local setup." :
        "BLE companion transport is unavailable in this release.";
    out_view->runtime_note = runtime_available ?
        "Pairing controls require a measured BLE runtime artifact." :
        "No BLE pairing or transport artifact is present for public release.";
    out_view->toggle_label = runtime_available && input->companion_enabled ?
        "Disable" : "Enable";
    out_view->production_note =
        "USB remains the reliable companion path for production validation.";
    out_view->state_color = runtime_available && input->companion_enabled ?
        0xA7F3D0U : 0xFBBF24U;
}
