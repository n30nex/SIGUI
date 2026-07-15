#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bootloader_random.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/ctr_drbg.h"
#include "platform/secure_random.h"

static int s_event;
static int s_enable_event;
static int s_fill_event;
static int s_disable_event;
static int s_seed_event;
static int s_enable_calls;
static int s_hardware_fill_calls;
static int s_disable_calls;
static int s_take_calls;
static int s_give_calls;
static int s_random_calls;
static int s_random_fails;
static int s_entropy_enabled;
static int s_reseed_interval;
static size_t s_entropy_length;
static uint32_t s_seed_sum;
static int (*s_saved_entropy)(void *, unsigned char *, size_t);
static void *s_saved_entropy_context;

void bootloader_random_enable(void)
{
    s_enable_calls++;
    s_entropy_enabled = 1;
    s_enable_event = ++s_event;
}

void esp_fill_random(void *buffer, size_t length)
{
    unsigned char *bytes = buffer;
    if (!s_entropy_enabled) {
        memset(buffer, 0, length);
        return;
    }
    s_hardware_fill_calls++;
    s_fill_event = ++s_event;
    for (size_t i = 0; i < length; ++i) {
        bytes[i] = (unsigned char)(i + 1U);
    }
}

void bootloader_random_disable(void)
{
    s_disable_calls++;
    s_entropy_enabled = 0;
    s_disable_event = ++s_event;
}

SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *buffer)
{
    buffer->value = 1U;
    return buffer;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t handle, TickType_t ticks_to_wait)
{
    (void)ticks_to_wait;
    s_take_calls++;
    if (!handle || handle->value == 0U) {
        return 0;
    }
    handle->value = 0U;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t handle)
{
    s_give_calls++;
    if (!handle) {
        return 0;
    }
    handle->value = 1U;
    return pdTRUE;
}

void mbedtls_ctr_drbg_init(mbedtls_ctr_drbg_context *context)
{
    memset(context, 0, sizeof(*context));
}

void mbedtls_ctr_drbg_free(mbedtls_ctr_drbg_context *context)
{
    memset(context, 0, sizeof(*context));
}

void mbedtls_ctr_drbg_set_entropy_len(mbedtls_ctr_drbg_context *context,
                                      size_t length)
{
    (void)context;
    s_entropy_length = length;
}

int mbedtls_ctr_drbg_set_nonce_len(mbedtls_ctr_drbg_context *context,
                                   size_t length)
{
    (void)context;
    return length == 0U ? 0 : -1;
}

int mbedtls_ctr_drbg_seed(
    mbedtls_ctr_drbg_context *context,
    int (*entropy)(void *, unsigned char *, size_t), void *entropy_context,
    const unsigned char *custom, size_t custom_length)
{
    unsigned char seed[48] = {0};
    s_seed_event = ++s_event;
    if (!custom || custom_length != strlen("SIGUI D1L secure random v1") ||
        memcmp(custom, "SIGUI D1L secure random v1", custom_length) != 0 ||
        s_entropy_length != sizeof(seed) ||
        entropy(entropy_context, seed, sizeof(seed)) != 0) {
        return -1;
    }
    for (size_t i = 0; i < sizeof(seed); ++i) {
        s_seed_sum += seed[i];
    }
    context->seeded = 1;
    context->entropy = entropy;
    context->entropy_context = entropy_context;
    s_saved_entropy = entropy;
    s_saved_entropy_context = entropy_context;
    return 0;
}

void mbedtls_ctr_drbg_set_reseed_interval(mbedtls_ctr_drbg_context *context,
                                          int interval)
{
    context->reseed_interval = interval;
    s_reseed_interval = interval;
}

int mbedtls_ctr_drbg_random(void *opaque, unsigned char *output,
                            size_t output_length)
{
    mbedtls_ctr_drbg_context *context = opaque;
    s_random_calls++;
    if (!context || !context->seeded || s_random_fails) {
        return -1;
    }
    for (size_t i = 0; i < output_length; ++i) {
        output[i] = (unsigned char)(0xa0U + i + (s_seed_sum & 0x0fU));
    }
    return 0;
}

static int all_zero(const unsigned char *bytes, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        if (bytes[i] != 0U) {
            return 0;
        }
    }
    return 1;
}

int main(void)
{
    unsigned char output[16];
    memset(output, 0xa5, sizeof(output));
    if (d1l_secure_random_fill(output, sizeof(output)) !=
            ESP_ERR_INVALID_STATE ||
        !all_zero(output, sizeof(output)) || s_enable_calls != 0) {
        return 1;
    }
    if (d1l_secure_random_init() != ESP_OK ||
        !d1l_secure_random_ready() || d1l_secure_random_status() != ESP_OK) {
        return 2;
    }
    if (s_enable_calls != 1 || s_hardware_fill_calls != 1 ||
        s_disable_calls != 1 || s_entropy_enabled ||
        !(s_enable_event < s_fill_event && s_fill_event < s_disable_event &&
          s_disable_event < s_seed_event) ||
        s_seed_sum == 0U || s_reseed_interval != INT_MAX - 1) {
        return 3;
    }
    if (d1l_secure_random_init() != ESP_OK || s_enable_calls != 1 ||
        s_hardware_fill_calls != 1 || s_disable_calls != 1) {
        return 4;
    }
    memset(output, 0, sizeof(output));
    if (d1l_secure_random_fill(output, sizeof(output)) != ESP_OK ||
        all_zero(output, sizeof(output)) || s_take_calls != 1 ||
        s_give_calls != 1 || s_random_calls != 1 || s_enable_calls != 1) {
        return 5;
    }
    if (!s_saved_entropy ||
        s_saved_entropy(s_saved_entropy_context, output, 1U) !=
            MBEDTLS_ERR_CTR_DRBG_ENTROPY_SOURCE_FAILED) {
        return 6;
    }
    s_random_fails = 1;
    memset(output, 0xa5, sizeof(output));
    if (d1l_secure_random_fill(output, sizeof(output)) != ESP_FAIL ||
        !all_zero(output, sizeof(output)) || s_take_calls != 2 ||
        s_give_calls != 2 || s_enable_calls != 1 ||
        d1l_secure_random_ready() || d1l_secure_random_status() != ESP_FAIL) {
        return 7;
    }
    s_random_fails = 0;
    memset(output, 0xa5, sizeof(output));
    if (d1l_secure_random_fill(output, sizeof(output)) != ESP_FAIL ||
        !all_zero(output, sizeof(output)) || s_random_calls != 2 ||
        s_take_calls != 2 || s_give_calls != 2) {
        return 8;
    }
    if (d1l_secure_random_fill(NULL, sizeof(output)) != ESP_ERR_INVALID_ARG ||
        d1l_secure_random_fill(output, 0U) != ESP_ERR_INVALID_ARG ||
        d1l_secure_random_fill(output, D1L_SECURE_RANDOM_MAX_REQUEST + 1U) !=
            ESP_ERR_INVALID_ARG) {
        return 9;
    }
    puts("secure random boot seed and late DRBG: ok");
    return 0;
}
