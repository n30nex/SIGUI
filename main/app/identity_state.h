#pragma once

#include <stdbool.h>
#include <stdint.h>

#define D1L_IDENTITY_STATE_PUBLIC_KEY_LEN 32U
#define D1L_IDENTITY_STATE_PRIVATE_KEY_LEN 64U

typedef enum {
    D1L_IDENTITY_STATE_ABSENT = 0,
    D1L_IDENTITY_STATE_CONSISTENT,
    D1L_IDENTITY_STATE_INCONSISTENT,
} d1l_identity_state_t;

/*
 * Classifies persisted Ed25519 material without modifying it. A ready identity
 * is consistent only when its stored public key exactly matches the public key
 * derived from the stored private key. Any partial, residual, or mismatched
 * state is inconsistent rather than absent.
 */
d1l_identity_state_t d1l_identity_state_classify(
    bool identity_ready,
    const uint8_t public_key[D1L_IDENTITY_STATE_PUBLIC_KEY_LEN],
    const uint8_t private_key[D1L_IDENTITY_STATE_PRIVATE_KEY_LEN]);

const char *d1l_identity_state_name(d1l_identity_state_t state);
