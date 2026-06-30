#include <stdio.h>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "d1l_config.h"
#include "app/settings_model.h"
#include "diagnostics/crash_log.h"
#include "hal/indicator_board.h"
#include "hal/rp2040_bridge.h"
#include "mesh/contact_store.h"
#include "mesh/dm_store.h"
#include "mesh/message_store.h"
#include "mesh/node_store.h"
#include "mesh/packet_log.h"
#include "mesh/read_state.h"
#include "mesh/route_store.h"
#include "mesh/meshcore_service.h"
#include "storage/storage_status.h"
#include "ui/ui_phase1.h"
#include "comms/connectivity_manager.h"
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

    esp_err_t storage_ret = d1l_storage_status_init();
    if (storage_ret != ESP_OK) {
        ESP_LOGW(TAG, "storage status init failed: %s", esp_err_to_name(storage_ret));
    }

    esp_err_t crash_log_ret = d1l_crash_log_init();
    if (crash_log_ret != ESP_OK) {
        ESP_LOGW(TAG, "crash/reset log init failed: %s", esp_err_to_name(crash_log_ret));
    }

    esp_err_t settings_ret = d1l_settings_load();
    if (settings_ret != ESP_OK) {
        ESP_LOGW(TAG, "settings load failed: %s", esp_err_to_name(settings_ret));
    }
    esp_err_t message_store_ret = d1l_message_store_init();
    if (message_store_ret != ESP_OK) {
        ESP_LOGW(TAG, "message store load failed: %s", esp_err_to_name(message_store_ret));
    }
    esp_err_t dm_store_ret = d1l_dm_store_init();
    if (dm_store_ret != ESP_OK) {
        ESP_LOGW(TAG, "DM store load failed: %s", esp_err_to_name(dm_store_ret));
    }
    esp_err_t node_store_ret = d1l_node_store_init();
    if (node_store_ret != ESP_OK) {
        ESP_LOGW(TAG, "node store load failed: %s", esp_err_to_name(node_store_ret));
    }
    esp_err_t contact_store_ret = d1l_contact_store_init();
    if (contact_store_ret != ESP_OK) {
        ESP_LOGW(TAG, "contact store load failed: %s", esp_err_to_name(contact_store_ret));
    }
    esp_err_t read_state_ret = d1l_read_state_init();
    if (read_state_ret != ESP_OK) {
        ESP_LOGW(TAG, "read state load failed: %s", esp_err_to_name(read_state_ret));
    }
    esp_err_t route_store_ret = d1l_route_store_init();
    if (route_store_ret != ESP_OK) {
        ESP_LOGW(TAG, "route store load failed: %s", esp_err_to_name(route_store_ret));
    }
    esp_err_t packet_log_ret = d1l_packet_log_init();
    if (packet_log_ret != ESP_OK) {
        ESP_LOGW(TAG, "packet log load failed: %s", esp_err_to_name(packet_log_ret));
    }
    esp_err_t connectivity_ret = d1l_connectivity_init();
    if (connectivity_ret != ESP_OK) {
        ESP_LOGW(TAG, "connectivity policy init failed: %s", esp_err_to_name(connectivity_ret));
    }
    d1l_meshcore_service_init();

    esp_err_t board_ret = d1l_board_init();
    esp_err_t rp2040_ret = d1l_rp2040_bridge_init();
    d1l_storage_status_note_rp2040(rp2040_ret);
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
