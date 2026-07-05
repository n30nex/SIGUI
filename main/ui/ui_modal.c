#include "ui_modal.h"

void d1l_ui_modal_hide(lv_obj_t *obj)
{
    if (obj) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

void d1l_ui_modal_show(lv_obj_t *obj)
{
    if (obj) {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(obj);
    }
}

bool d1l_ui_modal_visible(const lv_obj_t *obj)
{
    return obj && !lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

void d1l_ui_modal_configure_scroll(lv_obj_t *obj)
{
    if (!obj) {
        return;
    }
    lv_obj_add_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(obj, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_AUTO);
}
