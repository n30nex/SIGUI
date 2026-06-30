#include "storage_status.h"

#include <string.h>

#include "sdkconfig.h"

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

esp_err_t d1l_storage_status_init(void)
{
    memset(&s_status, 0, sizeof(s_status));
    s_status.initialized = true;
    s_status.mount_point = D1L_STORAGE_SD_MOUNT_POINT;
    s_status.data_root = D1L_STORAGE_SD_DATA_ROOT;
    s_status.format_supported = false;
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
        s_status.note = "RP2040 bridge is not ready; SD data storage remains on onboard fallback";
    }
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
