#pragma once

#include <pthread.h>
#include <stdint.h>

typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef uint32_t TickType_t;
typedef void *TaskHandle_t;
typedef pthread_mutex_t portMUX_TYPE;

#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define portMAX_DELAY UINT32_MAX
#define configTICK_RATE_HZ 1000U
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define tskIDLE_PRIORITY 0U
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

#define portENTER_CRITICAL(mux) ((void)pthread_mutex_lock((mux)))
#define portEXIT_CRITICAL(mux) ((void)pthread_mutex_unlock((mux)))
