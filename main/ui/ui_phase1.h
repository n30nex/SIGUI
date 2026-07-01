#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t d1l_ui_phase1_start(void);
esp_err_t d1l_ui_phase1_show_home(void);
esp_err_t d1l_ui_phase1_request_tab(const char *name);
const char *d1l_ui_phase1_active_tab_name(void);
const char *d1l_ui_phase1_pending_tab_name(void);
bool d1l_ui_phase1_tab_switch_pending(void);
