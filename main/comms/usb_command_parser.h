#ifndef D1L_USB_COMMAND_PARSER_H
#define D1L_USB_COMMAND_PARSER_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The production console reserves one byte for the canonical terminator. */
#define D1L_USB_COMMAND_MAX_BYTES 255U

typedef enum {
    D1L_USB_COMMAND_ADMIT_OK = 0,
    D1L_USB_COMMAND_ADMIT_EMPTY,
    D1L_USB_COMMAND_ADMIT_TOO_LONG,
    D1L_USB_COMMAND_ADMIT_EMBEDDED_NUL,
    D1L_USB_COMMAND_ADMIT_CONTROL_BYTE,
    D1L_USB_COMMAND_ADMIT_INVALID_ARGUMENT,
} d1l_usb_command_admit_status_t;

typedef struct {
    const char *text;
    size_t length;
} d1l_usb_command_view_t;

/*
 * Canonicalize one delimiter-free command using the receive boundary's real
 * byte count.  On success, text[length] is the only NUL in the command and
 * strlen-style legacy handlers are therefore bounded by the admitted length.
 * On every rejection, the complete caller-owned buffer is wiped before this
 * function returns.
 */
d1l_usb_command_admit_status_t d1l_usb_command_admit_in_place(
    char *buffer, size_t received_length, size_t capacity,
    d1l_usb_command_view_t *out_view);

const char *d1l_usb_command_admit_status_code(
    d1l_usb_command_admit_status_t status);

/* Explicit-length helpers for dispatchers and native/fuzz contracts. */
bool d1l_usb_command_equals(const d1l_usb_command_view_t *command,
                            const char *literal, size_t literal_length);
bool d1l_usb_command_has_argument(const d1l_usb_command_view_t *command,
                                  const char *prefix, size_t prefix_length);
bool d1l_usb_command_token_or_argument(const d1l_usb_command_view_t *command,
                                       const char *token,
                                       size_t token_length);
bool d1l_usb_command_parse_int_exact(const char *text, int *out_value);
bool d1l_usb_command_parse_double_exact(const char *text, double *out_value);

#ifdef __cplusplus
}
#endif

#endif
