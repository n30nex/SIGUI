#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "app/settings_envelope.h"

static void assert_zero_header(const d1l_settings_envelope_header_t *header)
{
    const d1l_settings_envelope_header_t zero = {0};
    assert(memcmp(header, &zero, sizeof(zero)) == 0);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    const size_t expected_length = size > 0U ? data[0] : 0U;
    const uint8_t *blob = size > 1U ? data + 1U : NULL;
    const size_t blob_length = size > 1U ? size - 1U : 0U;
    d1l_settings_envelope_header_t first_header;
    d1l_settings_envelope_header_t second_header;
    const uint8_t *first_payload = (const uint8_t *)(uintptr_t)1U;
    const uint8_t *second_payload = (const uint8_t *)(uintptr_t)2U;
    memset(&first_header, 0xa5, sizeof(first_header));
    memset(&second_header, 0x5a, sizeof(second_header));
    const d1l_settings_envelope_validation_t first =
        d1l_settings_envelope_validate(
            blob, blob_length, expected_length, &first_header,
            &first_payload);
    const d1l_settings_envelope_validation_t second =
        d1l_settings_envelope_validate(
            blob, blob_length, expected_length, &second_header,
            &second_payload);
    assert(first == second);
    assert(memcmp(&first_header, &second_header, sizeof(first_header)) == 0);
    assert(first_payload == second_payload);
    assert(d1l_settings_envelope_requires_preservation(first) ==
           (first == D1L_SETTINGS_ENVELOPE_MALFORMED ||
            first == D1L_SETTINGS_ENVELOPE_SCHEMA_NEWER ||
            first == D1L_SETTINGS_ENVELOPE_CHECKSUM_MISMATCH));

    uint32_t magic = 0U;
    if (blob != NULL && blob_length >= sizeof(magic)) {
        memcpy(&magic, blob, sizeof(magic));
    }
    if (magic == D1L_SETTINGS_ENVELOPE_MAGIC &&
        blob_length >= sizeof(first_header)) {
        assert(memcmp(&first_header, blob, sizeof(first_header)) == 0);
    } else {
        assert_zero_header(&first_header);
    }
    if (first == D1L_SETTINGS_ENVELOPE_VALID) {
        assert(first_payload == blob + sizeof(first_header));
        assert(first_payload >= blob);
        assert(first_payload + expected_length <= blob + blob_length);
        assert(first_header.payload_length == expected_length);
        assert(first_header.revision != 0U);
    } else {
        assert(first_payload == NULL);
    }

    uint8_t payload[64];
    const size_t payload_length = 1U + (size > 0U ? data[0] % 64U : 0U);
    for (size_t index = 0U; index < payload_length; ++index) {
        payload[index] = size > 0U ? data[index % size] : (uint8_t)index;
    }
    uint32_t revision = 1U;
    if (size >= 4U) {
        memcpy(&revision, data, sizeof(revision));
        revision |= 1U;
    }
    uint8_t built[sizeof(d1l_settings_envelope_header_t) + sizeof(payload)];
    size_t written = 0U;
    assert(d1l_settings_envelope_build(
        built, sizeof(built), payload, payload_length, revision, &written));
    assert(written == sizeof(d1l_settings_envelope_header_t) + payload_length);
    d1l_settings_envelope_header_t built_header = {0};
    const uint8_t *built_payload = NULL;
    assert(d1l_settings_envelope_validate(
               built, written, payload_length, &built_header,
               &built_payload) == D1L_SETTINGS_ENVELOPE_VALID);
    assert(built_payload == built + sizeof(built_header));
    assert(memcmp(built_payload, payload, payload_length) == 0);

    uint8_t undersized[sizeof(built)];
    memset(undersized, 0x69, sizeof(undersized));
    written = SIZE_MAX;
    assert(!d1l_settings_envelope_build(
        undersized, sizeof(d1l_settings_envelope_header_t) +
            payload_length - 1U,
        payload, payload_length, revision, &written));
    assert(written == 0U);
    for (size_t index = 0U; index < sizeof(undersized); ++index) {
        assert(undersized[index] == 0x69U);
    }
    return 0;
}
