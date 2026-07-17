#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "mesh/meshcore_crypto.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    uint8_t secret[D1L_MESHCORE_CRYPTO_SECRET_SIZE];
    for (size_t index = 0U; index < sizeof(secret); ++index) {
        secret[index] = size > 0U ? data[index % size] : (uint8_t)index;
    }
    const size_t plaintext_length =
        1U + (size > 0U ? (size_t)(data[0] & 0x7fU) : 0U);
    uint8_t plaintext[128];
    for (size_t index = 0U; index < plaintext_length; ++index) {
        plaintext[index] = size > 0U ?
            data[(index + 1U) % size] : (uint8_t)(index * 17U);
    }

    uint8_t encrypted[130];
    memset(encrypted, 0xa5, sizeof(encrypted));
    size_t encrypted_length = 0U;
    assert(d1l_meshcore_crypto_encrypt_then_mac(
               secret, encrypted, sizeof(encrypted), plaintext,
               plaintext_length, &encrypted_length) == ESP_OK);
    const size_t padded_length =
        ((plaintext_length + D1L_MESHCORE_CRYPTO_BLOCK_SIZE - 1U) /
         D1L_MESHCORE_CRYPTO_BLOCK_SIZE) * D1L_MESHCORE_CRYPTO_BLOCK_SIZE;
    assert(encrypted_length ==
           D1L_MESHCORE_CRYPTO_MAC_SIZE + padded_length);

    uint8_t decrypted[128];
    memset(decrypted, 0x5a, sizeof(decrypted));
    assert(d1l_meshcore_crypto_decrypt_after_mac(
               secret, decrypted, sizeof(decrypted), encrypted,
               encrypted_length) == padded_length);
    assert(memcmp(decrypted, plaintext, plaintext_length) == 0);
    for (size_t index = plaintext_length; index < padded_length; ++index) {
        assert(decrypted[index] == 0U);
    }

    uint8_t tampered[sizeof(encrypted)];
    memcpy(tampered, encrypted, encrypted_length);
    tampered[0] ^= 0x01U;
    memset(decrypted, 0x3c, sizeof(decrypted));
    assert(d1l_meshcore_crypto_decrypt_after_mac(
               secret, decrypted, sizeof(decrypted), tampered,
               encrypted_length) == 0U);
    for (size_t index = 0U; index < sizeof(decrypted); ++index) {
        assert(decrypted[index] == 0x3cU);
    }

    uint8_t too_small[130];
    memset(too_small, 0x69, sizeof(too_small));
    size_t rejected_length = SIZE_MAX;
    assert(d1l_meshcore_crypto_encrypt_then_mac(
               secret, too_small, encrypted_length - 1U, plaintext,
               plaintext_length, &rejected_length) == ESP_ERR_INVALID_SIZE);
    assert(rejected_length == 0U);
    for (size_t index = 0U; index < sizeof(too_small); ++index) {
        assert(too_small[index] == 0x69U);
    }
    memset(decrypted, 0x96, sizeof(decrypted));
    assert(d1l_meshcore_crypto_decrypt_after_mac(
               secret, decrypted, padded_length - 1U, encrypted,
               encrypted_length) == 0U);
    for (size_t index = 0U; index < sizeof(decrypted); ++index) {
        assert(decrypted[index] == 0x96U);
    }
    return 0;
}
