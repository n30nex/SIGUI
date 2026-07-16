#pragma once

#include <stdint.h>

typedef int BaseType_t;
typedef uint32_t TickType_t;

typedef struct {
    unsigned value;
} StaticSemaphore_t;

typedef StaticSemaphore_t *SemaphoreHandle_t;

typedef struct {
    unsigned value;
} portMUX_TYPE;

#define portMUX_INITIALIZER_UNLOCKED {0U}
#define portMAX_DELAY UINT32_MAX
#define pdTRUE 1
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

#define portENTER_CRITICAL(mux) do { (void)(mux); } while (0)
#define portEXIT_CRITICAL(mux) do { (void)(mux); } while (0)
