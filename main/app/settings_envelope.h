#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define D1L_SETTINGS_ENVELOPE_MAGIC 0x53473144UL
#define D1L_SETTINGS_ENVELOPE_SCHEMA_VERSION 1U

typedef struct {
    uint32_t magic;
    uint16_t schema_version;
    uint16_t reserved;
    uint32_t payload_length;
    uint32_t revision;
    uint32_t payload_checksum;
} d1l_settings_envelope_header_t;

typedef enum {
    D1L_SETTINGS_ENVELOPE_VALID = 0,
    D1L_SETTINGS_ENVELOPE_NOT_ENVELOPE,
    D1L_SETTINGS_ENVELOPE_MALFORMED,
    D1L_SETTINGS_ENVELOPE_SCHEMA_NEWER,
    D1L_SETTINGS_ENVELOPE_CHECKSUM_MISMATCH,
} d1l_settings_envelope_validation_t;

uint32_t d1l_settings_envelope_checksum(const void *payload, size_t payload_length);
bool d1l_settings_envelope_next_revision(uint32_t current_revision,
                                         uint32_t *next_revision);
bool d1l_settings_envelope_build(void *destination, size_t destination_size,
                                 const void *payload, size_t payload_length,
                                 uint32_t revision, size_t *written_length);
d1l_settings_envelope_validation_t d1l_settings_envelope_validate(
    const void *blob, size_t blob_length, size_t expected_payload_length,
    d1l_settings_envelope_header_t *header,
    const uint8_t **payload);
bool d1l_settings_envelope_requires_preservation(
    d1l_settings_envelope_validation_t validation);
