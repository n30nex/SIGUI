#ifndef D1L_ED25519_CANONICAL_H
#define D1L_ED25519_CANONICAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif
#include "ge.h"
#ifdef __cplusplus
}
#endif

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

/*
 * Ed25519 encodes a point as a little-endian field element y plus the sign of
 * x in the top bit. The vendored decoder reduces non-canonical y encodings and
 * accepts low-order points. Reject both classes before signature verification:
 * a low-order public key can otherwise make the verification equation
 * independent of the signed message.
 *
 * Point bytes are public attacker-controlled input. This check is therefore
 * deliberately simple and need not be constant-time.
 */
static inline bool d1l_ed25519_encoded_point_is_strict(
    const uint8_t point[D1L_ED25519_SCALAR_BYTES])
{
    static const uint8_t field_prime[D1L_ED25519_SCALAR_BYTES] = {
        0xEDU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
        0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
        0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
        0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x7FU,
    };
    static const uint8_t identity[D1L_ED25519_SCALAR_BYTES] = {0x01U};
    if (point == NULL) {
        return false;
    }

    bool canonical_y = false;
    for (size_t remaining = D1L_ED25519_SCALAR_BYTES; remaining > 0U;
         --remaining) {
        const size_t index = remaining - 1U;
        const uint8_t value = index == D1L_ED25519_SCALAR_BYTES - 1U
                                  ? (uint8_t)(point[index] & 0x7FU)
                                  : point[index];
        if (value < field_prime[index]) {
            canonical_y = true;
            break;
        }
        if (value > field_prime[index]) {
            return false;
        }
    }
    if (!canonical_y) {
        return false;
    }

    ge_p3 decoded;
    if (ge_frombytes_negate_vartime(&decoded, point) != 0) {
        return false;
    }
    for (size_t doubling = 0U; doubling < 3U; ++doubling) {
        ge_p1p1 doubled;
        ge_p3_dbl(&doubled, &decoded);
        ge_p1p1_to_p3(&decoded, &doubled);
    }
    uint8_t multiplied_by_cofactor[D1L_ED25519_SCALAR_BYTES];
    ge_p3_tobytes(multiplied_by_cofactor, &decoded);
    return memcmp(multiplied_by_cofactor, identity, sizeof(identity)) != 0;
}

#endif
