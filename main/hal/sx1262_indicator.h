#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    bool present;
    bool expander_ready;
    int busy;
    int dio1;
    int ver_pin;
    uint8_t status_byte;
    const char *tcxo_default;
    const char *failure_code;
} d1l_radiohw_status_t;

esp_err_t d1l_sx1262_probe(d1l_radiohw_status_t *status);
