#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define D1L_MESHCORE_IDENTITY_KEY_BYTES 32U
#define D1L_MESHCORE_IDENTITY_PRIVATE_KEY_BYTES 64U

/* Derive one runtime ECDH secret only from strict Ed25519 public points.
 * Inputs and output must not overlap. On every rejection the output is zero.
 */
bool d1l_meshcore_identity_derive_shared_secret(
    const uint8_t peer_public_key[D1L_MESHCORE_IDENTITY_KEY_BYTES],
    const uint8_t local_public_key[D1L_MESHCORE_IDENTITY_KEY_BYTES],
    const uint8_t local_private_key[D1L_MESHCORE_IDENTITY_PRIVATE_KEY_BYTES],
    uint8_t out_secret[D1L_MESHCORE_IDENTITY_KEY_BYTES]);

#ifdef __cplusplus
}
#endif
