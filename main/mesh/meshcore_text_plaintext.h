#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mesh/user_text.h"

typedef struct {
    const uint8_t *text;
    size_t text_length;
    d1l_user_text_info_t text_info;
    uint8_t extended_attempt;
} d1l_meshcore_text_plaintext_view_t;

/* Decodes the user-text portion of an authenticated, AES-block-padded
 * MeshCore plaintext.  When enabled, the one optional extended-attempt byte
 * after the text terminator follows the pinned upstream rule (>3, low two
 * bits matching low_attempt).  Every remaining byte must be canonical zero
 * padding.  Output is assigned only on success. */
bool d1l_meshcore_text_plaintext_view(
    const uint8_t *padded_text, size_t padded_length,
    bool allow_extended_attempt, uint8_t low_attempt,
    d1l_meshcore_text_plaintext_view_t *out_view);
