#include "map_point_projection.h"

#include <math.h>
#include <string.h>

#include "map_math.h"

#define D1L_MAP_MERCATOR_MAX_LAT_E7 850511288LL
#define D1L_MAP_LONGITUDE_MAX_E6 180000000LL
#define D1L_MAP_E6_TO_E7 10LL
#define D1L_MAP_E7_SCALE 10000000.0
#define D1L_MAP_PI 3.14159265358979323846

static int64_t clamp_i64(int64_t value, int64_t low, int64_t high)
{
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

static bool longitude_e6_valid(int32_t longitude_e6)
{
    return longitude_e6 >= -D1L_MAP_LONGITUDE_MAX_E6 &&
           longitude_e6 <= D1L_MAP_LONGITUDE_MAX_E6;
}

static bool view_valid(const d1l_map_projection_view_t *view)
{
    return view &&
           view->zoom >= D1L_MAP_VIEW_MIN_ZOOM &&
           view->zoom <= D1L_MAP_VIEW_MAX_ZOOM &&
           view->viewport_width > 0U &&
           view->viewport_width <= D1L_MAP_VIEW_MAX_WIDTH &&
           view->viewport_height > 0U &&
           view->viewport_height <= D1L_MAP_VIEW_MAX_HEIGHT &&
           longitude_e6_valid(view->center_lon_e6);
}

static double world_x_from_lon_e6(int32_t longitude_e6, double world_size)
{
    const double longitude = (double)longitude_e6 / 1000000.0;
    return ((longitude + 180.0) / 360.0) * world_size;
}

static double world_y_from_lat_e7(int64_t latitude_e7, double world_size)
{
    const double latitude = (double)latitude_e7 / D1L_MAP_E7_SCALE;
    const double latitude_rad = latitude * D1L_MAP_PI / 180.0;
    return (1.0 -
            log(tan(latitude_rad) + (1.0 / cos(latitude_rad))) / D1L_MAP_PI) *
           0.5 * world_size;
}

static int16_t bounded_screen_coordinate(double coordinate, uint16_t extent)
{
    const int16_t maximum = (int16_t)(extent - 1U);
    if (coordinate <= 0.0) {
        return 0;
    }
    if (coordinate >= (double)maximum) {
        return maximum;
    }
    return (int16_t)lround(coordinate);
}

bool d1l_map_point_project_e6(const d1l_map_projection_view_t *view,
                              int32_t point_lat_e6,
                              int32_t point_lon_e6,
                              d1l_map_projected_point_t *out_point)
{
    if (!out_point) {
        return false;
    }
    memset(out_point, 0, sizeof(*out_point));
    if (!view_valid(view) || !longitude_e6_valid(point_lon_e6)) {
        return false;
    }

    const int64_t center_lat_e7_unclipped =
        (int64_t)view->center_lat_e6 * D1L_MAP_E6_TO_E7;
    const int64_t point_lat_e7_unclipped =
        (int64_t)point_lat_e6 * D1L_MAP_E6_TO_E7;
    const int64_t center_lat_e7 = clamp_i64(
        center_lat_e7_unclipped,
        -D1L_MAP_MERCATOR_MAX_LAT_E7,
        D1L_MAP_MERCATOR_MAX_LAT_E7);
    const int64_t point_lat_e7 = clamp_i64(
        point_lat_e7_unclipped,
        -D1L_MAP_MERCATOR_MAX_LAT_E7,
        D1L_MAP_MERCATOR_MAX_LAT_E7);
    const double world_size =
        (double)(1UL << view->zoom) * (double)D1L_MAP_TILE_PIXEL_SIZE;
    const double center_world_x =
        world_x_from_lon_e6(view->center_lon_e6, world_size);
    const double point_world_x = world_x_from_lon_e6(point_lon_e6, world_size);
    double delta_x = point_world_x - center_world_x;
    const double half_world = world_size * 0.5;
    if (delta_x > half_world) {
        delta_x -= world_size;
    } else if (delta_x < -half_world) {
        delta_x += world_size;
    }

    const double center_world_y = world_y_from_lat_e7(center_lat_e7, world_size);
    const double point_world_y = world_y_from_lat_e7(point_lat_e7, world_size);
    const double screen_x = ((double)view->viewport_width * 0.5) + delta_x +
                            (double)view->pan_x_pixels;
    const double screen_y = ((double)view->viewport_height * 0.5) +
                            (point_world_y - center_world_y) +
                            (double)view->pan_y_pixels;
    out_point->screen_x =
        bounded_screen_coordinate(screen_x, view->viewport_width);
    out_point->screen_y =
        bounded_screen_coordinate(screen_y, view->viewport_height);
    out_point->visible = screen_x >= 0.0 &&
                         screen_x < (double)view->viewport_width &&
                         screen_y >= 0.0 &&
                         screen_y < (double)view->viewport_height;
    out_point->latitude_clipped = point_lat_e7 != point_lat_e7_unclipped;
    return true;
}
