#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    bool uart_ready;
    esp_err_t init_result;
    uint32_t buffered_bytes;
    int uart_port;
    int tx_gpio;
    int rx_gpio;
    int baud_rate;
    uint8_t reset_expander_pin;
} d1l_rp2040_status_t;

esp_err_t d1l_rp2040_bridge_init(void);
esp_err_t d1l_rp2040_bridge_status(d1l_rp2040_status_t *out_status);
