#pragma once

#include <stdint.h>

typedef struct {
    uint16_t width;
    uint16_t height;
    int hsync_gpio;
    int vsync_gpio;
    int de_gpio;
    int pclk_gpio;
    int backlight_gpio;
    int data_gpio_nums[16];
} d1l_display_pins_t;

typedef struct {
    int sda_gpio;
    int scl_gpio;
    uint8_t expander_address;
} d1l_i2c_pins_t;

typedef struct {
    int sclk_gpio;
    int mosi_gpio;
    int miso_gpio;
    uint8_t expander_cs;
    uint8_t expander_reset;
    uint8_t expander_busy;
    uint8_t expander_dio1;
    const char *tcxo_default;
} d1l_sx1262_pins_t;

typedef struct {
    int esp_rx_gpio;
    int esp_tx_gpio;
    uint8_t expander_reset;
    int uart_port;
    int baud_rate;
} d1l_rp2040_pins_t;

const d1l_display_pins_t *d1l_display_pins(void);
const d1l_i2c_pins_t *d1l_i2c_pins(void);
const d1l_sx1262_pins_t *d1l_sx1262_pins(void);
const d1l_rp2040_pins_t *d1l_rp2040_pins(void);
