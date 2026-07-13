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

static uint8_t clamp_u8(int32_t value)
{
    if (value < 0) {
        return 0U;
    }
    if (value > 255) {
        return 255U;
    }
    return (uint8_t)value;
}

static int32_t dark_style_chroma_adjust(int32_t delta)
{
    const int32_t scaled = delta * 3;
    if (scaled > 0) {
        return (scaled + 4) / 8;
    }
    if (scaled < 0) {
        return (scaled - 4) / 8;
    }
    return 0;
}

static uint16_t dark_style_rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
    /* OSM Standard remains the cached/source image. Invert only luminance,
     * then retain a small amount of source chroma.
     * Pale land becomes dark, while black road labels and boundaries become
     * bright enough to read beneath high-contrast node markers. */
    const uint32_t luminance =
        ((uint32_t)77U * red + (uint32_t)150U * green +
         (uint32_t)29U * blue + 128U) >> 8U;
    const int32_t dark_luminance =
        14 + (int32_t)(((255U - luminance) * 207U + 128U) >> 8U);
    const uint8_t styled_red = clamp_u8(
        dark_luminance +
        dark_style_chroma_adjust((int32_t)red - (int32_t)luminance));
    const uint8_t styled_green = clamp_u8(
        dark_luminance +
        dark_style_chroma_adjust((int32_t)green - (int32_t)luminance));
    const uint8_t styled_blue = clamp_u8(
        dark_luminance +
        dark_style_chroma_adjust((int32_t)blue - (int32_t)luminance));
    return (uint16_t)(((uint16_t)(styled_red >> 3U) << 11U) |
                      ((uint16_t)(styled_green >> 2U) << 5U) |
                      ((uint16_t)(styled_blue >> 3U)));
}

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
        out_pixels[i] = dark_style_rgb565(red, green, blue);
    }
    lodepng_free(rgb);
    return ESP_OK;
}
