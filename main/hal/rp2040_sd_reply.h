#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool card_present;
    bool filesystem_mounted;
    bool deskos_root_ready;
    bool needs_fat32;
    bool file_ops_supported;
    bool atomic_rename_supported;
    uint32_t capacity_kb;
    uint32_t free_kb;
    uint32_t file_line_max;
    uint32_t file_chunk_max;
    uint32_t path_max;
    uint32_t probe_error;
    uint32_t probe_data;
    uint32_t mount_error;
    uint32_t mount_data;
    char state[32];
    char filesystem[16];
    char note[96];
    char probe_power[8];
    char probe_mode[16];
} d1l_rp2040_sd_reply_t;

/* Parses the mandatory SD STATUS/MOUNT reply fields and rejects incomplete or
 * internally inconsistent replies. This is deliberately platform-independent
 * so the exact production parser policy can run in host tests. */
bool d1l_rp2040_sd_reply_parse(const char *line,
                               const char *expected_prefix,
                               d1l_rp2040_sd_reply_t *out_reply);
