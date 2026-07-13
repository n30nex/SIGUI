#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t d1l_route_store_worker_start(void);
esp_err_t d1l_route_store_worker_force_flush(uint32_t timeout_ms);
/* Preempt and hold retained persistence for an urgent storage transition. */
esp_err_t d1l_route_store_worker_quiesce_begin(uint32_t timeout_ms);
/* Wait for the current retained persistence sequence without cancelling it. */
esp_err_t d1l_route_store_worker_quiesce_wait_begin(uint32_t timeout_ms);
void d1l_route_store_worker_quiesce_end(void);
/* True for non-owners once persistence preemption is requested or owned. */
bool d1l_route_store_persistence_should_yield(void);
