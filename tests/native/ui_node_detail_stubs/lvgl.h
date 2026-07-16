#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int lv_coord_t;
typedef uint32_t lv_color_t;
typedef int lv_event_code_t;

typedef struct _lv_obj_t lv_obj_t;
typedef struct _lv_event_t lv_event_t;
typedef void (*lv_event_cb_t)(lv_event_t *event);

struct _lv_event_t {
    void *user_data;
    lv_event_code_t code;
};

struct _lv_obj_t {
    bool valid;
    int kind;
    int x;
    int y;
    int width;
    int height;
    uint32_t flags;
    char text[256];
    lv_obj_t *parent;
    lv_obj_t *children[64];
    size_t child_count;
    lv_event_cb_t event_cb;
    void *event_user_data;
};

enum {
    LV_EVENT_CLICKED = 1,
    LV_LABEL_LONG_DOT = 1,
    LV_LABEL_LONG_WRAP = 2,
    LV_OBJ_FLAG_HIDDEN = 1 << 0,
    LV_OBJ_FLAG_SCROLLABLE = 1 << 1,
    LV_STATE_PRESSED = 1 << 2,
};

extern const int lv_font_montserrat_24;

lv_obj_t *lv_obj_create(lv_obj_t *parent);
lv_obj_t *lv_label_create(lv_obj_t *parent);
lv_obj_t *lv_btn_create(lv_obj_t *parent);
bool lv_obj_is_valid(const lv_obj_t *object);
void lv_obj_del(lv_obj_t *object);
void lv_obj_clean(lv_obj_t *object);
void lv_obj_set_size(lv_obj_t *object, int width, int height);
void lv_obj_set_width(lv_obj_t *object, int width);
void lv_obj_set_pos(lv_obj_t *object, int x, int y);
void lv_obj_set_style_radius(lv_obj_t *object, int value, int selector);
void lv_obj_set_style_bg_color(lv_obj_t *object, lv_color_t color,
                               int selector);
void lv_obj_set_style_border_color(lv_obj_t *object, lv_color_t color,
                                   int selector);
void lv_obj_set_style_border_width(lv_obj_t *object, int value, int selector);
void lv_obj_set_style_pad_all(lv_obj_t *object, int value, int selector);
void lv_obj_set_style_shadow_width(lv_obj_t *object, int value, int selector);
void lv_obj_set_style_text_color(lv_obj_t *object, lv_color_t color,
                                 int selector);
void lv_obj_set_style_text_font(lv_obj_t *object, const void *font,
                                int selector);
void lv_obj_clear_flag(lv_obj_t *object, uint32_t flag);
void lv_obj_add_flag(lv_obj_t *object, uint32_t flag);
bool lv_obj_has_flag(const lv_obj_t *object, uint32_t flag);
void lv_obj_center(lv_obj_t *object);
void lv_obj_move_foreground(lv_obj_t *object);
void lv_obj_add_event_cb(lv_obj_t *object, lv_event_cb_t callback,
                         lv_event_code_t code, void *user_data);
void *lv_event_get_user_data(lv_event_t *event);
void lv_label_set_text(lv_obj_t *label, const char *text);
void lv_label_set_long_mode(lv_obj_t *label, int mode);
lv_color_t lv_color_hex(uint32_t color);

lv_obj_t *lv_test_find_button(lv_obj_t *root, const char *label);
bool lv_test_has_label(const lv_obj_t *root, const char *text);
void lv_test_click(lv_obj_t *button);
