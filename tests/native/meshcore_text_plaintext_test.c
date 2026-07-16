#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "mesh/meshcore_text_plaintext.h"

static void test_attempt_matrix(void)
{
    size_t accepted_count = 0U;
    size_t mismatch_rejected_count = 0U;
    bool accepted_zero = false;
    bool accepted_low_boundary = false;
    bool accepted_extended_boundary = false;
    bool accepted_maximum = false;
    bool rejected_extended_boundary_mismatch = false;
    bool rejected_wrapped_maximum_mismatch = false;

    for (unsigned int candidate = 0U; candidate <= UINT8_MAX; ++candidate) {
        const uint8_t attempt = (uint8_t)candidate;
        const uint8_t low_attempt = attempt & 0x03U;
        uint8_t plaintext[8U] = {'o', 'k', 0U, 0U, 0U, 0U, 0U, 0U};
        if (attempt > 3U) {
            plaintext[3] = attempt;
        }

        d1l_meshcore_text_plaintext_view_t view = {0};
        assert(d1l_meshcore_text_plaintext_view(
            plaintext, sizeof(plaintext), true, low_attempt, &view));
        assert(view.text == plaintext);
        assert(view.text_length == 2U);
        assert(view.text_info.character_count == 2U);
        assert(view.extended_attempt == (attempt > 3U ? attempt : 0U));
        accepted_count++;

        accepted_zero |= attempt == 0U;
        accepted_low_boundary |= attempt == 3U;
        accepted_extended_boundary |= attempt == 4U;
        accepted_maximum |= attempt == UINT8_MAX;

        /* Attempts 0..3 are represented entirely by the authenticated low
         * bits.  Attempts 4..255 also carry the full attempt byte, so the
         * production decoder must reject every disagreement with those bits. */
        if (attempt > 3U) {
            const uint8_t mismatched_low = (low_attempt + 1U) & 0x03U;
            d1l_meshcore_text_plaintext_view_t sentinel = {
                .text = (const uint8_t *)1,
                .text_length = 99U,
                .extended_attempt = 77U,
            };
            assert(!d1l_meshcore_text_plaintext_view(
                plaintext, sizeof(plaintext), true, mismatched_low,
                &sentinel));
            assert(sentinel.text == (const uint8_t *)1);
            assert(sentinel.text_length == 99U);
            assert(sentinel.extended_attempt == 77U);
            mismatch_rejected_count++;

            rejected_extended_boundary_mismatch |= attempt == 4U;
            rejected_wrapped_maximum_mismatch |=
                attempt == UINT8_MAX && mismatched_low == 0U;
        }
    }

    assert(accepted_count == 256U);
    assert(mismatch_rejected_count == 252U);
    assert(accepted_zero && accepted_low_boundary);
    assert(accepted_extended_boundary && accepted_maximum);
    assert(rejected_extended_boundary_mismatch);
    assert(rejected_wrapped_maximum_mismatch);
}

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

    test_attempt_matrix();

    puts("native MeshCore text plaintext: ok");
    return 0;
}
