#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    bool board_ready;
    bool ui_ready;
    esp_err_t board_error;
    uint32_t boot_count;
} d1l_app_model_t;

d1l_app_model_t *d1l_app_model_get(void);
