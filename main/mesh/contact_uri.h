#ifndef D1L_CONTACT_URI_H
#define D1L_CONTACT_URI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define D1L_CONTACT_URI_SCHEME "meshcore://contact/add?"
#define D1L_CONTACT_URI_MAX_LEN 223U
#define D1L_CONTACT_URI_NAME_LEN 32U
#define D1L_CONTACT_URI_PUBLIC_KEY_HEX_LEN 65U

typedef struct {
    char name[D1L_CONTACT_URI_NAME_LEN];
    char public_key_hex[D1L_CONTACT_URI_PUBLIC_KEY_HEX_LEN];
    uint8_t type_id;
} d1l_contact_uri_t;

/*
 * Parses an exact, bounded MeshCore contact URI. Unknown or duplicate fields,
 * malformed percent escapes, embedded NUL bytes, unsupported roles, and
 * non-canonical key lengths all fail closed. The output is always zeroed on
 * rejection.
 */
bool d1l_contact_uri_parse(const char *input, size_t input_len,
                           d1l_contact_uri_t *out);

/* Emits the deterministic name/public_key/type form used by QR export. */
bool d1l_contact_uri_format(const d1l_contact_uri_t *contact, char *dest,
                            size_t dest_size);

#ifdef __cplusplus
}
#endif

#endif
