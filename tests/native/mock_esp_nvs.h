#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

void mock_nvs_reset(void);
bool mock_nvs_seed_blob(const char *namespace_name, const char *key,
                        const void *value, size_t length);
size_t mock_nvs_copy_blob(const char *namespace_name, const char *key,
                          void *dest, size_t dest_size);
void mock_nvs_fail_next_set(esp_err_t error);
void mock_nvs_fail_next_open(esp_err_t error);
void mock_timer_set_us(int64_t now_us);
