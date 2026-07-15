#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ui/ui_home_view.h"

static void test_default_view_is_bounded_and_truthful(void)
{
    d1l_ui_home_view_input_t input = {0};
    d1l_ui_home_view_model_t view;
    d1l_ui_home_view(&input, &view);

    assert(strcmp(view.messages_status, "All caught up") == 0);
    assert(strcmp(view.network_status, "0 contacts | 0 nearby") == 0);
    assert(strcmp(view.map_status, "Set a location") == 0);
    assert(strcmp(view.more_status, "0 packets captured") == 0);
    assert(strcmp(view.time_value, "Syncing") == 0);
    assert(strcmp(view.wifi_value, "Off") == 0);
    assert(strcmp(view.ble_value, "Off") == 0);
    assert(strcmp(view.sd_value, "fallback") == 0);
    assert(view.messages_status_color == 0x5EEAD4U);
    assert(view.map_status_color == 0xFBBF24U);
    assert(!view.storage_needs_attention);
}

static void test_ready_view_owns_all_rendered_strings(void)
{
    d1l_ui_home_view_input_t input = {
        .public_unread_count = 2U,
        .dm_unread_count = 3U,
        .contact_count = 4U,
        .node_count = 5U,
        .packet_count = 1U,
        .map_location_set = true,
        .map_tile_cache_ready = true,
        .wifi_connected = true,
        .wifi_enabled = true,
        .ble_companion_enabled = true,
        .time_available = true,
        .time_label = "12:34",
        .storage_data_enabled = true,
        .storage_sd_state = "fat32_ready",
    };
    d1l_ui_home_view_model_t view;
    d1l_ui_home_view(&input, &view);

    assert(strcmp(view.messages_status, "5 unread") == 0);
    assert(strcmp(view.network_status, "4 contacts | 5 nearby") == 0);
    assert(strcmp(view.map_status, "Ready to open") == 0);
    assert(strcmp(view.more_status, "1 packet captured") == 0);
    assert(strcmp(view.time_value, "12:34") == 0);
    assert(strcmp(view.wifi_value, "Connected") == 0);
    assert(strcmp(view.ble_value, "On") == 0);
    assert(strcmp(view.sd_value, "ready") == 0);
    assert(view.sd_value_color == 0x5EEAD4U);
}

static void test_storage_and_map_fail_closed_states(void)
{
    d1l_ui_home_view_input_t input = {
        .map_location_set = true,
        .storage_setup_action = "insert_card",
    };
    d1l_ui_home_view_model_t view;
    d1l_ui_home_view(&input, &view);
    assert(strcmp(view.sd_value, "no card") == 0);
    assert(strcmp(view.map_status, "Insert SD") == 0);

    input.storage_setup_action = "inspect_rp2040_sd_mount_error_firmware_path";
    input.storage_retained_backup_degraded = true;
    d1l_ui_home_view(&input, &view);
    assert(strcmp(view.sd_value, "storage issue") == 0);
    assert(strcmp(view.map_status, "Check SD") == 0);
    assert(view.storage_needs_attention);
    assert(view.sd_value_color == 0xF87171U);

    input.storage_setup_action = NULL;
    input.storage_retained_backup_degraded = false;
    input.storage_sd_needs_fat32 = true;
    d1l_ui_home_view(&input, &view);
    assert(strcmp(view.map_status, "Needs FAT32") == 0);
}

static void test_unread_count_saturates_instead_of_wrapping(void)
{
    d1l_ui_home_view_input_t input = {
        .public_unread_count = SIZE_MAX,
        .dm_unread_count = 1U,
    };
    d1l_ui_home_view_model_t view;
    d1l_ui_home_view(&input, &view);
    assert(strcmp(view.messages_status, "All caught up") != 0);
    assert(strstr(view.messages_status, "unread") != NULL);
}

int main(void)
{
    test_default_view_is_bounded_and_truthful();
    test_ready_view_owns_all_rendered_strings();
    test_storage_and_map_fail_closed_states();
    test_unread_count_saturates_instead_of_wrapping();
    puts("native UI home view model: ok");
    return 0;
}
