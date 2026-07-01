#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    bool ready;
    esp_err_t init_result;
    uint8_t i2c_addresses[16];
    uint8_t i2c_count;
} d1l_board_status_t;

typedef struct {
    bool pressed;
    bool coordinate_valid;
    uint8_t touches;
    uint16_t x;
    uint16_t y;
    int32_t raw_x;
    int32_t raw_y;
    esp_err_t init_result;
    esp_err_t read_result;
    uint32_t init_attempts;
} d1l_board_touch_state_t;

esp_err_t d1l_board_init(void);
const d1l_board_status_t *d1l_board_status(void);
esp_err_t d1l_board_i2c_scan(d1l_board_status_t *out_status);
esp_err_t d1l_board_display_color_test(void);
esp_err_t d1l_board_touch_read(d1l_board_touch_state_t *out_state);
esp_err_t d1l_board_touch_sample(uint8_t *touches, uint16_t *x, uint16_t *y);
bool d1l_board_button_pressed(void);
