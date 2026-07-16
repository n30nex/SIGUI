#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define D1L_TIMEZONE_SETTING_SCHEMA_VERSION 1U
#define D1L_TIMEZONE_OFFSET_MINUTES_MIN (-720)
#define D1L_TIMEZONE_OFFSET_MINUTES_MAX 840
#define D1L_TIMEZONE_LABEL_LEN 10U
#define D1L_TIME_DISPLAY_CLOCK_LEN 8U

bool d1l_time_display_offset_valid(int32_t offset_minutes);
bool d1l_time_display_parse_timezone(const char *text,
                                     int16_t *out_offset_minutes);
bool d1l_time_display_timezone_label(int16_t offset_minutes,
                                     char *destination,
                                     size_t destination_size);
bool d1l_time_display_format_clock(int64_t utc_epoch_sec,
                                   int16_t offset_minutes,
                                   bool approximate,
                                   char *destination,
                                   size_t destination_size);
