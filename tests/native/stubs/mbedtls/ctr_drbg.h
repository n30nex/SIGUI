#pragma once

#include <stddef.h>

#define MBEDTLS_ERR_CTR_DRBG_ENTROPY_SOURCE_FAILED (-0x0034)

typedef struct {
    int seeded;
    int reseed_interval;
    int (*entropy)(void *, unsigned char *, size_t);
    void *entropy_context;
} mbedtls_ctr_drbg_context;

void mbedtls_ctr_drbg_init(mbedtls_ctr_drbg_context *context);
void mbedtls_ctr_drbg_free(mbedtls_ctr_drbg_context *context);
void mbedtls_ctr_drbg_set_entropy_len(mbedtls_ctr_drbg_context *context,
                                      size_t length);
int mbedtls_ctr_drbg_set_nonce_len(mbedtls_ctr_drbg_context *context,
                                   size_t length);
int mbedtls_ctr_drbg_seed(
    mbedtls_ctr_drbg_context *context,
    int (*entropy)(void *, unsigned char *, size_t), void *entropy_context,
    const unsigned char *custom, size_t custom_length);
void mbedtls_ctr_drbg_set_reseed_interval(mbedtls_ctr_drbg_context *context,
                                          int interval);
int mbedtls_ctr_drbg_random(void *context, unsigned char *output,
                            size_t output_length);
