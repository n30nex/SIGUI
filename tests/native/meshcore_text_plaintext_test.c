#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "mesh/meshcore_text_plaintext.h"

int main(void)
{
    d1l_meshcore_text_plaintext_view_t view = {0};
    const uint8_t public_padded[] = {'h', 'e', 'l', 'l', 'o', 0U, 0U, 0U};
    assert(d1l_meshcore_text_plaintext_view(
        public_padded, sizeof(public_padded), false, 0U, &view));
    assert(view.text_length == 5U);
    assert(view.text_info.character_count == 5U);
    assert(view.extended_attempt == 0U);

    uint8_t exact_block[16U];
    memset(exact_block, 'x', sizeof(exact_block));
    assert(d1l_meshcore_text_plaintext_view(
        exact_block, sizeof(exact_block), false, 0U, &view));
    assert(view.text_length == sizeof(exact_block));

    const uint8_t extended[] = {'o', 'k', 0U, 5U, 0U, 0U};
    assert(d1l_meshcore_text_plaintext_view(
        extended, sizeof(extended), true, 1U, &view));
    assert(view.text_length == 2U);
    assert(view.extended_attempt == 5U);

    d1l_meshcore_text_plaintext_view_t sentinel = {
        .text = (const uint8_t *)1,
        .text_length = 99U,
        .extended_attempt = 77U,
    };
    const uint8_t embedded_nul[] = {'o', 'k', 0U, 'x', 0U};
    assert(!d1l_meshcore_text_plaintext_view(
        embedded_nul, sizeof(embedded_nul), false, 0U, &sentinel));
    assert(sentinel.text_length == 99U && sentinel.extended_attempt == 77U);

    const uint8_t bad_extended[] = {'o', 'k', 0U, 6U, 0U};
    assert(!d1l_meshcore_text_plaintext_view(
        bad_extended, sizeof(bad_extended), true, 1U, &view));
    const uint8_t malformed_padding[] = {'o', 'k', 0U, 0U, 'x'};
    assert(!d1l_meshcore_text_plaintext_view(
        malformed_padding, sizeof(malformed_padding), false, 0U, &view));
    const uint8_t invalid_utf8[] = {0xe2U, 0x82U, 0U, 0U};
    assert(!d1l_meshcore_text_plaintext_view(
        invalid_utf8, sizeof(invalid_utf8), false, 0U, &view));
    uint8_t oversized[139U];
    memset(oversized, 'z', sizeof(oversized));
    assert(!d1l_meshcore_text_plaintext_view(
        oversized, sizeof(oversized), false, 0U, &view));

    puts("native MeshCore text plaintext: ok");
    return 0;
}
