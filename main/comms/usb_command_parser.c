#include "usb_command_parser.h"

#include <errno.h>
#include <float.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void wipe_bytes(void *data, size_t length)
{
    if (!data) {
        return;
    }
    volatile uint8_t *bytes = (volatile uint8_t *)data;
    for (size_t i = 0U; i < length; ++i) {
        bytes[i] = 0U;
    }
}

static d1l_usb_command_admit_status_t reject_command(
    char *buffer, size_t capacity, d1l_usb_command_view_t *out_view,
    d1l_usb_command_admit_status_t status)
{
    wipe_bytes(buffer, capacity);
    if (out_view) {
        out_view->text = NULL;
        out_view->length = 0U;
    }
    return status;
}

d1l_usb_command_admit_status_t d1l_usb_command_admit_in_place(
    char *buffer, size_t received_length, size_t capacity,
    d1l_usb_command_view_t *out_view)
{
    if (out_view) {
        out_view->text = NULL;
        out_view->length = 0U;
    }
    if (!buffer || !out_view || capacity == 0U) {
        return reject_command(buffer, capacity, out_view,
                              D1L_USB_COMMAND_ADMIT_INVALID_ARGUMENT);
    }
    if (received_length >= capacity ||
        received_length > D1L_USB_COMMAND_MAX_BYTES) {
        return reject_command(buffer, capacity, out_view,
                              D1L_USB_COMMAND_ADMIT_TOO_LONG);
    }

    for (size_t i = 0U; i < received_length; ++i) {
        const uint8_t byte = (uint8_t)buffer[i];
        if (byte == 0U) {
            return reject_command(buffer, capacity, out_view,
                                  D1L_USB_COMMAND_ADMIT_EMBEDDED_NUL);
        }
        /* Commands use ASCII space as their only grammar separator.  Reject
         * all C0 controls and DEL so CR/LF injection, tabs, escape sequences,
         * and other invisible trailing bytes never reach side-effecting
         * handlers.  UTF-8 bytes (0x80..0xff) remain available to text args. */
        if (byte < 0x20U || byte == 0x7fU) {
            return reject_command(buffer, capacity, out_view,
                                  D1L_USB_COMMAND_ADMIT_CONTROL_BYTE);
        }
    }

    size_t canonical_length = received_length;
    while (canonical_length > 0U && buffer[canonical_length - 1U] == ' ') {
        canonical_length--;
    }
    if (canonical_length == 0U) {
        return reject_command(buffer, capacity, out_view,
                              D1L_USB_COMMAND_ADMIT_EMPTY);
    }

    buffer[canonical_length] = '\0';
    if (canonical_length + 1U < capacity) {
        wipe_bytes(buffer + canonical_length + 1U,
                   capacity - canonical_length - 1U);
    }
    out_view->text = buffer;
    out_view->length = canonical_length;
    return D1L_USB_COMMAND_ADMIT_OK;
}

const char *d1l_usb_command_admit_status_code(
    d1l_usb_command_admit_status_t status)
{
    switch (status) {
    case D1L_USB_COMMAND_ADMIT_OK:
        return "OK";
    case D1L_USB_COMMAND_ADMIT_EMPTY:
        return "EMPTY_COMMAND";
    case D1L_USB_COMMAND_ADMIT_TOO_LONG:
        return "LINE_TOO_LONG";
    case D1L_USB_COMMAND_ADMIT_EMBEDDED_NUL:
        return "EMBEDDED_NUL";
    case D1L_USB_COMMAND_ADMIT_CONTROL_BYTE:
        return "INVALID_CONTROL_BYTE";
    case D1L_USB_COMMAND_ADMIT_INVALID_ARGUMENT:
    default:
        return "INVALID_COMMAND_BYTES";
    }
}

bool d1l_usb_command_equals(const d1l_usb_command_view_t *command,
                            const char *literal, size_t literal_length)
{
    return command && command->text && literal &&
           command->length == literal_length &&
           memcmp(command->text, literal, literal_length) == 0;
}

bool d1l_usb_command_has_argument(const d1l_usb_command_view_t *command,
                                  const char *prefix, size_t prefix_length)
{
    return command && command->text && prefix && prefix_length > 0U &&
           prefix[prefix_length - 1U] == ' ' &&
           command->length > prefix_length &&
           memcmp(command->text, prefix, prefix_length) == 0;
}

bool d1l_usb_command_token_or_argument(const d1l_usb_command_view_t *command,
                                       const char *token,
                                       size_t token_length)
{
    if (!command || !command->text || !token || token_length == 0U ||
        command->length < token_length ||
        memcmp(command->text, token, token_length) != 0) {
        return false;
    }
    if (command->length == token_length) {
        return true;
    }
    return command->length > token_length + 1U &&
           command->text[token_length] == ' ';
}

bool d1l_usb_command_parse_int_exact(const char *text, int *out_value)
{
    if (!text || !out_value) {
        return false;
    }
    errno = 0;
    char *end = NULL;
    const long value = strtol(text, &end, 10);
    if (end == text || !end || *end != '\0' || errno == ERANGE ||
        value < INT_MIN || value > INT_MAX) {
        return false;
    }
    *out_value = (int)value;
    return true;
}

bool d1l_usb_command_parse_double_exact(const char *text, double *out_value)
{
    if (!text || !out_value) {
        return false;
    }
    errno = 0;
    char *end = NULL;
    const double value = strtod(text, &end);
    if (end == text || !end || *end != '\0' || errno == ERANGE ||
        value != value || value < -DBL_MAX || value > DBL_MAX) {
        return false;
    }
    *out_value = value;
    return true;
}
