#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ui/ui_channel_sheets.h"

static bool all_zero(const char *buffer, size_t size)
{
    for (size_t i = 0U; i < size; ++i) {
        if (buffer[i] != '\0') {
            return false;
        }
    }
    return true;
}

static d1l_channel_info_t channel(uint64_t channel_id, const char *name)
{
    d1l_channel_info_t value = {.channel_id = channel_id, .enabled = true};
    snprintf(value.name, sizeof(value.name), "%s", name);
    return value;
}

static void test_redacted_selection_is_bounded_and_invalidates_secret(void)
{
    d1l_ui_channel_sheets_controller_t controller = {0};
    d1l_channel_info_t selected = channel(7U, "Field Ops");

    assert(d1l_ui_channel_sheets_set_channel(&controller, &selected));
    assert(d1l_ui_channel_sheets_channel(&controller) != NULL);
    assert(d1l_ui_channel_sheets_channel(&controller)->channel_id == 7U);
    assert(strcmp(d1l_ui_channel_sheets_channel(&controller)->name,
                  "Field Ops") == 0);

    assert(d1l_ui_channel_sheets_set_export_uri(
        &controller, "meshcore://channel/add?name=Field%20Ops&secret=0011"));
    assert(controller.export_uri[0] != '\0');

    selected = channel(8U, "Backup");
    assert(d1l_ui_channel_sheets_set_channel(&controller, &selected));
    assert(all_zero(controller.export_uri, sizeof(controller.export_uri)));

    memset(selected.name, 'X', sizeof(selected.name));
    assert(!d1l_ui_channel_sheets_set_channel(&controller, &selected));
    assert(d1l_ui_channel_sheets_channel(&controller) == NULL);
    assert(all_zero(controller.export_uri, sizeof(controller.export_uri)));
}

static void test_export_buffer_is_fail_closed_and_full_buffer_zeroed(void)
{
    d1l_ui_channel_sheets_controller_t controller = {0};
    d1l_channel_info_t selected = channel(9U, "Private");
    assert(d1l_ui_channel_sheets_set_channel(&controller, &selected));

    memset(controller.export_uri, 'S', sizeof(controller.export_uri));
    d1l_ui_channel_sheets_clear_export_uri(&controller);
    assert(all_zero(controller.export_uri, sizeof(controller.export_uri)));

    const char valid[] =
        "meshcore://channel/add?name=Private&secret=0123456789abcdef";
    assert(d1l_ui_channel_sheets_set_export_uri(&controller, valid));
    assert(strcmp(controller.export_uri, valid) == 0);
    d1l_ui_channel_sheets_clear_export_uri(&controller);
    assert(all_zero(controller.export_uri, sizeof(controller.export_uri)));

    char unterminated[D1L_CHANNEL_SHARE_URI_LEN];
    memset(unterminated, 'U', sizeof(unterminated));
    memset(controller.export_uri, 'S', sizeof(controller.export_uri));
    assert(!d1l_ui_channel_sheets_set_export_uri(&controller, unterminated));
    assert(all_zero(controller.export_uri, sizeof(controller.export_uri)));

    memset(controller.export_uri, 'S', sizeof(controller.export_uri));
    assert(!d1l_ui_channel_sheets_set_export_uri(&controller, ""));
    assert(all_zero(controller.export_uri, sizeof(controller.export_uri)));
}

int main(void)
{
    test_redacted_selection_is_bounded_and_invalidates_secret();
    test_export_buffer_is_fail_closed_and_full_buffer_zeroed();
    puts("native UI channel sheet secrets: ok");
    return 0;
}
