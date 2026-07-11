#include "map_math.h"

#include <math.h>
#include <string.h>

#include "storage/map_tile_store.h"

#define D1L_MAP_MERCATOR_MAX_LAT_E7 850511288L
#define D1L_MAP_PI 3.14159265358979323846

typedef struct {
    d1l_map_tile_placement_t tile;
    double distance_sq;
} planned_tile_t;

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

static uint32_t wrap_tile_x(int64_t x, uint32_t tile_count)
{
    int64_t wrapped = x % (int64_t)tile_count;
    if (wrapped < 0) {
        wrapped += tile_count;
    }
    return (uint32_t)wrapped;
}

static bool already_planned(const planned_tile_t *tiles,
                            size_t count,
                            uint32_t x,
                            uint32_t y)
{
    for (size_t i = 0; i < count; ++i) {
        if (tiles[i].tile.x == x && tiles[i].tile.y == y) {
            return true;
        }
    }
    return false;
}

static void sort_center_first(planned_tile_t *tiles, size_t count)
{
    for (size_t i = 1; i < count; ++i) {
        planned_tile_t value = tiles[i];
        size_t j = i;
        while (j > 0 && tiles[j - 1U].distance_sq > value.distance_sq) {
            tiles[j] = tiles[j - 1U];
            --j;
        }
        tiles[j] = value;
    }
}

bool d1l_map_math_plan_current_view(int32_t lat_e7,
                                    int32_t lon_e7,
                                    uint8_t zoom,
                                    uint16_t width,
                                    uint16_t height,
                                    d1l_map_tile_plan_t *out_plan)
{
    if (!out_plan || zoom > D1L_MAP_TILE_ZOOM_MAX || width == 0U || height == 0U ||
        width > D1L_MAP_VIEW_MAX_WIDTH || height > D1L_MAP_VIEW_MAX_HEIGHT ||
        lon_e7 < -1800000000LL || lon_e7 > 1800000000LL) {
        return false;
    }

    memset(out_plan, 0, sizeof(*out_plan));
    const int64_t clamped_lat_e7 =
        clamp_i64(lat_e7, -D1L_MAP_MERCATOR_MAX_LAT_E7, D1L_MAP_MERCATOR_MAX_LAT_E7);
    const double latitude = (double)clamped_lat_e7 / 10000000.0;
    const double longitude = (double)lon_e7 / 10000000.0;
    const double latitude_rad = latitude * D1L_MAP_PI / 180.0;
    const uint32_t tile_count = 1UL << zoom;
    const double center_tile_x = ((longitude + 180.0) / 360.0) * (double)tile_count;
    const double center_tile_y =
        (1.0 - log(tan(latitude_rad) + (1.0 / cos(latitude_rad))) / D1L_MAP_PI) *
        0.5 * (double)tile_count;
    const double center_world_x = center_tile_x * D1L_MAP_TILE_PIXEL_SIZE;
    const double center_world_y = center_tile_y * D1L_MAP_TILE_PIXEL_SIZE;
    const double left = center_world_x - ((double)width / 2.0);
    const double top = center_world_y - ((double)height / 2.0);
    const int64_t min_unwrapped_x = (int64_t)floor(left / D1L_MAP_TILE_PIXEL_SIZE);
    const int64_t max_unwrapped_x =
        (int64_t)floor((left + (double)width - 1.0) / D1L_MAP_TILE_PIXEL_SIZE);
    const int64_t min_y = (int64_t)floor(top / D1L_MAP_TILE_PIXEL_SIZE);
    const int64_t max_y =
        (int64_t)floor((top + (double)height - 1.0) / D1L_MAP_TILE_PIXEL_SIZE);

    if ((max_unwrapped_x - min_unwrapped_x + 1LL) > 3LL ||
        (max_y - min_y + 1LL) > 3LL) {
        return false;
    }

    planned_tile_t planned[D1L_MAP_VIEW_MAX_TILES] = {0};
    size_t planned_count = 0;
    for (int64_t raw_y = min_y; raw_y <= max_y; ++raw_y) {
        if (raw_y < 0 || raw_y >= (int64_t)tile_count) {
            continue;
        }
        const uint32_t y = (uint32_t)raw_y;
        for (int64_t raw_x = min_unwrapped_x; raw_x <= max_unwrapped_x; ++raw_x) {
            const uint32_t x = wrap_tile_x(raw_x, tile_count);
            if (already_planned(planned, planned_count, x, y)) {
                continue;
            }
            if (planned_count >= D1L_MAP_VIEW_MAX_TILES) {
                return false;
            }

            const double tile_center_x = ((double)raw_x + 0.5) * D1L_MAP_TILE_PIXEL_SIZE;
            const double tile_center_y = ((double)raw_y + 0.5) * D1L_MAP_TILE_PIXEL_SIZE;
            const double dx = tile_center_x - center_world_x;
            const double dy = tile_center_y - center_world_y;
            planned[planned_count].tile.zoom = zoom;
            planned[planned_count].tile.x = x;
            planned[planned_count].tile.y = y;
            planned[planned_count].tile.screen_x =
                (int16_t)lround(((double)raw_x * D1L_MAP_TILE_PIXEL_SIZE) - left);
            planned[planned_count].tile.screen_y =
                (int16_t)lround(((double)raw_y * D1L_MAP_TILE_PIXEL_SIZE) - top);
            planned[planned_count].distance_sq = (dx * dx) + (dy * dy);
            ++planned_count;
        }
    }

    sort_center_first(planned, planned_count);
    out_plan->lat_e7 = (int32_t)clamped_lat_e7;
    out_plan->lon_e7 = lon_e7;
    out_plan->zoom = zoom;
    out_plan->width = width;
    out_plan->height = height;
    out_plan->count = (uint8_t)planned_count;
    for (size_t i = 0; i < planned_count; ++i) {
        out_plan->tiles[i] = planned[i].tile;
    }
    return planned_count > 0U;
}
