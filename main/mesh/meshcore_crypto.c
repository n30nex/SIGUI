#include "meshcore_crypto.h"

#include <stdbool.h>
#include <string.h>

#include "mbedtls/aes.h"
#include "mbedtls/md.h"

static void crypto_secure_zero(void *data, size_t size)
{
    volatile uint8_t *bytes = (volatile uint8_t *)data;
    while (bytes && size > 0U) {
        *bytes++ = 0U;
        --size;
    }
}

static bool crypto_bytes_equal(const uint8_t *left, const uint8_t *right,
                               size_t size)
{
    uint8_t difference = 0U;
    for (size_t index = 0U; index < size; ++index) {
        difference |= (uint8_t)(left[index] ^ right[index]);
    }
    return difference == 0U;
}

esp_err_t d1l_meshcore_crypto_encrypt_then_mac(
    const uint8_t secret[D1L_MESHCORE_CRYPTO_SECRET_SIZE],
    uint8_t *destination, size_t destination_size,
    const uint8_t *source, size_t source_length,
    size_t *out_length)
{
    if (out_length) {
        *out_length = 0U;
    }
    if (!secret || !destination || !source || !out_length ||
        source_length == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (source_length > SIZE_MAX - (D1L_MESHCORE_CRYPTO_BLOCK_SIZE - 1U)) {
        return ESP_ERR_INVALID_SIZE;
    }
    const size_t encrypted_length =
        ((source_length + D1L_MESHCORE_CRYPTO_BLOCK_SIZE - 1U) /
         D1L_MESHCORE_CRYPTO_BLOCK_SIZE) * D1L_MESHCORE_CRYPTO_BLOCK_SIZE;
    if (encrypted_length > SIZE_MAX - D1L_MESHCORE_CRYPTO_MAC_SIZE ||
        D1L_MESHCORE_CRYPTO_MAC_SIZE + encrypted_length > destination_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    int result = mbedtls_aes_setkey_enc(&aes, secret, 128U);
    if (result != 0) {
        mbedtls_aes_free(&aes);
        return ESP_FAIL;
    }

    uint8_t block[D1L_MESHCORE_CRYPTO_BLOCK_SIZE];
    uint8_t *ciphertext = destination + D1L_MESHCORE_CRYPTO_MAC_SIZE;
    for (size_t offset = 0U; offset < encrypted_length;
         offset += sizeof(block)) {
        memset(block, 0, sizeof(block));
        const size_t remaining = source_length > offset ?
            source_length - offset : 0U;
        const size_t copy_length = remaining > sizeof(block) ?
            sizeof(block) : remaining;
        if (copy_length > 0U) {
            memcpy(block, source + offset, copy_length);
        }
        result = mbedtls_aes_crypt_ecb(
            &aes, MBEDTLS_AES_ENCRYPT, block, ciphertext + offset);
        if (result != 0) {
            mbedtls_aes_free(&aes);
            crypto_secure_zero(block, sizeof(block));
            return ESP_FAIL;
        }
    }
    mbedtls_aes_free(&aes);
    crypto_secure_zero(block, sizeof(block));

    const mbedtls_md_info_t *digest =
        mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!digest) {
        return ESP_FAIL;
    }
    uint8_t hmac[32];
    result = mbedtls_md_hmac(
        digest, secret, D1L_MESHCORE_CRYPTO_SECRET_SIZE,
        ciphertext, encrypted_length, hmac);
    if (result != 0) {
        crypto_secure_zero(hmac, sizeof(hmac));
        return ESP_FAIL;
    }
    memcpy(destination, hmac, D1L_MESHCORE_CRYPTO_MAC_SIZE);
    crypto_secure_zero(hmac, sizeof(hmac));
    *out_length = D1L_MESHCORE_CRYPTO_MAC_SIZE + encrypted_length;
    return ESP_OK;
}

size_t d1l_meshcore_crypto_decrypt_after_mac(
    const uint8_t secret[D1L_MESHCORE_CRYPTO_SECRET_SIZE],
    uint8_t *destination, size_t destination_size,
    const uint8_t *source, size_t source_length)
{
    if (!secret || !destination || !source ||
        source_length <= D1L_MESHCORE_CRYPTO_MAC_SIZE) {
        return 0U;
    }
    const size_t encrypted_length =
        source_length - D1L_MESHCORE_CRYPTO_MAC_SIZE;
    if ((encrypted_length % D1L_MESHCORE_CRYPTO_BLOCK_SIZE) != 0U ||
        encrypted_length > destination_size) {
        return 0U;
    }

    const mbedtls_md_info_t *digest =
        mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!digest) {
        return 0U;
    }
    uint8_t hmac[32];
    const int hmac_result = mbedtls_md_hmac(
        digest, secret, D1L_MESHCORE_CRYPTO_SECRET_SIZE,
        source + D1L_MESHCORE_CRYPTO_MAC_SIZE, encrypted_length, hmac);
    const bool authenticated = hmac_result == 0 &&
        crypto_bytes_equal(hmac, source, D1L_MESHCORE_CRYPTO_MAC_SIZE);
    crypto_secure_zero(hmac, sizeof(hmac));
    if (!authenticated) {
        return 0U;
    }

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    int result = mbedtls_aes_setkey_dec(&aes, secret, 128U);
    if (result != 0) {
        mbedtls_aes_free(&aes);
        return 0U;
    }
    for (size_t offset = 0U; offset < encrypted_length;
         offset += D1L_MESHCORE_CRYPTO_BLOCK_SIZE) {
        result = mbedtls_aes_crypt_ecb(
            &aes, MBEDTLS_AES_DECRYPT,
            source + D1L_MESHCORE_CRYPTO_MAC_SIZE + offset,
            destination + offset);
        if (result != 0) {
            mbedtls_aes_free(&aes);
            return 0U;
        }
    }
    mbedtls_aes_free(&aes);
    return encrypted_length;
}
