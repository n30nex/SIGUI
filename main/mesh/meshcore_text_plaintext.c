#include "meshcore_text_plaintext.h"

bool d1l_meshcore_text_plaintext_view(
    const uint8_t *padded_text, size_t padded_length,
    bool allow_extended_attempt, uint8_t low_attempt,
    d1l_meshcore_text_plaintext_view_t *out_view)
{
    if (!padded_text || !out_view || padded_length == 0U || low_attempt > 3U) {
        return false;
    }

    size_t text_length = 0U;
    while (text_length < padded_length && padded_text[text_length] != 0U) {
        text_length++;
    }

    uint8_t extended_attempt = 0U;
    size_t padding_start = text_length;
    if (text_length < padded_length && allow_extended_attempt &&
        text_length + 1U < padded_length &&
        padded_text[text_length + 1U] > 3U) {
        extended_attempt = padded_text[text_length + 1U];
        if ((extended_attempt & 0x03U) != low_attempt) {
            return false;
        }
        padding_start = text_length + 2U;
    }

    for (size_t index = padding_start; index < padded_length; ++index) {
        if (padded_text[index] != 0U) {
            return false;
        }
    }

    const d1l_user_text_info_t text_info = d1l_user_text_validate_span(
        padded_text, text_length, false);
    if (text_info.result != D1L_USER_TEXT_OK) {
        return false;
    }

    const d1l_meshcore_text_plaintext_view_t view = {
        .text = padded_text,
        .text_length = text_length,
        .text_info = text_info,
        .extended_attempt = extended_attempt,
    };
    *out_view = view;
    return true;
}
