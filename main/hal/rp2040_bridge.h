#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define D1L_RP2040_FILE_PROTOCOL_VERSION 1U
#define D1L_RP2040_FILE_LINE_MAX 512U
#define D1L_RP2040_FILE_PATH_MAX 96U
#define D1L_RP2040_FILE_CHUNK_MAX 192U

typedef struct {
    bool uart_ready;
    esp_err_t init_result;
    uint32_t buffered_bytes;
    int uart_port;
    int tx_gpio;
    int rx_gpio;
    int baud_rate;
    uint8_t reset_expander_pin;
} d1l_rp2040_status_t;

typedef struct {
    bool bridge_ready;
    bool protocol_supported;
    bool atomic_rename_supported;
    bool response_truncated;
    bool sd_touched;
    uint32_t protocol_version;
    uint32_t file_line_max;
    uint32_t file_chunk_max;
    uint32_t path_max;
    esp_err_t last_error;
    char note[64];
} d1l_rp2040_ping_t;

typedef struct {
    bool bridge_ready;
    bool protocol_supported;
    bool card_present;
    bool filesystem_mounted;
    bool deskos_root_ready;
    bool needs_fat32;
    bool data_ready;
    bool file_ops_supported;
    bool atomic_rename_supported;
    bool response_truncated;
    uint32_t capacity_kb;
    uint32_t free_kb;
    uint32_t file_line_max;
    uint32_t file_chunk_max;
    uint32_t path_max;
    uint32_t probe_error;
    uint32_t probe_data;
    uint32_t mount_error;
    uint32_t mount_data;
    esp_err_t last_error;
    char state[32];
    char filesystem[16];
    char note[96];
    char probe_power[8];
    char probe_mode[16];
} d1l_rp2040_sd_status_t;

typedef struct {
    bool bridge_ready;
    bool protocol_supported;
    bool mount_selected;
    bool response_truncated;
    bool high_dedicated_present;
    bool high_shared_present;
    bool low_dedicated_present;
    bool low_shared_present;
    uint32_t spi_hz;
    uint32_t high_dedicated_error;
    uint32_t high_dedicated_data;
    uint32_t high_dedicated_capacity_kb;
    uint32_t high_shared_error;
    uint32_t high_shared_data;
    uint32_t high_shared_capacity_kb;
    uint32_t low_dedicated_error;
    uint32_t low_dedicated_data;
    uint32_t low_dedicated_capacity_kb;
    uint32_t low_shared_error;
    uint32_t low_shared_data;
    uint32_t low_shared_capacity_kb;
    esp_err_t last_error;
    char pins[40];
    char selected_power[8];
    char selected_mode[16];
    char note[64];
} d1l_rp2040_sd_diag_t;

typedef struct {
    bool bridge_ready;
    bool protocol_supported;
    bool ok;
    bool response_truncated;
    bool exists;
    bool is_directory;
    bool eof;
    uint16_t request_id;
    uint32_t size;
    uint32_t offset;
    uint32_t length;
    uint32_t crc32;
    esp_err_t last_error;
    char op[12];
    char err[24];
    char note[48];
} d1l_rp2040_file_result_t;

esp_err_t d1l_rp2040_bridge_init(void);
esp_err_t d1l_rp2040_bridge_status(d1l_rp2040_status_t *out_status);
esp_err_t d1l_rp2040_bridge_reset(uint32_t hold_ms, uint32_t settle_ms);
esp_err_t d1l_rp2040_bridge_ping(d1l_rp2040_ping_t *out_ping, uint32_t timeout_ms);
esp_err_t d1l_rp2040_bridge_probe_sd(d1l_rp2040_sd_status_t *out_status, uint32_t timeout_ms);
esp_err_t d1l_rp2040_bridge_mount_sd(d1l_rp2040_sd_status_t *out_status, uint32_t timeout_ms);
esp_err_t d1l_rp2040_bridge_sd_diag(d1l_rp2040_sd_diag_t *out_diag, uint32_t timeout_ms);
esp_err_t d1l_rp2040_bridge_file_stat(const char *path,
                                      d1l_rp2040_file_result_t *out_result,
                                      uint32_t timeout_ms);
esp_err_t d1l_rp2040_bridge_file_read(const char *path,
                                      uint32_t offset,
                                      uint8_t *out_data,
                                      size_t max_len,
                                      d1l_rp2040_file_result_t *out_result,
                                      uint32_t timeout_ms);
esp_err_t d1l_rp2040_bridge_file_write(const char *path,
                                       uint32_t offset,
                                       const uint8_t *data,
                                       size_t len,
                                       bool truncate,
                                       d1l_rp2040_file_result_t *out_result,
                                       uint32_t timeout_ms);
esp_err_t d1l_rp2040_bridge_file_append(const char *path,
                                        const uint8_t *data,
                                        size_t len,
                                        d1l_rp2040_file_result_t *out_result,
                                        uint32_t timeout_ms);
esp_err_t d1l_rp2040_bridge_file_delete(const char *path,
                                        d1l_rp2040_file_result_t *out_result,
                                        uint32_t timeout_ms);
esp_err_t d1l_rp2040_bridge_file_rename(const char *from_path,
                                        const char *to_path,
                                        bool replace,
                                        d1l_rp2040_file_result_t *out_result,
                                        uint32_t timeout_ms);
