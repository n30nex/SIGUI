#include "ui_screen.h"

static void prepare_content_root(lv_obj_t *content)
{
    if (!content) {
        return;
    }
    lv_obj_clean(content);
    lv_obj_scroll_to_y(content, 0, LV_ANIM_OFF);
}

static bool dispatch_screen(d1l_ui_screen_t screen,
                            const d1l_app_snapshot_t *snapshot,
                            const d1l_ui_screen_renderer_t *renderers,
                            size_t renderer_count)
{
    if (!snapshot || !renderers) {
        return false;
    }
    for (size_t i = 0; i < renderer_count; ++i) {
        if (renderers[i].screen == screen && renderers[i].render) {
            renderers[i].render(snapshot);
            return true;
        }
    }
    return false;
}

bool d1l_ui_screen_render(d1l_ui_screen_t screen,
                          const d1l_app_snapshot_t *snapshot,
                          lv_obj_t *content,
                          const d1l_ui_screen_renderer_t *renderers,
                          size_t renderer_count)
{
    if (!content) {
        return false;
    }
    prepare_content_root(content);
    return dispatch_screen(screen, snapshot, renderers, renderer_count);
}
