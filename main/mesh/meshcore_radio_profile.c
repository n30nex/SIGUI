#include "meshcore_radio_profile.h"

#include <string.h>

#include "d1l_config.h"

static const d1l_radio_profile_t s_uscan = {
    .profile_id = D1L_RADIO_PROFILE_ID,
    .region_label = D1L_RADIO_REGION_LABEL,
    .frequency_hz = D1L_RADIO_FREQ_HZ,
    .bandwidth_khz = D1L_RADIO_BW_KHZ,
    .spreading_factor = D1L_RADIO_SF,
    .coding_rate = D1L_RADIO_CR,
    .tx_power_dbm = D1L_RADIO_TX_POWER_DBM,
    .tcxo = D1L_RADIO_TCXO,
    .rx_boost = true,
};

const d1l_radio_profile_t *d1l_radio_profile_uscan_default(void)
{
    return &s_uscan;
}

bool d1l_radio_profile_is_safe_uscan(const d1l_radio_profile_t *profile)
{
    if (!profile) {
        return false;
    }
    return profile->frequency_hz == D1L_RADIO_FREQ_HZ &&
           profile->spreading_factor == D1L_RADIO_SF &&
           profile->coding_rate == D1L_RADIO_CR &&
           profile->tx_power_dbm <= D1L_RADIO_TX_POWER_DBM &&
           strcmp(profile->tcxo, "NONE") == 0;
}
