#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ui/ui_chrome.h"

static void test_home_uses_full_height_without_dock(void)
{
    const d1l_ui_chrome_layout_t layout =
        d1l_ui_chrome_layout_for_screen(D1L_UI_SCREEN_HOME);
    assert(layout.content_y == 16U);
    assert(layout.content_height == 464U);
    assert(!layout.content_scrollable);
    assert(!layout.dock_visible);
    assert(!layout.header_detail_visible);
    assert(strcmp(layout.title, "DeskOS") == 0);
}

static void test_docked_screens_gain_height_without_overlap(void)
{
    const d1l_ui_screen_t screens[] = {
        D1L_UI_SCREEN_MESSAGES,
        D1L_UI_SCREEN_NODES,
        D1L_UI_SCREEN_MAP,
        D1L_UI_SCREEN_PACKETS,
        D1L_UI_SCREEN_SETTINGS,
    };
    assert(D1L_UI_DOCKED_CONTENT_Y == 56U);
    assert(D1L_UI_DOCK_Y == 428U);
    assert(D1L_UI_DOCK_HEIGHT == 52U);
    assert(D1L_UI_DOCKED_CONTENT_HEIGHT == 372U);
    for (size_t i = 0; i < sizeof(screens) / sizeof(screens[0]); ++i) {
        const d1l_ui_chrome_layout_t layout =
            d1l_ui_chrome_layout_for_screen(screens[i]);
        assert(layout.content_y == D1L_UI_DOCKED_CONTENT_Y);
        assert(layout.content_height == D1L_UI_DOCKED_CONTENT_HEIGHT);
        assert(layout.content_height > 362U);
        assert(layout.content_y + layout.content_height == D1L_UI_DOCK_Y);
        assert(layout.content_scrollable);
        assert(layout.dock_visible);
        assert(layout.header_detail_visible);
        assert(strcmp(layout.title, "MeshCore DeskOS") == 0);
    }
}

int main(void)
{
    test_home_uses_full_height_without_dock();
    test_docked_screens_gain_height_without_overlap();
    puts("native UI chrome contract: ok");
    return 0;
}
