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

typedef struct {
    bool bridge_ready;
    bool protocol_supported;
    bool card_present;
    bool filesystem_mounted;
    bool deskos_root_ready;
    bool format_required;
    bool format_supported;
    bool data_ready;
    bool response_truncated;
    uint32_t capacity_kb;
    uint32_t free_kb;
    esp_err_t last_error;
    char state[24];
    char filesystem[16];
    char note[96];
} d1l_rp2040_sd_status_t;

esp_err_t d1l_rp2040_bridge_init(void);
esp_err_t d1l_rp2040_bridge_status(d1l_rp2040_status_t *out_status);
esp_err_t d1l_rp2040_bridge_probe_sd(d1l_rp2040_sd_status_t *out_status, uint32_t timeout_ms);
