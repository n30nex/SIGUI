#ifndef D1L_ED25519_CANONICAL_H
#define D1L_ED25519_CANONICAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define D1L_ED25519_SCALAR_BYTES 32U
#define D1L_ED25519_SIGNATURE_BYTES 64U

/*
 * Ed25519 signatures encode S little-endian in bytes 32..63. The vendored
 * verifier rejects only the top three bits, so reject S >= L before calling
 * it. S is attacker-controlled public input; an early-exit comparison does
 * not expose secret material.
 */
static inline bool d1l_ed25519_signature_s_is_canonical(
    const uint8_t signature[D1L_ED25519_SIGNATURE_BYTES])
{
    static const uint8_t group_order[D1L_ED25519_SCALAR_BYTES] = {
        0xEDU, 0xD3U, 0xF5U, 0x5CU, 0x1AU, 0x63U, 0x12U, 0x58U,
        0xD6U, 0x9CU, 0xF7U, 0xA2U, 0xDEU, 0xF9U, 0xDEU, 0x14U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x10U,
    };
    if (signature == NULL) {
        return false;
    }
    for (size_t remaining = D1L_ED25519_SCALAR_BYTES; remaining > 0U;
         --remaining) {
        const size_t index = remaining - 1U;
        const uint8_t value =
            signature[D1L_ED25519_SCALAR_BYTES + index];
        if (value < group_order[index]) {
            return true;
        }
        if (value > group_order[index]) {
            return false;
        }
    }
    return false;
}

#endif
