#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define D1L_SECURE_RANDOM_MAX_REQUEST 64U

/*
 * Seeds the process-lifetime CSPRNG during the first instructions of app_main,
 * before ADC or RF users start.  Later callers never enable hardware entropy;
 * they either consume the locked DRBG or fail closed.
 */
esp_err_t d1l_secure_random_init(void);

/* Fills the complete destination or clears it and returns an error. */
esp_err_t d1l_secure_random_fill(void *dest, size_t length);

bool d1l_secure_random_ready(void);
esp_err_t d1l_secure_random_status(void);
