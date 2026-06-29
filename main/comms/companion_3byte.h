#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define D1L_COMPANION3_HEADER_SIZE 3U
#define D1L_COMPANION3_MAX_FRAME_SIZE 512U
#define D1L_COMPANION3_APP_TO_RADIO '<'
#define D1L_COMPANION3_RADIO_TO_APP '>'

typedef enum {
    D1L_COMPANION3_WAITING = 0,
    D1L_COMPANION3_FRAME_COMPLETE,
    D1L_COMPANION3_FRAME_DROPPED,
} d1l_companion3_feed_result_t;

typedef struct {
    uint8_t state;
    uint8_t frame_type;
    uint16_t frame_len;
    uint16_t rx_len;
    bool dropping;
} d1l_companion3_decoder_t;

bool d1l_companion3_is_frame_type(uint8_t frame_type);
esp_err_t d1l_companion3_encode(uint8_t frame_type, const uint8_t *payload, uint16_t payload_len,
                                uint8_t *out, size_t out_cap, size_t *out_len);
void d1l_companion3_decoder_init(d1l_companion3_decoder_t *decoder);
esp_err_t d1l_companion3_decoder_feed(d1l_companion3_decoder_t *decoder, uint8_t byte,
                                      uint8_t *dest, size_t dest_cap, uint8_t *frame_type,
                                      uint16_t *frame_len, d1l_companion3_feed_result_t *result);
