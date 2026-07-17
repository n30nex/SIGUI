#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "comms/usb_command_parser.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char command_buffer[D1L_USB_COMMAND_MAX_BYTES + 1U];
    memset(command_buffer, 0xa5, sizeof(command_buffer));
    const size_t copied = size < sizeof(command_buffer) ?
        size : sizeof(command_buffer);
    if (copied > 0U) {
        memcpy(command_buffer, data, copied);
    }

    d1l_usb_command_view_t view = {
        .text = (const char *)1,
        .length = SIZE_MAX,
    };
    const d1l_usb_command_admit_status_t status =
        d1l_usb_command_admit_in_place(
            command_buffer, size, sizeof(command_buffer), &view);
    if (status == D1L_USB_COMMAND_ADMIT_OK) {
        assert(view.text == command_buffer);
        assert(view.length > 0U && view.length <= size);
        assert(view.length <= D1L_USB_COMMAND_MAX_BYTES);
        assert(command_buffer[view.length] == '\0');
        assert(strlen(command_buffer) == view.length);
        for (size_t i = 0U; i < view.length; ++i) {
            const uint8_t byte = (uint8_t)command_buffer[i];
            assert(byte >= 0x20U && byte != 0x7fU);
        }
        int integer = 0;
        double real = 0.0;
        (void)d1l_usb_command_parse_int_exact(view.text, &integer);
        (void)d1l_usb_command_parse_double_exact(view.text, &real);
    } else {
        assert(view.text == NULL && view.length == 0U);
        for (size_t i = 0U; i < sizeof(command_buffer); ++i) {
            assert(command_buffer[i] == '\0');
        }
    }
    return 0;
}
