#pragma once

#include <stdint.h>

#include "esp_err.h"

esp_err_t d1l_route_store_worker_start(void);
esp_err_t d1l_route_store_worker_force_flush(uint32_t timeout_ms);
