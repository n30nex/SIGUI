#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ui_navigation.h"

typedef struct {
    uint16_t content_y;
    uint16_t content_height;
    bool content_scrollable;
    bool dock_visible;
    bool header_detail_visible;
    const char *title;
} d1l_ui_chrome_layout_t;

d1l_ui_chrome_layout_t d1l_ui_chrome_layout_for_screen(d1l_ui_screen_t screen);
