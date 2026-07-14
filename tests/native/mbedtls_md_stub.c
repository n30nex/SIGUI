#include "mbedtls/md.h"

#include <string.h>

static const mbedtls_md_info_t s_sha256 = {
    .type = MBEDTLS_MD_SHA256,
};

const mbedtls_md_info_t *mbedtls_md_info_from_type(mbedtls_md_type_t md_type)
{
    return md_type == MBEDTLS_MD_SHA256 ? &s_sha256 : NULL;
}

int mbedtls_md(const mbedtls_md_info_t *md_info, const unsigned char *input,
               size_t ilen, unsigned char *output)
{
    if (!md_info || md_info->type != MBEDTLS_MD_SHA256 || !input ||
        ilen == 0U || !output) {
        return -1;
    }
    memset(output, 0, 32U);
    /* This host-only stub is deliberately deterministic, not cryptographic.
     * Production links ESP-IDF mbedTLS. Keeping byte zero dependent on the
     * first secret byte lets the native store test exercise bounded hash
     * collisions without weakening or replacing production crypto. */
    output[0] = (uint8_t)(input[0] ^ (uint8_t)ilen);
    for (size_t i = 0U; i < ilen; ++i) {
        output[1U + (i % 31U)] ^= (uint8_t)(input[i] + (uint8_t)i);
    }
    return 0;
}
