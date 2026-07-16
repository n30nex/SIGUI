#include "meshcore_identity_exchange.h"

#include <stddef.h>
#include <string.h>

#include "ed_25519.h"
#include "mesh/ed25519_canonical.h"

static void secure_zero(void *value, size_t size)
{
    volatile uint8_t *bytes = (volatile uint8_t *)value;
    while (bytes && size-- > 0U) {
        *bytes++ = 0U;
    }
}

bool d1l_meshcore_identity_derive_shared_secret(
    const uint8_t peer_public_key[D1L_MESHCORE_IDENTITY_KEY_BYTES],
    const uint8_t local_public_key[D1L_MESHCORE_IDENTITY_KEY_BYTES],
    const uint8_t local_private_key[D1L_MESHCORE_IDENTITY_PRIVATE_KEY_BYTES],
    uint8_t out_secret[D1L_MESHCORE_IDENTITY_KEY_BYTES])
{
    if (!out_secret) {
        return false;
    }
    secure_zero(out_secret, D1L_MESHCORE_IDENTITY_KEY_BYTES);
    if (!peer_public_key || !local_public_key || !local_private_key ||
        !d1l_ed25519_encoded_point_is_strict(peer_public_key) ||
        !d1l_ed25519_encoded_point_is_strict(local_public_key)) {
        return false;
    }

    uint8_t derived_local_public[D1L_MESHCORE_IDENTITY_KEY_BYTES] = {0};
    ed25519_derive_pub(derived_local_public, local_private_key);
    uint8_t local_difference = 0U;
    for (size_t i = 0U; i < sizeof(derived_local_public); ++i) {
        local_difference |=
            (uint8_t)(derived_local_public[i] ^ local_public_key[i]);
    }
    secure_zero(derived_local_public, sizeof(derived_local_public));
    if (local_difference != 0U) {
        return false;
    }

    uint8_t derived[D1L_MESHCORE_IDENTITY_KEY_BYTES] = {0};
    ed25519_key_exchange(derived, peer_public_key, local_private_key);
    uint8_t nonzero = 0U;
    for (size_t i = 0U; i < sizeof(derived); ++i) {
        nonzero |= derived[i];
    }
    if (nonzero == 0U) {
        secure_zero(derived, sizeof(derived));
        return false;
    }
    for (size_t i = 0U; i < sizeof(derived); ++i) {
        out_secret[i] = derived[i];
    }
    secure_zero(derived, sizeof(derived));
    return true;
}
