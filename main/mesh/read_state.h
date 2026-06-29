#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint32_t last_public_read_seq;
    uint32_t last_dm_read_seq;
    uint32_t newest_public_rx_seq;
    uint32_t newest_dm_rx_seq;
    uint32_t public_unread_count;
    uint32_t dm_unread_count;
    uint32_t muted_dm_unread_count;
    uint32_t mark_read_count;
} d1l_read_state_stats_t;

esp_err_t d1l_read_state_init(void);
esp_err_t d1l_read_state_clear(void);
esp_err_t d1l_read_state_mark_public_read(void);
esp_err_t d1l_read_state_mark_dm_read(void);
esp_err_t d1l_read_state_mark_all_read(void);
d1l_read_state_stats_t d1l_read_state_stats(void);
