#include "map_tile_store.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"

#define D1L_MAP_TILE_CANARY_Z 12U
#define D1L_MAP_TILE_CANARY_X 1U
#define D1L_MAP_TILE_CANARY_Y 2U
#define D1L_MAP_TILE_HTTP_TIMEOUT_MS 15000
#define D1L_MAP_TILE_USER_AGENT "MeshCore-DeskOS-D1L/1.0 offline-tile-cache"
#define D1L_MAP_TILE_ATTRIBUTION_PATH "map/tiles/attribution.json"
#define D1L_MAP_TILE_ATTRIBUTION_TMP_PATH "map/tiles/attribution.tmp"

bool d1l_map_tile_store_coord_valid(uint8_t z, uint32_t x, uint32_t y)
{
    if (z > D1L_MAP_TILE_ZOOM_MAX) {
        return false;
    }
    const uint32_t limit = 1UL << z;
    return x < limit && y < limit;
}

static bool contains_case_insensitive(const char *text, const char *needle)
{
    if (!text || !needle || needle[0] == '\0') {
        return false;
    }
    const size_t needle_len = strlen(needle);
    for (size_t i = 0; text[i] != '\0'; ++i) {
        size_t matched = 0;
        while (matched < needle_len && text[i + matched] != '\0' &&
               tolower((unsigned char)text[i + matched]) ==
                   tolower((unsigned char)needle[matched])) {
            matched++;
        }
        if (matched == needle_len) {
            return true;
        }
    }
    return false;
}

bool d1l_map_tile_provider_template_allowed(const char *url_template)
{
    if (!url_template || url_template[0] == '\0') {
        return false;
    }
    if (strncmp(url_template, "https://", strlen("https://")) != 0) {
        return false;
    }
    if (!strstr(url_template, "{z}") ||
        !strstr(url_template, "{x}") ||
        !strstr(url_template, "{y}")) {
        return false;
    }
    if (contains_case_insensitive(url_template, "tile.openstreetmap.org") ||
        contains_case_insensitive(url_template, "openstreetmap.org") ||
        contains_case_insensitive(url_template, "osm.org")) {
        return false;
    }
    return true;
}

bool d1l_map_tile_attribution_valid(const char *attribution)
{
    if (!attribution || attribution[0] == '\0') {
        return false;
    }
    size_t len = 0;
    while (attribution[len] != '\0') {
        const unsigned char ch = (unsigned char)attribution[len];
        if (ch < 32 || ch > 126 || ch == '"' || ch == '\\') {
            return false;
        }
        len++;
        if (len > D1L_MAP_TILE_ATTRIBUTION_MAX) {
            return false;
        }
    }
    return true;
}

bool d1l_map_tile_store_path(uint8_t z, uint32_t x, uint32_t y,
                             char *dest, size_t dest_size)
{
    if (!dest || dest_size == 0 || !d1l_map_tile_store_coord_valid(z, x, y)) {
        return false;
    }
    const int len = snprintf(dest, dest_size, "map/tiles/z%u/x%lu/y%lu.tile",
                             (unsigned)z, (unsigned long)x, (unsigned long)y);
    return len > 0 && (size_t)len < dest_size;
}

static bool tile_result_paths(d1l_map_tile_download_result_t *result)
{
    if (!result || !d1l_map_tile_store_coord_valid(result->z, result->x, result->y)) {
        return false;
    }
    const int final_len = snprintf(result->path, sizeof(result->path),
                                   "map/tiles/z%u/x%lu/y%lu.tile",
                                   (unsigned)result->z,
                                   (unsigned long)result->x,
                                   (unsigned long)result->y);
    const int tmp_len = snprintf(result->tmp_path, sizeof(result->tmp_path),
                                 "map/tiles/z%u/x%lu/y%lu.tmp",
                                 (unsigned)result->z,
                                 (unsigned long)result->x,
                                 (unsigned long)result->y);
    snprintf(result->attribution_path, sizeof(result->attribution_path), "%s",
             D1L_MAP_TILE_ATTRIBUTION_PATH);
    snprintf(result->attribution_tmp_path, sizeof(result->attribution_tmp_path), "%s",
             D1L_MAP_TILE_ATTRIBUTION_TMP_PATH);
    return final_len > 0 && tmp_len > 0 &&
           (size_t)final_len < sizeof(result->path) &&
           (size_t)tmp_len < sizeof(result->tmp_path);
}

static bool append_text(char *dest, size_t dest_size, size_t *offset, const char *text)
{
    if (!dest || !offset || !text) {
        return false;
    }
    while (*text) {
        if (*offset + 1U >= dest_size) {
            return false;
        }
        dest[(*offset)++] = *text++;
    }
    dest[*offset] = '\0';
    return true;
}

static bool append_u32(char *dest, size_t dest_size, size_t *offset, uint32_t value)
{
    char token[12];
    const int len = snprintf(token, sizeof(token), "%lu", (unsigned long)value);
    return len > 0 && (size_t)len < sizeof(token) &&
           append_text(dest, dest_size, offset, token);
}

static bool build_tile_url(const char *url_template,
                           uint8_t z,
                           uint32_t x,
                           uint32_t y,
                           char *dest,
                           size_t dest_size)
{
    if (!url_template || !dest || dest_size == 0) {
        return false;
    }
    size_t out = 0;
    for (size_t i = 0; url_template[i] != '\0'; ++i) {
        if (strncmp(&url_template[i], "{z}", 3U) == 0) {
            if (!append_u32(dest, dest_size, &out, z)) {
                return false;
            }
            i += 2U;
        } else if (strncmp(&url_template[i], "{x}", 3U) == 0) {
            if (!append_u32(dest, dest_size, &out, x)) {
                return false;
            }
            i += 2U;
        } else if (strncmp(&url_template[i], "{y}", 3U) == 0) {
            if (!append_u32(dest, dest_size, &out, y)) {
                return false;
            }
            i += 2U;
        } else {
            char ch[2] = {url_template[i], '\0'};
            if (!append_text(dest, dest_size, &out, ch)) {
                return false;
            }
        }
    }
    return out > 0;
}

static bool result_path_set(d1l_map_tile_canary_result_t *result,
                            const char *token)
{
    if (!result || !token || token[0] == '\0') {
        return false;
    }
    result->z = D1L_MAP_TILE_CANARY_Z;
    result->x = D1L_MAP_TILE_CANARY_X;
    result->y = D1L_MAP_TILE_CANARY_Y;
    const int final_len = snprintf(result->path, sizeof(result->path),
                                   "map/tiles/z%u/x%lu/y%lu-%s.tile",
                                   (unsigned)D1L_MAP_TILE_CANARY_Z,
                                   (unsigned long)D1L_MAP_TILE_CANARY_X,
                                   (unsigned long)D1L_MAP_TILE_CANARY_Y,
                                   token);
    const int tmp_len = snprintf(result->tmp_path, sizeof(result->tmp_path),
                                 "map/tiles/z%u/x%lu/y%lu-%s.tmp",
                                 (unsigned)D1L_MAP_TILE_CANARY_Z,
                                 (unsigned long)D1L_MAP_TILE_CANARY_X,
                                 (unsigned long)D1L_MAP_TILE_CANARY_Y,
                                 token);
    return final_len > 0 && tmp_len > 0 &&
           (size_t)final_len < sizeof(result->path) &&
           (size_t)tmp_len < sizeof(result->tmp_path);
}

static void download_step(d1l_map_tile_download_result_t *result,
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

static void result_step(d1l_map_tile_canary_result_t *result,
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

static esp_err_t build_canary_payload(const char *token,
                                      uint8_t *payload,
                                      size_t payload_size,
                                      size_t *payload_len)
{
    if (!token || !payload || !payload_len) {
        return ESP_ERR_INVALID_ARG;
    }
    const int len = snprintf((char *)payload, payload_size,
                             "{\"schema\":1,\"kind\":\"map_tile_cache_canary\","
                             "\"token\":\"%s\",\"z\":%u,\"x\":%lu,\"y\":%lu,"
                             "\"public_rf_tx\":false,\"formats_sd\":false}\n",
                             token,
                             (unsigned)D1L_MAP_TILE_CANARY_Z,
                             (unsigned long)D1L_MAP_TILE_CANARY_X,
                             (unsigned long)D1L_MAP_TILE_CANARY_Y);
    if (len <= 0 || (size_t)len >= payload_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    *payload_len = (size_t)len;
    return ESP_OK;
}

bool d1l_map_tile_store_token_valid(const char *token)
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
        if (len > D1L_MAP_TILE_CANARY_TOKEN_MAX) {
            return false;
        }
    }
    return true;
}

bool d1l_map_tile_store_sd_ready(const d1l_storage_status_t *status)
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

static esp_err_t write_attribution_metadata(d1l_map_tile_download_result_t *result)
{
    char payload[D1L_RP2040_FILE_CHUNK_MAX];
    const int len = snprintf(payload, sizeof(payload),
                             "{\"schema\":1,\"kind\":\"map_tile_attribution\","
                             "\"attribution\":\"%s\",\"policy\":\"%s\","
                             "\"public_rf_tx\":false,\"formats_sd\":false}\n",
                             result->attribution,
                             D1L_MAP_TILE_PROVIDER_POLICY);
    if (len <= 0 || (size_t)len >= sizeof(payload)) {
        return ESP_ERR_INVALID_SIZE;
    }
    d1l_rp2040_file_result_t file = {0};
    esp_err_t ret = d1l_rp2040_bridge_file_write(result->attribution_tmp_path, 0U,
                                                 (const uint8_t *)payload,
                                                 (size_t)len, true, &file, 3000U);
    if (ret != ESP_OK || file.length != (uint32_t)len) {
        download_step(result, "write_attribution", ret == ESP_OK ? ESP_FAIL : ret, &file);
        return result->last_error;
    }
    ret = d1l_rp2040_bridge_file_rename(result->attribution_tmp_path,
                                        result->attribution_path, true,
                                        &file, 3000U);
    if (ret != ESP_OK) {
        download_step(result, "rename_attribution", ret, &file);
        return ret;
    }
    result->attribution_saved = true;
    return ESP_OK;
}

esp_err_t d1l_map_tile_store_download(const char *url_template,
                                      const char *attribution,
                                      uint8_t z,
                                      uint32_t x,
                                      uint32_t y,
                                      const d1l_storage_status_t *status,
                                      bool wifi_connected,
                                      d1l_map_tile_download_result_t *out_result)
{
    if (!url_template || !attribution || !out_result) {
        return ESP_ERR_INVALID_ARG;
    }

    d1l_map_tile_download_result_t result = {
        .z = z,
        .x = x,
        .y = y,
        .wifi_connected = wifi_connected,
        .sd_ready = d1l_map_tile_store_sd_ready(status),
        .public_rf_tx = false,
        .formats_sd = false,
        .last_error = ESP_OK,
    };
    snprintf(result.attribution, sizeof(result.attribution), "%s", attribution);
    result.provider_allowed = d1l_map_tile_provider_template_allowed(url_template);
    if (!result.provider_allowed) {
        download_step(&result, "provider", ESP_ERR_INVALID_ARG, NULL);
        *out_result = result;
        return result.last_error;
    }
    if (!d1l_map_tile_attribution_valid(attribution)) {
        download_step(&result, "attribution", ESP_ERR_INVALID_ARG, NULL);
        *out_result = result;
        return result.last_error;
    }
    if (!d1l_map_tile_store_coord_valid(z, x, y) || !tile_result_paths(&result)) {
        download_step(&result, "coordinate", ESP_ERR_INVALID_ARG, NULL);
        *out_result = result;
        return result.last_error;
    }
    if (!build_tile_url(url_template, z, x, y, result.url, sizeof(result.url))) {
        download_step(&result, "url", ESP_ERR_INVALID_SIZE, NULL);
        *out_result = result;
        return result.last_error;
    }
    if (!wifi_connected) {
        download_step(&result, "wifi", ESP_ERR_INVALID_STATE, NULL);
        *out_result = result;
        return result.last_error;
    }
    if (!result.sd_ready) {
        download_step(&result, "preflight", ESP_ERR_NOT_SUPPORTED, NULL);
        *out_result = result;
        return result.last_error;
    }

    esp_http_client_config_t config = {
        .url = result.url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = D1L_MAP_TILE_HTTP_TIMEOUT_MS,
        .user_agent = D1L_MAP_TILE_USER_AGENT,
        .buffer_size = D1L_RP2040_FILE_CHUNK_MAX,
        .buffer_size_tx = 512,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        download_step(&result, "http_init", ESP_FAIL, NULL);
        *out_result = result;
        return result.last_error;
    }

    d1l_rp2040_file_result_t file = {0};
    (void)d1l_rp2040_bridge_file_delete(result.tmp_path, &file, 3000U);
    esp_err_t ret = esp_http_client_open(client, 0);
    if (ret != ESP_OK) {
        download_step(&result, "http_open", ret, NULL);
        esp_http_client_cleanup(client);
        *out_result = result;
        return ret;
    }
    const int64_t content_length = esp_http_client_fetch_headers(client);
    result.status_code = esp_http_client_get_status_code(client);
    if (content_length < 0) {
        download_step(&result, "http_headers", ESP_FAIL, NULL);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        *out_result = result;
        return result.last_error;
    }
    if (content_length > (int64_t)D1L_MAP_TILE_DOWNLOAD_MAX_BYTES) {
        download_step(&result, "too_large", ESP_ERR_INVALID_SIZE, NULL);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        *out_result = result;
        return result.last_error;
    }
    if (result.status_code != 200) {
        download_step(&result, "http_status", ESP_FAIL, NULL);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        *out_result = result;
        return result.last_error;
    }

    uint8_t buffer[D1L_RP2040_FILE_CHUNK_MAX];
    int idle_reads = 0;
    while (!esp_http_client_is_complete_data_received(client)) {
        const int read_len = esp_http_client_read(client, (char *)buffer, sizeof(buffer));
        if (read_len < 0) {
            download_step(&result, "http_read", ESP_FAIL, NULL);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            *out_result = result;
            return result.last_error;
        }
        if (read_len == 0) {
            if (++idle_reads > 3) {
                break;
            }
            continue;
        }
        idle_reads = 0;
        if (result.bytes + (size_t)read_len > D1L_MAP_TILE_DOWNLOAD_MAX_BYTES) {
            download_step(&result, "too_large", ESP_ERR_INVALID_SIZE, NULL);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            *out_result = result;
            return result.last_error;
        }
        ret = d1l_rp2040_bridge_file_write(result.tmp_path, (uint32_t)result.bytes,
                                           buffer, (size_t)read_len,
                                           result.bytes == 0U, &file, 3000U);
        if (ret != ESP_OK || file.length != (uint32_t)read_len) {
            download_step(&result, "write_tmp", ret == ESP_OK ? ESP_FAIL : ret, &file);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            *out_result = result;
            return result.last_error;
        }
        result.write_tmp = true;
        result.bytes += (size_t)read_len;
    }
    if (!esp_http_client_is_complete_data_received(client) || result.bytes == 0U) {
        download_step(&result, "http_incomplete", ESP_FAIL, NULL);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        *out_result = result;
        return result.last_error;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    ret = write_attribution_metadata(&result);
    if (ret != ESP_OK) {
        *out_result = result;
        return ret;
    }
    ret = d1l_rp2040_bridge_file_rename(result.tmp_path, result.path, true, &file, 3000U);
    if (ret != ESP_OK) {
        download_step(&result, "rename_final", ret, &file);
        *out_result = result;
        return ret;
    }
    result.rename_replace = true;
    download_step(&result, "ok", ESP_OK, &file);
    *out_result = result;
    return ESP_OK;
}

esp_err_t d1l_map_tile_store_write_canary(const char *token,
                                          const d1l_storage_status_t *status,
                                          d1l_map_tile_canary_result_t *out_result)
{
    if (!token || !out_result) {
        return ESP_ERR_INVALID_ARG;
    }

    d1l_map_tile_canary_result_t result = {
        .public_rf_tx = false,
        .formats_sd = false,
        .last_error = ESP_OK,
    };
    if (!d1l_map_tile_store_token_valid(token)) {
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
    if (!d1l_map_tile_store_sd_ready(status)) {
        result_step(&result, "preflight", ESP_ERR_NOT_SUPPORTED, NULL);
        *out_result = result;
        return ESP_ERR_NOT_SUPPORTED;
    }

    uint8_t payload[D1L_RP2040_FILE_CHUNK_MAX];
    size_t payload_len = 0;
    esp_err_t payload_ret = build_canary_payload(token, payload, sizeof(payload), &payload_len);
    if (payload_ret != ESP_OK) {
        result_step(&result, "payload", payload_ret, NULL);
        *out_result = result;
        return payload_ret;
    }
    result.bytes = payload_len;

    uint8_t read_buf[D1L_RP2040_FILE_CHUNK_MAX] = {0};
    d1l_rp2040_file_result_t file = {0};
    esp_err_t ret = d1l_rp2040_bridge_file_delete(result.tmp_path, &file, 3000U);
    if (ret != ESP_OK && ret != ESP_ERR_NOT_FOUND && strcmp(file.err, "not_found") != 0) {
        result_step(&result, "cleanup_tmp", ret, &file);
        *out_result = result;
        return ret;
    }

    ret = d1l_rp2040_bridge_file_write(result.tmp_path, 0U, payload,
                                       (size_t)payload_len, true, &file, 3000U);
    if (ret != ESP_OK || file.length != (uint32_t)payload_len ||
        file.size != (uint32_t)payload_len) {
        result_step(&result, "write_tmp", ret == ESP_OK ? ESP_FAIL : ret, &file);
        *out_result = result;
        return result.last_error;
    }
    result.write_tmp = true;

    ret = d1l_rp2040_bridge_file_read(result.tmp_path, 0U, read_buf,
                                      (size_t)payload_len, &file, 3000U);
    if (ret != ESP_OK || file.length != (uint32_t)payload_len ||
        memcmp(read_buf, payload, (size_t)payload_len) != 0) {
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
    if (ret != ESP_OK || !file.exists || file.is_directory ||
        file.size != (uint32_t)payload_len) {
        result_step(&result, "stat_final", ret == ESP_OK ? ESP_FAIL : ret, &file);
        *out_result = result;
        return result.last_error;
    }
    result.stat_final = true;

    memset(read_buf, 0, sizeof(read_buf));
    ret = d1l_rp2040_bridge_file_read(result.path, 0U, read_buf,
                                      (size_t)payload_len, &file, 3000U);
    if (ret != ESP_OK || file.length != (uint32_t)payload_len ||
        memcmp(read_buf, payload, (size_t)payload_len) != 0) {
        result_step(&result, "read_final", ret == ESP_OK ? ESP_FAIL : ret, &file);
        *out_result = result;
        return result.last_error;
    }
    result.read_final = true;
    result_step(&result, "ok", ESP_OK, &file);
    *out_result = result;
    return ESP_OK;
}

esp_err_t d1l_map_tile_store_check_canary(const char *token,
                                          const d1l_storage_status_t *status,
                                          d1l_map_tile_canary_result_t *out_result)
{
    if (!token || !out_result) {
        return ESP_ERR_INVALID_ARG;
    }

    d1l_map_tile_canary_result_t result = {
        .public_rf_tx = false,
        .formats_sd = false,
        .last_error = ESP_OK,
    };
    if (!d1l_map_tile_store_token_valid(token)) {
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
    if (!d1l_map_tile_store_sd_ready(status)) {
        result_step(&result, "preflight", ESP_ERR_NOT_SUPPORTED, NULL);
        *out_result = result;
        return ESP_ERR_NOT_SUPPORTED;
    }

    uint8_t payload[D1L_RP2040_FILE_CHUNK_MAX];
    size_t payload_len = 0;
    esp_err_t payload_ret = build_canary_payload(token, payload, sizeof(payload), &payload_len);
    if (payload_ret != ESP_OK) {
        result_step(&result, "payload", payload_ret, NULL);
        *out_result = result;
        return payload_ret;
    }
    result.bytes = payload_len;

    d1l_rp2040_file_result_t file = {0};
    esp_err_t ret = d1l_rp2040_bridge_file_stat(result.path, &file, 3000U);
    if (ret != ESP_OK || !file.exists || file.is_directory ||
        file.size != (uint32_t)payload_len) {
        result_step(&result, "stat_final", ret == ESP_OK ? ESP_FAIL : ret, &file);
        *out_result = result;
        return result.last_error;
    }
    result.stat_final = true;

    uint8_t read_buf[D1L_RP2040_FILE_CHUNK_MAX] = {0};
    ret = d1l_rp2040_bridge_file_read(result.path, 0U, read_buf, payload_len,
                                      &file, 3000U);
    if (ret != ESP_OK || file.length != (uint32_t)payload_len ||
        memcmp(read_buf, payload, payload_len) != 0) {
        result_step(&result, "read_final", ret == ESP_OK ? ESP_FAIL : ret, &file);
        *out_result = result;
        return result.last_error;
    }
    result.read_final = true;
    result_step(&result, "ok", ESP_OK, &file);
    *out_result = result;
    return ESP_OK;
}
