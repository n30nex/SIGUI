#pragma once

#include <stddef.h>
#include <stdint.h>

void esp_fill_random(void *buffer, size_t length);

static inline uint32_t esp_random(void)
{
    static uint32_t state = 0x6D2B79F5U;
    state = state * 1664525U + 1013904223U;
    return state;
}
