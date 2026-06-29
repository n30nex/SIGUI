#include <stdio.h>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "d1l_config.h"
#include "app/settings_model.h"
#include "hal/indicator_board.h"
#include "hal/rp2040_bridge.h"
#include "mesh/packet_log.h"
#include "mesh/meshcore_service.h"
#include "ui/ui_phase1.h"
#include "comms/usb_console.h"

static const char *TAG = "d1l_main";

void app_main(void)
{
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_ret);

    printf("{\"schema\":%d,\"event\":\"boot\",\"firmware\":\"%s\",\"version\":\"%s\",\"target\":\"seeed_indicator_d1l\"}\n",
           D1L_CONSOLE_SCHEMA, D1L_FIRMWARE_NAME, D1L_FIRMWARE_VERSION);

    esp_err_t settings_ret = d1l_settings_load();
    if (settings_ret != ESP_OK) {
        ESP_LOGW(TAG, "settings load failed: %s", esp_err_to_name(settings_ret));
    }
    d1l_packet_log_init();
    d1l_meshcore_service_init();

    esp_err_t board_ret = d1l_board_init();
    esp_err_t rp2040_ret = d1l_rp2040_bridge_init();
    if (rp2040_ret != ESP_OK) {
        ESP_LOGW(TAG, "RP2040 bridge UART init failed: %s", esp_err_to_name(rp2040_ret));
    }

    if (board_ret == ESP_OK) {
        ESP_LOGI(TAG, "D1L board initialized");
        d1l_ui_phase1_start();
    } else {
        ESP_LOGE(TAG, "D1L board init failed: %s", esp_err_to_name(board_ret));
        printf("{\"schema\":%d,\"event\":\"board_init\",\"ok\":false,\"code\":\"%s\",\"hint\":\"verify D1L hardware, I2C expander, and display power\"}\n",
               D1L_CONSOLE_SCHEMA, esp_err_to_name(board_ret));
    }

    d1l_usb_console_run();
}
