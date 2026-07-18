#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MBEDTLS_AES_DECRYPT 0
#define MBEDTLS_AES_ENCRYPT 1

typedef struct {
    uint8_t key[16];
    int ready;
} mbedtls_aes_context;

void mbedtls_aes_init(mbedtls_aes_context *context);
void mbedtls_aes_free(mbedtls_aes_context *context);
int mbedtls_aes_setkey_enc(mbedtls_aes_context *context,
                           const unsigned char *key,
                           unsigned int key_bits);
int mbedtls_aes_setkey_dec(mbedtls_aes_context *context,
                           const unsigned char *key,
                           unsigned int key_bits);
int mbedtls_aes_crypt_ecb(mbedtls_aes_context *context, int mode,
                          const unsigned char input[16],
                          unsigned char output[16]);

#ifdef __cplusplus
}
#endif
