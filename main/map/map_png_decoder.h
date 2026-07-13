#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define D1L_MAP_DECODED_TILE_WIDTH 256U
#define D1L_MAP_DECODED_TILE_HEIGHT 256U
#define D1L_MAP_DECODED_TILE_PIXELS \
    (D1L_MAP_DECODED_TILE_WIDTH * D1L_MAP_DECODED_TILE_HEIGHT)
#define D1L_MAP_RENDER_STYLE_ID "local-dark-v1"

esp_err_t d1l_map_png_decode_rgb565(const uint8_t *png,
                                    size_t png_len,
                                    uint16_t *out_pixels,
                                    size_t out_pixel_count);
