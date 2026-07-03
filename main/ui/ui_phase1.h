#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    bool ok;
    bool surface_supported;
    bool target_found;
    bool scrollable;
    bool moved;
    char surface[24];
    char tab[16];
    int32_t before_y;
    int32_t after_y;
    int32_t scroll_top_before;
    int32_t scroll_bottom_before;
    int32_t scroll_top_after;
    int32_t scroll_bottom_after;
} d1l_ui_scroll_probe_result_t;

esp_err_t d1l_ui_phase1_start(void);
esp_err_t d1l_ui_phase1_show_home(void);
esp_err_t d1l_ui_phase1_request_tab(const char *name);
esp_err_t d1l_ui_phase1_scroll_probe(const char *surface,
                                      d1l_ui_scroll_probe_result_t *result);
const char *d1l_ui_phase1_active_tab_name(void);
const char *d1l_ui_phase1_pending_tab_name(void);
bool d1l_ui_phase1_tab_switch_pending(void);
