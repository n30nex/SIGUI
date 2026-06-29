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

esp_err_t d1l_board_init(void)
{
    esp_err_t ret = bsp_board_init();
    s_status.init_result = ret;
    s_status.ready = (ret == ESP_OK);

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
    indev_data_t data = {0};
    esp_err_t ret = indev_get_major_value(&data);
    if (ret != ESP_OK) {
        return ret;
    }
    *touches = data.btn_val ? 1 : 0;
    *x = (uint16_t)data.x;
    *y = (uint16_t)data.y;
    return ESP_OK;
}

bool d1l_board_button_pressed(void)
{
    if (!s_status.ready) {
        return false;
    }
    return bsp_btn_get_state(BOARD_BTN_ID_USER);
}
