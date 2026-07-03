#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "hal/rp2040_bridge.h"
#include "storage/storage_status.h"

#define D1L_MAP_TILE_CANARY_TOKEN_MAX 31U
#define D1L_MAP_TILE_ZOOM_MAX 18U
#define D1L_MAP_TILE_URL_TEMPLATE_MAX 192U
#define D1L_MAP_TILE_ATTRIBUTION_MAX 64U
#define D1L_MAP_TILE_DOWNLOAD_MAX_BYTES (128U * 1024U)
#define D1L_MAP_TILE_CACHE_POLICY "sd_offline_cache_when_ready"
#define D1L_MAP_TILE_CACHE_PATH_TEMPLATE "map/tiles/z{z}/x{x}/y{y}.tile"
#define D1L_MAP_TILE_DOWNLOAD_STATE "tile_render_pending"
#define D1L_MAP_TILE_DOWNLOAD_REQUIRES "Connected Wi-Fi, ready SD cache, saved location, allowed provider, attribution, and tile rendering proof"
#define D1L_MAP_TILE_PROVIDER_POLICY "provider_config_required_no_public_osm_bulk"
#define D1L_MAP_TILE_PROVIDER_ATTRIBUTION "Provider attribution required on Map and release notices"

typedef struct {
    char token[D1L_MAP_TILE_CANARY_TOKEN_MAX + 1U];
    char path[D1L_RP2040_FILE_PATH_MAX + 1U];
    char tmp_path[D1L_RP2040_FILE_PATH_MAX + 1U];
    char step[20];
    uint8_t z;
    uint32_t x;
    uint32_t y;
    size_t bytes;
    bool write_tmp;
    bool read_tmp;
    bool rename_replace;
    bool stat_final;
    bool read_final;
    bool public_rf_tx;
    bool formats_sd;
    esp_err_t last_error;
    d1l_rp2040_file_result_t file;
} d1l_map_tile_canary_result_t;

typedef struct {
    char url[D1L_MAP_TILE_URL_TEMPLATE_MAX + 32U];
    char attribution[D1L_MAP_TILE_ATTRIBUTION_MAX + 1U];
    char path[D1L_RP2040_FILE_PATH_MAX + 1U];
    char tmp_path[D1L_RP2040_FILE_PATH_MAX + 1U];
    char attribution_path[D1L_RP2040_FILE_PATH_MAX + 1U];
    char attribution_tmp_path[D1L_RP2040_FILE_PATH_MAX + 1U];
    char step[24];
    uint8_t z;
    uint32_t x;
    uint32_t y;
    size_t bytes;
    int status_code;
    bool provider_allowed;
    bool attribution_saved;
    bool wifi_connected;
    bool sd_ready;
    bool write_tmp;
    bool rename_replace;
    bool public_rf_tx;
    bool formats_sd;
    esp_err_t last_error;
    d1l_rp2040_file_result_t file;
} d1l_map_tile_download_result_t;

bool d1l_map_tile_store_token_valid(const char *token);
bool d1l_map_tile_attribution_valid(const char *attribution);
bool d1l_map_tile_store_sd_ready(const d1l_storage_status_t *status);
bool d1l_map_tile_provider_template_allowed(const char *url_template);
bool d1l_map_tile_store_coord_valid(uint8_t z, uint32_t x, uint32_t y);
bool d1l_map_tile_store_path(uint8_t z, uint32_t x, uint32_t y,
                             char *dest, size_t dest_size);
esp_err_t d1l_map_tile_store_download(const char *url_template,
                                      const char *attribution,
                                      uint8_t z,
                                      uint32_t x,
                                      uint32_t y,
                                      const d1l_storage_status_t *status,
                                      bool wifi_connected,
                                      d1l_map_tile_download_result_t *out_result);
esp_err_t d1l_map_tile_store_write_canary(const char *token,
                                          const d1l_storage_status_t *status,
                                          d1l_map_tile_canary_result_t *out_result);
esp_err_t d1l_map_tile_store_check_canary(const char *token,
                                          const d1l_storage_status_t *status,
                                          d1l_map_tile_canary_result_t *out_result);
