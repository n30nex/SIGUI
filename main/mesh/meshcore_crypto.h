#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define D1L_MESHCORE_CRYPTO_SECRET_SIZE 32U
#define D1L_MESHCORE_CRYPTO_BLOCK_SIZE 16U
#define D1L_MESHCORE_CRYPTO_MAC_SIZE 2U

#ifdef __cplusplus
extern "C" {
#endif

/* MeshCore's pinned packet primitive: AES-128 ECB with zero padding, followed
 * by a two-byte prefix of HMAC-SHA-256 over the ciphertext. */
esp_err_t d1l_meshcore_crypto_encrypt_then_mac(
    const uint8_t secret[D1L_MESHCORE_CRYPTO_SECRET_SIZE],
    uint8_t *destination, size_t destination_size,
    const uint8_t *source, size_t source_length,
    size_t *out_length);

/* Authenticate before decrypting. Authentication or shape rejection leaves
 * destination bytes untouched and returns zero. */
size_t d1l_meshcore_crypto_decrypt_after_mac(
    const uint8_t secret[D1L_MESHCORE_CRYPTO_SECRET_SIZE],
    uint8_t *destination, size_t destination_size,
    const uint8_t *source, size_t source_length);

#ifdef __cplusplus
}
#endif
