#include "ui_phase1.h"

#include "esp_heap_caps.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "app/app_model.h"
#include "bsp_lcd.h"
#include "indev/indev.h"

static const char *TAG = "d1l_ui";
static lv_disp_draw_buf_t s_draw_buf;
static lv_color_t *s_buf1;
static lv_color_t *s_buf2;
static bool s_started = false;

static void lv_tick_task(void *arg)
{
    (void)arg;
    lv_tick_inc(5);
}

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_err_t ret = bsp_lcd_flush(area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_map);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "flush failed: %s", esp_err_to_name(ret));
    }
    lv_disp_flush_ready(drv);
}

static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    indev_data_t sample = {0};
    if (indev_get_major_value(&sample) == ESP_OK && sample.btn_val) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = sample.x;
        data->point.y = sample.y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void ui_task(void *arg)
{
    (void)arg;
    while (true) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

esp_err_t d1l_ui_phase1_show_home(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x071018), 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "MeshCore DeskOS D1L");
    lv_obj_set_style_text_color(title, lv_color_hex(0xF4F7FB), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

    lv_obj_t *profile = lv_label_create(screen);
    lv_label_set_text(profile, "US/CAN 910.525  BW62.5  SF7  CR5");
    lv_obj_set_style_text_color(profile, lv_color_hex(0x5EEAD4), 0);
    lv_obj_align(profile, LV_ALIGN_TOP_MID, 0, 64);

    const char *tiles[] = {"Display", "Touch", "Radio", "Packets", "Nodes", "Settings"};
    for (int i = 0; i < 6; ++i) {
        int row = i / 2;
        int col = i % 2;
        lv_obj_t *tile = lv_obj_create(screen);
        lv_obj_set_size(tile, 196, 82);
        lv_obj_set_style_radius(tile, 8, 0);
        lv_obj_set_style_bg_color(tile, lv_color_hex(0x121B24), 0);
        lv_obj_set_style_border_color(tile, lv_color_hex(0x263241), 0);
        lv_obj_set_style_border_width(tile, 1, 0);
        lv_obj_align(tile, LV_ALIGN_TOP_LEFT, 34 + col * 216, 118 + row * 100);

        lv_obj_t *label = lv_label_create(tile);
        lv_label_set_text(label, tiles[i]);
        lv_obj_set_style_text_color(label, lv_color_hex(0xE5EDF5), 0);
        lv_obj_center(label);
    }

    lv_obj_t *footer = lv_label_create(screen);
    lv_label_set_text(footer, "Phase 1 hardware bring-up");
    lv_obj_set_style_text_color(footer, lv_color_hex(0x8EA0AE), 0);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -24);

    lv_scr_load(screen);
    return ESP_OK;
}

esp_err_t d1l_ui_phase1_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    lv_init();
    const size_t buffer_pixels = 480 * 40;
    s_buf1 = heap_caps_malloc(buffer_pixels * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    s_buf2 = heap_caps_malloc(buffer_pixels * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_buf1 || !s_buf2) {
        s_buf1 = heap_caps_malloc(buffer_pixels * sizeof(lv_color_t), MALLOC_CAP_8BIT);
        s_buf2 = heap_caps_malloc(buffer_pixels * sizeof(lv_color_t), MALLOC_CAP_8BIT);
    }
    if (!s_buf1 || !s_buf2) {
        return ESP_ERR_NO_MEM;
    }

    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, s_buf2, buffer_pixels);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 480;
    disp_drv.ver_res = 480;
    disp_drv.flush_cb = flush_cb;
    disp_drv.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read_cb;
    lv_indev_drv_register(&indev_drv);

    const esp_timer_create_args_t tick_args = {
        .callback = &lv_tick_task,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, 5000));

    ESP_RETURN_ON_ERROR(d1l_ui_phase1_show_home(), TAG, "home screen failed");
    xTaskCreate(ui_task, "d1l_ui", 4096, NULL, 5, NULL);
    d1l_app_model_get()->ui_ready = true;
    s_started = true;
    return ESP_OK;
}
