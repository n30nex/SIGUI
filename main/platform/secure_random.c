#include "secure_random.h"

#include <limits.h>
#include <stdbool.h>
#include <string.h>

#include "bootloader_random.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/ctr_drbg.h"

#define D1L_SECURE_RANDOM_SEED_BYTES 48U

typedef struct {
    uint8_t bytes[D1L_SECURE_RANDOM_SEED_BYTES];
    size_t offset;
    bool available;
} d1l_secure_random_seed_source_t;

static mbedtls_ctr_drbg_context s_drbg;
static d1l_secure_random_seed_source_t s_seed_source;
static StaticSemaphore_t s_mutex_storage;
static SemaphoreHandle_t s_mutex;
static bool s_init_attempted;
static bool s_ready;
static esp_err_t s_status = ESP_ERR_INVALID_STATE;

static void clear_sensitive_bytes(void *data, size_t length)
{
    volatile uint8_t *cursor = data;
    while (cursor && length > 0U) {
        *cursor++ = 0U;
        length--;
    }
}

static int consume_boot_seed(void *context, unsigned char *output,
                             size_t length)
{
    d1l_secure_random_seed_source_t *source = context;
    if (!source || !output || !source->available || length == 0U ||
        source->offset > sizeof(source->bytes) ||
        length > sizeof(source->bytes) - source->offset) {
        return MBEDTLS_ERR_CTR_DRBG_ENTROPY_SOURCE_FAILED;
    }
    memcpy(output, source->bytes + source->offset, length);
    clear_sensitive_bytes(source->bytes + source->offset, length);
    source->offset += length;
    return 0;
}

esp_err_t d1l_secure_random_init(void)
{
    if (s_init_attempted) {
        return s_status;
    }
    s_init_attempted = true;

    s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_storage);
    if (!s_mutex) {
        s_status = ESP_ERR_NO_MEM;
        return s_status;
    }

    mbedtls_ctr_drbg_init(&s_drbg);
    memset(&s_seed_source, 0, sizeof(s_seed_source));

    /* Espressif documents this as safe only during early application startup,
     * before ADC, Wi-Fi, or Bluetooth ownership. app_main calls this function
     * before NVS, board, radio, connectivity, or UI initialization. */
    bootloader_random_enable();
    esp_fill_random(s_seed_source.bytes, sizeof(s_seed_source.bytes));
    bootloader_random_disable();
    s_seed_source.available = true;

    mbedtls_ctr_drbg_set_entropy_len(&s_drbg,
                                     D1L_SECURE_RANDOM_SEED_BYTES);
    const int nonce_ret = mbedtls_ctr_drbg_set_nonce_len(&s_drbg, 0U);
    static const unsigned char personalization[] =
        "SIGUI D1L secure random v1";
    const int seed_ret = nonce_ret == 0 ?
        mbedtls_ctr_drbg_seed(
            &s_drbg, consume_boot_seed, &s_seed_source, personalization,
            sizeof(personalization) - 1U) : nonce_ret;

    s_seed_source.available = false;
    clear_sensitive_bytes(s_seed_source.bytes,
                          sizeof(s_seed_source.bytes));
    s_seed_source.offset = 0U;
    if (seed_ret != 0) {
        mbedtls_ctr_drbg_free(&s_drbg);
        s_status = ESP_FAIL;
        return s_status;
    }

    /* No later reseed is allowed to reach a hardware RNG whose entropy state
     * depends on optional RF. INT_MAX - 1 is process-lifetime for this bounded
     * firmware use and leaves one signed-counter value for mbedTLS to enter
     * its reseed path without overflowing; the retained callback then has no
     * seed and fails closed. */
    mbedtls_ctr_drbg_set_reseed_interval(&s_drbg, INT_MAX - 1);
    s_ready = true;
    s_status = ESP_OK;
    return ESP_OK;
}

esp_err_t d1l_secure_random_fill(void *dest, size_t length)
{
    if (!dest || length == 0U || length > D1L_SECURE_RANDOM_MAX_REQUEST) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready || !s_mutex) {
        clear_sensitive_bytes(dest, length);
        return s_status == ESP_OK ? ESP_ERR_INVALID_STATE : s_status;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        clear_sensitive_bytes(dest, length);
        return ESP_ERR_TIMEOUT;
    }
    /* A caller may have waited while the previous owner latched the DRBG
     * failed. Recheck under the same mutex that protects generation so no
     * queued caller can touch a failed context. */
    if (!s_ready) {
        const esp_err_t failed_status = s_status;
        (void)xSemaphoreGive(s_mutex);
        clear_sensitive_bytes(dest, length);
        return failed_status == ESP_OK ? ESP_ERR_INVALID_STATE : failed_status;
    }
    const int random_ret = mbedtls_ctr_drbg_random(&s_drbg, dest, length);
    if (random_ret != 0) {
        s_ready = false;
        s_status = ESP_FAIL;
    }
    (void)xSemaphoreGive(s_mutex);
    if (random_ret != 0) {
        clear_sensitive_bytes(dest, length);
        return s_status;
    }
    return ESP_OK;
}

bool d1l_secure_random_ready(void)
{
    return s_ready;
}

esp_err_t d1l_secure_random_status(void)
{
    return s_status;
}
