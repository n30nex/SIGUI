#include "ui_map.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "map/map_view_service.h"
#include "ui_keyboard.h"

#define MAP_COLOR_BG 0x071018U
#define MAP_COLOR_PANEL 0x111923U
#define MAP_COLOR_PANEL_PRESSED 0x182533U
#define MAP_COLOR_BORDER 0x263241U
#define MAP_COLOR_TEXT 0xF4F7FBU
#define MAP_COLOR_MUTED 0x8EA0AEU
#define MAP_COLOR_DETAIL 0xD5E0E9U
#define MAP_COLOR_GOOD 0x5EEAD4U
#define MAP_COLOR_INFO 0x93C5FDU
#define MAP_COLOR_WARN 0xFBBF24U
#define MAP_VIEWPORT_WIDTH 446U
#define MAP_VIEWPORT_HEIGHT 288U
#define MAP_DRAG_THRESHOLD_PIXELS 24
#define MAP_DRAG_MAX_X_PIXELS ((int32_t)MAP_VIEWPORT_WIDTH / 3)
#define MAP_DRAG_MAX_Y_PIXELS ((int32_t)MAP_VIEWPORT_HEIGHT / 3)

static uint32_t s_viewport_generation;
static uint32_t s_viewport_revision;
static uint32_t s_viewport_suppression_depth;
static bool s_viewport_suppress_next_acquire;
static bool s_viewport_allocation_failed;
static portMUX_TYPE s_viewport_lease_lock = portMUX_INITIALIZER_UNLOCKED;
static uint16_t *s_viewport_pixels;
static lv_img_dsc_t s_viewport_image;
static lv_obj_t *s_viewport_image_obj;
static lv_obj_t *s_viewport_status_label;
static lv_obj_t *s_viewport_detail_label;
static lv_obj_t *s_viewport_readiness_label;
static lv_obj_t *s_viewport_attribution_label;
static lv_obj_t *s_viewport_zoom_label;
static lv_obj_t *s_viewport_zoom_in_button;
static lv_obj_t *s_viewport_zoom_out_button;
static bool s_viewport_position_initialized;
static int32_t s_viewport_saved_lat_e7;
static int32_t s_viewport_saved_lon_e7;
static int32_t s_viewport_lat_e7;
static int32_t s_viewport_lon_e7;
static uint8_t s_viewport_zoom = D1L_MAP_VIEW_DEFAULT_ZOOM;
static bool s_viewport_drag_active;
static int32_t s_viewport_drag_x;
static int32_t s_viewport_drag_y;

static lv_obj_t *map_label(lv_obj_t *parent, const char *text, uint32_t color)
{
    if (!parent || !text) {
        return NULL;
    }
    lv_obj_t *label = lv_label_create(parent);
    if (!label) {
        return NULL;
    }
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

static void map_label_dot(lv_obj_t *label, int width)
{
    if (!label) {
        return;
    }
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, width);
}

static void map_label_wrap(lv_obj_t *label, int width)
{
    if (!label) {
        return;
    }
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, width);
}

static lv_obj_t *map_panel(lv_obj_t *parent, int x, int y, int width, int height)
{
    if (!parent) {
        return NULL;
    }
    lv_obj_t *panel = lv_obj_create(parent);
    if (!panel) {
        return NULL;
    }
    lv_obj_set_size(panel, width, height);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_style_radius(panel, 10, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(MAP_COLOR_PANEL), 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(MAP_COLOR_BORDER), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}

static lv_obj_t *map_button(lv_obj_t *parent, const char *text,
                            int x, int y, int width, int height,
                            lv_event_cb_t callback)
{
    if (!parent || !text) {
        return NULL;
    }
    lv_obj_t *button = lv_btn_create(parent);
    if (!button) {
        return NULL;
    }
    lv_obj_set_size(button, width, height);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_style_radius(button, 9, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x1E2A36), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x263545), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_t *label = map_label(button, text, MAP_COLOR_TEXT);
    if (label) {
        lv_obj_center(label);
    }
    if (callback) {
        lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, NULL);
    }
    return button;
}

static void map_style_primary_button(lv_obj_t *button)
{
    if (!button) {
        return;
    }
    lv_obj_set_style_bg_color(button, lv_color_hex(0x12362F), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x174A40), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(button, lv_color_hex(MAP_COLOR_GOOD), 0);
    lv_obj_set_style_border_width(button, 1, 0);
}

static void map_render_header(lv_obj_t *parent, const char *title,
                              const char *subtitle, const char *back_label,
                              lv_event_cb_t back_callback)
{
    if (!parent) {
        return;
    }
    int title_x = 16;
    if (back_label && back_callback) {
        const int back_width = strcmp(back_label, "Back to Map") == 0 ? 120 : 80;
        map_button(parent, back_label, 16, 8, back_width, 44, back_callback);
        title_x = 16 + back_width + 16;
    }
    lv_obj_t *title_label = map_label(parent, title, MAP_COLOR_TEXT);
    if (title_label) {
        lv_obj_set_style_text_font(title_label, &lv_font_montserrat_24, 0);
        map_label_dot(title_label, 480 - title_x - 16);
        lv_obj_set_pos(title_label, title_x, 6);
    }
    lv_obj_t *subtitle_label = map_label(parent, subtitle ? subtitle : "",
                                         MAP_COLOR_MUTED);
    if (subtitle_label) {
        map_label_dot(subtitle_label, 480 - title_x - 16);
        lv_obj_set_pos(subtitle_label, title_x, 34);
    }
}

static const char *map_location_status(const d1l_app_snapshot_t *snapshot)
{
    return snapshot && snapshot->map_location_set ? "Saved" : "Not set";
}

static const char *map_wifi_status(const d1l_app_snapshot_t *snapshot)
{
    if (!snapshot) {
        return "Unavailable";
    }
    if (snapshot->wifi_connected) {
        return "Connected";
    }
    if (snapshot->wifi_connecting) {
        return "Connecting";
    }
    return snapshot->wifi_enabled ? "Not connected" : "Off";
}

static const char *map_sd_status(const d1l_app_snapshot_t *snapshot)
{
    if (!snapshot) {
        return "Unavailable";
    }
    if (snapshot->storage_retained_sd_degraded ||
        (snapshot->storage_sd_state &&
         (strcmp(snapshot->storage_sd_state, "error") == 0 ||
          strcmp(snapshot->storage_sd_state, "bridge_reported") == 0))) {
        return "Error";
    }
    if (snapshot->storage_sd_needs_fat32) {
        return "Needs FAT32";
    }
    if (snapshot->map_tile_cache_ready) {
        return "Ready";
    }
    if (!snapshot->storage_sd_present) {
        return "Not inserted";
    }
    return snapshot->storage_sd_mounted ? "Preparing" : "Not mounted";
}

static uint8_t map_zoom_clamp(uint8_t zoom)
{
    if (zoom < D1L_MAP_VIEW_MIN_ZOOM) {
        return D1L_MAP_VIEW_MIN_ZOOM;
    }
    if (zoom > D1L_MAP_VIEW_MAX_ZOOM) {
        return D1L_MAP_VIEW_MAX_ZOOM;
    }
    return zoom;
}

static int32_t map_drag_clamp(int32_t value, int32_t limit)
{
    if (value < -limit) {
        return -limit;
    }
    if (value > limit) {
        return limit;
    }
    return value;
}

static void map_viewport_sync_position(const d1l_app_snapshot_t *snapshot)
{
    if (!snapshot || !snapshot->map_location_set) {
        s_viewport_position_initialized = false;
        return;
    }
    if (s_viewport_position_initialized &&
        s_viewport_saved_lat_e7 == snapshot->map_lat_e7 &&
        s_viewport_saved_lon_e7 == snapshot->map_lon_e7) {
        return;
    }
    s_viewport_saved_lat_e7 = snapshot->map_lat_e7;
    s_viewport_saved_lon_e7 = snapshot->map_lon_e7;
    s_viewport_lat_e7 = snapshot->map_lat_e7;
    s_viewport_lon_e7 = snapshot->map_lon_e7;
    s_viewport_zoom = map_zoom_clamp(snapshot->map_tile_zoom);
    s_viewport_position_initialized = true;
}

static bool map_text_contains(const char *text, const char *needle)
{
    return text && needle && strstr(text, needle) != NULL;
}

static void map_viewport_copy(const d1l_app_snapshot_t *snapshot,
                              const char **title, const char **detail,
                              uint32_t *color)
{
    if (!snapshot || !title || !detail || !color) {
        return;
    }
    const char *download_state = snapshot->map_tile_download_state;
    if (snapshot->storage_retained_sd_degraded ||
        (snapshot->storage_sd_state &&
         (strcmp(snapshot->storage_sd_state, "error") == 0 ||
          strcmp(snapshot->storage_sd_state, "bridge_reported") == 0)) ||
        map_text_contains(download_state, "error") ||
        map_text_contains(download_state, "failed")) {
        *title = "Map unavailable";
        *detail = "Check the SD card, then reopen Map.";
        *color = MAP_COLOR_WARN;
    } else if (!snapshot->map_location_set) {
        *title = "Set a location";
        *detail = "Open Options to choose the area shown here.";
        *color = MAP_COLOR_WARN;
    } else if (!snapshot->map_tile_cache_ready) {
        *title = "SD card needed";
        *detail = "Map tiles are kept on a ready FAT32 card.";
        *color = MAP_COLOR_WARN;
    } else if (!snapshot->wifi_connected) {
        *title = snapshot->wifi_connecting ? "Connecting to Wi-Fi" : "Wi-Fi needed";
        *detail = "Connect Wi-Fi to load this local map area.";
        *color = MAP_COLOR_WARN;
    } else if (map_text_contains(download_state, "download") ||
               map_text_contains(download_state, "loading") ||
               map_text_contains(download_state, "request")) {
        *title = "Loading map";
        *detail = "Downloading a small area around your location.";
        *color = MAP_COLOR_INFO;
    } else if (!snapshot->map_tile_render_supported) {
        *title = "Map view starting";
        *detail = "The built-in map renderer is not ready yet.";
        *color = MAP_COLOR_INFO;
    } else {
        *title = "Loading map";
        *detail = "Preparing the saved area around your location.";
        *color = MAP_COLOR_INFO;
    }
}

static void map_set_hidden(lv_obj_t *object, bool hidden)
{
    if (!object) {
        return;
    }
    if (hidden) {
        lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(object, LV_OBJ_FLAG_HIDDEN);
    }
}

static bool map_viewport_consume_acquire_suppression(void)
{
    bool suppressed;
    portENTER_CRITICAL(&s_viewport_lease_lock);
    suppressed = s_viewport_suppression_depth > 0U || s_viewport_suppress_next_acquire;
    s_viewport_suppress_next_acquire = false;
    portEXIT_CRITICAL(&s_viewport_lease_lock);
    return suppressed;
}

static bool map_viewport_ensure_pixels(void)
{
    if (s_viewport_pixels) {
        return true;
    }
    const size_t bytes = (size_t)MAP_VIEWPORT_WIDTH *
        (size_t)MAP_VIEWPORT_HEIGHT * sizeof(uint16_t);
    s_viewport_pixels = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_viewport_pixels) {
        s_viewport_allocation_failed = true;
        return false;
    }
    s_viewport_allocation_failed = false;
    memset(s_viewport_pixels, 0, bytes);
    memset(&s_viewport_image, 0, sizeof(s_viewport_image));
    s_viewport_image.header.always_zero = 0;
    s_viewport_image.header.cf = LV_IMG_CF_TRUE_COLOR;
    s_viewport_image.header.w = MAP_VIEWPORT_WIDTH;
    s_viewport_image.header.h = MAP_VIEWPORT_HEIGHT;
    s_viewport_image.data_size = (uint32_t)bytes;
    s_viewport_image.data = (const uint8_t *)s_viewport_pixels;
    return true;
}

static void map_viewport_apply_service_status(const d1l_map_view_status_t *status)
{
    if (!status || !s_viewport_status_label || !s_viewport_detail_label ||
        !s_viewport_readiness_label) {
        return;
    }
    const char *title = "Map view starting";
    const char *detail = "Preparing the built-in local map.";
    uint32_t color = MAP_COLOR_INFO;
    bool show_overlay = !status->frame_ready;

    if (s_viewport_allocation_failed) {
        title = "Map unavailable";
        detail = "Not enough map memory. Other device features remain available.";
        color = MAP_COLOR_WARN;
        show_overlay = true;
    } else if (status->rate_limited) {
        title = "Map paused";
        detail = "The tile service asked us to wait before retrying.";
        color = MAP_COLOR_WARN;
        show_overlay = true;
    } else if (strcmp(status->phase, "sd_cache_required") == 0) {
        title = "SD card needed";
        detail = "Map tiles are kept on a ready FAT32 card.";
        color = MAP_COLOR_WARN;
        show_overlay = true;
    } else if (strcmp(status->phase, "wifi_required") == 0) {
        title = "Wi-Fi needed";
        detail = "Connect Wi-Fi to load this local map area.";
        color = MAP_COLOR_WARN;
        show_overlay = true;
    } else if (strcmp(status->phase, "time_sync") == 0) {
        title = "Secure time needed";
        detail = "Keep Wi-Fi connected, then reopen Map.";
        color = MAP_COLOR_WARN;
        show_overlay = true;
    } else if (status->failed_tiles > 0U && !status->frame_ready) {
        title = "Map unavailable";
        detail = "Tiles could not be loaded. Check Wi-Fi and the SD card.";
        color = MAP_COLOR_WARN;
        show_overlay = true;
    } else if (!status->frame_ready &&
               strcmp(status->phase, "loading_cache") == 0) {
        title = "Loading saved map";
        detail = "Reading saved tiles from the SD card.";
        color = MAP_COLOR_INFO;
        show_overlay = true;
    } else if (!status->frame_ready && strcmp(status->phase, "queued") == 0) {
        title = "Preparing map";
        detail = "Checking saved tiles first.";
        color = MAP_COLOR_INFO;
        show_overlay = true;
    } else if (!status->frame_ready) {
        title = "Downloading map";
        detail = "Fetching only the visible current view.";
        color = MAP_COLOR_INFO;
        show_overlay = true;
    }

    lv_label_set_text(s_viewport_status_label, title);
    lv_obj_set_style_text_color(s_viewport_status_label, lv_color_hex(color), 0);
    lv_label_set_text(s_viewport_detail_label, detail);
    char readiness[96];
    snprintf(readiness, sizeof(readiness), "Wi-Fi %s  |  SD %s",
             status->wifi_connected ? "Connected" : "Not connected",
             status->sd_cache_ready ? "Ready" : "Not ready");
    lv_label_set_text(s_viewport_readiness_label, readiness);
    map_set_hidden(s_viewport_status_label, !show_overlay);
    map_set_hidden(s_viewport_detail_label, !show_overlay);
    map_set_hidden(s_viewport_readiness_label, !show_overlay);
    if (s_viewport_attribution_label) {
        lv_obj_move_foreground(s_viewport_attribution_label);
    }
}

static void map_viewport_update_controls(void)
{
    if (s_viewport_zoom_label) {
        char zoom[8];
        snprintf(zoom, sizeof(zoom), "z%u", (unsigned)s_viewport_zoom);
        lv_label_set_text(s_viewport_zoom_label, zoom);
    }
    if (s_viewport_zoom_in_button) {
        if (s_viewport_zoom >= D1L_MAP_VIEW_MAX_ZOOM) {
            lv_obj_add_state(s_viewport_zoom_in_button, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(s_viewport_zoom_in_button, LV_STATE_DISABLED);
        }
    }
    if (s_viewport_zoom_out_button) {
        if (s_viewport_zoom <= D1L_MAP_VIEW_MIN_ZOOM) {
            lv_obj_add_state(s_viewport_zoom_out_button, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(s_viewport_zoom_out_button, LV_STATE_DISABLED);
        }
    }
}

static bool map_viewport_request_interactive_view(void)
{
    uint32_t active_generation;
    bool suppressed;
    portENTER_CRITICAL(&s_viewport_lease_lock);
    active_generation = s_viewport_generation;
    suppressed = s_viewport_suppression_depth > 0U;
    portEXIT_CRITICAL(&s_viewport_lease_lock);
    if (!s_viewport_position_initialized || active_generation == 0U || suppressed) {
        return false;
    }

    uint32_t generation = 0U;
    esp_err_t ret = d1l_map_view_service_acquire_visible(
        s_viewport_lat_e7, s_viewport_lon_e7, s_viewport_zoom,
        MAP_VIEWPORT_WIDTH, MAP_VIEWPORT_HEIGHT, &generation);
    if (ret != ESP_OK || generation == 0U) {
        return false;
    }

    portENTER_CRITICAL(&s_viewport_lease_lock);
    if (s_viewport_suppression_depth == 0U && s_viewport_generation != 0U) {
        s_viewport_generation = generation;
        s_viewport_revision = 0U;
    } else {
        generation = 0U;
    }
    portEXIT_CRITICAL(&s_viewport_lease_lock);
    if (generation == 0U) {
        d1l_map_view_service_release_visible(generation);
        return false;
    }
    map_viewport_update_controls();
    d1l_map_view_status_t status = {0};
    d1l_map_view_service_status(&status);
    map_viewport_apply_service_status(&status);
    return true;
}

static void map_viewport_reset_image_position(void)
{
    if (s_viewport_image_obj) {
        lv_obj_center(s_viewport_image_obj);
    }
}

static void map_viewport_drag_event_cb(lv_event_t *event)
{
    if (!event) {
        return;
    }
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        portENTER_CRITICAL(&s_viewport_lease_lock);
        s_viewport_drag_active = true;
        s_viewport_drag_x = 0;
        s_viewport_drag_y = 0;
        portEXIT_CRITICAL(&s_viewport_lease_lock);
        return;
    }
    if (code == LV_EVENT_PRESSING) {
        lv_indev_t *indev = lv_indev_get_act();
        if (!indev) {
            return;
        }
        lv_point_t vector = {0};
        lv_indev_get_vect(indev, &vector);
        int32_t preview_x = 0;
        int32_t preview_y = 0;
        bool drag_active;
        portENTER_CRITICAL(&s_viewport_lease_lock);
        drag_active = s_viewport_drag_active;
        if (drag_active) {
            s_viewport_drag_x = map_drag_clamp(
                s_viewport_drag_x + vector.x, MAP_DRAG_MAX_X_PIXELS);
            s_viewport_drag_y = map_drag_clamp(
                s_viewport_drag_y + vector.y, MAP_DRAG_MAX_Y_PIXELS);
            preview_x = s_viewport_drag_x;
            preview_y = s_viewport_drag_y;
        }
        portEXIT_CRITICAL(&s_viewport_lease_lock);
        if (!drag_active) {
            return;
        }
        if (s_viewport_image_obj) {
            lv_obj_set_pos(s_viewport_image_obj, 1 + preview_x, 1 + preview_y);
        }
        return;
    }
    if (code == LV_EVENT_PRESS_LOST) {
        portENTER_CRITICAL(&s_viewport_lease_lock);
        s_viewport_drag_active = false;
        s_viewport_drag_x = 0;
        s_viewport_drag_y = 0;
        portEXIT_CRITICAL(&s_viewport_lease_lock);
        map_viewport_reset_image_position();
        return;
    }
    if (code != LV_EVENT_RELEASED) {
        return;
    }

    int32_t drag_x;
    int32_t drag_y;
    bool drag_active;
    portENTER_CRITICAL(&s_viewport_lease_lock);
    drag_active = s_viewport_drag_active;
    drag_x = s_viewport_drag_x;
    drag_y = s_viewport_drag_y;
    s_viewport_drag_active = false;
    s_viewport_drag_x = 0;
    s_viewport_drag_y = 0;
    portEXIT_CRITICAL(&s_viewport_lease_lock);
    if (!drag_active) {
        map_viewport_reset_image_position();
        return;
    }
    map_viewport_reset_image_position();
    if (abs(drag_x) < MAP_DRAG_THRESHOLD_PIXELS &&
        abs(drag_y) < MAP_DRAG_THRESHOLD_PIXELS) {
        return;
    }

    const int32_t old_lat_e7 = s_viewport_lat_e7;
    const int32_t old_lon_e7 = s_viewport_lon_e7;
    const int32_t delta_x = -drag_x;
    const int32_t delta_y = -drag_y;
    if (!d1l_map_math_pan_center(old_lat_e7, old_lon_e7, s_viewport_zoom,
                                 delta_x, delta_y, &s_viewport_lat_e7,
                                 &s_viewport_lon_e7) ||
        !map_viewport_request_interactive_view()) {
        s_viewport_lat_e7 = old_lat_e7;
        s_viewport_lon_e7 = old_lon_e7;
    }
}

static void map_viewport_zoom_in_event_cb(lv_event_t *event)
{
    (void)event;
    if (s_viewport_zoom >= D1L_MAP_VIEW_MAX_ZOOM) {
        return;
    }
    const uint8_t old_zoom = s_viewport_zoom;
    ++s_viewport_zoom;
    if (!map_viewport_request_interactive_view()) {
        s_viewport_zoom = old_zoom;
    }
    map_viewport_update_controls();
}

static void map_viewport_zoom_out_event_cb(lv_event_t *event)
{
    (void)event;
    if (s_viewport_zoom <= D1L_MAP_VIEW_MIN_ZOOM) {
        return;
    }
    const uint8_t old_zoom = s_viewport_zoom;
    --s_viewport_zoom;
    if (!map_viewport_request_interactive_view()) {
        s_viewport_zoom = old_zoom;
    }
    map_viewport_update_controls();
}

static void map_viewport_recenter_event_cb(lv_event_t *event)
{
    (void)event;
    if (!s_viewport_position_initialized) {
        return;
    }
    const int32_t old_lat_e7 = s_viewport_lat_e7;
    const int32_t old_lon_e7 = s_viewport_lon_e7;
    s_viewport_lat_e7 = s_viewport_saved_lat_e7;
    s_viewport_lon_e7 = s_viewport_saved_lon_e7;
    if (!map_viewport_request_interactive_view()) {
        s_viewport_lat_e7 = old_lat_e7;
        s_viewport_lon_e7 = old_lon_e7;
    }
}

bool d1l_ui_map_viewport_refresh(void)
{
    uint32_t generation;
    portENTER_CRITICAL(&s_viewport_lease_lock);
    generation = s_viewport_generation;
    const bool suppressed = s_viewport_suppression_depth > 0U;
    const bool drag_active = s_viewport_drag_active;
    portEXIT_CRITICAL(&s_viewport_lease_lock);
    if (suppressed || generation == 0U || !s_viewport_image_obj ||
        drag_active) {
        return false;
    }
    d1l_map_view_status_t status = {0};
    d1l_map_view_service_status(&status);
    if (!status.visible || status.generation != generation) {
        return false;
    }

    d1l_map_view_frame_t frame = {0};
    esp_err_t ret = d1l_map_view_service_acquire_frame(s_viewport_revision, &frame);
    bool copied = false;
    if (ret == ESP_OK && frame.held) {
        const size_t capacity = (size_t)MAP_VIEWPORT_WIDTH *
            (size_t)MAP_VIEWPORT_HEIGHT;
        if (frame.generation == generation && frame.pixels &&
            frame.pixel_count > 0U && frame.pixel_count <= capacity &&
            frame.width > 0U && frame.height > 0U) {
            if (map_viewport_ensure_pixels()) {
                lv_img_cache_invalidate_src(&s_viewport_image);
                memcpy(s_viewport_pixels, frame.pixels,
                       frame.pixel_count * sizeof(uint16_t));
                s_viewport_image.header.w = frame.width;
                s_viewport_image.header.h = frame.height;
                s_viewport_image.data_size =
                    (uint32_t)(frame.pixel_count * sizeof(uint16_t));
                portENTER_CRITICAL(&s_viewport_lease_lock);
                if (s_viewport_generation == generation &&
                    s_viewport_suppression_depth == 0U) {
                    s_viewport_revision = frame.revision;
                    copied = true;
                }
                portEXIT_CRITICAL(&s_viewport_lease_lock);
            }
        }
        d1l_map_view_service_release_frame(&frame);
    }
    if (copied) {
        lv_img_cache_invalidate_src(&s_viewport_image);
        lv_img_set_src(s_viewport_image_obj, &s_viewport_image);
        lv_obj_set_size(s_viewport_image_obj, s_viewport_image.header.w,
                        s_viewport_image.header.h);
        lv_obj_center(s_viewport_image_obj);
        lv_obj_clear_flag(s_viewport_image_obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_invalidate(s_viewport_image_obj);
    }
    d1l_map_view_service_status(&status);
    map_viewport_apply_service_status(&status);
    return copied;
}

static lv_obj_t *map_menu_row(lv_obj_t *parent, int y, const char *title,
                              const char *status, uint32_t accent,
                              lv_event_cb_t callback)
{
    lv_obj_t *row = map_panel(parent, 16, y, 448, 68);
    if (!row) {
        return NULL;
    }
    lv_obj_set_style_bg_color(row, lv_color_hex(MAP_COLOR_PANEL_PRESSED), LV_STATE_PRESSED);
    if (callback) {
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, callback, LV_EVENT_CLICKED, NULL);
    }
    lv_obj_t *title_label = map_label(row, title, accent);
    map_label_dot(title_label, 190);
    if (title_label) {
        lv_obj_set_pos(title_label, 28, 24);
    }
    lv_obj_t *status_label = map_label(row, status, MAP_COLOR_MUTED);
    map_label_dot(status_label, 164);
    if (status_label) {
        lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(status_label, 236, 24);
    }
    lv_obj_t *chevron = map_label(row, ">", MAP_COLOR_MUTED);
    if (chevron) {
        lv_obj_set_pos(chevron, 422, 24);
    }
    return row;
}

static void map_render_status_row(lv_obj_t *parent, int y, const char *title,
                                  const char *value, uint32_t color)
{
    lv_obj_t *row = map_panel(parent, 12, y, 424, 48);
    if (!row) {
        return;
    }
    lv_obj_t *title_label = map_label(row, title, MAP_COLOR_DETAIL);
    map_label_dot(title_label, 210);
    if (title_label) {
        lv_obj_set_pos(title_label, 12, 15);
    }
    lv_obj_t *value_label = map_label(row, value, color);
    map_label_dot(value_label, 194);
    if (value_label) {
        lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(value_label, 218, 15);
    }
}

static void map_saved_area_status(const d1l_map_view_status_t *status,
                                  char *value, size_t value_len,
                                  uint32_t *color)
{
    if (!value || value_len == 0U || !color) {
        return;
    }
    *color = MAP_COLOR_MUTED;
    snprintf(value, value_len, "%s", "Open Map to save");
    if (!status || status->planned_tiles == 0U) {
        return;
    }
    if (status->worker_running) {
        snprintf(value, value_len, "Saving %u/%u",
                 (unsigned)status->rendered_tiles,
                 (unsigned)status->planned_tiles);
        *color = MAP_COLOR_INFO;
    } else if (!status->worker_running &&
               status->rendered_tiles == status->planned_tiles &&
               status->failed_tiles == 0U) {
        snprintf(value, value_len, "%s", "Local area saved");
        *color = MAP_COLOR_GOOD;
    } else if (status->rendered_tiles > 0U) {
        snprintf(value, value_len, "%s", "Partly saved");
        *color = MAP_COLOR_WARN;
    }
}

void d1l_ui_map_render(lv_obj_t *parent,
                       const d1l_app_snapshot_t *snapshot,
                       const d1l_ui_map_callbacks_t *callbacks)
{
    if (!parent || !snapshot || !callbacks) {
        return;
    }
    map_viewport_sync_position(snapshot);
    d1l_ui_map_viewport_release();
    s_viewport_image_obj = NULL;
    s_viewport_status_label = NULL;
    s_viewport_detail_label = NULL;
    s_viewport_readiness_label = NULL;
    s_viewport_attribution_label = NULL;
    s_viewport_zoom_label = NULL;
    s_viewport_zoom_in_button = NULL;
    s_viewport_zoom_out_button = NULL;
    lv_obj_set_style_pad_bottom(parent, 0, 0);
    lv_obj_t *title = map_label(parent, "Map", MAP_COLOR_TEXT);
    if (title) {
        lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
        lv_obj_set_pos(title, 16, 6);
    }
    map_button(parent, "Options", 344, 4, 120, 44, callbacks->open_options);

    lv_obj_t *viewport = map_panel(parent, 16, 58, 448, 290);
    if (!viewport) {
        return;
    }
    lv_obj_set_style_bg_color(viewport, lv_color_hex(0x0B151E), 0);
    lv_obj_add_flag(viewport, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(viewport, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(viewport, map_viewport_drag_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(viewport, map_viewport_drag_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(viewport, map_viewport_drag_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(viewport, map_viewport_drag_event_cb, LV_EVENT_PRESS_LOST, NULL);
    s_viewport_image_obj = lv_img_create(viewport);
    if (s_viewport_image_obj) {
        lv_obj_set_size(s_viewport_image_obj, MAP_VIEWPORT_WIDTH, MAP_VIEWPORT_HEIGHT);
        lv_obj_center(s_viewport_image_obj);
        lv_obj_add_flag(s_viewport_image_obj, LV_OBJ_FLAG_HIDDEN);
    }
    const char *state_title = "Map unavailable";
    const char *state_detail = "Map status is unavailable.";
    uint32_t state_color = MAP_COLOR_WARN;
    map_viewport_copy(snapshot, &state_title, &state_detail, &state_color);

    lv_obj_t *status = map_label(viewport, state_title, state_color);
    s_viewport_status_label = status;
    if (status) {
        lv_obj_set_style_text_font(status, &lv_font_montserrat_24, 0);
        map_label_dot(status, 400);
        lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(status, 24, 94);
    }
    lv_obj_t *detail = map_label(viewport, state_detail, MAP_COLOR_DETAIL);
    s_viewport_detail_label = detail;
    if (detail) {
        map_label_wrap(detail, 400);
        lv_obj_set_style_text_align(detail, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(detail, 24, 134);
    }
    char readiness[96];
    snprintf(readiness, sizeof(readiness), "Wi-Fi %s  |  SD %s",
             map_wifi_status(snapshot), map_sd_status(snapshot));
    lv_obj_t *ready = map_label(viewport, readiness, MAP_COLOR_MUTED);
    s_viewport_readiness_label = ready;
    if (ready) {
        map_label_dot(ready, 400);
        lv_obj_set_style_text_align(ready, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(ready, 24, 188);
    }
    lv_obj_t *attribution = map_label(viewport, "(c) OpenStreetMap contributors",
                                      MAP_COLOR_DETAIL);
    s_viewport_attribution_label = attribution;
    if (attribution) {
        map_label_dot(attribution, 416);
        lv_obj_set_style_text_align(attribution, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(attribution, 16, 260);
        lv_obj_set_style_bg_color(attribution, lv_color_hex(0x071018), 0);
        lv_obj_set_style_bg_opa(attribution, LV_OPA_80, 0);
        lv_obj_set_style_pad_all(attribution, 2, 0);
    }

    if (snapshot->map_location_set) {
        map_button(viewport, "Center", 12, 12, 92, 48,
                   map_viewport_recenter_event_cb);
        lv_obj_t *drag_hint = map_label(viewport, "Drag to pan", MAP_COLOR_DETAIL);
        if (drag_hint) {
            map_label_dot(drag_hint, 128);
            lv_obj_set_pos(drag_hint, 116, 29);
            lv_obj_set_style_bg_color(drag_hint, lv_color_hex(MAP_COLOR_BG), 0);
            lv_obj_set_style_bg_opa(drag_hint, LV_OPA_80, 0);
            lv_obj_set_style_pad_all(drag_hint, 2, 0);
        }
        s_viewport_zoom_label = map_label(viewport, "z10", MAP_COLOR_DETAIL);
        if (s_viewport_zoom_label) {
            map_label_dot(s_viewport_zoom_label, 44);
            lv_obj_set_style_text_align(s_viewport_zoom_label, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_pos(s_viewport_zoom_label, 292, 29);
            lv_obj_set_style_bg_color(s_viewport_zoom_label, lv_color_hex(MAP_COLOR_BG), 0);
            lv_obj_set_style_bg_opa(s_viewport_zoom_label, LV_OPA_80, 0);
            lv_obj_set_style_pad_all(s_viewport_zoom_label, 2, 0);
        }
        s_viewport_zoom_out_button = map_button(
            viewport, "-", 344, 12, 44, 48, map_viewport_zoom_out_event_cb);
        s_viewport_zoom_in_button = map_button(
            viewport, "+", 392, 12, 44, 48, map_viewport_zoom_in_event_cb);
        map_viewport_update_controls();
    }

    const bool acquire_suppressed = map_viewport_consume_acquire_suppression();
    const bool viewport_memory_ready = !acquire_suppressed && s_viewport_image_obj &&
        map_viewport_ensure_pixels();
    if (snapshot->map_location_set && viewport_memory_ready && !acquire_suppressed) {
        uint32_t generation = 0U;
        esp_err_t ret = d1l_map_view_service_acquire_visible(
            s_viewport_lat_e7, s_viewport_lon_e7, s_viewport_zoom,
            MAP_VIEWPORT_WIDTH, MAP_VIEWPORT_HEIGHT,
            &generation);
        if (ret == ESP_OK && generation != 0U) {
            bool accepted;
            portENTER_CRITICAL(&s_viewport_lease_lock);
            accepted = s_viewport_suppression_depth == 0U;
            if (accepted) {
                s_viewport_generation = generation;
                s_viewport_revision = 0U;
            }
            portEXIT_CRITICAL(&s_viewport_lease_lock);
            if (accepted) {
                (void)d1l_ui_map_viewport_refresh();
            } else {
                d1l_map_view_service_release_visible(generation);
            }
        } else if (s_viewport_status_label && s_viewport_detail_label) {
            lv_label_set_text(s_viewport_status_label, "Map unavailable");
            lv_label_set_text(s_viewport_detail_label,
                              "The built-in map service could not start.");
        }
    } else if (snapshot->map_location_set && acquire_suppressed &&
               s_viewport_status_label && s_viewport_detail_label) {
        lv_label_set_text(s_viewport_status_label, "Map preview");
        lv_label_set_text(s_viewport_detail_label,
                          "Automation view - no map tiles requested.");
        lv_obj_set_style_text_color(s_viewport_status_label,
                                    lv_color_hex(MAP_COLOR_INFO), 0);
    } else if (snapshot->map_location_set && !acquire_suppressed &&
               !viewport_memory_ready &&
               s_viewport_status_label && s_viewport_detail_label) {
        lv_label_set_text(s_viewport_status_label, "Map unavailable");
        lv_label_set_text(s_viewport_detail_label,
                          "Not enough map memory. Other device features remain available.");
    }
}

static void map_render_options_root(lv_obj_t *parent,
                                    const d1l_app_snapshot_t *snapshot,
                                    const d1l_ui_map_callbacks_t *callbacks)
{
    map_render_header(parent, "Map options", "Location and cache", "Back to Map",
                      callbacks->close_options);
    map_menu_row(parent, 68, "Set location", map_location_status(snapshot),
                 snapshot->map_location_set ? MAP_COLOR_GOOD : MAP_COLOR_WARN,
                 callbacks->open_location);
    const bool cache_ready = snapshot->map_tile_cache_ready;
    map_menu_row(parent, 140, "Cache status", cache_ready ? "SD ready" : "Needs SD",
                 cache_ready ? MAP_COLOR_GOOD : MAP_COLOR_WARN,
                 callbacks->open_cache_status);
    lv_obj_t *policy = map_panel(parent, 16, 280, 448, 80);
    if (policy) {
        lv_obj_t *text = map_label(policy,
            "Tiles download only while Map is open. Reopening the same area uses the saved copy.",
            MAP_COLOR_MUTED);
        map_label_wrap(text, 418);
        if (text) {
            lv_obj_set_pos(text, 14, 14);
        }
    }
}

static void map_render_cache_status(lv_obj_t *parent,
                                    const d1l_app_snapshot_t *snapshot,
                                    const d1l_ui_map_callbacks_t *callbacks)
{
    map_render_header(parent, "Cache status", "Read-only readiness", "Back",
                      callbacks->back_to_options);
    d1l_map_view_status_t view_status = {0};
    d1l_map_view_service_status(&view_status);
    char map_view_value[32];
    uint32_t map_view_color = MAP_COLOR_MUTED;
    map_saved_area_status(&view_status, map_view_value, sizeof(map_view_value),
                          &map_view_color);

    lv_obj_t *panel = map_panel(parent, 16, 64, 448, 324);
    if (!panel) {
        return;
    }
    map_render_status_row(panel, 16, "Wi-Fi", map_wifi_status(snapshot),
                          snapshot->wifi_connected ? MAP_COLOR_GOOD : MAP_COLOR_WARN);
    map_render_status_row(panel, 80, "SD card", map_sd_status(snapshot),
                          snapshot->map_tile_cache_ready ? MAP_COLOR_GOOD : MAP_COLOR_WARN);
    map_render_status_row(panel, 144, "Location", map_location_status(snapshot),
                          snapshot->map_location_set ? MAP_COLOR_GOOD : MAP_COLOR_WARN);
    map_render_status_row(panel, 208, "Map view", map_view_value, map_view_color);

    lv_obj_t *built_in = map_label(panel, "OpenStreetMap is built in.", MAP_COLOR_MUTED);
    if (built_in) {
        map_label_dot(built_in, 420);
        lv_obj_set_style_text_align(built_in, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(built_in, 12, 274);
    }
    lv_obj_t *attribution = map_label(panel, "(c) OpenStreetMap contributors",
                                      MAP_COLOR_DETAIL);
    if (attribution) {
        map_label_dot(attribution, 420);
        lv_obj_set_style_text_align(attribution, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(attribution, 12, 298);
    }
}

void d1l_ui_map_render_options(lv_obj_t *parent,
                               const d1l_app_snapshot_t *snapshot,
                               d1l_ui_map_options_page_t page,
                               const d1l_ui_map_callbacks_t *callbacks)
{
    if (!parent || !snapshot || !callbacks) {
        return;
    }
    lv_obj_clean(parent);
    if (page == D1L_UI_MAP_OPTIONS_CACHE_STATUS) {
        map_render_cache_status(parent, snapshot, callbacks);
    } else {
        map_render_options_root(parent, snapshot, callbacks);
    }
}

static void map_format_coordinate(char *dest, size_t dest_len, int32_t value_e7)
{
    if (!dest || dest_len == 0) {
        return;
    }
    const int64_t value = value_e7;
    const bool negative = value < 0;
    const uint64_t magnitude = (uint64_t)(negative ? -value : value);
    snprintf(dest, dest_len, "%s%llu.%07llu", negative ? "-" : "",
             (unsigned long long)(magnitude / 10000000ULL),
             (unsigned long long)(magnitude % 10000000ULL));
}

static void map_configure_textarea(lv_obj_t *textarea, const char *placeholder,
                                   const char *text, uint32_t max_length)
{
    if (!textarea) {
        return;
    }
    lv_textarea_set_one_line(textarea, true);
    lv_textarea_set_max_length(textarea, max_length);
    lv_textarea_set_placeholder_text(textarea, placeholder);
    lv_textarea_set_text(textarea, text ? text : "");
    lv_obj_set_style_radius(textarea, 8, 0);
    lv_obj_set_style_bg_color(textarea, lv_color_hex(MAP_COLOR_BG), 0);
    lv_obj_set_style_border_color(textarea, lv_color_hex(MAP_COLOR_BORDER), 0);
    lv_obj_set_style_text_color(textarea, lv_color_hex(MAP_COLOR_TEXT), 0);
    lv_obj_set_style_text_color(textarea, lv_color_hex(MAP_COLOR_MUTED),
                                LV_PART_TEXTAREA_PLACEHOLDER);
}

void d1l_ui_map_render_location(lv_obj_t *parent,
                                const d1l_app_snapshot_t *snapshot,
                                int32_t latitude_e7,
                                int32_t longitude_e7,
                                const d1l_ui_map_callbacks_t *callbacks,
                                d1l_ui_map_controls_t *controls)
{
    if (!parent || !snapshot || !callbacks || !controls) {
        return;
    }
    memset(controls, 0, sizeof(*controls));
    lv_obj_clean(parent);
    map_render_header(parent, "Set location", "Map center in decimal degrees",
                      "Back", callbacks->close_location);

    char latitude[20] = "";
    char longitude[20] = "";
    if (snapshot->map_location_set) {
        map_format_coordinate(latitude, sizeof(latitude), latitude_e7);
        map_format_coordinate(longitude, sizeof(longitude), longitude_e7);
    }

    lv_obj_t *intro = map_panel(parent, 16, 64, 448, 50);
    if (intro) {
        lv_obj_t *hint = map_label(intro,
            "Set the center used for the local map area.", MAP_COLOR_MUTED);
        map_label_dot(hint, 420);
        if (hint) {
            lv_obj_set_pos(hint, 12, 16);
        }
    }
    lv_obj_t *latitude_label = map_label(parent, "Latitude", MAP_COLOR_GOOD);
    if (latitude_label) {
        lv_obj_set_pos(latitude_label, 28, 124);
    }
    controls->latitude_textarea = lv_textarea_create(parent);
    if (controls->latitude_textarea) {
        lv_obj_set_size(controls->latitude_textarea, 448, 48);
        lv_obj_set_pos(controls->latitude_textarea, 16, 146);
        map_configure_textarea(controls->latitude_textarea, "e.g. 43.6532000", latitude, 14);
        if (callbacks->location_textarea) {
            lv_obj_add_event_cb(controls->latitude_textarea, callbacks->location_textarea,
                                LV_EVENT_FOCUSED, NULL);
            lv_obj_add_event_cb(controls->latitude_textarea, callbacks->location_textarea,
                                LV_EVENT_CLICKED, NULL);
        }
    }
    lv_obj_t *longitude_label = map_label(parent, "Longitude", MAP_COLOR_GOOD);
    if (longitude_label) {
        lv_obj_set_pos(longitude_label, 28, 206);
    }
    controls->longitude_textarea = lv_textarea_create(parent);
    if (controls->longitude_textarea) {
        lv_obj_set_size(controls->longitude_textarea, 448, 48);
        lv_obj_set_pos(controls->longitude_textarea, 16, 228);
        map_configure_textarea(controls->longitude_textarea, "e.g. -79.3832000", longitude, 15);
        if (callbacks->location_textarea) {
            lv_obj_add_event_cb(controls->longitude_textarea, callbacks->location_textarea,
                                LV_EVENT_FOCUSED, NULL);
            lv_obj_add_event_cb(controls->longitude_textarea, callbacks->location_textarea,
                                LV_EVENT_CLICKED, NULL);
        }
    }
    lv_obj_t *keyboard_hint = map_panel(parent, 16, 288, 448, 54);
    if (keyboard_hint) {
        lv_obj_t *hint = map_label(keyboard_hint,
            "The keyboard opens only while editing a coordinate.", MAP_COLOR_MUTED);
        map_label_dot(hint, 420);
        if (hint) {
            lv_obj_set_pos(hint, 12, 18);
        }
    }
    const int save_width = snapshot->map_location_set ? 216 : 448;
    lv_obj_t *save = map_button(parent, "Save location", 16, 356, save_width, 52,
                                callbacks->save_location);
    map_style_primary_button(save);
    if (snapshot->map_location_set) {
        lv_obj_t *clear = map_button(parent, "Clear location", 248, 356, 216, 52,
                                     callbacks->clear_location);
        if (clear) {
            lv_obj_set_style_border_color(clear, lv_color_hex(MAP_COLOR_WARN), 0);
            lv_obj_set_style_border_width(clear, 1, 0);
        }
    }
    controls->location_keyboard = lv_keyboard_create(parent);
    if (controls->location_keyboard) {
        d1l_ui_keyboard_configure_input(controls->location_keyboard,
                                        controls->latitude_textarea,
                                        16, 274, 448, 150);
        lv_obj_add_flag(controls->location_keyboard, LV_OBJ_FLAG_HIDDEN);
        if (callbacks->location_keyboard) {
            lv_obj_add_event_cb(controls->location_keyboard,
                                callbacks->location_keyboard, LV_EVENT_READY, NULL);
            lv_obj_add_event_cb(controls->location_keyboard,
                                callbacks->location_keyboard, LV_EVENT_CANCEL, NULL);
        }
    }
}

void d1l_ui_map_viewport_release(void)
{
    uint32_t generation;
    portENTER_CRITICAL(&s_viewport_lease_lock);
    generation = s_viewport_generation;
    s_viewport_generation = 0U;
    s_viewport_revision = 0U;
    s_viewport_drag_active = false;
    s_viewport_drag_x = 0;
    s_viewport_drag_y = 0;
    portEXIT_CRITICAL(&s_viewport_lease_lock);
    if (generation != 0U) {
        d1l_map_view_service_release_visible(generation);
    }
}

void d1l_ui_map_viewport_set_suppressed(bool suppressed)
{
    portENTER_CRITICAL(&s_viewport_lease_lock);
    if (suppressed) {
        if (s_viewport_suppression_depth != UINT32_MAX) {
            ++s_viewport_suppression_depth;
        }
    } else if (s_viewport_suppression_depth > 0U) {
        --s_viewport_suppression_depth;
    }
    portEXIT_CRITICAL(&s_viewport_lease_lock);
    if (suppressed) {
        d1l_ui_map_viewport_release();
    }
}

void d1l_ui_map_viewport_suppress_next_acquire(void)
{
    portENTER_CRITICAL(&s_viewport_lease_lock);
    s_viewport_suppress_next_acquire = true;
    portEXIT_CRITICAL(&s_viewport_lease_lock);
    d1l_ui_map_viewport_release();
}

bool d1l_ui_map_viewport_lease_active(void)
{
    bool active;
    portENTER_CRITICAL(&s_viewport_lease_lock);
    active = s_viewport_generation != 0U && s_viewport_suppression_depth == 0U;
    portEXIT_CRITICAL(&s_viewport_lease_lock);
    return active;
}

uint32_t d1l_ui_map_viewport_generation(void)
{
    uint32_t generation;
    portENTER_CRITICAL(&s_viewport_lease_lock);
    generation = s_viewport_generation;
    portEXIT_CRITICAL(&s_viewport_lease_lock);
    return generation;
}
