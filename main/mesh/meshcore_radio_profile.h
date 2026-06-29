#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const char *profile_id;
    const char *region_label;
    uint32_t frequency_hz;
    float bandwidth_khz;
    uint8_t spreading_factor;
    uint8_t coding_rate;
    int8_t tx_power_dbm;
    const char *tcxo;
    bool rx_boost;
} d1l_radio_profile_t;

const d1l_radio_profile_t *d1l_radio_profile_uscan_default(void);
bool d1l_radio_profile_is_safe_uscan(const d1l_radio_profile_t *profile);
