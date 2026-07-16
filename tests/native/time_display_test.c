#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "platform/time_display.h"

static void test_offsets_and_parser_are_bounded(void)
{
    assert(d1l_time_display_offset_valid(-720));
    assert(d1l_time_display_offset_valid(840));
    assert(!d1l_time_display_offset_valid(-721));
    assert(!d1l_time_display_offset_valid(841));

    int16_t offset = 99;
    assert(d1l_time_display_parse_timezone("UTC", &offset));
    assert(offset == 0);
    assert(d1l_time_display_parse_timezone("UTC+14:00", &offset));
    assert(offset == 840);
    assert(d1l_time_display_parse_timezone("UTC-12:00", &offset));
    assert(offset == -720);
    assert(d1l_time_display_parse_timezone("UTC+05:45", &offset));
    assert(offset == 345);
    assert(!d1l_time_display_parse_timezone("utc", &offset));
    assert(!d1l_time_display_parse_timezone("UTC+14:01", &offset));
    assert(!d1l_time_display_parse_timezone("UTC-12:01", &offset));
    assert(!d1l_time_display_parse_timezone("UTC+05:60", &offset));
    assert(!d1l_time_display_parse_timezone("UTC+05:45 ", &offset));
    assert(!d1l_time_display_parse_timezone(NULL, &offset));
    assert(!d1l_time_display_parse_timezone("UTC", NULL));
}

static void test_labels_are_explicit_fixed_offsets(void)
{
    char label[D1L_TIMEZONE_LABEL_LEN] = {0};
    assert(d1l_time_display_timezone_label(0, label, sizeof(label)));
    assert(strcmp(label, "UTC") == 0);
    assert(d1l_time_display_timezone_label(840, label, sizeof(label)));
    assert(strcmp(label, "UTC+14:00") == 0);
    assert(d1l_time_display_timezone_label(-720, label, sizeof(label)));
    assert(strcmp(label, "UTC-12:00") == 0);
    assert(!d1l_time_display_timezone_label(841, label, sizeof(label)));
    assert(label[0] == '\0');
    assert(!d1l_time_display_timezone_label(0, label, 3U));
    assert(label[0] == '\0');
}

static void test_clock_conversion_is_display_only_and_floor_modulo(void)
{
    char clock[D1L_TIME_DISPLAY_CLOCK_LEN] = {0};
    assert(d1l_time_display_format_clock(0, 0, false,
                                         clock, sizeof(clock)));
    assert(strcmp(clock, "00:00") == 0);
    assert(d1l_time_display_format_clock(0, 840, false,
                                         clock, sizeof(clock)));
    assert(strcmp(clock, "14:00") == 0);
    assert(d1l_time_display_format_clock(0, -720, false,
                                         clock, sizeof(clock)));
    assert(strcmp(clock, "12:00") == 0);
    assert(d1l_time_display_format_clock(-60, 0, false,
                                         clock, sizeof(clock)));
    assert(strcmp(clock, "23:59") == 0);
    assert(d1l_time_display_format_clock(3660, 0, true,
                                         clock, sizeof(clock)));
    assert(strcmp(clock, "~01:01") == 0);
    assert(!d1l_time_display_format_clock(INT64_MAX, 840, false,
                                          clock, sizeof(clock)));
    assert(clock[0] == '\0');
    assert(!d1l_time_display_format_clock(INT64_MIN, -720, false,
                                          clock, sizeof(clock)));
    assert(clock[0] == '\0');
}

int main(void)
{
    test_offsets_and_parser_are_bounded();
    test_labels_are_explicit_fixed_offsets();
    test_clock_conversion_is_display_only_and_floor_modulo();
    puts("native time display: ok");
    return 0;
}
