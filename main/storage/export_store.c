#include "export_store.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool result_path_set(d1l_export_canary_result_t *result,
                            const char *token)
{
    if (!result || !token || token[0] == '\0') {
        return false;
    }
    const int final_len = snprintf(result->path, sizeof(result->path),
                                   "exports/diagnostics/export-canary-%s.json",
                                   token);
    const int tmp_len = snprintf(result->tmp_path, sizeof(result->tmp_path),
                                 "exports/diagnostics/export-canary-%s.tmp",
                                 token);
    return final_len > 0 && tmp_len > 0 &&
           (size_t)final_len < sizeof(result->path) &&
           (size_t)tmp_len < sizeof(result->tmp_path);
}

static void result_step(d1l_export_canary_result_t *result,
                        const char *step,
                        esp_err_t ret,
                        const d1l_rp2040_file_result_t *file)
{
    if (!result) {
        return;
    }
    snprintf(result->step, sizeof(result->step), "%s", step ? step : "unknown");
    result->last_error = ret;
    if (file) {
        result->file = *file;
    }
}

bool d1l_export_store_token_valid(const char *token)
{
    if (!token || token[0] == '\0') {
        return false;
    }
    size_t len = 0;
    while (token[len] != '\0') {
        const unsigned char ch = (unsigned char)token[len];
        if (!(isalnum(ch) || ch == '-' || ch == '_' || ch == '.')) {
            return false;
        }
        len++;
        if (len > D1L_EXPORT_CANARY_TOKEN_MAX) {
            return false;
        }
    }
    return true;
}

bool d1l_export_store_sd_ready(const d1l_storage_status_t *status)
{
    return status &&
           status->sd_present &&
           status->sd_mounted &&
           status->sd_data_root_ready &&
           status->rp2040_sd_protocol_supported &&
           status->file_ops_supported &&
           status->atomic_rename_supported &&
           status->file_line_max >= D1L_RP2040_FILE_LINE_MAX &&
           status->file_chunk_max >= D1L_RP2040_FILE_CHUNK_MAX &&
           status->path_max >= D1L_RP2040_FILE_PATH_MAX;
}

esp_err_t d1l_export_store_write_canary(const char *token,
                                        const d1l_storage_status_t *status,
                                        d1l_export_canary_result_t *out_result)
{
    if (!token || !out_result) {
        return ESP_ERR_INVALID_ARG;
    }

    d1l_export_canary_result_t result = {
        .public_rf_tx = false,
        .formats_sd = false,
        .last_error = ESP_OK,
    };
    if (!d1l_export_store_token_valid(token)) {
        result_step(&result, "token", ESP_ERR_INVALID_ARG, NULL);
        *out_result = result;
        return ESP_ERR_INVALID_ARG;
    }
    snprintf(result.token, sizeof(result.token), "%s", token);
    if (!result_path_set(&result, token)) {
        result_step(&result, "path", ESP_ERR_INVALID_SIZE, NULL);
        *out_result = result;
        return ESP_ERR_INVALID_SIZE;
    }

    if (!d1l_export_store_sd_ready(status)) {
        result_step(&result, "preflight", ESP_ERR_NOT_SUPPORTED, NULL);
        *out_result = result;
        return ESP_ERR_NOT_SUPPORTED;
    }

    uint8_t payload[D1L_RP2040_FILE_CHUNK_MAX];
    const int payload_len = snprintf((char *)payload, sizeof(payload),
                                     "{\"schema\":1,\"kind\":\"diagnostic_export_canary\","
                                     "\"token\":\"%s\",\"public_rf_tx\":false,"
                                     "\"formats_sd\":false}\n",
                                     token);
    if (payload_len <= 0 || (size_t)payload_len >= sizeof(payload)) {
        result_step(&result, "payload", ESP_ERR_INVALID_SIZE, NULL);
        *out_result = result;
        return ESP_ERR_INVALID_SIZE;
    }
    result.bytes = (size_t)payload_len;

    d1l_rp2040_file_result_t file = {0};
    esp_err_t ret = d1l_rp2040_bridge_file_delete(result.tmp_path, &file, 3000U);
    if (ret != ESP_OK && ret != ESP_ERR_NOT_FOUND && strcmp(file.err, "not_found") != 0) {
        result_step(&result, "cleanup_tmp", ret, &file);
        *out_result = result;
        return ret;
    }

    ret = d1l_rp2040_bridge_file_write(result.tmp_path, 0U, payload, result.bytes, true,
                                       &file, 3000U);
    if (ret != ESP_OK || file.length != result.bytes || file.size != result.bytes) {
        result_step(&result, "write_tmp", ret == ESP_OK ? ESP_FAIL : ret, &file);
        *out_result = result;
        return result.last_error;
    }
    result.write_tmp = true;

    uint8_t read_buf[D1L_RP2040_FILE_CHUNK_MAX] = {0};
    ret = d1l_rp2040_bridge_file_read(result.tmp_path, 0U, read_buf, result.bytes,
                                      &file, 3000U);
    if (ret != ESP_OK || file.length != result.bytes ||
        memcmp(read_buf, payload, result.bytes) != 0) {
        result_step(&result, "read_tmp", ret == ESP_OK ? ESP_FAIL : ret, &file);
        *out_result = result;
        return result.last_error;
    }
    result.read_tmp = true;

    ret = d1l_rp2040_bridge_file_rename(result.tmp_path, result.path, true,
                                        &file, 3000U);
    if (ret != ESP_OK) {
        result_step(&result, "rename_final", ret, &file);
        *out_result = result;
        return ret;
    }
    result.rename_replace = true;

    ret = d1l_rp2040_bridge_file_stat(result.path, &file, 3000U);
    if (ret != ESP_OK || !file.exists || file.is_directory || file.size != result.bytes) {
        result_step(&result, "stat_final", ret == ESP_OK ? ESP_FAIL : ret, &file);
        *out_result = result;
        return result.last_error;
    }
    result.stat_final = true;

    memset(read_buf, 0, sizeof(read_buf));
    ret = d1l_rp2040_bridge_file_read(result.path, 0U, read_buf, result.bytes,
                                      &file, 3000U);
    if (ret != ESP_OK || file.length != result.bytes ||
        memcmp(read_buf, payload, result.bytes) != 0) {
        result_step(&result, "read_final", ret == ESP_OK ? ESP_FAIL : ret, &file);
        *out_result = result;
        return result.last_error;
    }
    result.read_final = true;
    result_step(&result, "ok", ESP_OK, &file);
    *out_result = result;
    return ESP_OK;
}
