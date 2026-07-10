#pragma once

#include <stdbool.h>

#include "lvgl.h"

/* All modal lifecycle and query calls are UI-task-only. */
void d1l_ui_modal_reset(void);
void d1l_ui_modal_hide(lv_obj_t *obj);
void d1l_ui_modal_show(lv_obj_t *obj);
bool d1l_ui_modal_visible(const lv_obj_t *obj);
bool d1l_ui_modal_has_active(void);
void d1l_ui_modal_configure_scroll(lv_obj_t *obj);
