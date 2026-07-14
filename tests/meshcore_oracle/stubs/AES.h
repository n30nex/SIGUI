#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>

#ifndef AES_DEC_PREKEYED
#define AES_DEC_PREKEYED
#endif

extern "C" {
#include "../../../third_party/sensecap_indicator_esp32/components/LoRaWAN/soft-se/aes.h"
}

/*
 * Host adapter for the Arduino Crypto AES128 interface used by pinned
 * MeshCore Utils.cpp. The implementation is the already-vendored Seeed/Semtech
 * Brian Gladman AES source and is independently checked with a FIPS-197 KAT by
 * the oracle vector executable.
 */
class AES128 {
  public:
    void setKey(const uint8_t *key, std::size_t key_len)
    {
        if (key == nullptr || key_len != 16U ||
            aes_set_key(key, static_cast<length_type>(key_len), &context_) !=
                0U) {
            std::abort();
        }
        ready_ = true;
    }

    void encryptBlock(uint8_t *output, const uint8_t *input)
    {
        if (!ready_ || output == nullptr || input == nullptr ||
            lorawan_aes_encrypt(input, output, &context_) != 0U) {
            std::abort();
        }
    }

    void decryptBlock(uint8_t *output, const uint8_t *input)
    {
        if (!ready_ || output == nullptr || input == nullptr ||
            aes_decrypt(input, output, &context_) != 0U) {
            std::abort();
        }
    }

  private:
    aes_context context_{};
    bool ready_ = false;
};
