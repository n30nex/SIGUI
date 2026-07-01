#include "indicator_board.h"

#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "bsp_board.h"
#include "bsp_i2c.h"
#include "bsp_lcd.h"
#include "bsp_btn.h"
#include "indev/indev.h"

#include "app/app_model.h"
#include "hal/backlight.h"

static const char *TAG = "d1l_board";
static d1l_board_status_t s_status = {
    .ready = false,
    .init_result = ESP_ERR_INVALID_STATE,
    .i2c_count = 0,
};
static esp_err_t s_touch_init_result = ESP_ERR_INVALID_STATE;
static uint32_t s_touch_init_attempts = 0;

static uint16_t clamp_touch_coord(int32_t value, uint16_t max)
{
    if (value < 0) {
        return 0;
    }
    if (value >= max) {
        return (uint16_t)(max - 1);
    }
    return (uint16_t)value;
}

static esp_err_t d1l_board_touch_ensure_ready(void)
{
    if (!s_status.ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_touch_init_result == ESP_OK) {
        return ESP_OK;
    }

    s_touch_init_attempts++;
    s_touch_init_result = indev_init_default();
    if (s_touch_init_result != ESP_OK) {
        ESP_LOGW(TAG, "touch init attempt %lu failed: %s",
                 (unsigned long)s_touch_init_attempts,
                 esp_err_to_name(s_touch_init_result));
    }
    return s_touch_init_result;
}

esp_err_t d1l_board_init(void)
{
    esp_err_t ret = bsp_board_init();
    s_status.init_result = ret;
    s_status.ready = (ret == ESP_OK);
    s_touch_init_result = ESP_ERR_INVALID_STATE;
    s_touch_init_attempts = 0;

    d1l_app_model_t *model = d1l_app_model_get();
    model->board_ready = s_status.ready;
    model->board_error = ret;

    if (ret == ESP_OK) {
        d1l_backlight_set_percent(70);
        d1l_board_i2c_scan(&s_status);
    }
    return ret;
}

const d1l_board_status_t *d1l_board_status(void)
{
    return &s_status;
}

esp_err_t d1l_board_i2c_scan(d1l_board_status_t *out_status)
{
    if (!out_status) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t found[16] = {0};
    uint8_t count = bsp_i2c_scan_device(found, sizeof(found));
    out_status->i2c_count = count > sizeof(out_status->i2c_addresses) ? sizeof(out_status->i2c_addresses) : count;
    memcpy(out_status->i2c_addresses, found, out_status->i2c_count);
    return ESP_OK;
}

esp_err_t d1l_board_display_color_test(void)
{
    if (!s_status.ready) {
        return ESP_ERR_INVALID_STATE;
    }

    const int width = 480;
    const int height = 480;
    uint16_t colors[] = {
        0xF800, /* red */
        0x07E0, /* green */
        0x001F, /* blue */
        0xFFFF, /* white */
        0x0000, /* black */
        0xFFE0, /* yellow */
    };
    uint16_t *line = heap_caps_malloc(width * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!line) {
        line = heap_caps_malloc(width * sizeof(uint16_t), MALLOC_CAP_8BIT);
    }
    if (!line) {
        return ESP_ERR_NO_MEM;
    }

    for (int y = 0; y < height; ++y) {
        uint16_t color = colors[(y * 6) / height];
        for (int x = 0; x < width; ++x) {
            line[x] = color;
        }
        esp_err_t ret = bsp_lcd_flush(0, y, width, y + 1, line);
        if (ret != ESP_OK) {
            free(line);
            ESP_LOGE(TAG, "display flush failed at y=%d: %s", y, esp_err_to_name(ret));
            return ret;
        }
    }
    free(line);
    return ESP_OK;
}

esp_err_t d1l_board_touch_sample(uint8_t *touches, uint16_t *x, uint16_t *y)
{
    if (!s_status.ready || !touches || !x || !y) {
        return ESP_ERR_INVALID_STATE;
    }
    d1l_board_touch_state_t state = {0};
    esp_err_t ret = d1l_board_touch_read(&state);
    if (ret != ESP_OK) {
        return ret;
    }
    *touches = state.touches;
    *x = state.pressed ? state.x : 0;
    *y = state.pressed ? state.y : 0;
    return ESP_OK;
}

esp_err_t d1l_board_touch_read(d1l_board_touch_state_t *out_state)
{
    if (!out_state) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_state, 0, sizeof(*out_state));
    out_state->init_result = ESP_ERR_INVALID_STATE;
    out_state->read_result = ESP_ERR_INVALID_STATE;
    if (!s_status.ready) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t init_ret = d1l_board_touch_ensure_ready();
    out_state->init_result = init_ret;
    out_state->init_attempts = s_touch_init_attempts;
    if (init_ret != ESP_OK) {
        return init_ret;
    }

    indev_data_t data = {0};
    esp_err_t ret = indev_get_major_value(&data);
    if (ret == ESP_ERR_INVALID_STATE) {
        s_touch_init_result = ESP_ERR_INVALID_STATE;
        init_ret = d1l_board_touch_ensure_ready();
        out_state->init_result = init_ret;
        out_state->init_attempts = s_touch_init_attempts;
        if (init_ret == ESP_OK) {
            ret = indev_get_major_value(&data);
        }
    }
    out_state->read_result = ret;
    if (ret != ESP_OK) {
        return ret;
    }

    out_state->pressed = data.pressed;
    out_state->touches = data.pressed ? 1 : 0;
    out_state->raw_x = data.x;
    out_state->raw_y = data.y;
    out_state->coordinate_valid = data.pressed &&
                                  data.x >= 0 && data.x < 480 &&
                                  data.y >= 0 && data.y < 480;
    out_state->x = data.pressed ? clamp_touch_coord(data.x, 480) : 0;
    out_state->y = data.pressed ? clamp_touch_coord(data.y, 480) : 0;
    return ESP_OK;
}

bool d1l_board_button_pressed(void)
{
    if (!s_status.ready) {
        return false;
    }
    return bsp_btn_get_state(BOARD_BTN_ID_USER);
}
