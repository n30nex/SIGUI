#include "companion_3byte.h"

#include <string.h>

enum {
    DECODER_IDLE = 0,
    DECODER_LEN_LSB,
    DECODER_LEN_MSB,
    DECODER_PAYLOAD,
};

bool d1l_companion3_is_frame_type(uint8_t frame_type)
{
    return frame_type == D1L_COMPANION3_APP_TO_RADIO || frame_type == D1L_COMPANION3_RADIO_TO_APP;
}

esp_err_t d1l_companion3_encode(uint8_t frame_type, const uint8_t *payload, uint16_t payload_len,
                                uint8_t *out, size_t out_cap, size_t *out_len)
{
    if (!d1l_companion3_is_frame_type(frame_type) || out == NULL || out_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (payload_len > 0 && payload == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (payload_len > D1L_COMPANION3_MAX_FRAME_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (out_cap < D1L_COMPANION3_HEADER_SIZE + payload_len) {
        return ESP_ERR_NO_MEM;
    }

    out[0] = frame_type;
    out[1] = (uint8_t)(payload_len & 0xFFU);
    out[2] = (uint8_t)(payload_len >> 8);
    if (payload_len > 0) {
        memcpy(&out[D1L_COMPANION3_HEADER_SIZE], payload, payload_len);
    }
    *out_len = D1L_COMPANION3_HEADER_SIZE + payload_len;
    return ESP_OK;
}

void d1l_companion3_decoder_init(d1l_companion3_decoder_t *decoder)
{
    if (decoder == NULL) {
        return;
    }
    decoder->state = DECODER_IDLE;
    decoder->frame_type = 0;
    decoder->frame_len = 0;
    decoder->rx_len = 0;
    decoder->dropping = false;
}

esp_err_t d1l_companion3_decoder_feed(d1l_companion3_decoder_t *decoder, uint8_t byte,
                                      uint8_t *dest, size_t dest_cap, uint8_t *frame_type,
                                      uint16_t *frame_len, d1l_companion3_feed_result_t *result)
{
    if (decoder == NULL || result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *result = D1L_COMPANION3_WAITING;

    switch (decoder->state) {
    case DECODER_IDLE:
        if (d1l_companion3_is_frame_type(byte)) {
            decoder->frame_type = byte;
            decoder->frame_len = 0;
            decoder->rx_len = 0;
            decoder->dropping = false;
            decoder->state = DECODER_LEN_LSB;
        }
        break;
    case DECODER_LEN_LSB:
        decoder->frame_len = byte;
        decoder->state = DECODER_LEN_MSB;
        break;
    case DECODER_LEN_MSB:
        decoder->frame_len |= ((uint16_t)byte) << 8;
        decoder->rx_len = 0;
        decoder->dropping = decoder->frame_len > D1L_COMPANION3_MAX_FRAME_SIZE ||
                            decoder->frame_len > dest_cap;
        if (decoder->frame_len == 0) {
            if (frame_type != NULL) {
                *frame_type = decoder->frame_type;
            }
            if (frame_len != NULL) {
                *frame_len = 0;
            }
            d1l_companion3_decoder_init(decoder);
            *result = D1L_COMPANION3_FRAME_COMPLETE;
            return ESP_OK;
        }
        decoder->state = DECODER_PAYLOAD;
        if (decoder->dropping) {
            return ESP_ERR_INVALID_SIZE;
        }
        break;
    case DECODER_PAYLOAD:
        if (!decoder->dropping && dest != NULL && decoder->rx_len < dest_cap) {
            dest[decoder->rx_len] = byte;
        }
        decoder->rx_len++;
        if (decoder->rx_len >= decoder->frame_len) {
            const bool dropped = decoder->dropping;
            const uint8_t completed_type = decoder->frame_type;
            const uint16_t completed_len = decoder->frame_len;
            d1l_companion3_decoder_init(decoder);
            if (dropped) {
                *result = D1L_COMPANION3_FRAME_DROPPED;
                return ESP_ERR_INVALID_SIZE;
            }
            if (frame_type != NULL) {
                *frame_type = completed_type;
            }
            if (frame_len != NULL) {
                *frame_len = completed_len;
            }
            *result = D1L_COMPANION3_FRAME_COMPLETE;
        }
        break;
    default:
        d1l_companion3_decoder_init(decoder);
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}
