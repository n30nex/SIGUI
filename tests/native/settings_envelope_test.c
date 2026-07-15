#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app/settings_envelope.h"

typedef struct {
    uint32_t schema;
    uint8_t data[19];
} test_payload_t;

static void test_build_and_validate(void)
{
    const test_payload_t source = {
        .schema = 7U,
        .data = {1U, 3U, 5U, 7U, 9U},
    };
    uint8_t blob[sizeof(d1l_settings_envelope_header_t) +
                 sizeof(source)] = {0};
    size_t written = 0U;
    assert(d1l_settings_envelope_build(blob, sizeof(blob), &source,
                                       sizeof(source), 19U, &written));
    assert(written == sizeof(blob));

    d1l_settings_envelope_header_t header = {0};
    const uint8_t *payload = NULL;
    assert(d1l_settings_envelope_validate(
               blob, written, sizeof(source), &header, &payload) ==
           D1L_SETTINGS_ENVELOPE_VALID);
    assert(header.magic == D1L_SETTINGS_ENVELOPE_MAGIC);
    assert(header.schema_version == D1L_SETTINGS_ENVELOPE_SCHEMA_VERSION);
    assert(header.payload_length == sizeof(source));
    assert(header.revision == 19U);
    assert(payload != NULL);
    assert(memcmp(payload, &source, sizeof(source)) == 0);

    assert(!d1l_settings_envelope_build(blob, sizeof(blob), &source,
                                        sizeof(source), 0U, NULL));
    assert(!d1l_settings_envelope_build(blob, sizeof(blob) - 1U, &source,
                                        sizeof(source), 1U, NULL));
}

static void test_corruption_and_malformed_headers(void)
{
    const test_payload_t source = {.schema = 7U, .data = {0xa5U}};
    uint8_t blob[sizeof(d1l_settings_envelope_header_t) +
                 sizeof(source)] = {0};
    assert(d1l_settings_envelope_build(blob, sizeof(blob), &source,
                                       sizeof(source), 1U, NULL));

    blob[sizeof(d1l_settings_envelope_header_t) + 4U] ^= 0x80U;
    assert(d1l_settings_envelope_validate(
               blob, sizeof(blob), sizeof(source), NULL, NULL) ==
           D1L_SETTINGS_ENVELOPE_CHECKSUM_MISMATCH);
    blob[sizeof(d1l_settings_envelope_header_t) + 4U] ^= 0x80U;

    d1l_settings_envelope_header_t header = {0};
    memcpy(&header, blob, sizeof(header));
    header.payload_length--;
    memcpy(blob, &header, sizeof(header));
    assert(d1l_settings_envelope_validate(
               blob, sizeof(blob), sizeof(source), NULL, NULL) ==
           D1L_SETTINGS_ENVELOPE_MALFORMED);

    header.payload_length = sizeof(source);
    header.revision = 0U;
    memcpy(blob, &header, sizeof(header));
    assert(d1l_settings_envelope_validate(
               blob, sizeof(blob), sizeof(source), NULL, NULL) ==
           D1L_SETTINGS_ENVELOPE_MALFORMED);
    assert(d1l_settings_envelope_validate(
               blob, sizeof(uint32_t), sizeof(source), NULL, NULL) ==
           D1L_SETTINGS_ENVELOPE_MALFORMED);
}

static void test_unknown_newer_requires_preservation(void)
{
    const test_payload_t source = {.schema = 7U};
    uint8_t blob[sizeof(d1l_settings_envelope_header_t) +
                 sizeof(source)] = {0};
    assert(d1l_settings_envelope_build(blob, sizeof(blob), &source,
                                       sizeof(source), 3U, NULL));
    d1l_settings_envelope_header_t header = {0};
    memcpy(&header, blob, sizeof(header));
    header.schema_version = D1L_SETTINGS_ENVELOPE_SCHEMA_VERSION + 1U;
    memcpy(blob, &header, sizeof(header));

    const d1l_settings_envelope_validation_t validation =
        d1l_settings_envelope_validate(
            blob, sizeof(blob), sizeof(source), NULL, NULL);
    assert(validation == D1L_SETTINGS_ENVELOPE_SCHEMA_NEWER);
    assert(d1l_settings_envelope_requires_preservation(validation));
    assert(d1l_settings_envelope_requires_preservation(
        D1L_SETTINGS_ENVELOPE_MALFORMED));
    assert(d1l_settings_envelope_requires_preservation(
        D1L_SETTINGS_ENVELOPE_CHECKSUM_MISMATCH));
    assert(!d1l_settings_envelope_requires_preservation(
        D1L_SETTINGS_ENVELOPE_NOT_ENVELOPE));
}

static void test_revision_saturation(void)
{
    uint32_t next = 99U;
    assert(d1l_settings_envelope_next_revision(0U, &next));
    assert(next == 1U);
    assert(d1l_settings_envelope_next_revision(41U, &next));
    assert(next == 42U);
    next = 99U;
    assert(!d1l_settings_envelope_next_revision(UINT32_MAX, &next));
    assert(next == 99U);
    assert(!d1l_settings_envelope_next_revision(1U, NULL));
}

int main(void)
{
    test_build_and_validate();
    test_corruption_and_malformed_headers();
    test_unknown_newer_requires_preservation();
    test_revision_saturation();
    puts("native settings envelope: ok");
    return 0;
}
