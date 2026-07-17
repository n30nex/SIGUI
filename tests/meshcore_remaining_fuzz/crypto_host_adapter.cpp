#include <cstddef>
#include <cstdint>
#include <cstring>

#include "mbedtls/aes.h"
#include "mbedtls/md.h"

#include "AES.h"
#include "SHA256.h"

extern "C" void mbedtls_aes_init(mbedtls_aes_context *context)
{
    if (context != nullptr) {
        std::memset(context, 0, sizeof(*context));
    }
}

extern "C" void mbedtls_aes_free(mbedtls_aes_context *context)
{
    if (context != nullptr) {
        volatile uint8_t *bytes =
            reinterpret_cast<volatile uint8_t *>(context);
        for (std::size_t index = 0U; index < sizeof(*context); ++index) {
            bytes[index] = 0U;
        }
    }
}

static int set_key(mbedtls_aes_context *context, const unsigned char *key,
                   unsigned int key_bits)
{
    if (context == nullptr || key == nullptr || key_bits != 128U) {
        return -1;
    }
    std::memcpy(context->key, key, sizeof(context->key));
    context->ready = 1;
    return 0;
}

extern "C" int mbedtls_aes_setkey_enc(
    mbedtls_aes_context *context, const unsigned char *key,
    unsigned int key_bits)
{
    return set_key(context, key, key_bits);
}

extern "C" int mbedtls_aes_setkey_dec(
    mbedtls_aes_context *context, const unsigned char *key,
    unsigned int key_bits)
{
    return set_key(context, key, key_bits);
}

extern "C" int mbedtls_aes_crypt_ecb(
    mbedtls_aes_context *context, int mode,
    const unsigned char input[16], unsigned char output[16])
{
    if (context == nullptr || context->ready != 1 || input == nullptr ||
        output == nullptr ||
        (mode != MBEDTLS_AES_ENCRYPT && mode != MBEDTLS_AES_DECRYPT)) {
        return -1;
    }
    AES128 aes;
    aes.setKey(context->key, sizeof(context->key));
    if (mode == MBEDTLS_AES_ENCRYPT) {
        aes.encryptBlock(output, input);
    } else {
        aes.decryptBlock(output, input);
    }
    return 0;
}

extern "C" const mbedtls_md_info_t *mbedtls_md_info_from_type(
    mbedtls_md_type_t type)
{
    static const mbedtls_md_info_t sha256 = {MBEDTLS_MD_SHA256};
    return type == MBEDTLS_MD_SHA256 ? &sha256 : nullptr;
}

extern "C" int mbedtls_md_hmac(
    const mbedtls_md_info_t *info, const unsigned char *key,
    size_t key_length, const unsigned char *input, size_t input_length,
    unsigned char *output)
{
    if (info == nullptr || info->type != MBEDTLS_MD_SHA256 || key == nullptr ||
        output == nullptr || (input == nullptr && input_length != 0U)) {
        return -1;
    }
    SHA256 sha256;
    sha256.resetHMAC(key, key_length);
    sha256.update(input, input_length);
    sha256.finalizeHMAC(key, key_length, output, 32U);
    return 0;
}
