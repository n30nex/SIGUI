#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "mesh/meshcore_crypto.h"
#include "mesh/meshcore_wire.h"

#define D1L_MESHCORE_FUZZ_MAX_DECRYPTED_BYTES \
    (((D1L_MESHCORE_MAX_RAW_PACKET - D1L_MESHCORE_CRYPTO_MAC_SIZE) / \
      D1L_MESHCORE_CRYPTO_BLOCK_SIZE) * D1L_MESHCORE_CRYPTO_BLOCK_SIZE)

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    uint8_t secret[D1L_MESHCORE_CRYPTO_SECRET_SIZE];
    for (size_t index = 0U; index < sizeof(secret); ++index) {
        secret[index] = size > 0U ? data[index % size] : (uint8_t)index;
    }
    const uint8_t plaintext_selector = size > 1U ? data[1] :
        (size > 0U ? data[0] : 0U);
    const size_t plaintext_length = 1U +
        ((size_t)plaintext_selector % D1L_MESHCORE_FUZZ_MAX_DECRYPTED_BYTES);
    uint8_t plaintext[D1L_MESHCORE_FUZZ_MAX_DECRYPTED_BYTES];
    for (size_t index = 0U; index < plaintext_length; ++index) {
        plaintext[index] = size > 0U ?
            data[(index + 1U) % size] : (uint8_t)(index * 17U);
    }

    uint8_t encrypted[D1L_MESHCORE_CRYPTO_MAC_SIZE +
                      D1L_MESHCORE_FUZZ_MAX_DECRYPTED_BYTES];
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

    uint8_t decrypted[D1L_MESHCORE_FUZZ_MAX_DECRYPTED_BYTES];
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

    uint8_t too_small[sizeof(encrypted)];
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

    /* Exercise attacker-controlled ciphertext independently of the valid
     * round trip above, including the largest block-aligned authenticated
     * payload that can fit inside one production raw packet. */
    uint8_t attacker_source[D1L_MESHCORE_MAX_RAW_PACKET];
    for (size_t index = 0U; index < sizeof(attacker_source); ++index) {
        attacker_source[index] = size > 1U ?
            data[1U + (index % (size - 1U))] : (uint8_t)(index * 31U);
    }
    const size_t attacker_length = size > 0U ? data[0] : 0U;
    uint8_t attacker_destination[D1L_MESHCORE_FUZZ_MAX_DECRYPTED_BYTES];
    memset(attacker_destination, 0xc7, sizeof(attacker_destination));
    const size_t attacker_plaintext_length =
        d1l_meshcore_crypto_decrypt_after_mac(
            secret, attacker_destination, sizeof(attacker_destination),
            attacker_source, attacker_length);
    if (attacker_plaintext_length == 0U) {
        for (size_t index = 0U; index < sizeof(attacker_destination); ++index) {
            assert(attacker_destination[index] == 0xc7U);
        }
    } else {
        assert(attacker_length > D1L_MESHCORE_CRYPTO_MAC_SIZE);
        assert(attacker_plaintext_length ==
               attacker_length - D1L_MESHCORE_CRYPTO_MAC_SIZE);
        assert((attacker_plaintext_length %
                D1L_MESHCORE_CRYPTO_BLOCK_SIZE) == 0U);
        assert(attacker_plaintext_length <=
               D1L_MESHCORE_FUZZ_MAX_DECRYPTED_BYTES);
    }
    return 0;
}
