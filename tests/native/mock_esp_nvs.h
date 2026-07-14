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
void mock_nvs_fail_open_after(size_t successful_opens, esp_err_t error);
void mock_nvs_run_during_next_set(void (*hook)(void));
void mock_semaphore_run_after_takes(size_t take_count, void (*hook)(void));
void mock_timer_set_us(int64_t now_us);
