#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct _lv_obj_t lv_obj_t;

void d1l_ui_keyboard_configure_compose(lv_obj_t *keyboard);
bool d1l_ui_keyboard_normalize_probe_target(const char *name, char *out_target,
                                            size_t out_target_len);
bool d1l_ui_keyboard_probe_target_is_dm(const char *target);
bool d1l_ui_keyboard_probe_target_is_compose(const char *target);
bool d1l_ui_keyboard_probe_target_is_onboarding(const char *target);
bool d1l_ui_keyboard_probe_requires_hidden_dock(const char *target);
int32_t d1l_ui_keyboard_probe_min_width(const char *target);
int32_t d1l_ui_keyboard_probe_min_height(const char *target);
