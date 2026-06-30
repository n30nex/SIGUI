#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define D1L_STORAGE_SD_MOUNT_POINT "/sdcard"
#define D1L_STORAGE_SD_DATA_ROOT "/sdcard/deskos"

typedef struct {
    bool initialized;
    bool direct_supported;
    bool rp2040_bridge_required;
    bool rp2040_bridge_ready;
    bool rp2040_sd_protocol_supported;
    bool sd_present;
    bool sd_mounted;
    bool sd_data_root_ready;
    bool format_required;
    bool format_supported;
    bool setup_required;
    bool setup_supported;
    bool data_enabled;
    bool file_ops_supported;
    bool atomic_rename_supported;
    bool response_truncated;
    uint32_t capacity_kb;
    uint32_t free_kb;
    uint32_t file_line_max;
    uint32_t file_chunk_max;
    uint32_t path_max;
    esp_err_t last_error;
    const char *sd_state;
    const char *sd_interface;
    const char *sd_filesystem;
    const char *mount_point;
    const char *data_root;
    const char *data_backend;
    const char *message_store_backend;
    const char *dm_store_backend;
    const char *packet_log_backend;
    const char *route_store_backend;
    const char *map_tile_backend;
    const char *export_backend;
    const char *setup_action;
    const char *format_action;
    const char *note;
} d1l_storage_status_t;

esp_err_t d1l_storage_status_init(void);
void d1l_storage_status_note_rp2040(esp_err_t rp2040_init_result);
esp_err_t d1l_storage_status_refresh(uint32_t timeout_ms);
esp_err_t d1l_storage_format_sd_confirmed(const char *confirmation, uint32_t timeout_ms);
void d1l_storage_status(d1l_storage_status_t *out_status);
