#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "app/app_model.h"
#include "lvgl.h"
#include "ui_navigation.h"

typedef void (*d1l_ui_screen_render_fn_t)(const d1l_app_snapshot_t *snapshot);

typedef struct {
    d1l_ui_screen_t screen;
    d1l_ui_screen_render_fn_t render;
} d1l_ui_screen_renderer_t;

bool d1l_ui_screen_render(d1l_ui_screen_t screen,
                          const d1l_app_snapshot_t *snapshot,
                          lv_obj_t *content,
                          const d1l_ui_screen_renderer_t *renderers,
                          size_t renderer_count);
