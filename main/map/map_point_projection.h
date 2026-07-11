#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int32_t center_lat_e6;
    int32_t center_lon_e6;
    uint8_t zoom;
    uint16_t viewport_width;
    uint16_t viewport_height;
    /* Positive offsets match the on-screen map image moving right/down. */
    int32_t pan_x_pixels;
    int32_t pan_y_pixels;
} d1l_map_projection_view_t;

typedef struct {
    /* Always bounded to the viewport when projection succeeds. */
    int16_t screen_x;
    int16_t screen_y;
    /* True only when the unclipped marker center is inside the viewport. */
    bool visible;
    /* True when the point latitude was clamped to Web Mercator's limit. */
    bool latitude_clipped;
} d1l_map_projected_point_t;

/* Project signed E6 coordinates using the same Web Mercator pixel space as
 * d1l_map_math_plan_current_view(). Longitudes across the antimeridian use the
 * shortest wrapped distance from the current center. */
bool d1l_map_point_project_e6(const d1l_map_projection_view_t *view,
                              int32_t point_lat_e6,
                              int32_t point_lon_e6,
                              d1l_map_projected_point_t *out_point);
