#include "storage_status.h"

#include <string.h>

#include "sdkconfig.h"

#include "hal/rp2040_bridge.h"

static d1l_storage_status_t s_status;

static void set_nvs_fallback_backends(d1l_storage_status_t *status)
{
    status->data_backend = "nvs";
    status->message_store_backend = "nvs";
    status->dm_store_backend = "nvs";
    status->packet_log_backend = "nvs";
    status->route_store_backend = "nvs";
    status->map_tile_backend = "unavailable";
    status->export_backend = "serial";
}

static void set_default_actions(d1l_storage_status_t *status)
{
    status->setup_action = "not_available";
    status->format_action = "not_available";
    status->sd_filesystem = "unknown";
}

static const char *stable_sd_state(const char *state)
{
    if (!state || state[0] == '\0') {
        return "unknown";
    }
    if (strcmp(state, "ready") == 0) {
        return "ready";
    }
    if (strcmp(state, "no_card") == 0) {
        return "no_card";
    }
    if (strcmp(state, "setup_required") == 0) {
        return "setup_required";
    }
    if (strcmp(state, "unformatted") == 0) {
        return "unformatted";
    }
    if (strcmp(state, "error") == 0) {
        return "error";
    }
    if (strcmp(state, "protocol_pending") == 0) {
        return "protocol_pending";
    }
    if (strcmp(state, "bridge_unavailable") == 0) {
        return "bridge_unavailable";
    }
    return "bridge_reported";
}

static const char *stable_filesystem(const char *filesystem)
{
    if (!filesystem || filesystem[0] == '\0') {
        return "unknown";
    }
    if (strcmp(filesystem, "fat32") == 0) {
        return "fat32";
    }
    if (strcmp(filesystem, "exfat") == 0) {
        return "exfat";
    }
    if (strcmp(filesystem, "fatfs") == 0) {
        return "fatfs";
    }
    return "reported";
}

static void apply_rp2040_sd_status(const d1l_rp2040_sd_status_t *sd)
{
    s_status.rp2040_bridge_ready = sd->bridge_ready;
    s_status.rp2040_sd_protocol_supported = sd->protocol_supported;
    s_status.sd_present = sd->card_present;
    s_status.sd_mounted = sd->filesystem_mounted;
    s_status.sd_data_root_ready = sd->deskos_root_ready;
    s_status.format_required = sd->format_required;
    s_status.format_supported = sd->format_supported;
    s_status.setup_required = sd->card_present && (!sd->filesystem_mounted ||
                                                    !sd->deskos_root_ready ||
                                                    sd->format_required);
    s_status.setup_supported = sd->protocol_supported && sd->card_present;
    s_status.response_truncated = sd->response_truncated;
    s_status.capacity_kb = sd->capacity_kb;
    s_status.free_kb = sd->free_kb;
    s_status.last_error = sd->last_error;
    s_status.sd_state = stable_sd_state(sd->state);
    s_status.sd_filesystem = stable_filesystem(sd->filesystem);
    s_status.data_enabled = false;

    if (!sd->bridge_ready) {
        s_status.setup_action = "bridge_unavailable";
        s_status.format_action = "not_available";
        s_status.note = "RP2040 UART bridge is unavailable; onboard NVS remains the default data store";
    } else if (!sd->protocol_supported) {
        s_status.setup_action = "bridge_protocol_pending";
        s_status.format_action = "not_available";
        s_status.note = "RP2040 UART is ready, but the DeskOS SD status protocol is not implemented on the bridge yet";
    } else if (!sd->card_present) {
        s_status.setup_action = "insert_card";
        s_status.format_action = "not_available";
        s_status.note = "No SD card reported by the RP2040 bridge; onboard NVS remains the default data store";
    } else if (s_status.setup_required) {
        s_status.setup_action = sd->format_supported ? "format_confirmation_required" :
                                "manual_format_required";
        s_status.format_action = sd->format_supported ? "confirm_required" : "not_available";
        s_status.note =
            "SD card is present but not ready for DeskOS data; setup must be explicitly confirmed before any format";
    } else {
        s_status.setup_action = "store_migration_pending";
        s_status.format_action = "not_needed";
        s_status.note =
            "SD card is valid, but retained stores remain on onboard NVS until SD-backed store migration is enabled";
        s_status.map_tile_backend = "sd_pending_store_migration";
    }
}

esp_err_t d1l_storage_status_init(void)
{
    memset(&s_status, 0, sizeof(s_status));
    s_status.initialized = true;
    s_status.mount_point = D1L_STORAGE_SD_MOUNT_POINT;
    s_status.data_root = D1L_STORAGE_SD_DATA_ROOT;
    s_status.format_supported = false;
    s_status.sd_filesystem = "unknown";
    s_status.last_error = ESP_ERR_NOT_SUPPORTED;

#if CONFIG_LCD_BOARD_SENSECAP_INDICATOR_D1L
    s_status.direct_supported = false;
    s_status.rp2040_bridge_required = true;
    s_status.sd_interface = "rp2040";
    s_status.sd_state = "pending_bridge";
    s_status.note =
        "D1L microSD is not exposed through ESP32 SDMMC/SDSPI; using onboard fallback until RP2040 SD bridge is implemented";
#else
    s_status.direct_supported = false;
    s_status.rp2040_bridge_required = false;
    s_status.sd_interface = "unknown";
    s_status.sd_state = "unsupported";
    s_status.note = "SD data storage is not enabled for this board profile";
#endif

    set_nvs_fallback_backends(&s_status);
    set_default_actions(&s_status);
    return ESP_OK;
}

void d1l_storage_status_note_rp2040(esp_err_t rp2040_init_result)
{
    if (!s_status.initialized) {
        (void)d1l_storage_status_init();
    }
    s_status.rp2040_bridge_ready = (rp2040_init_result == ESP_OK);
    if (s_status.rp2040_bridge_required && !s_status.rp2040_bridge_ready) {
        s_status.sd_state = "rp2040_unavailable";
        s_status.last_error = rp2040_init_result;
        s_status.setup_action = "bridge_unavailable";
        s_status.format_action = "not_available";
        s_status.note = "RP2040 bridge is not ready; SD data storage remains on onboard fallback";
    } else if (s_status.rp2040_bridge_required) {
        s_status.sd_state = "protocol_pending";
        s_status.last_error = ESP_ERR_NOT_SUPPORTED;
        s_status.setup_action = "bridge_protocol_pending";
        s_status.format_action = "not_available";
        s_status.note = "RP2040 UART is ready; DeskOS SD status protocol is pending on the bridge";
    }
}

esp_err_t d1l_storage_status_refresh(uint32_t timeout_ms)
{
    if (!s_status.initialized) {
        (void)d1l_storage_status_init();
    }

    if (!s_status.rp2040_bridge_required) {
        s_status.last_error = ESP_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }

    d1l_rp2040_sd_status_t sd = {0};
    esp_err_t ret = d1l_rp2040_bridge_probe_sd(&sd, timeout_ms);
    apply_rp2040_sd_status(&sd);
    set_nvs_fallback_backends(&s_status);
    if (ret == ESP_OK && sd.protocol_supported && sd.data_ready) {
        s_status.map_tile_backend = "sd_pending_store_migration";
    }
    return ret;
}

esp_err_t d1l_storage_format_sd_confirmed(const char *confirmation, uint32_t timeout_ms)
{
    if (!s_status.initialized) {
        (void)d1l_storage_status_init();
    }

    if (!confirmation || strcmp(confirmation, D1L_RP2040_SD_FORMAT_CONFIRMATION) != 0) {
        s_status.last_error = ESP_ERR_INVALID_ARG;
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_status.rp2040_bridge_required || !s_status.rp2040_sd_protocol_supported) {
        s_status.last_error = ESP_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!s_status.sd_present) {
        s_status.last_error = ESP_ERR_NOT_FOUND;
        return ESP_ERR_NOT_FOUND;
    }
    if (!s_status.format_supported) {
        s_status.last_error = ESP_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!s_status.setup_required) {
        s_status.last_error = ESP_ERR_INVALID_STATE;
        return ESP_ERR_INVALID_STATE;
    }

    d1l_rp2040_sd_status_t sd = {0};
    esp_err_t ret = d1l_rp2040_bridge_format_sd(&sd, confirmation, timeout_ms);
    apply_rp2040_sd_status(&sd);
    set_nvs_fallback_backends(&s_status);
    if (ret == ESP_OK && sd.protocol_supported && sd.data_ready) {
        s_status.map_tile_backend = "sd_pending_store_migration";
    }
    return ret;
}

void d1l_storage_status(d1l_storage_status_t *out_status)
{
    if (!out_status) {
        return;
    }
    if (!s_status.initialized) {
        (void)d1l_storage_status_init();
    }
    *out_status = s_status;
}
