#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MBEDTLS_MD_NONE = 0,
    MBEDTLS_MD_SHA256 = 6,
} mbedtls_md_type_t;

typedef struct {
    mbedtls_md_type_t type;
} mbedtls_md_info_t;

const mbedtls_md_info_t *mbedtls_md_info_from_type(mbedtls_md_type_t type);
int mbedtls_md_hmac(const mbedtls_md_info_t *info,
                    const unsigned char *key, size_t key_length,
                    const unsigned char *input, size_t input_length,
                    unsigned char *output);

#ifdef __cplusplus
}
#endif
