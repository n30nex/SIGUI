#include "settings_envelope.h"

#include <limits.h>
#include <string.h>

_Static_assert(sizeof(d1l_settings_envelope_header_t) == 20U,
               "settings envelope header layout must remain stable");

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{
    crc = ~crc;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8U; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1U) ^ (0xedb88320UL & mask);
        }
    }
    return ~crc;
}

uint32_t d1l_settings_envelope_checksum(const void *payload, size_t payload_length)
{
    if (!payload && payload_length != 0U) {
        return 0U;
    }
    return crc32_update(0U, (const uint8_t *)payload, payload_length);
}

bool d1l_settings_envelope_next_revision(uint32_t current_revision,
                                         uint32_t *next_revision)
{
    if (!next_revision || current_revision == UINT32_MAX) {
        return false;
    }
    *next_revision = current_revision + 1U;
    return *next_revision != 0U;
}

bool d1l_settings_envelope_build(void *destination, size_t destination_size,
                                 const void *payload, size_t payload_length,
                                 uint32_t revision, size_t *written_length)
{
    if (written_length) {
        *written_length = 0U;
    }
    if (!destination || !payload || payload_length == 0U ||
        payload_length > UINT32_MAX || revision == 0U ||
        payload_length > SIZE_MAX - sizeof(d1l_settings_envelope_header_t)) {
        return false;
    }
    const size_t required = sizeof(d1l_settings_envelope_header_t) + payload_length;
    if (destination_size < required) {
        return false;
    }

    const d1l_settings_envelope_header_t header = {
        .magic = D1L_SETTINGS_ENVELOPE_MAGIC,
        .schema_version = D1L_SETTINGS_ENVELOPE_SCHEMA_VERSION,
        .reserved = 0U,
        .payload_length = (uint32_t)payload_length,
        .revision = revision,
        .payload_checksum = d1l_settings_envelope_checksum(payload, payload_length),
    };
    memcpy(destination, &header, sizeof(header));
    memcpy((uint8_t *)destination + sizeof(header), payload, payload_length);
    if (written_length) {
        *written_length = required;
    }
    return true;
}

d1l_settings_envelope_validation_t d1l_settings_envelope_validate(
    const void *blob, size_t blob_length, size_t expected_payload_length,
    d1l_settings_envelope_header_t *out_header,
    const uint8_t **out_payload)
{
    if (out_header) {
        memset(out_header, 0, sizeof(*out_header));
    }
    if (out_payload) {
        *out_payload = NULL;
    }
    if (!blob) {
        return D1L_SETTINGS_ENVELOPE_NOT_ENVELOPE;
    }

    uint32_t magic = 0U;
    if (blob_length >= sizeof(magic)) {
        memcpy(&magic, blob, sizeof(magic));
    }
    if (magic != D1L_SETTINGS_ENVELOPE_MAGIC) {
        return D1L_SETTINGS_ENVELOPE_NOT_ENVELOPE;
    }
    if (blob_length < sizeof(d1l_settings_envelope_header_t)) {
        return D1L_SETTINGS_ENVELOPE_MALFORMED;
    }

    d1l_settings_envelope_header_t header = {0};
    memcpy(&header, blob, sizeof(header));
    if (out_header) {
        *out_header = header;
    }
    if (header.schema_version > D1L_SETTINGS_ENVELOPE_SCHEMA_VERSION) {
        return D1L_SETTINGS_ENVELOPE_SCHEMA_NEWER;
    }
    const size_t total_length =
        sizeof(header) + (size_t)header.payload_length;
    if (header.schema_version != D1L_SETTINGS_ENVELOPE_SCHEMA_VERSION ||
        header.reserved != 0U || header.revision == 0U ||
        header.payload_length != expected_payload_length ||
        total_length < sizeof(header) || blob_length != total_length) {
        return D1L_SETTINGS_ENVELOPE_MALFORMED;
    }

    const uint8_t *payload = (const uint8_t *)blob + sizeof(header);
    if (header.payload_checksum !=
        d1l_settings_envelope_checksum(payload, header.payload_length)) {
        return D1L_SETTINGS_ENVELOPE_CHECKSUM_MISMATCH;
    }
    if (out_payload) {
        *out_payload = payload;
    }
    return D1L_SETTINGS_ENVELOPE_VALID;
}

bool d1l_settings_envelope_requires_preservation(
    d1l_settings_envelope_validation_t validation)
{
    return validation == D1L_SETTINGS_ENVELOPE_MALFORMED ||
           validation == D1L_SETTINGS_ENVELOPE_SCHEMA_NEWER ||
           validation == D1L_SETTINGS_ENVELOPE_CHECKSUM_MISMATCH;
}
