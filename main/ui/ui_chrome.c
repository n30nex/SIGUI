#include "ui_chrome.h"

d1l_ui_chrome_layout_t d1l_ui_chrome_layout_for_screen(d1l_ui_screen_t screen)
{
    if (screen == D1L_UI_SCREEN_HOME) {
        return (d1l_ui_chrome_layout_t){
            .content_y = 24,
            .content_height = 456,
            .content_scrollable = false,
            .dock_visible = false,
            .header_detail_visible = false,
            .title = "DeskOS",
        };
    }
    return (d1l_ui_chrome_layout_t){
        .content_y = 56,
        .content_height = 362,
        .content_scrollable = true,
        .dock_visible = true,
        .header_detail_visible = true,
        .title = "MeshCore DeskOS",
    };
}
