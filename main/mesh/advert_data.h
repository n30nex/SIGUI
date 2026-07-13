#ifndef D1L_ADVERT_DATA_H
#define D1L_ADVERT_DATA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define D1L_ADVERT_DATA_MAX_LEN 32U
#define D1L_ADVERT_DATA_NAME_LEN 24U

typedef struct {
    char type_code;
    char name[D1L_ADVERT_DATA_NAME_LEN];
    bool location_valid;
    int32_t lat_e6;
    int32_t lon_e6;
} d1l_advert_data_t;

bool d1l_advert_data_parse(const uint8_t *app_data, size_t app_data_len,
                           d1l_advert_data_t *out);

#ifdef __cplusplus
}
#endif

#endif
