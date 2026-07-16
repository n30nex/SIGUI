#include <SHA256.h>

extern "C" {
#include "mbedtls/md.h"
}

namespace {

const mbedtls_md_info_t kSha256Info = {
    MBEDTLS_MD_SHA256,
};

} // namespace

extern "C" const mbedtls_md_info_t *
mbedtls_md_info_from_type(mbedtls_md_type_t type)
{
    return type == MBEDTLS_MD_SHA256 ? &kSha256Info : nullptr;
}

extern "C" int mbedtls_md(const mbedtls_md_info_t *info,
                          const unsigned char *input, std::size_t input_len,
                          unsigned char *output)
{
    if (info != &kSha256Info || input == nullptr || input_len == 0U ||
        output == nullptr) {
        return -1;
    }
    SHA256 sha;
    sha.update(input, input_len);
    sha.finalize(output, 32U);
    return 0;
}
