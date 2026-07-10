#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app/app_model.h"
#include "lvgl.h"

typedef enum {
    D1L_UI_MAP_OPTIONS_ROOT = 0,
    D1L_UI_MAP_OPTIONS_CACHE_STATUS,
} d1l_ui_map_options_page_t;

typedef struct {
    lv_event_cb_t open_options;
    lv_event_cb_t close_options;
    lv_event_cb_t open_location;
    lv_event_cb_t open_cache_status;
    lv_event_cb_t back_to_options;
    lv_event_cb_t close_location;
    lv_event_cb_t save_location;
    lv_event_cb_t clear_location;
    lv_event_cb_t location_textarea;
    lv_event_cb_t location_keyboard;
} d1l_ui_map_callbacks_t;

typedef struct {
    lv_obj_t *latitude_textarea;
    lv_obj_t *longitude_textarea;
    lv_obj_t *location_keyboard;
} d1l_ui_map_controls_t;

void d1l_ui_map_render(lv_obj_t *parent,
                       const d1l_app_snapshot_t *snapshot,
                       const d1l_ui_map_callbacks_t *callbacks);

void d1l_ui_map_render_options(lv_obj_t *parent,
                               const d1l_app_snapshot_t *snapshot,
                               d1l_ui_map_options_page_t page,
                               const d1l_ui_map_callbacks_t *callbacks);

void d1l_ui_map_render_location(lv_obj_t *parent,
                                const d1l_app_snapshot_t *snapshot,
                                int32_t latitude_e7,
                                int32_t longitude_e7,
                                const d1l_ui_map_callbacks_t *callbacks,
                                d1l_ui_map_controls_t *controls);

/* The UI owns the visible-map lease. Covers and probes must release it before
 * they obscure or inspect the Map screen. */
void d1l_ui_map_viewport_release(void);
void d1l_ui_map_viewport_set_suppressed(bool suppressed);
void d1l_ui_map_viewport_suppress_next_acquire(void);
bool d1l_ui_map_viewport_refresh(void);
bool d1l_ui_map_viewport_lease_active(void);
uint32_t d1l_ui_map_viewport_generation(void);
