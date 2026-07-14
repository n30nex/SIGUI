#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum {
    MBEDTLS_MD_NONE = 0,
    MBEDTLS_MD_SHA256 = 6,
} mbedtls_md_type_t;

typedef struct mbedtls_md_info_t {
    mbedtls_md_type_t type;
} mbedtls_md_info_t;

const mbedtls_md_info_t *mbedtls_md_info_from_type(mbedtls_md_type_t md_type);
int mbedtls_md(const mbedtls_md_info_t *md_info, const unsigned char *input,
               size_t ilen, unsigned char *output);
