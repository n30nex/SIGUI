#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

typedef struct {
    const char *server;
    bool wait_for_sync;
} esp_sntp_config_t;

#define ESP_NETIF_SNTP_DEFAULT_CONFIG(server_name) \
    ((esp_sntp_config_t) { .server = (server_name), .wait_for_sync = false })

esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *config);
esp_err_t esp_netif_sntp_sync_wait(TickType_t ticks_to_wait);
