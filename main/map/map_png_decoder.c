#include "map_png_decoder.h"

#include <stddef.h>
#include <stdint.h>

#include "esp_heap_caps.h"

/* Compile LVGL's bundled permissively licensed LodePNG decoder as a private,
 * decode-only instance. Its allocations are routed directly to PSRAM so the
 * background worker never calls LVGL object or memory APIs. */
#define LV_USE_PNG 1
#define LODEPNG_NO_COMPILE_ALLOCATORS 1
#define LODEPNG_NO_COMPILE_DISK 1
#define LODEPNG_NO_COMPILE_ENCODER 1
#define LODEPNG_NO_COMPILE_ANCILLARY_CHUNKS 1

void *lodepng_malloc(size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void *lodepng_realloc(void *ptr, size_t size)
{
    return heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void lodepng_free(void *ptr)
{
    heap_caps_free(ptr);
}

#include "../../third_party/sensecap_indicator_esp32/components/lvgl/src/extra/libs/png/lodepng.c"

esp_err_t d1l_map_png_decode_rgb565(const uint8_t *png,
                                    size_t png_len,
                                    uint16_t *out_pixels,
                                    size_t out_pixel_count)
{
    if (!png || png_len == 0U || !out_pixels ||
        out_pixel_count < D1L_MAP_DECODED_TILE_PIXELS) {
        return ESP_ERR_INVALID_ARG;
    }

    unsigned char *rgb = NULL;
    unsigned width = 0U;
    unsigned height = 0U;
    const unsigned error = lodepng_decode_memory(&rgb, &width, &height, png, png_len,
                                                  LCT_RGB, 8U);
    if (error != 0U || !rgb || width != D1L_MAP_DECODED_TILE_WIDTH ||
        height != D1L_MAP_DECODED_TILE_HEIGHT) {
        lodepng_free(rgb);
        return ESP_ERR_INVALID_RESPONSE;
    }

    for (size_t i = 0; i < D1L_MAP_DECODED_TILE_PIXELS; ++i) {
        const uint8_t red = rgb[(i * 3U) + 0U];
        const uint8_t green = rgb[(i * 3U) + 1U];
        const uint8_t blue = rgb[(i * 3U) + 2U];
        out_pixels[i] = (uint16_t)(((uint16_t)(red >> 3U) << 11U) |
                                   ((uint16_t)(green >> 2U) << 5U) |
                                   ((uint16_t)(blue >> 3U)));
    }
    lodepng_free(rgb);
    return ESP_OK;
}
