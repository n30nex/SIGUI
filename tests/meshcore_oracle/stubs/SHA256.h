#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

/*
 * Deterministic host-only SHA-256/HMAC-SHA-256 adapter for the Arduino Crypto
 * interface used by pinned MeshCore Utils.cpp. It is intentionally not linked
 * into firmware. The oracle checks it against FIPS 180-4 and RFC 4231 vectors
 * before using it for protocol vectors.
 */
class SHA256 {
  public:
    SHA256() { reset(); }

    void update(const void *source, std::size_t length)
    {
        if (length > 0U && source == nullptr) {
            std::abort();
        }
        const auto *bytes = static_cast<const uint8_t *>(source);
        total_bytes_ += length;
        while (length > 0U) {
            const std::size_t chunk =
                length < block_.size() - block_size_
                    ? length
                    : block_.size() - block_size_;
            std::memcpy(&block_[block_size_], bytes, chunk);
            block_size_ += chunk;
            bytes += chunk;
            length -= chunk;
            if (block_size_ == block_.size()) {
                transform(block_.data());
                block_size_ = 0U;
            }
        }
    }

    void finalize(uint8_t *output, std::size_t output_len)
    {
        if (output == nullptr || output_len > 32U) {
            std::abort();
        }
        const uint64_t message_bits = total_bytes_ * 8U;
        block_[block_size_++] = 0x80U;
        if (block_size_ > 56U) {
            while (block_size_ < block_.size()) {
                block_[block_size_++] = 0U;
            }
            transform(block_.data());
            block_size_ = 0U;
        }
        while (block_size_ < 56U) {
            block_[block_size_++] = 0U;
        }
        for (std::size_t index = 0U; index < 8U; ++index) {
            block_[63U - index] =
                static_cast<uint8_t>(message_bits >> (index * 8U));
        }
        transform(block_.data());

        std::array<uint8_t, 32U> digest{};
        for (std::size_t word = 0U; word < state_.size(); ++word) {
            digest[word * 4U] = static_cast<uint8_t>(state_[word] >> 24U);
            digest[word * 4U + 1U] =
                static_cast<uint8_t>(state_[word] >> 16U);
            digest[word * 4U + 2U] =
                static_cast<uint8_t>(state_[word] >> 8U);
            digest[word * 4U + 3U] = static_cast<uint8_t>(state_[word]);
        }
        std::memcpy(output, digest.data(), output_len);
        reset();
    }

    void resetHMAC(const void *key, std::size_t key_len)
    {
        std::array<uint8_t, 64U> normalized{};
        if (key_len > 0U && key == nullptr) {
            std::abort();
        }
        if (key_len > normalized.size()) {
            SHA256 key_hash;
            key_hash.update(key, key_len);
            key_hash.finalize(normalized.data(), 32U);
        } else if (key_len > 0U) {
            std::memcpy(normalized.data(), key, key_len);
        }
        std::array<uint8_t, 64U> inner_pad{};
        for (std::size_t index = 0U; index < normalized.size(); ++index) {
            inner_pad[index] = static_cast<uint8_t>(normalized[index] ^ 0x36U);
            outer_pad_[index] =
                static_cast<uint8_t>(normalized[index] ^ 0x5CU);
        }
        reset();
        update(inner_pad.data(), inner_pad.size());
        hmac_ready_ = true;
    }

    void finalizeHMAC(const void *key, std::size_t key_len, uint8_t *output,
                      std::size_t output_len)
    {
        (void)key;
        (void)key_len;
        if (!hmac_ready_) {
            std::abort();
        }
        std::array<uint8_t, 32U> inner_digest{};
        finalize(inner_digest.data(), inner_digest.size());
        update(outer_pad_.data(), outer_pad_.size());
        update(inner_digest.data(), inner_digest.size());
        finalize(output, output_len);
        hmac_ready_ = false;
    }

  private:
    static constexpr std::array<uint32_t, 64U> kRoundConstants = {
        0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U,
        0x3956C25BU, 0x59F111F1U, 0x923F82A4U, 0xAB1C5ED5U,
        0xD807AA98U, 0x12835B01U, 0x243185BEU, 0x550C7DC3U,
        0x72BE5D74U, 0x80DEB1FEU, 0x9BDC06A7U, 0xC19BF174U,
        0xE49B69C1U, 0xEFBE4786U, 0x0FC19DC6U, 0x240CA1CCU,
        0x2DE92C6FU, 0x4A7484AAU, 0x5CB0A9DCU, 0x76F988DAU,
        0x983E5152U, 0xA831C66DU, 0xB00327C8U, 0xBF597FC7U,
        0xC6E00BF3U, 0xD5A79147U, 0x06CA6351U, 0x14292967U,
        0x27B70A85U, 0x2E1B2138U, 0x4D2C6DFCU, 0x53380D13U,
        0x650A7354U, 0x766A0ABBU, 0x81C2C92EU, 0x92722C85U,
        0xA2BFE8A1U, 0xA81A664BU, 0xC24B8B70U, 0xC76C51A3U,
        0xD192E819U, 0xD6990624U, 0xF40E3585U, 0x106AA070U,
        0x19A4C116U, 0x1E376C08U, 0x2748774CU, 0x34B0BCB5U,
        0x391C0CB3U, 0x4ED8AA4AU, 0x5B9CCA4FU, 0x682E6FF3U,
        0x748F82EEU, 0x78A5636FU, 0x84C87814U, 0x8CC70208U,
        0x90BEFFFAU, 0xA4506CEBU, 0xBEF9A3F7U, 0xC67178F2U,
    };

    static uint32_t rotate_right(uint32_t value, uint32_t shift)
    {
        return (value >> shift) | (value << (32U - shift));
    }

    void reset()
    {
        state_ = {0x6A09E667U, 0xBB67AE85U, 0x3C6EF372U, 0xA54FF53AU,
                  0x510E527FU, 0x9B05688CU, 0x1F83D9ABU, 0x5BE0CD19U};
        block_.fill(0U);
        block_size_ = 0U;
        total_bytes_ = 0U;
    }

    void transform(const uint8_t block[64])
    {
        std::array<uint32_t, 64U> words{};
        for (std::size_t index = 0U; index < 16U; ++index) {
            words[index] =
                (static_cast<uint32_t>(block[index * 4U]) << 24U) |
                (static_cast<uint32_t>(block[index * 4U + 1U]) << 16U) |
                (static_cast<uint32_t>(block[index * 4U + 2U]) << 8U) |
                static_cast<uint32_t>(block[index * 4U + 3U]);
        }
        for (std::size_t index = 16U; index < words.size(); ++index) {
            const uint32_t before = words[index - 15U];
            const uint32_t after = words[index - 2U];
            const uint32_t sigma0 = rotate_right(before, 7U) ^
                                    rotate_right(before, 18U) ^ (before >> 3U);
            const uint32_t sigma1 = rotate_right(after, 17U) ^
                                    rotate_right(after, 19U) ^ (after >> 10U);
            words[index] = words[index - 16U] + sigma0 + words[index - 7U] +
                           sigma1;
        }

        uint32_t a = state_[0];
        uint32_t b = state_[1];
        uint32_t c = state_[2];
        uint32_t d = state_[3];
        uint32_t e = state_[4];
        uint32_t f = state_[5];
        uint32_t g = state_[6];
        uint32_t h = state_[7];
        for (std::size_t index = 0U; index < words.size(); ++index) {
            const uint32_t sum1 = rotate_right(e, 6U) ^ rotate_right(e, 11U) ^
                                  rotate_right(e, 25U);
            const uint32_t choice = (e & f) ^ ((~e) & g);
            const uint32_t temp1 = h + sum1 + choice + kRoundConstants[index] +
                                   words[index];
            const uint32_t sum0 = rotate_right(a, 2U) ^ rotate_right(a, 13U) ^
                                  rotate_right(a, 22U);
            const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = sum0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<uint32_t, 8U> state_{};
    std::array<uint8_t, 64U> block_{};
    std::array<uint8_t, 64U> outer_pad_{};
    std::size_t block_size_ = 0U;
    uint64_t total_bytes_ = 0U;
    bool hmac_ready_ = false;
};
