#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mesh/meshcore_crypto.h"

static void test_fixed_vector(void)
{
    uint8_t secret[D1L_MESHCORE_CRYPTO_SECRET_SIZE];
    for (size_t index = 0U; index < sizeof(secret); ++index) {
        secret[index] = (uint8_t)index;
    }
    const uint8_t plaintext[] = {0x78U, 0x56U, 0x34U, 0x12U};
    const uint8_t expected[] = {
        0x44U, 0xe4U, 0x29U, 0x8fU, 0xdcU, 0x7dU,
        0xa6U, 0x2cU, 0x81U, 0xabU, 0x4eU, 0x54U,
        0xc6U, 0x7dU, 0x6bU, 0xffU, 0xe7U, 0x3eU,
    };
    uint8_t encrypted[sizeof(expected)] = {0};
    size_t encrypted_length = 0U;
    assert(d1l_meshcore_crypto_encrypt_then_mac(
               secret, encrypted, sizeof(encrypted), plaintext,
               sizeof(plaintext), &encrypted_length) == ESP_OK);
    assert(encrypted_length == sizeof(expected));
    assert(memcmp(encrypted, expected, sizeof(expected)) == 0);

    uint8_t decrypted[D1L_MESHCORE_CRYPTO_BLOCK_SIZE] = {0xa5U};
    assert(d1l_meshcore_crypto_decrypt_after_mac(
               secret, decrypted, sizeof(decrypted), encrypted,
               encrypted_length) == sizeof(decrypted));
    assert(memcmp(decrypted, plaintext, sizeof(plaintext)) == 0);
    for (size_t index = sizeof(plaintext); index < sizeof(decrypted); ++index) {
        assert(decrypted[index] == 0U);
    }
}

static void test_bounds_and_auth_before_decrypt(void)
{
    uint8_t secret[D1L_MESHCORE_CRYPTO_SECRET_SIZE] = {0};
    uint8_t plaintext[33];
    for (size_t index = 0U; index < sizeof(plaintext); ++index) {
        plaintext[index] = (uint8_t)(0x80U + index);
    }
    uint8_t encrypted[50];
    memset(encrypted, 0xa5, sizeof(encrypted));
    size_t encrypted_length = 99U;
    assert(d1l_meshcore_crypto_encrypt_then_mac(
               secret, encrypted, sizeof(encrypted) - 1U, plaintext,
               sizeof(plaintext), &encrypted_length) == ESP_ERR_INVALID_SIZE);
    assert(encrypted_length == 0U);
    for (size_t index = 0U; index < sizeof(encrypted); ++index) {
        assert(encrypted[index] == 0xa5U);
    }

    assert(d1l_meshcore_crypto_encrypt_then_mac(
               secret, encrypted, sizeof(encrypted), plaintext,
               sizeof(plaintext), &encrypted_length) == ESP_OK);
    assert(encrypted_length == sizeof(encrypted));
    uint8_t tampered[sizeof(encrypted)];
    memcpy(tampered, encrypted, sizeof(tampered));
    tampered[0] ^= 0x01U;
    uint8_t destination[48];
    memset(destination, 0x5a, sizeof(destination));
    assert(d1l_meshcore_crypto_decrypt_after_mac(
               secret, destination, sizeof(destination), tampered,
               sizeof(tampered)) == 0U);
    for (size_t index = 0U; index < sizeof(destination); ++index) {
        assert(destination[index] == 0x5aU);
    }
    assert(d1l_meshcore_crypto_decrypt_after_mac(
               secret, destination, sizeof(destination), encrypted,
               sizeof(encrypted) - 1U) == 0U);
    for (size_t index = 0U; index < sizeof(destination); ++index) {
        assert(destination[index] == 0x5aU);
    }
}

int main(void)
{
    test_fixed_vector();
    test_bounds_and_auth_before_decrypt();
    puts("native MeshCore decrypt/auth crypto: ok");
    return 0;
}
