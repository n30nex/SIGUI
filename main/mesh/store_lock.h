#pragma once

#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef struct {
    StaticSemaphore_t buffer;
    SemaphoreHandle_t handle;
    portMUX_TYPE init_mux;
} d1l_store_lock_t;

#define D1L_STORE_LOCK_INITIALIZER { .handle = NULL, .init_mux = portMUX_INITIALIZER_UNLOCKED }

static inline SemaphoreHandle_t d1l_store_lock_handle(d1l_store_lock_t *lock)
{
    if (!lock) {
        return NULL;
    }
    if (!lock->handle) {
        portENTER_CRITICAL(&lock->init_mux);
        if (!lock->handle) {
            lock->handle = xSemaphoreCreateMutexStatic(&lock->buffer);
        }
        portEXIT_CRITICAL(&lock->init_mux);
    }
    return lock->handle;
}

static inline void d1l_store_lock_take(d1l_store_lock_t *lock)
{
    SemaphoreHandle_t handle = d1l_store_lock_handle(lock);
    if (handle) {
        (void)xSemaphoreTake(handle, portMAX_DELAY);
    }
}

static inline bool d1l_store_lock_try_take(d1l_store_lock_t *lock)
{
    SemaphoreHandle_t handle = d1l_store_lock_handle(lock);
    return handle && xSemaphoreTake(handle, 0) == pdTRUE;
}

static inline void d1l_store_lock_give(d1l_store_lock_t *lock)
{
    if (lock && lock->handle) {
        (void)xSemaphoreGive(lock->handle);
    }
}
