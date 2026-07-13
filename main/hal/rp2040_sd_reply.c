#include "rp2040_sd_reply.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

static bool line_has_prefix(const char *line, const char *prefix)
{
    if (!line || !prefix || prefix[0] == '\0') {
        return false;
    }
    const size_t prefix_len = strlen(prefix);
    const size_t line_len = strlen(line);
    if (line_len <= prefix_len) {
        return false;
    }
    return strncmp(line, prefix, prefix_len) == 0 &&
           line[prefix_len] == ' ';
}

static bool token_value_span(const char *line, const char *key,
                             const char **out_value, size_t *out_len)
{
    if (!line || !key || !out_value || !out_len) {
        return false;
    }
    const size_t key_len = strlen(key);
    const char *p = line;
    while (*p) {
        while (*p == ' ') {
            ++p;
        }
        const char *token = p;
        while (*p && *p != ' ') {
            ++p;
        }
        const size_t token_len = (size_t)(p - token);
        if (token_len > key_len + 1U &&
            strncmp(token, key, key_len) == 0 &&
            token[key_len] == '=') {
            *out_value = token + key_len + 1U;
            *out_len = token_len - key_len - 1U;
            return true;
        }
    }
    return false;
}

static bool copy_token(const char *line, const char *key,
                       char *dst, size_t dst_size)
{
    const char *value = NULL;
    size_t value_len = 0U;
    if (!dst || dst_size == 0U ||
        !token_value_span(line, key, &value, &value_len) ||
        value_len == 0U || value_len >= dst_size) {
        return false;
    }
    memcpy(dst, value, value_len);
    dst[value_len] = '\0';
    return true;
}

static bool parse_bool_token(const char *line, const char *key, bool *out_value)
{
    const char *value = NULL;
    size_t value_len = 0U;
    if (!out_value || !token_value_span(line, key, &value, &value_len) ||
        value_len != 1U || (value[0] != '0' && value[0] != '1')) {
        return false;
    }
    *out_value = value[0] == '1';
    return true;
}

static bool parse_u32_token(const char *line, const char *key, uint32_t *out_value)
{
    const char *value = NULL;
    size_t value_len = 0U;
    if (!out_value || !token_value_span(line, key, &value, &value_len) ||
        value_len == 0U) {
        return false;
    }
    uint32_t parsed = 0U;
    for (size_t i = 0U; i < value_len; ++i) {
        if (value[i] < '0' || value[i] > '9') {
            return false;
        }
        const uint32_t digit = (uint32_t)(value[i] - '0');
        if (parsed > (UINT32_MAX - digit) / 10U) {
            return false;
        }
        parsed = parsed * 10U + digit;
    }
    *out_value = parsed;
    return true;
}

static bool state_is_supported(const char *state)
{
    static const char *const supported_states[] = {
        "mount_required",
        "mount_pending",
        "creating_deskos_files",
        "ready",
        "not_fat32_or_unmountable",
        "deskos_manifest_invalid",
        "error",
        "no_card",
    };

    if (!state) {
        return false;
    }
    for (size_t i = 0U;
         i < sizeof(supported_states) / sizeof(supported_states[0]); ++i) {
        if (strcmp(state, supported_states[i]) == 0) {
            return true;
        }
    }
    return false;
}

static bool reply_is_consistent(const d1l_rp2040_sd_reply_t *reply)
{
    if (!reply || !state_is_supported(reply->state) ||
        reply->free_kb > reply->capacity_kb ||
        reply->file_line_max == 0U || reply->file_chunk_max == 0U ||
        reply->path_max == 0U) {
        return false;
    }
    if ((reply->filesystem_mounted && !reply->card_present) ||
        (reply->deskos_root_ready && !reply->filesystem_mounted) ||
        (reply->needs_fat32 && !reply->card_present) ||
        (reply->file_ops_supported && !reply->deskos_root_ready) ||
        (reply->atomic_rename_supported && !reply->file_ops_supported)) {
        return false;
    }
    if (strcmp(reply->state, "no_card") == 0) {
        return !reply->card_present && !reply->filesystem_mounted &&
               !reply->deskos_root_ready && !reply->needs_fat32 &&
               !reply->file_ops_supported && !reply->atomic_rename_supported;
    }
    if (strcmp(reply->state, "ready") == 0) {
        return reply->card_present && reply->filesystem_mounted &&
               reply->deskos_root_ready && !reply->needs_fat32 &&
               strcmp(reply->filesystem, "fat32") == 0 &&
               reply->file_ops_supported && reply->atomic_rename_supported;
    }
    return !reply->deskos_root_ready && !reply->file_ops_supported &&
           !reply->atomic_rename_supported;
}

bool d1l_rp2040_sd_reply_parse(const char *line,
                               const char *expected_prefix,
                               d1l_rp2040_sd_reply_t *out_reply)
{
    if (!out_reply) {
        return false;
    }
    memset(out_reply, 0, sizeof(*out_reply));
    if (!line_has_prefix(line, expected_prefix) ||
        !copy_token(line, "state", out_reply->state, sizeof(out_reply->state)) ||
        !parse_bool_token(line, "present", &out_reply->card_present) ||
        !parse_bool_token(line, "mounted", &out_reply->filesystem_mounted) ||
        !parse_bool_token(line, "deskos", &out_reply->deskos_root_ready) ||
        !copy_token(line, "fs", out_reply->filesystem, sizeof(out_reply->filesystem)) ||
        !parse_bool_token(line, "needs_fat32", &out_reply->needs_fat32) ||
        !parse_u32_token(line, "capacity_kb", &out_reply->capacity_kb) ||
        !parse_u32_token(line, "free_kb", &out_reply->free_kb) ||
        !copy_token(line, "note", out_reply->note, sizeof(out_reply->note)) ||
        !copy_token(line, "probe_power", out_reply->probe_power,
                    sizeof(out_reply->probe_power)) ||
        !copy_token(line, "probe_mode", out_reply->probe_mode,
                    sizeof(out_reply->probe_mode)) ||
        !parse_u32_token(line, "probe_err", &out_reply->probe_error) ||
        !parse_u32_token(line, "probe_data", &out_reply->probe_data) ||
        !parse_u32_token(line, "mount_err", &out_reply->mount_error) ||
        !parse_u32_token(line, "mount_data", &out_reply->mount_data) ||
        !parse_bool_token(line, "file_ops", &out_reply->file_ops_supported) ||
        !parse_u32_token(line, "file_line_max", &out_reply->file_line_max) ||
        !parse_u32_token(line, "file_chunk_max", &out_reply->file_chunk_max) ||
        !parse_u32_token(line, "path_max", &out_reply->path_max) ||
        !parse_bool_token(line, "atomic_rename",
                          &out_reply->atomic_rename_supported)) {
        memset(out_reply, 0, sizeof(*out_reply));
        return false;
    }
    if (!reply_is_consistent(out_reply)) {
        memset(out_reply, 0, sizeof(*out_reply));
        return false;
    }
    return true;
}
