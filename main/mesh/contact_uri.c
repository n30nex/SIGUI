#include "contact_uri.h"

#include <stdio.h>
#include <string.h>

static bool is_hex(char value)
{
    return (value >= '0' && value <= '9') ||
           (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

static uint8_t hex_value(char value)
{
    if (value >= '0' && value <= '9') {
        return (uint8_t)(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return (uint8_t)(10 + value - 'a');
    }
    return (uint8_t)(10 + value - 'A');
}

static char lower_hex(char value)
{
    return (value >= 'A' && value <= 'F')
               ? (char)(value + ('a' - 'A'))
               : value;
}

static bool input_is_bounded_text(const char *input, size_t input_len)
{
    if (!input || input_len == 0U || input_len > D1L_CONTACT_URI_MAX_LEN) {
        return false;
    }
    for (size_t i = 0U; i < input_len; ++i) {
        if (input[i] == '\0') {
            return false;
        }
    }
    return true;
}

static bool field_name_is(const char *field, size_t field_len,
                          const char *expected)
{
    const size_t expected_len = strlen(expected);
    return field_len == expected_len &&
           memcmp(field, expected, expected_len) == 0;
}

static bool utf8_name_is_valid(const unsigned char *text, size_t text_len)
{
    if (!text || text_len == 0U) {
        return false;
    }

    size_t cursor = 0U;
    while (cursor < text_len) {
        const unsigned char lead = text[cursor];
        uint32_t codepoint = 0U;
        uint32_t minimum = 0U;
        size_t continuation_count = 0U;

        if (lead <= 0x7fU) {
            codepoint = lead;
        } else if (lead >= 0xc2U && lead <= 0xdfU) {
            codepoint = (uint32_t)(lead & 0x1fU);
            minimum = 0x80U;
            continuation_count = 1U;
        } else if (lead >= 0xe0U && lead <= 0xefU) {
            codepoint = (uint32_t)(lead & 0x0fU);
            minimum = 0x800U;
            continuation_count = 2U;
        } else if (lead >= 0xf0U && lead <= 0xf4U) {
            codepoint = (uint32_t)(lead & 0x07U);
            minimum = 0x10000U;
            continuation_count = 3U;
        } else {
            return false;
        }

        if (cursor + continuation_count >= text_len) {
            return false;
        }
        for (size_t i = 1U; i <= continuation_count; ++i) {
            const unsigned char continuation = text[cursor + i];
            if ((continuation & 0xc0U) != 0x80U) {
                return false;
            }
            codepoint = (codepoint << 6U) |
                        (uint32_t)(continuation & 0x3fU);
        }

        if (codepoint < minimum || codepoint > 0x10ffffU ||
            (codepoint >= 0xd800U && codepoint <= 0xdfffU) ||
            codepoint <= 0x1fU ||
            (codepoint >= 0x7fU && codepoint <= 0x9fU)) {
            return false;
        }
        cursor += continuation_count + 1U;
    }
    return true;
}

static bool decode_name(const char *encoded, size_t encoded_len,
                        char dest[D1L_CONTACT_URI_NAME_LEN])
{
    size_t out = 0U;
    for (size_t i = 0U; i < encoded_len; ++i) {
        unsigned char value = (unsigned char)encoded[i];
        if (value == '%') {
            if (i + 2U >= encoded_len || !is_hex(encoded[i + 1U]) ||
                !is_hex(encoded[i + 2U])) {
                return false;
            }
            value = (unsigned char)((hex_value(encoded[i + 1U]) << 4) |
                                    hex_value(encoded[i + 2U]));
            i += 2U;
        } else if (value == '+') {
            value = ' ';
        }
        if (out + 1U >= D1L_CONTACT_URI_NAME_LEN) {
            return false;
        }
        dest[out++] = (char)value;
    }
    dest[out] = '\0';
    return utf8_name_is_valid((const unsigned char *)dest, out);
}

static bool parse_public_key(
    const char *value, size_t value_len,
    char dest[D1L_CONTACT_URI_PUBLIC_KEY_HEX_LEN])
{
    if (value_len != D1L_CONTACT_URI_PUBLIC_KEY_HEX_LEN - 1U) {
        return false;
    }
    for (size_t i = 0U; i < value_len; ++i) {
        if (!is_hex(value[i])) {
            return false;
        }
        dest[i] = lower_hex(value[i]);
    }
    dest[value_len] = '\0';
    return true;
}

bool d1l_contact_uri_parse(const char *input, size_t input_len,
                           d1l_contact_uri_t *out)
{
    if (!out) {
        return false;
    }
    d1l_contact_uri_t parsed;
    memset(&parsed, 0, sizeof(parsed));
    *out = parsed;

    static const char prefix[] = D1L_CONTACT_URI_SCHEME;
    const size_t prefix_len = sizeof(prefix) - 1U;
    if (!input_is_bounded_text(input, input_len) || input_len <= prefix_len ||
        memcmp(input, prefix, prefix_len) != 0) {
        return false;
    }

    bool have_name = false;
    bool have_public_key = false;
    bool have_type = false;
    size_t cursor = prefix_len;
    while (cursor < input_len) {
        size_t field_end = cursor;
        while (field_end < input_len && input[field_end] != '&') {
            field_end++;
        }
        if (field_end == cursor) {
            return false;
        }

        size_t equals = cursor;
        while (equals < field_end && input[equals] != '=') {
            equals++;
        }
        if (equals == cursor || equals == field_end) {
            return false;
        }
        for (size_t i = equals + 1U; i < field_end; ++i) {
            if (input[i] == '=') {
                return false;
            }
        }

        const char *field = &input[cursor];
        const size_t field_len = equals - cursor;
        const char *value = &input[equals + 1U];
        const size_t value_len = field_end - equals - 1U;
        if (field_name_is(field, field_len, "name")) {
            if (have_name || !decode_name(value, value_len, parsed.name)) {
                return false;
            }
            have_name = true;
        } else if (field_name_is(field, field_len, "public_key")) {
            if (have_public_key ||
                !parse_public_key(value, value_len, parsed.public_key_hex)) {
                return false;
            }
            have_public_key = true;
        } else if (field_name_is(field, field_len, "type")) {
            if (have_type || value_len != 1U || value[0] < '1' ||
                value[0] > '4') {
                return false;
            }
            parsed.type_id = (uint8_t)(value[0] - '0');
            have_type = true;
        } else {
            return false;
        }

        if (field_end == input_len) {
            cursor = input_len;
        } else {
            cursor = field_end + 1U;
            if (cursor == input_len) {
                return false;
            }
        }
    }

    if (!have_name || !have_public_key || !have_type) {
        return false;
    }
    *out = parsed;
    return true;
}

static bool name_is_valid(const char name[D1L_CONTACT_URI_NAME_LEN])
{
    if (!name || name[0] == '\0') {
        return false;
    }
    for (size_t i = 0U; i < D1L_CONTACT_URI_NAME_LEN; ++i) {
        if (name[i] == '\0') {
            return utf8_name_is_valid((const unsigned char *)name, i);
        }
    }
    return false;
}

static bool public_key_is_valid(
    const char key[D1L_CONTACT_URI_PUBLIC_KEY_HEX_LEN])
{
    if (!key) {
        return false;
    }
    for (size_t i = 0U; i < D1L_CONTACT_URI_PUBLIC_KEY_HEX_LEN - 1U; ++i) {
        if (!is_hex(key[i])) {
            return false;
        }
    }
    return key[D1L_CONTACT_URI_PUBLIC_KEY_HEX_LEN - 1U] == '\0';
}

static bool encode_name(const char *name, char *dest, size_t dest_size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t out = 0U;
    for (size_t i = 0U; name[i] != '\0'; ++i) {
        const unsigned char value = (unsigned char)name[i];
        if ((value >= 'A' && value <= 'Z') ||
            (value >= 'a' && value <= 'z') ||
            (value >= '0' && value <= '9') || value == '-' || value == '_' ||
            value == '.') {
            if (out + 1U >= dest_size) {
                return false;
            }
            dest[out++] = (char)value;
        } else if (value == ' ') {
            if (out + 1U >= dest_size) {
                return false;
            }
            dest[out++] = '+';
        } else {
            if (out + 3U >= dest_size) {
                return false;
            }
            dest[out++] = '%';
            dest[out++] = hex[(value >> 4) & 0x0fU];
            dest[out++] = hex[value & 0x0fU];
        }
    }
    dest[out] = '\0';
    return true;
}

bool d1l_contact_uri_format(const d1l_contact_uri_t *contact, char *dest,
                            size_t dest_size)
{
    if (!dest || dest_size == 0U) {
        return false;
    }
    dest[0] = '\0';
    if (!contact || !name_is_valid(contact->name) ||
        !public_key_is_valid(contact->public_key_hex) ||
        contact->type_id < 1U || contact->type_id > 4U) {
        return false;
    }

    char encoded_name[D1L_CONTACT_URI_NAME_LEN * 3U] = {0};
    if (!encode_name(contact->name, encoded_name, sizeof(encoded_name))) {
        return false;
    }
    char normalized_key[D1L_CONTACT_URI_PUBLIC_KEY_HEX_LEN] = {0};
    for (size_t i = 0U; i < sizeof(normalized_key) - 1U; ++i) {
        normalized_key[i] = lower_hex(contact->public_key_hex[i]);
    }

    const int written = snprintf(
        dest, dest_size,
        D1L_CONTACT_URI_SCHEME "name=%s&public_key=%s&type=%u",
        encoded_name, normalized_key, (unsigned)contact->type_id);
    if (written < 0 || (size_t)written >= dest_size) {
        dest[0] = '\0';
        return false;
    }
    return true;
}
