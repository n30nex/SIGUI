#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t d1l_route_store_worker_start(void);
esp_err_t d1l_route_store_worker_force_flush(uint32_t timeout_ms);
/* Hold the retained persistence sequence across a bounded storage transition. */
esp_err_t d1l_route_store_worker_quiesce_begin(uint32_t timeout_ms);
void d1l_route_store_worker_quiesce_end(void);
/* True for any non-owner persistence task while quiesce is pending. */
bool d1l_route_store_persistence_should_yield(void);
