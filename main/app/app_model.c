#include "app_model.h"

static d1l_app_model_t s_model = {
    .board_ready = false,
    .ui_ready = false,
    .board_error = ESP_ERR_INVALID_STATE,
    .boot_count = 0,
};

d1l_app_model_t *d1l_app_model_get(void)
{
    return &s_model;
}
