#include "usb_console.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "d1l_config.h"
#include "app/app_model.h"
#include "app/settings_model.h"
#include "comms/connectivity_manager.h"
#include "comms/companion_3byte.h"
#include "diagnostics/crash_log.h"
#include "diagnostics/health_monitor.h"
#include "hal/backlight.h"
#include "hal/indicator_board.h"
#include "hal/rp2040_bridge.h"
#include "hal/sx1262_indicator.h"
#include "mesh/contact_store.h"
#include "mesh/dm_store.h"
#include "mesh/mesh_inspector.h"
#include "mesh/message_store.h"
#include "mesh/node_store.h"
#include "mesh/packet_log.h"
#include "mesh/read_state.h"
#include "mesh/route_store.h"
#include "mesh/route_store_worker.h"
#include "mesh/meshcore_radio_profile.h"
#include "mesh/meshcore_service.h"
#include "map/map_png_decoder.h"
#include "map/map_view_service.h"
#include "storage/export_store.h"
#include "storage/map_tile_store.h"
#include "storage/storage_status.h"
#include "ui/ui_phase1.h"

static const size_t D1L_CONSOLE_MESSAGE_PAGE_SIZE = 8U;
static const uint32_t D1L_STORAGE_FILE_CANARY_OP_TIMEOUT_MS = 30000U;
static const uint32_t D1L_STORAGE_FILE_CANARY_MANAGER_PAUSE_MS = 60000U;
static const uint32_t D1L_RETAINED_CANARY_QUIESCE_TIMEOUT_MS = 15000U;
static const uint32_t D1L_REBOOT_QUIESCE_TIMEOUT_MS = 15000U;

static bool parse_next_u32_arg(const char **cursor, uint32_t *out_value);

static uint32_t deadline_remaining_ms(int64_t started_us, uint32_t timeout_ms)
{
    const int64_t elapsed_us = esp_timer_get_time() - started_us;
    const int64_t deadline_us = (int64_t)timeout_ms * 1000LL;
    if (elapsed_us >= deadline_us) {
        return 0U;
    }
    return (uint32_t)((deadline_us - elapsed_us + 999LL) / 1000LL);
}

static uint32_t reboot_deadline_remaining_ms(int64_t started_us)
{
    return deadline_remaining_ms(started_us, D1L_REBOOT_QUIESCE_TIMEOUT_MS);
}

static void trim_line(char *line)
{
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r' || isspace((unsigned char)line[n - 1]))) {
        line[--n] = '\0';
    }
}

static void ok_begin(const char *cmd)
{
    printf("{\"schema\":%d,\"ok\":true,\"cmd\":\"%s\"", D1L_CONSOLE_SCHEMA, cmd);
}

static void err_result(const char *cmd, const char *code, const char *hint)
{
    printf("{\"schema\":%d,\"ok\":false,\"cmd\":\"%s\",\"code\":\"%s\",\"hint\":\"%s\"}\n",
           D1L_CONSOLE_SCHEMA, cmd, code, hint ? hint : "");
}

static const char *bool_json(bool value)
{
    return value ? "true" : "false";
}

static void print_e7_json(int32_t value)
{
    int64_t scaled = value;
    if (scaled < 0) {
        putchar('-');
        scaled = -scaled;
    }
    printf("%lld.%07lld", (long long)(scaled / 10000000LL),
           (long long)(scaled % 10000000LL));
}

static bool parse_coord_e7(const char *text, const char **out_end,
                           int32_t min_e7, int32_t max_e7, int32_t *out_value)
{
    if (!text || !out_value) {
        return false;
    }
    char *end = NULL;
    const double value = strtod(text, &end);
    if (end == text || value != value) {
        return false;
    }
    const double min_value = ((double)min_e7) / 10000000.0;
    const double max_value = ((double)max_e7) / 10000000.0;
    if (value < min_value || value > max_value) {
        return false;
    }
    const double scaled = value * 10000000.0;
    const int64_t rounded = (int64_t)(scaled >= 0 ? scaled + 0.5 : scaled - 0.5);
    if (rounded < min_e7 || rounded > max_e7) {
        return false;
    }
    *out_value = (int32_t)rounded;
    if (out_end) {
        *out_end = end;
    }
    return true;
}

static void print_json_string(const char *text)
{
    putchar('"');
    if (text) {
        for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
            switch (*p) {
            case '"':
                fputs("\\\"", stdout);
                break;
            case '\\':
                fputs("\\\\", stdout);
                break;
            case '\b':
                fputs("\\b", stdout);
                break;
            case '\f':
                fputs("\\f", stdout);
                break;
            case '\n':
                fputs("\\n", stdout);
                break;
            case '\r':
                fputs("\\r", stdout);
                break;
            case '\t':
                fputs("\\t", stdout);
                break;
            default:
                if (*p < 0x20U) {
                    printf("\\u%04x", (unsigned)*p);
                } else {
                    putchar((int)*p);
                }
                break;
            }
        }
    }
    putchar('"');
}

static void print_hex_bytes_json(const uint8_t *bytes, size_t len)
{
    static const char hex[] = "0123456789ABCDEF";
    putchar('"');
    for (size_t i = 0; bytes && i < len; ++i) {
        putchar(hex[(bytes[i] >> 4U) & 0x0FU]);
        putchar(hex[bytes[i] & 0x0FU]);
    }
    putchar('"');
}

static void print_retained_sd_store_json(const d1l_retained_blob_store_sd_stats_t *stats)
{
    const d1l_retained_blob_store_sd_stats_t empty = {0};
    if (!stats) {
        stats = &empty;
    }
    printf("{\"sd_read_fail_count\":%lu,\"sd_write_fail_count\":%lu,"
           "\"sd_rename_fail_count\":%lu,\"sd_last_error\":\"%s\","
           "\"sd_degraded_latched\":%s,\"nvs_mirror_fail_count\":%lu,"
           "\"nvs_mirror_last_error\":\"%s\"}",
           (unsigned long)stats->sd_read_fail_count,
           (unsigned long)stats->sd_write_fail_count,
           (unsigned long)stats->sd_rename_fail_count,
           esp_err_to_name(stats->sd_last_error),
           bool_json(stats->sd_degraded_latched),
           (unsigned long)stats->nvs_mirror_fail_count,
           esp_err_to_name(stats->nvs_mirror_last_error));
}

static bool parse_fingerprint_token(const char *src, char *dest, size_t dest_size)
{
    if (!src || !dest || dest_size < D1L_NODE_FINGERPRINT_LEN) {
        return false;
    }
    size_t len = 0;
    while (src[len] != '\0' && !isspace((unsigned char)src[len])) {
        len++;
    }
    if (len != D1L_NODE_FINGERPRINT_LEN - 1U) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)src[i];
        if (!isxdigit(c)) {
            return false;
        }
        dest[i] = (char)toupper(c);
    }
    dest[len] = '\0';
    return true;
}

static bool parse_bool_token(const char *src, bool *out_value)
{
    if (!src || !out_value) {
        return false;
    }
    if (strcmp(src, "1") == 0 || strcmp(src, "true") == 0 || strcmp(src, "on") == 0) {
        *out_value = true;
        return true;
    }
    if (strcmp(src, "0") == 0 || strcmp(src, "false") == 0 || strcmp(src, "off") == 0) {
        *out_value = false;
        return true;
    }
    return false;
}

static void hex_prefix(char *dest, size_t dest_size, const uint8_t *src, size_t src_len)
{
    static const char hex[] = "0123456789ABCDEF";
    if (!dest || dest_size == 0) {
        return;
    }
    size_t out = 0;
    for (size_t i = 0; src && i < src_len && out + 2U < dest_size; ++i) {
        dest[out++] = hex[(src[i] >> 4) & 0x0fU];
        dest[out++] = hex[src[i] & 0x0fU];
    }
    dest[out] = '\0';
}

static void print_hex_field(const char *name, const uint8_t *src, size_t src_len)
{
    char text[(D1L_TOUCH_RAW_REG_BYTES * 2U) + 1U] = {0};
    hex_prefix(text, sizeof(text), src, src_len);
    printf(",\"%s\":\"%s\"", name, text);
}

static void sanitize_node_name(char *name)
{
    if (name == NULL) {
        return;
    }
    for (size_t i = 0; i < D1L_NODE_NAME_LEN && name[i] != '\0'; ++i) {
        const unsigned char c = (unsigned char)name[i];
        if (c < 32 || c > 126 || c == '"' || c == '\\') {
            name[i] = '_';
        }
    }
}

static void cmd_version(void)
{
    ok_begin("version");
    printf(",\"firmware\":\"%s\",\"version\":\"%s\",\"build_commit\":\"%s\","
           "\"idf\":\"%s\",\"meshcore_ca_desk_mode\":true}\n",
           D1L_FIRMWARE_NAME, D1L_FIRMWARE_VERSION, D1L_BUILD_GIT_COMMIT,
           esp_get_idf_version());
}

static void cmd_board(void)
{
    const d1l_board_status_t *status = d1l_board_status();
    ok_begin("board");
    printf(",\"target\":\"seeed_indicator_d1l\",\"ready\":%s,\"init_result\":\"%s\",\"display\":{\"width\":480,\"height\":480},\"button_gpio\":38,\"backlight_gpio\":45}\n",
           status->ready ? "true" : "false", esp_err_to_name(status->init_result));
}

static void cmd_settings_get(void)
{
    const d1l_settings_t *settings = d1l_settings_current();
    const d1l_meshcore_service_status_t mesh = d1l_meshcore_service_status();
    ok_begin("settings get");
    printf(",\"node_name\":\"%s\",\"role\":\"%s\",\"onboarding_complete\":%s,\"wifi_enabled\":%s,\"ble_companion_enabled\":%s,\"observer_enabled\":%s,\"high_contrast\":%s,\"night_mode\":%s,\"path_hash_bytes\":%u,\"map_location\":{\"set\":%s,\"lat\":",
           settings->node_name, d1l_settings_role_name(settings->role),
           bool_json(settings->onboarding_complete),
           bool_json(settings->wifi_enabled), bool_json(settings->ble_companion_enabled),
           bool_json(settings->observer_enabled), bool_json(settings->high_contrast),
           bool_json(settings->night_mode), settings->path_hash_bytes,
           bool_json(settings->map_location_set));
    print_e7_json(settings->map_location_set ? settings->map_lat_e7 : 0);
    printf(",\"lon\":");
    print_e7_json(settings->map_location_set ? settings->map_lon_e7 : 0);
    printf(",\"source\":\"%s\"},\"map_tiles\":{\"source\":",
           settings->map_location_set ? "manual" : "unset");
    print_json_string(D1L_MAP_TILE_SOURCE_ID);
    printf(",\"built_in\":true,\"zoom\":%u,\"url_template\":",
           (unsigned)settings->map_tile_zoom);
    print_json_string(D1L_MAP_TILE_SOURCE_URL_TEMPLATE);
    printf(",\"attribution\":");
    print_json_string(D1L_MAP_TILE_ATTRIBUTION);
    printf(",\"policy\":\"%s\"},\"radio\":{\"frequency_hz\":%lu,\"bandwidth_khz\":%.1f,\"sf\":%u,\"cr\":%u,\"tx_power_dbm\":%d,\"rx_boost\":%s,\"tcxo\":\"%s\",\"applied_to_radio\":%s,\"radio_apply_pending\":%s,\"radio_apply_error\":\"%s\"}}\n",
           D1L_MAP_TILE_PROVIDER_POLICY,
           (unsigned long)settings->frequency_hz,
           ((float)settings->bandwidth_tenths_khz) / 10.0f,
           settings->spreading_factor, settings->coding_rate, settings->tx_power_dbm,
           bool_json(settings->rx_boost), d1l_settings_tcxo_name(settings->tcxo_mode),
           bool_json(mesh.radio_applied), bool_json(mesh.radio_apply_pending),
           esp_err_to_name(mesh.radio_apply_error));
}

static void cmd_settings_reset(void)
{
    esp_err_t ret = d1l_settings_reset();
    if (ret != ESP_OK) {
        err_result("settings reset", esp_err_to_name(ret), "settings reset failed");
        return;
    }
    d1l_meshcore_service_init();
    ok_begin("settings reset");
    printf(",\"persisted\":true,\"node_name\":\"%s\"}\n", d1l_settings_current()->node_name);
}

static void cmd_settings_onboarding_status(void)
{
    const d1l_settings_t *settings = d1l_settings_current();
    ok_begin("settings onboarding status");
    printf(",\"complete\":%s,\"node_name\":\"%s\",\"role\":\"%s\",\"region\":\"Canada/USA\",\"radio_profile\":\"uscan-meshcore-default\",\"wifi_enabled\":%s,\"ble_companion_enabled\":%s,\"observer_enabled\":%s}\n",
           bool_json(settings->onboarding_complete), settings->node_name,
           d1l_settings_role_name(settings->role), bool_json(settings->wifi_enabled),
           bool_json(settings->ble_companion_enabled), bool_json(settings->observer_enabled));
}

static void cmd_settings_onboarding_reset(void)
{
    esp_err_t ret = d1l_settings_reset_onboarding();
    if (ret != ESP_OK) {
        err_result("settings onboarding reset", esp_err_to_name(ret), "could not reset onboarding state");
        return;
    }
    ok_begin("settings onboarding reset");
    printf(",\"complete\":false,\"persisted\":true}\n");
}

static void cmd_settings_onboarding_complete(const char *line)
{
    const char *arg = line + strlen("settings onboarding complete ");
    while (*arg == ' ') {
        arg++;
    }
    if (*arg == '\0') {
        arg = "D1L Desk";
    }
    size_t arg_len = strlen(arg);
    if (arg_len >= D1L_NODE_NAME_LEN) {
        err_result("settings onboarding complete", "INVALID_NAME", "usage: settings onboarding complete <1-31 chars>");
        return;
    }
    esp_err_t ret = d1l_settings_complete_onboarding(arg, false, false, false);
    if (ret == ESP_OK) {
        d1l_meshcore_service_init();
        ret = d1l_meshcore_service_ensure_identity();
    }
    if (ret != ESP_OK) {
        err_result("settings onboarding complete", esp_err_to_name(ret), "could not persist onboarding choices");
        return;
    }
    const d1l_settings_t *settings = d1l_settings_current();
    ok_begin("settings onboarding complete");
    printf(",\"complete\":true,\"persisted\":true,\"node_name\":\"%s\",\"role\":\"%s\",\"wifi_enabled\":false,\"ble_companion_enabled\":false,\"observer_enabled\":false,\"identity_ready\":%s}\n",
           settings->node_name, d1l_settings_role_name(settings->role),
           bool_json(settings->identity_ready));
}

static void cmd_settings_set_name(const char *line)
{
    const char *arg = line + strlen("settings set name ");
    while (*arg == ' ') {
        arg++;
    }
    if (*arg == '\0') {
        err_result("settings set name", "INVALID_NAME", "usage: settings set name <1-31 chars>");
        return;
    }
    size_t arg_len = strlen(arg);
    if (arg_len >= D1L_NODE_NAME_LEN) {
        err_result("settings set name", "INVALID_NAME", "usage: settings set name <1-31 chars>");
        return;
    }
    d1l_settings_t settings = *d1l_settings_current();
    memcpy(settings.node_name, arg, arg_len + 1U);
    sanitize_node_name(settings.node_name);
    esp_err_t ret = d1l_settings_save(&settings);
    if (ret != ESP_OK) {
        err_result("settings set name", esp_err_to_name(ret), "could not persist node name");
        return;
    }
    ok_begin("settings set name");
    printf(",\"persisted\":true,\"node_name\":\"%s\"}\n", d1l_settings_current()->node_name);
}

static void cmd_settings_set_pathhash(const char *line)
{
    int value = atoi(line + strlen("settings set pathhash "));
    if (value < 1 || value > 3) {
        err_result("settings set pathhash", "INVALID_PATH_HASH_BYTES", "usage: settings set pathhash <1|2|3>");
        return;
    }
    d1l_settings_t settings = *d1l_settings_current();
    settings.path_hash_bytes = (uint8_t)value;
    esp_err_t ret = d1l_settings_save(&settings);
    if (ret != ESP_OK) {
        err_result("settings set pathhash", esp_err_to_name(ret), "could not persist path hash setting");
        return;
    }
    d1l_meshcore_service_init();
    ok_begin("settings set pathhash");
    printf(",\"persisted\":true,\"path_hash_bytes\":%u,\"legacy_warning\":%s}\n",
           d1l_settings_current()->path_hash_bytes,
           value > 1 ? "true" : "false");
}

static void print_map_location_result(const char *cmd, const d1l_settings_t *settings,
                                      bool persisted)
{
    ok_begin(cmd);
    printf(",\"persisted\":%s,\"map_location\":{\"set\":%s,\"lat\":",
           bool_json(persisted), bool_json(settings && settings->map_location_set));
    print_e7_json(settings && settings->map_location_set ? settings->map_lat_e7 : 0);
    printf(",\"lon\":");
    print_e7_json(settings && settings->map_location_set ? settings->map_lon_e7 : 0);
    printf(",\"lat_e7\":%ld,\"lon_e7\":%ld,\"source\":\"%s\"}}\n",
           (long)(settings && settings->map_location_set ? settings->map_lat_e7 : 0),
           (long)(settings && settings->map_location_set ? settings->map_lon_e7 : 0),
           settings && settings->map_location_set ? "manual" : "unset");
}

static void persist_map_location_from_args(const char *cmd, const char *arg)
{
    while (*arg == ' ') {
        arg++;
    }
    int32_t lat_e7 = 0;
    int32_t lon_e7 = 0;
    const char *end = NULL;
    if (!parse_coord_e7(arg, &end, D1L_MAP_LOCATION_LAT_E7_MIN,
                        D1L_MAP_LOCATION_LAT_E7_MAX, &lat_e7)) {
        err_result(cmd, "INVALID_LOCATION",
                   "usage: map center set <lat -90..90> <lon -180..180>");
        return;
    }
    while (end && *end == ' ') {
        end++;
    }
    if (!parse_coord_e7(end, &end, D1L_MAP_LOCATION_LON_E7_MIN,
                        D1L_MAP_LOCATION_LON_E7_MAX, &lon_e7)) {
        err_result(cmd, "INVALID_LOCATION",
                   "usage: map center set <lat -90..90> <lon -180..180>");
        return;
    }
    while (end && *end == ' ') {
        end++;
    }
    if (end && *end != '\0') {
        err_result(cmd, "INVALID_LOCATION",
                   "usage: map center set <lat -90..90> <lon -180..180>");
        return;
    }

    esp_err_t ret = d1l_app_model_set_map_location(lat_e7, lon_e7);
    if (ret != ESP_OK) {
        err_result(cmd, esp_err_to_name(ret), "could not persist map location");
        return;
    }
    print_map_location_result(cmd, d1l_settings_current(), true);
}

static void clear_map_location(const char *cmd)
{
    esp_err_t ret = d1l_app_model_clear_map_location();
    if (ret != ESP_OK) {
        err_result(cmd, esp_err_to_name(ret), "could not clear map location");
        return;
    }
    print_map_location_result(cmd, d1l_settings_current(), true);
}

static void cmd_settings_set_location(const char *line)
{
    persist_map_location_from_args("settings set location",
                                   line + strlen("settings set location "));
}

static void cmd_settings_clear_location(void)
{
    clear_map_location("settings clear location");
}

static void cmd_map_center(void)
{
    print_map_location_result("map center", d1l_settings_current(), false);
}

static void cmd_map_center_set(const char *line)
{
    persist_map_location_from_args("map center set",
                                   line + strlen("map center set "));
}

static void cmd_map_center_clear(void)
{
    clear_map_location("map center clear");
}

static void cmd_ui_status(void)
{
    ok_begin("ui status");
    printf(",\"started\":true,\"active_tab\":");
    print_json_string(d1l_ui_phase1_active_tab_name());
    printf(",\"pending\":%s,\"pending_tab\":",
           bool_json(d1l_ui_phase1_tab_switch_pending()));
    print_json_string(d1l_ui_phase1_pending_tab_name());
    printf("}\n");
}

static void print_ui_capture_status_fields(const d1l_ui_capture_status_t *status)
{
    const d1l_ui_capture_status_t empty = {
        .width = D1L_UI_CAPTURE_WIDTH,
        .height = D1L_UI_CAPTURE_HEIGHT,
        .bytes_per_pixel = D1L_UI_CAPTURE_BYTES_PER_PIXEL,
        .total_bytes = D1L_UI_CAPTURE_TOTAL_BYTES,
        .max_chunk_bytes = D1L_UI_CAPTURE_MAX_CHUNK_BYTES,
    };
    if (!status) {
        status = &empty;
    }
    printf(",\"available\":%s,\"active\":%s,\"shadow_ready\":%s,\"started\":%s",
           bool_json(status->available), bool_json(status->active),
           bool_json(status->shadow_ready), bool_json(status->started));
    printf(",\"width\":%u,\"height\":%u,\"bytes_per_pixel\":%u,\"pixel_format\":\"rgb565-le\"",
           (unsigned)status->width,
           (unsigned)status->height,
           (unsigned)status->bytes_per_pixel);
    printf(",\"total_bytes\":%lu,\"max_chunk_bytes\":%lu",
           (unsigned long)status->total_bytes,
           (unsigned long)status->max_chunk_bytes);
    printf(",\"frame_seq\":%lu,\"flush_count\":%lu,\"crc32\":\"%08lX\"",
           (unsigned long)status->frame_seq,
           (unsigned long)status->flush_count,
           (unsigned long)status->capture_crc32);
    printf(",\"active_tab\":");
    print_json_string(status->active_tab);
    printf(",\"pending_tab\":");
    print_json_string(status->pending_tab);
    printf(",\"tab_pending\":%s,\"content_pending\":%s,\"render_pending\":%s",
           bool_json(status->tab_pending),
           bool_json(status->content_pending),
           bool_json(status->render_pending));
    printf(",\"onboarding_visible\":%s,\"lock_visible\":%s",
           bool_json(status->onboarding_visible),
           bool_json(status->lock_visible));
    printf(",\"public_rf_tx\":false,\"formats_sd\":false");
}

static void cmd_ui_capture_status(void)
{
    d1l_ui_capture_status_t status = {0};
    esp_err_t ret = d1l_ui_capture_status(&status);
    if (ret != ESP_OK) {
        err_result("ui capture status", esp_err_to_name(ret),
                   "UI capture status is unavailable");
        return;
    }
    ok_begin("ui capture status");
    print_ui_capture_status_fields(&status);
    printf("}\n");
}

static void cmd_ui_capture_begin(void)
{
    d1l_ui_capture_status_t status = {0};
    esp_err_t ret = d1l_ui_capture_begin(&status);
    if (ret != ESP_OK) {
        err_result("ui capture begin", esp_err_to_name(ret),
                   "UI capture requires a started display with at least one flushed frame");
        return;
    }
    ok_begin("ui capture begin");
    print_ui_capture_status_fields(&status);
    printf("}\n");
}

static bool parse_ui_capture_chunk_args(const char *line, size_t *out_offset, size_t *out_len)
{
    static const char prefix[] = "ui capture chunk ";
    if (strncmp(line, prefix, strlen(prefix)) != 0 || !out_offset || !out_len) {
        return false;
    }
    const char *cursor = line + strlen(prefix);
    char *end = NULL;
    unsigned long offset = strtoul(cursor, &end, 10);
    if (end == cursor || !isspace((unsigned char)*end)) {
        return false;
    }
    while (*end && isspace((unsigned char)*end)) {
        end++;
    }
    cursor = end;
    unsigned long len = strtoul(cursor, &end, 10);
    if (end == cursor) {
        return false;
    }
    while (*end && isspace((unsigned char)*end)) {
        end++;
    }
    if (*end != '\0') {
        return false;
    }
    *out_offset = (size_t)offset;
    *out_len = (size_t)len;
    return true;
}

static void cmd_ui_capture_chunk(const char *line)
{
    size_t offset = 0;
    size_t requested_len = 0;
    if (!parse_ui_capture_chunk_args(line, &offset, &requested_len)) {
        err_result("ui capture chunk", "INVALID_ARG",
                   "usage: ui capture chunk <offset> <len>");
        return;
    }
    uint8_t bytes[D1L_UI_CAPTURE_MAX_CHUNK_BYTES];
    size_t len = 0;
    uint32_t chunk_crc32 = 0;
    d1l_ui_capture_status_t status = {0};
    esp_err_t ret = d1l_ui_capture_chunk(offset, requested_len, bytes, sizeof(bytes),
                                         &len, &chunk_crc32, &status);
    if (ret != ESP_OK) {
        err_result("ui capture chunk", esp_err_to_name(ret),
                   "call ui capture begin first, then request chunks inside total_bytes");
        return;
    }
    ok_begin("ui capture chunk");
    print_ui_capture_status_fields(&status);
    printf(",\"offset\":%lu,\"requested_len\":%lu,\"len\":%lu,\"chunk_crc32\":\"%08lX\",\"data_hex\":",
           (unsigned long)offset,
           (unsigned long)requested_len,
           (unsigned long)len,
           (unsigned long)chunk_crc32);
    print_hex_bytes_json(bytes, len);
    printf("}\n");
}

static void cmd_ui_capture_end(void)
{
    d1l_ui_capture_status_t status = {0};
    esp_err_t ret = d1l_ui_capture_end(&status);
    if (ret != ESP_OK) {
        err_result("ui capture end", esp_err_to_name(ret),
                   "UI capture was not initialized");
        return;
    }
    ok_begin("ui capture end");
    print_ui_capture_status_fields(&status);
    printf("}\n");
}

static void cmd_ui_tab(const char *line)
{
    const char *arg = line + strlen("ui tab ");
    while (*arg == ' ') {
        arg++;
    }
    char tab[16] = {0};
    size_t len = 0;
    while (arg[len] != '\0' && !isspace((unsigned char)arg[len]) && len + 1U < sizeof(tab)) {
        tab[len] = (char)tolower((unsigned char)arg[len]);
        len++;
    }
    if (len == 0 || (arg[len] != '\0' && !isspace((unsigned char)arg[len]))) {
        err_result("ui tab", "INVALID_TAB",
                   "usage: ui tab <home|messages|nodes|map|packets|settings>");
        return;
    }
    esp_err_t ret = d1l_ui_phase1_request_tab(tab);
    if (ret != ESP_OK) {
        err_result("ui tab", esp_err_to_name(ret),
                   "usage: ui tab <home|messages|nodes|map|packets|settings>");
        return;
    }
    ok_begin("ui tab");
    printf(",\"requested_tab\":");
    print_json_string(tab);
    printf(",\"pending\":true,\"active_tab\":");
    print_json_string(d1l_ui_phase1_active_tab_name());
    printf("}\n");
}

static void cmd_ui_scroll_probe(const char *line)
{
    const char *arg = line + strlen("ui scroll-probe ");
    while (*arg == ' ') {
        arg++;
    }
    char surface[24] = {0};
    size_t len = 0;
    while (arg[len] != '\0' && !isspace((unsigned char)arg[len]) &&
           len + 1U < sizeof(surface)) {
        surface[len] = (char)tolower((unsigned char)arg[len]);
        len++;
    }
    if (len == 0 || (arg[len] != '\0' && !isspace((unsigned char)arg[len]))) {
        err_result("ui scroll-probe", "INVALID_SURFACE",
                   "usage: ui scroll-probe <home|public_messages|dm_thread|nodes|contact_detail|contact_options|contact_forget|contact_route|mesh_roles|mesh_rooms|mesh_repeaters|packets|settings|storage|storage_card|storage_data|wifi|map|map_options|map_location|map_cache>");
        return;
    }

    d1l_ui_scroll_probe_result_t probe = {0};
    esp_err_t ret = d1l_ui_phase1_scroll_probe(surface, &probe);
    if (ret != ESP_OK) {
        err_result("ui scroll-probe", esp_err_to_name(ret),
                   "usage: ui scroll-probe <home|public_messages|dm_thread|nodes|contact_detail|contact_options|contact_forget|contact_route|mesh_roles|mesh_rooms|mesh_repeaters|packets|settings|storage|storage_card|storage_data|wifi|map|map_options|map_location|map_cache>");
        return;
    }

    printf("{\"schema\":%d,\"ok\":%s,\"cmd\":\"ui scroll-probe\",\"surface\":",
           D1L_CONSOLE_SCHEMA, bool_json(probe.ok));
    print_json_string(probe.surface);
    printf(",\"tab\":");
    print_json_string(probe.tab);
    printf(",\"surface_supported\":%s,\"target_found\":%s,\"scrollable\":%s,\"moved\":%s",
           bool_json(probe.surface_supported), bool_json(probe.target_found),
           bool_json(probe.scrollable), bool_json(probe.moved));
    printf(",\"before_y\":%ld,\"after_y\":%ld",
           (long)probe.before_y, (long)probe.after_y);
    printf(",\"scroll_top_before\":%ld,\"scroll_bottom_before\":%ld",
           (long)probe.scroll_top_before, (long)probe.scroll_bottom_before);
    printf(",\"scroll_top_after\":%ld,\"scroll_bottom_after\":%ld}\n",
           (long)probe.scroll_top_after, (long)probe.scroll_bottom_after);
}

static void cmd_ui_compose_probe(const char *line)
{
    const char *arg = line + strlen("ui compose-probe ");
    while (*arg == ' ') {
        arg++;
    }
    char target[16] = {0};
    size_t len = 0;
    while (arg[len] != '\0' && !isspace((unsigned char)arg[len]) &&
           len + 1U < sizeof(target)) {
        target[len] = (char)tolower((unsigned char)arg[len]);
        if (target[len] == '-') {
            target[len] = '_';
        }
        len++;
    }
    if (len == 0 || (arg[len] != '\0' && !isspace((unsigned char)arg[len]))) {
        err_result("ui compose-probe", "INVALID_TARGET",
                   "usage: ui compose-probe <public|public-long|dm|dm-long|public-search|packet-search|contact-edit|onboarding|map-location|wifi-ssid|wifi-password>");
        return;
    }

    d1l_ui_compose_probe_result_t probe = {0};
    esp_err_t ret = d1l_ui_phase1_compose_probe(target, &probe);
    if (ret != ESP_OK) {
        err_result("ui compose-probe", esp_err_to_name(ret),
                   "usage: ui compose-probe <public|public-long|dm|dm-long|public-search|packet-search|contact-edit|onboarding|map-location|wifi-ssid|wifi-password>");
        return;
    }

    printf("{\"schema\":%d,\"ok\":%s,\"cmd\":\"ui compose-probe\",\"target\":",
           D1L_CONSOLE_SCHEMA, bool_json(probe.ok));
    print_json_string(probe.target);
    printf(",\"target_supported\":%s,\"sheet_visible\":%s,\"textarea_visible\":%s,"
           "\"keyboard_visible\":%s,\"onboarding_visible\":%s,"
           "\"dock_hidden\":%s,\"dm_mode\":%s,\"active_tab\":",
           bool_json(probe.target_supported),
           bool_json(probe.sheet_visible),
           bool_json(probe.textarea_visible),
           bool_json(probe.keyboard_visible),
           bool_json(probe.onboarding_visible),
           bool_json(probe.dock_hidden),
           bool_json(probe.dm_mode));
    print_json_string(probe.active_tab);
    printf(",\"sheet\":{\"x\":%ld,\"y\":%ld,\"w\":%ld,\"h\":%ld},"
           "\"textarea\":{\"x\":%ld,\"y\":%ld,\"w\":%ld,\"h\":%ld},"
           "\"keyboard\":{\"x\":%ld,\"y\":%ld,\"w\":%ld,\"h\":%ld},"
           "\"public_rf_tx\":false,\"formats_sd\":false}\n",
           (long)probe.sheet_x, (long)probe.sheet_y,
           (long)probe.sheet_w, (long)probe.sheet_h,
           (long)probe.textarea_x, (long)probe.textarea_y,
           (long)probe.textarea_w, (long)probe.textarea_h,
           (long)probe.keyboard_x, (long)probe.keyboard_y,
           (long)probe.keyboard_w, (long)probe.keyboard_h);
}

static void cmd_identity_status(void)
{
    esp_err_t ret = d1l_meshcore_service_ensure_identity();
    if (ret != ESP_OK) {
        err_result("identity status", esp_err_to_name(ret), "could not generate or persist MeshCore local identity");
        return;
    }
    const d1l_settings_t *settings = d1l_settings_current();
    char fingerprint[17] = {0};
    hex_prefix(fingerprint, sizeof(fingerprint), settings->identity_public_key, 8U);
    ok_begin("identity status");
    printf(",\"node_name\":\"%s\",\"role\":\"%s\",\"meshcore_local_identity\":\"stored_nvs_ed25519\",\"public_key_ready\":%s,\"fingerprint\":\"%s\"}\n",
           settings->node_name, d1l_settings_role_name(settings->role),
           bool_json(settings->identity_ready), fingerprint);
}

static void cmd_i2c(void)
{
    d1l_board_status_t scan = *d1l_board_status();
    esp_err_t ret = d1l_board_i2c_scan(&scan);
    if (ret != ESP_OK) {
        err_result("i2c", esp_err_to_name(ret), "I2C scan failed; verify SDA GPIO39, SCL GPIO40, and D1L power");
        return;
    }
    ok_begin("i2c");
    printf(",\"count\":%u,\"addresses\":[", scan.i2c_count);
    for (uint8_t i = 0; i < scan.i2c_count; ++i) {
        printf("%s\"0x%02X\"", i ? "," : "", scan.i2c_addresses[i]);
    }
    printf("],\"expected\":[\"0x20\",\"0x48\"]}\n");
}

static void cmd_display_test(void)
{
    esp_err_t ret = d1l_board_display_color_test();
    if (ret != ESP_OK) {
        err_result("display test", esp_err_to_name(ret), "display test requires initialized D1L RGB panel");
        return;
    }
    ok_begin("display test");
    printf(",\"pattern\":\"rgb_white_black_yellow_bars\",\"manual_confirm\":true}\n");
}

static void cmd_touch_test(void)
{
    d1l_board_touch_state_t touch = {0};
    esp_err_t ret = d1l_board_touch_read(&touch);
    if (ret != ESP_OK) {
        printf("{\"schema\":%d,\"ok\":false,\"cmd\":\"touch test\",\"code\":\"%s\","
               "\"init_result\":\"%s\",\"read_result\":\"%s\",\"init_attempts\":%lu,"
               "\"hint\":\"touch sample requires initialized D1L touch controller\"}\n",
               D1L_CONSOLE_SCHEMA, esp_err_to_name(ret),
               esp_err_to_name(touch.init_result), esp_err_to_name(touch.read_result),
               (unsigned long)touch.init_attempts);
        return;
    }
    ok_begin("touch test");
    printf(",\"touches\":%u,\"pressed\":%s,\"x\":%u,\"y\":%u,\"raw_x\":%ld,\"raw_y\":%ld,"
           "\"init_result\":\"%s\",\"read_result\":\"%s\",\"init_attempts\":%lu,"
           "\"coordinate_valid\":%s,\"controller\":\"FT5x06\",\"i2c_address\":\"0x48\","
           "\"manual_confirm\":true}\n",
           touch.touches, bool_json(touch.pressed), touch.x, touch.y,
           (long)touch.raw_x, (long)touch.raw_y,
           esp_err_to_name(touch.init_result), esp_err_to_name(touch.read_result),
           (unsigned long)touch.init_attempts,
           bool_json(touch.coordinate_valid));
}

static void cmd_touch_raw(void)
{
    d1l_board_touch_raw_state_t raw = {0};
    esp_err_t ret = d1l_board_touch_raw_read(&raw);
    if (ret != ESP_OK) {
        printf("{\"schema\":%d,\"ok\":false,\"cmd\":\"touch raw\",\"code\":\"%s\","
               "\"init_result\":\"%s\",\"read_result\":\"%s\",\"init_attempts\":%lu,"
               "\"hint\":\"raw FT5x06 register read failed\"}\n",
               D1L_CONSOLE_SCHEMA, esp_err_to_name(ret),
               esp_err_to_name(raw.init_result), esp_err_to_name(raw.read_result),
               (unsigned long)raw.init_attempts);
        return;
    }
    ok_begin("touch raw");
    printf(",\"init_result\":\"%s\",\"read_result\":\"%s\",\"init_attempts\":%lu,"
           "\"controller\":\"FT5x06\",\"i2c_address\":\"0x48\","
           "\"touch_points_raw\":%u,\"touch_count\":%u,\"event_flag\":%u,"
           "\"touch_id\":%u,\"raw_x\":%u,\"raw_y\":%u",
           esp_err_to_name(raw.init_result), esp_err_to_name(raw.read_result),
           (unsigned long)raw.init_attempts, raw.touch_points_raw, raw.touch_count,
           raw.event_flag, raw.touch_id, raw.raw_x, raw.raw_y);
    print_hex_field("registers_00_1f", raw.registers_00_1f, sizeof(raw.registers_00_1f));
    print_hex_field("config_80_89", raw.config_80_89, sizeof(raw.config_80_89));
    print_hex_field("id_a1_a9", raw.id_a1_a9, sizeof(raw.id_a1_a9));
    printf("}\n");
}

static void cmd_button(void)
{
    ok_begin("button");
    printf(",\"pressed\":%s,\"user_gpio\":38,\"active_level\":0,\"board_ready\":%s}\n",
           d1l_board_button_pressed() ? "true" : "false",
           d1l_board_status()->ready ? "true" : "false");
}

static void cmd_backlight(const char *line)
{
    const char *arg = line + strlen("backlight");
    while (*arg == ' ') {
        arg++;
    }
    int percent = atoi(arg);
    esp_err_t ret = d1l_backlight_set_percent(percent);
    if (ret != ESP_OK) {
        err_result("backlight", esp_err_to_name(ret), "usage: backlight <0-100>");
        return;
    }
    ok_begin("backlight");
    printf(",\"percent\":%d}\n", percent);
}

static void cmd_radiohw(void)
{
    d1l_radiohw_status_t status;
    esp_err_t ret = d1l_sx1262_probe(&status);
    if (ret != ESP_OK) {
        printf("{\"schema\":%d,\"ok\":false,\"cmd\":\"radiohw\",\"code\":\"%s\",\"sx1262\":{\"present\":false,\"expander_ready\":%s,\"busy\":%d,\"dio1\":%d,\"ver_pin\":%d,\"tcxo_default\":\"NONE\"}}\n",
               D1L_CONSOLE_SCHEMA, status.failure_code ? status.failure_code : esp_err_to_name(ret),
               status.expander_ready ? "true" : "false", status.busy, status.dio1, status.ver_pin);
        return;
    }
    ok_begin("radiohw");
    printf(",\"sx1262\":{\"present\":%s,\"status_byte\":%u,\"busy\":%d,\"dio1\":%d,\"ver_pin\":%d,\"tcxo_default\":\"NONE\"}}\n",
           status.present ? "true" : "false", status.status_byte, status.busy, status.dio1, status.ver_pin);
}

static void print_radio_profile_result(const char *cmd)
{
    d1l_radio_profile_t profile = d1l_settings_radio_profile(NULL);
    d1l_meshcore_service_status_t mesh = d1l_meshcore_service_status();
    ok_begin(cmd);
    printf(",\"profile_id\":\"%s\",\"region\":\"%s\",\"frequency_hz\":%lu,\"bandwidth_khz\":%.1f,\"sf\":%u,\"cr\":%u,\"tx_power_dbm\":%d,\"tcxo\":\"%s\",\"rx_boost\":%s,\"persisted\":true,\"applied_to_radio\":%s,\"radio_apply_pending\":%s,\"radio_apply_error\":\"%s\"}\n",
           profile.profile_id, profile.region_label, (unsigned long)profile.frequency_hz,
           profile.bandwidth_khz, profile.spreading_factor, profile.coding_rate,
           profile.tx_power_dbm, profile.tcxo, bool_json(profile.rx_boost),
           bool_json(mesh.radio_applied), bool_json(mesh.radio_apply_pending),
           esp_err_to_name(mesh.radio_apply_error));
}

static void cmd_radio_get(void)
{
    print_radio_profile_result("radio get");
}

static void cmd_radio_set_preset_uscan(void)
{
    d1l_settings_t settings = *d1l_settings_current();
    const d1l_radio_profile_t *defaults = d1l_radio_profile_uscan_default();
    settings.frequency_hz = defaults->frequency_hz;
    settings.bandwidth_tenths_khz = (uint16_t)((defaults->bandwidth_khz * 10.0f) + 0.5f);
    settings.spreading_factor = defaults->spreading_factor;
    settings.coding_rate = defaults->coding_rate;
    settings.tx_power_dbm = defaults->tx_power_dbm;
    settings.rx_boost = defaults->rx_boost;
    settings.tcxo_mode = D1L_TCXO_NONE;
    esp_err_t ret = d1l_settings_save(&settings);
    if (ret != ESP_OK) {
        err_result("radio set preset uscan", esp_err_to_name(ret), "could not persist Canada/USA radio defaults");
        return;
    }
    print_radio_profile_result("radio set preset uscan");
}

static void cmd_radio_set_freq(const char *line)
{
    double mhz = atof(line + strlen("radio set freq "));
    if (mhz < 902.0 || mhz > 928.0) {
        err_result("radio set freq", "INVALID_FREQ", "Canada/USA D1L range is 902.000-928.000 MHz");
        return;
    }
    d1l_settings_t settings = *d1l_settings_current();
    settings.frequency_hz = (uint32_t)((mhz * 1000000.0) + 0.5);
    esp_err_t ret = d1l_settings_save(&settings);
    if (ret != ESP_OK) {
        err_result("radio set freq", esp_err_to_name(ret), "could not persist frequency");
        return;
    }
    print_radio_profile_result("radio set freq");
}

static void cmd_radio_set_bw(const char *line)
{
    double khz = atof(line + strlen("radio set bw "));
    if (khz < 7.8 || khz > 500.0) {
        err_result("radio set bw", "INVALID_BW", "usage: radio set bw <7.8-500.0>");
        return;
    }
    d1l_settings_t settings = *d1l_settings_current();
    settings.bandwidth_tenths_khz = (uint16_t)((khz * 10.0) + 0.5);
    esp_err_t ret = d1l_settings_save(&settings);
    if (ret != ESP_OK) {
        err_result("radio set bw", esp_err_to_name(ret), "could not persist bandwidth");
        return;
    }
    print_radio_profile_result("radio set bw");
}

static void cmd_radio_set_sf(const char *line)
{
    int sf = atoi(line + strlen("radio set sf "));
    if (sf < 5 || sf > 12) {
        err_result("radio set sf", "INVALID_SF", "usage: radio set sf <5-12>");
        return;
    }
    d1l_settings_t settings = *d1l_settings_current();
    settings.spreading_factor = (uint8_t)sf;
    esp_err_t ret = d1l_settings_save(&settings);
    if (ret != ESP_OK) {
        err_result("radio set sf", esp_err_to_name(ret), "could not persist spreading factor");
        return;
    }
    print_radio_profile_result("radio set sf");
}

static void cmd_radio_set_cr(const char *line)
{
    int cr = atoi(line + strlen("radio set cr "));
    if (cr < 5 || cr > 8) {
        err_result("radio set cr", "INVALID_CR", "usage: radio set cr <5-8>");
        return;
    }
    d1l_settings_t settings = *d1l_settings_current();
    settings.coding_rate = (uint8_t)cr;
    esp_err_t ret = d1l_settings_save(&settings);
    if (ret != ESP_OK) {
        err_result("radio set cr", esp_err_to_name(ret), "could not persist coding rate");
        return;
    }
    print_radio_profile_result("radio set cr");
}

static void cmd_radio_set_txpower(const char *line)
{
    int tx_power = atoi(line + strlen("radio set txpower "));
    if (tx_power < -9 || tx_power > D1L_RADIO_TX_POWER_DBM) {
        err_result("radio set txpower", "INVALID_TX_POWER", "usage: radio set txpower <-9-20>");
        return;
    }
    d1l_settings_t settings = *d1l_settings_current();
    settings.tx_power_dbm = (int8_t)tx_power;
    esp_err_t ret = d1l_settings_save(&settings);
    if (ret != ESP_OK) {
        err_result("radio set txpower", esp_err_to_name(ret), "could not persist TX power");
        return;
    }
    print_radio_profile_result("radio set txpower");
}

static void cmd_radio_set_rxboost(const char *line)
{
    const char *arg = line + strlen("radio set rxboost ");
    while (*arg == ' ') {
        arg++;
    }
    bool enabled = false;
    if (strcmp(arg, "1") == 0 || strcmp(arg, "on") == 0 || strcmp(arg, "true") == 0) {
        enabled = true;
    } else if (strcmp(arg, "0") == 0 || strcmp(arg, "off") == 0 || strcmp(arg, "false") == 0) {
        enabled = false;
    } else {
        err_result("radio set rxboost", "INVALID_RX_BOOST", "usage: radio set rxboost <0|1>");
        return;
    }
    d1l_settings_t settings = *d1l_settings_current();
    settings.rx_boost = enabled;
    esp_err_t ret = d1l_settings_save(&settings);
    if (ret != ESP_OK) {
        err_result("radio set rxboost", esp_err_to_name(ret), "could not persist RX boost");
        return;
    }
    print_radio_profile_result("radio set rxboost");
}

static void cmd_mesh_status(void)
{
    d1l_meshcore_service_status_t status = d1l_meshcore_service_status();
    ok_begin("mesh status");
    printf(",\"phase\":\"phase2_public_rf\",\"state\":\"%s\",\"radio_profile\":\"uscan-meshcore-default\",\"identity_ready\":%s,\"radio_ready\":%s,\"companion_framing_ready\":%s,\"path_hash_bytes\":%u,\"rx_packets\":%lu,\"rx_adverts\":%lu,\"tx_packets\":%lu,\"rejected_commands\":%lu,\"note\":\"Public group text TX/RX and signed advert TX/RX enabled for local RF validation\"}\n",
           d1l_meshcore_service_state_name(status.state), bool_json(status.identity_ready),
           bool_json(status.radio_ready), bool_json(status.companion_framing_ready),
           status.path_hash_bytes, (unsigned long)status.rx_packets,
           (unsigned long)status.rx_adverts, (unsigned long)status.tx_packets,
           (unsigned long)status.rejected_commands);
}

static void cmd_mesh_advert(const char *cmd, bool flood)
{
    esp_err_t ret = d1l_meshcore_service_request_advert(flood);
    if (ret != ESP_OK) {
        err_result(cmd,
                   ret == ESP_ERR_INVALID_STATE ? "MESHCORE_TX_BUSY_OR_RADIO_NOT_READY" :
                   ret == ESP_ERR_NOT_SUPPORTED ? "UNSUPPORTED_RADIO_PROFILE" :
                   "MESHCORE_ADVERT_FAILED",
                   "verify identity, radio profile, and TX state before retrying");
        return;
    }
    ok_begin(cmd);
    printf(",\"queued\":true,\"flood\":%s}\n", bool_json(flood));
}

static void cmd_mesh_send_public(const char *line)
{
    const char *text = line + strlen("mesh send public ");
    esp_err_t ret = d1l_meshcore_service_send_public(text);
    if (ret != ESP_OK) {
        err_result("mesh send public",
                   ret == ESP_ERR_INVALID_ARG ? "EMPTY_MESSAGE" :
                   ret == ESP_ERR_INVALID_SIZE ? "MESSAGE_TOO_LONG" :
                   ret == ESP_ERR_INVALID_STATE ? "MESHCORE_TX_BUSY_OR_RADIO_NOT_READY" :
                   ret == ESP_ERR_NOT_SUPPORTED ? "UNSUPPORTED_RADIO_PROFILE" : "MESHCORE_SEND_FAILED",
                   ret == ESP_ERR_INVALID_SIZE ? "max 138 characters" :
                   "verify radio profile is US/CAN 910.525 BW62.5 SF7 CR5 and try again");
        return;
    }
    ok_begin("mesh send public");
    printf(",\"queued\":true}\n");
}

static void cmd_companion_status(void)
{
    d1l_connectivity_status_t connectivity = {0};
    d1l_connectivity_status(&connectivity);
    ok_begin("companion status");
    printf(",\"usb_console\":\"ready\",\"framing\":\"meshcore-3byte\",\"header_bytes\":%u,\"app_to_radio\":\"%c\",\"radio_to_app\":\"%c\",\"length\":\"uint16_le\",\"max_payload_bytes\":%u,\"transport_codec\":\"ready\",\"meshcore_bridge\":\"ready\",\"wifi\":{\"setting_enabled\":%s,\"build_enabled\":%s,\"state\":\"%s\"},\"ble\":{\"setting_enabled\":%s,\"build_enabled\":%s,\"state\":\"%s\"},\"coexistence_policy\":\"%s\",\"path_hash_bytes_supported\":[1,2,3],\"default_path_hash_bytes\":1}\n",
           D1L_COMPANION3_HEADER_SIZE, D1L_COMPANION3_APP_TO_RADIO,
           D1L_COMPANION3_RADIO_TO_APP, D1L_COMPANION3_MAX_FRAME_SIZE,
           bool_json(connectivity.wifi_enabled_setting),
           bool_json(connectivity.wifi_build_enabled), connectivity.wifi_state,
           bool_json(connectivity.ble_companion_enabled_setting),
           bool_json(connectivity.ble_build_enabled), connectivity.ble_state,
           connectivity.coexistence_policy);
}

static void cmd_rp2040_status(void)
{
    d1l_rp2040_status_t status = {0};
    esp_err_t ret = d1l_rp2040_bridge_status(&status);
    ok_begin("rp2040 status");
    printf(",\"uart_ready\":%s,\"init_result\":\"%s\",\"uart_port\":%d,\"tx_gpio\":%d,\"rx_gpio\":%d,\"baud\":%d,\"reset_expander_pin\":%u,\"buffered_bytes\":%lu,\"hardware_validated\":false}\n",
           status.uart_ready ? "true" : "false", esp_err_to_name(ret), status.uart_port,
           status.tx_gpio, status.rx_gpio, status.baud_rate, status.reset_expander_pin,
           (unsigned long)status.buffered_bytes);
}

static void cmd_rp2040_set_baud(const char *line)
{
    const char *args = line + strlen("rp2040 set-baud ");
    while (*args == ' ') {
        args++;
    }
    uint32_t baud = 0;
    if (!parse_next_u32_arg(&args, &baud) || *args != '\0') {
        err_result("rp2040 set-baud", "INVALID_ARG",
                   "usage: rp2040 set-baud <9600|38400|57600|115200|230400|460800|921600|1000000>");
        return;
    }
    esp_err_t ret = d1l_rp2040_bridge_set_baud(baud);
    if (ret != ESP_OK) {
        err_result("rp2040 set-baud", esp_err_to_name(ret),
                   "could not set RP2040 UART baud; supported rates are bounded for safe probing");
        return;
    }
    d1l_rp2040_status_t status = {0};
    (void)d1l_rp2040_bridge_status(&status);
    ok_begin("rp2040 set-baud");
    printf(",\"baud\":%lu,\"uart_ready\":%s,\"public_rf_tx\":false,\"formats_sd\":false,\"sd_touched\":false}\n",
           (unsigned long)status.baud_rate,
           bool_json(status.uart_ready));
}

static void cmd_rp2040_baud_probe(const char *line)
{
    uint32_t timeout_ms = 700U;
    const char *args = line + strlen("rp2040 baud-probe");
    while (*args == ' ') {
        args++;
    }
    if (*args != '\0') {
        if (!parse_next_u32_arg(&args, &timeout_ms) || *args != '\0' ||
            timeout_ms < 100U || timeout_ms > 3000U) {
            err_result("rp2040 baud-probe", "INVALID_ARG",
                       "usage: rp2040 baud-probe [timeout_ms 100-3000]");
            return;
        }
    }

    d1l_rp2040_baud_probe_t *probe = calloc(1, sizeof(*probe));
    if (probe == NULL) {
        err_result("rp2040 baud-probe", "ESP_ERR_NO_MEM",
                   "could not allocate RP2040 baud probe report");
        return;
    }

    esp_err_t ret = d1l_rp2040_bridge_baud_probe(probe, timeout_ms);
    d1l_rp2040_status_t status = {0};
    (void)d1l_rp2040_bridge_status(&status);
    printf("{\"schema\":%d,\"ok\":%s,\"cmd\":\"rp2040 baud-probe\",\"code\":\"%s\","
           "\"bridge_ready\":%s,\"found_deskos\":%s,\"found_stock\":%s,"
           "\"selected_baud\":%lu,\"original_baud\":%lu,\"restored_baud\":%lu,"
           "\"timeout_ms\":%lu,\"tested_count\":%lu,"
           "\"public_rf_tx\":false,\"formats_sd\":false,\"sd_touched\":false,"
           "\"results\":[",
           D1L_CONSOLE_SCHEMA,
           bool_json(ret == ESP_OK),
           esp_err_to_name(ret),
           bool_json(probe->bridge_ready),
           bool_json(probe->found_deskos),
           bool_json(probe->found_stock),
           (unsigned long)probe->selected_baud,
           (unsigned long)probe->original_baud,
           (unsigned long)status.baud_rate,
           (unsigned long)timeout_ms,
           (unsigned long)probe->tested_count);
    for (uint32_t i = 0; i < probe->tested_count && i < D1L_RP2040_BAUD_PROBE_MAX_RESULTS; ++i) {
        const d1l_rp2040_baud_probe_result_t *result = &probe->results[i];
        if (i > 0U) {
            putchar(',');
        }
        printf("{\"baud\":%lu,\"deskos_ping_ok\":%s,\"ping_code\":\"%s\","
               "\"stock_response_seen\":%s,\"stock_json_seen\":%s,"
               "\"stock_code\":\"%s\",\"stock_bytes_read\":%lu,"
               "\"stock_response_truncated\":%s,\"stock_response_hex\":",
               (unsigned long)result->baud,
               bool_json(result->deskos_ping_ok),
               esp_err_to_name(result->ping_error),
               bool_json(result->stock_response_seen),
               bool_json(result->stock_json_seen),
               esp_err_to_name(result->stock_error),
               (unsigned long)result->stock_bytes_read,
               bool_json(result->stock_response_truncated));
        print_json_string(result->stock_response_hex);
        printf(",\"stock_response_ascii\":");
        print_json_string(result->stock_response_ascii);
        putchar('}');
    }
    printf("]}\n");
    free(probe);
}

static void cmd_rp2040_reset(void)
{
    const uint32_t hold_ms = 100U;
    const uint32_t settle_ms = 8000U;
    esp_err_t ret = d1l_rp2040_bridge_reset(hold_ms, settle_ms);
    if (ret != ESP_OK) {
        err_result("rp2040 reset", esp_err_to_name(ret),
                   "could not toggle RP2040 reset through the D1L IO expander");
        return;
    }
    d1l_rp2040_status_t status = {0};
    (void)d1l_rp2040_bridge_status(&status);
    ok_begin("rp2040 reset");
    printf(",\"reset_expander_pin\":%u,\"hold_ms\":%lu,\"settle_ms\":%lu,\"uart_ready\":%s,\"public_rf_tx\":false,\"formats_sd\":false}\n",
           status.reset_expander_pin,
           (unsigned long)hold_ms,
           (unsigned long)settle_ms,
           status.uart_ready ? "true" : "false");
}

static void cmd_rp2040_double_reset(const char *line)
{
    uint32_t hold_ms = 50U;
    uint32_t gap_ms = 150U;
    uint32_t settle_ms = 1500U;
    const char *args = line + strlen("rp2040 double-reset");
    while (*args == ' ') {
        args++;
    }
    if (*args != '\0') {
        if (!parse_next_u32_arg(&args, &hold_ms) ||
            !parse_next_u32_arg(&args, &gap_ms)) {
            err_result(line, "INVALID_ARG",
                       "usage: rp2040 double-reset [hold_ms gap_ms [settle_ms]]");
            return;
        }
        if (*args != '\0' && !parse_next_u32_arg(&args, &settle_ms)) {
            err_result(line, "INVALID_ARG",
                       "usage: rp2040 double-reset [hold_ms gap_ms [settle_ms]]");
            return;
        }
        if (*args != '\0') {
            err_result(line, "INVALID_ARG",
                       "usage: rp2040 double-reset [hold_ms gap_ms [settle_ms]]");
            return;
        }
    }
    if (hold_ms < 10U || hold_ms > 250U ||
        gap_ms < 25U || gap_ms > 1000U ||
        settle_ms < 500U || settle_ms > 5000U) {
        err_result(line, "INVALID_ARG",
                   "bounds: hold 10-250 ms, gap 25-1000 ms, settle 500-5000 ms");
        return;
    }
    esp_err_t ret = d1l_rp2040_bridge_double_reset(hold_ms, gap_ms, settle_ms);
    if (ret != ESP_OK) {
        err_result(line, esp_err_to_name(ret),
                   "could not perform RP2040 double reset through the D1L IO expander");
        return;
    }
    d1l_rp2040_status_t status = {0};
    (void)d1l_rp2040_bridge_status(&status);
    ok_begin(line);
    printf(",\"reset_expander_pin\":%u,\"hold_ms\":%lu,\"gap_ms\":%lu,\"settle_ms\":%lu,\"uart_ready\":%s,\"public_rf_tx\":false,\"formats_sd\":false,\"sd_touched\":false}\n",
           status.reset_expander_pin,
           (unsigned long)hold_ms,
           (unsigned long)gap_ms,
           (unsigned long)settle_ms,
           status.uart_ready ? "true" : "false");
}

static void cmd_rp2040_ping(void)
{
    d1l_rp2040_ping_t ping = {0};
    esp_err_t ret = d1l_rp2040_bridge_ping(&ping, D1L_STORAGE_RP2040_SD_BOOT_PROBE_TIMEOUT_MS);
    if (ret == ESP_OK && ping.bridge_ready && ping.protocol_supported) {
        d1l_storage_status_note_valid_bridge_response();
    }
    printf("{\"schema\":%d,\"ok\":%s,\"cmd\":\"rp2040 ping\",\"code\":\"%s\",\"bridge_ready\":%s,\"protocol_supported\":%s,\"response_truncated\":%s,\"version\":%lu,\"file_line_max\":%lu,\"file_chunk_max\":%lu,\"path_max\":%lu,\"atomic_rename\":%s,\"sd_touched\":%s,\"public_rf_tx\":false,\"formats_sd\":false,\"note\":",
           D1L_CONSOLE_SCHEMA,
           bool_json(ret == ESP_OK),
           esp_err_to_name(ret),
           bool_json(ping.bridge_ready),
           bool_json(ping.protocol_supported),
           bool_json(ping.response_truncated),
           (unsigned long)ping.protocol_version,
           (unsigned long)ping.file_line_max,
           (unsigned long)ping.file_chunk_max,
           (unsigned long)ping.path_max,
           bool_json(ping.atomic_rename_supported),
           bool_json(ping.sd_touched));
    print_json_string(ping.note[0] ? ping.note : "");
    printf("}\n");
}

static void cmd_rp2040_bootloader(void)
{
    esp_err_t ret = d1l_rp2040_bridge_enter_bootloader(D1L_STORAGE_RP2040_SD_BOOT_PROBE_TIMEOUT_MS);
    printf("{\"schema\":%d,\"ok\":%s,\"cmd\":\"rp2040 bootloader\",\"code\":\"%s\",\"public_rf_tx\":false,\"formats_sd\":false,\"sd_touched\":false,\"note\":",
           D1L_CONSOLE_SCHEMA,
           bool_json(ret == ESP_OK),
           esp_err_to_name(ret));
    print_json_string(ret == ESP_OK ?
                      "RP2040 bridge accepted UF2 bootloader request" :
                      "RP2040 bridge did not accept UF2 bootloader request");
    printf("}\n");
}

static void cmd_rp2040_stock_probe(void)
{
    d1l_rp2040_stock_probe_t probe = {0};
    esp_err_t ret = d1l_rp2040_bridge_stock_probe(&probe, D1L_STORAGE_RP2040_SD_BOOT_PROBE_TIMEOUT_MS);
    printf("{\"schema\":%d,\"ok\":%s,\"cmd\":\"rp2040 stock-probe\",\"code\":\"%s\",\"bridge_ready\":%s,\"response_seen\":%s,\"json_seen\":%s,\"response_truncated\":%s,\"bytes_read\":%lu,\"sent_cobs_hex\":",
           D1L_CONSOLE_SCHEMA,
           bool_json(ret == ESP_OK),
           esp_err_to_name(ret),
           bool_json(probe.bridge_ready),
           bool_json(probe.response_seen),
           bool_json(probe.json_seen),
           bool_json(probe.response_truncated),
           (unsigned long)probe.bytes_read);
    print_json_string(probe.sent_cobs_hex);
    printf(",\"response_hex\":");
    print_json_string(probe.response_hex);
    printf(",\"response_ascii\":");
    print_json_string(probe.response_ascii);
    printf(",\"public_rf_tx\":false,\"formats_sd\":false,\"sd_touched\":false,\"note\":");
    print_json_string(probe.note[0] ? probe.note : "");
    printf("}\n");
}

static void cmd_storage_status(void)
{
    d1l_storage_status_t status = {0};
    d1l_storage_status(&status);
    d1l_connectivity_status_t connectivity = {0};
    d1l_connectivity_status(&connectivity);
    ok_begin("storage status");
    printf(",\"manager\":{\"running\":%s,\"state\":",
           bool_json(status.manager_running));
    print_json_string(status.manager_state ? status.manager_state : "BRIDGE_WAIT");
    printf(",\"attempt\":%lu,\"backoff_ms\":%lu,\"force_nvs\":%s,"
           "\"initial_ping_timeouts\":%lu,\"bridge_response_seen\":%s,"
           "\"auto_reset_pending\":%s,\"auto_reset_attempted\":%s},\"sd\":{\"state\":",
           (unsigned long)status.manager_attempt,
           (unsigned long)status.manager_backoff_ms,
           bool_json(status.force_nvs),
           (unsigned long)status.manager_initial_ping_timeouts,
           bool_json(status.manager_bridge_response_seen),
           bool_json(status.manager_auto_reset_pending),
           bool_json(status.manager_auto_reset_attempted));
    print_json_string(status.sd_state ? status.sd_state : "unknown");
    printf(",\"interface\":");
    print_json_string(status.sd_interface ? status.sd_interface : "unknown");
    printf(",\"filesystem\":");
    print_json_string(status.sd_filesystem ? status.sd_filesystem : "unknown");
    printf(",\"direct_supported\":%s,\"rp2040_bridge_required\":%s,\"rp2040_bridge_ready\":%s,\"rp2040_protocol_supported\":%s,\"present\":%s,\"presence_stale\":%s,\"mounted\":%s,\"data_root_ready\":%s,\"needs_fat32\":%s,\"capacity_kb\":%lu,\"free_kb\":%lu,\"file_ops\":%s,\"file_line_max\":%lu,\"file_chunk_max\":%lu,\"path_max\":%lu,\"atomic_rename\":%s,\"response_truncated\":%s,\"status_stale\":%s,\"refresh_failures\":%lu,\"mount_point\":",
           bool_json(status.direct_supported),
           bool_json(status.rp2040_bridge_required),
           bool_json(status.rp2040_bridge_ready),
           bool_json(status.rp2040_sd_protocol_supported),
           bool_json(status.sd_present),
           bool_json(status.sd_presence_stale),
           bool_json(status.sd_mounted),
           bool_json(status.sd_data_root_ready),
           bool_json(status.sd_needs_fat32),
           (unsigned long)status.capacity_kb,
           (unsigned long)status.free_kb,
           bool_json(status.file_ops_supported),
           (unsigned long)status.file_line_max,
           (unsigned long)status.file_chunk_max,
           (unsigned long)status.path_max,
           bool_json(status.atomic_rename_supported),
           bool_json(status.response_truncated),
           bool_json(status.bridge_status_stale),
           (unsigned long)status.bridge_status_refresh_failures);
    print_json_string(status.mount_point ? status.mount_point : "");
    printf(",\"data_root\":");
    print_json_string(status.data_root ? status.data_root : "");
    printf(",\"probe_power\":");
    print_json_string(status.sd_probe_power[0] ? status.sd_probe_power : "unknown");
    printf(",\"probe_mode\":");
    print_json_string(status.sd_probe_mode[0] ? status.sd_probe_mode : "unknown");
    printf(",\"probe_error\":%lu,\"probe_data\":%lu,\"mount_error\":%lu,\"mount_data\":%lu,\"last_error\":\"%s\"},\"data_enabled\":%s,\"data_backend\":",
           (unsigned long)status.sd_probe_error,
           (unsigned long)status.sd_probe_data,
           (unsigned long)status.sd_mount_error,
           (unsigned long)status.sd_mount_data,
           esp_err_to_name(status.last_error), bool_json(status.data_enabled));
    print_json_string(status.data_backend ? status.data_backend : "nvs");
    printf(",\"message_store_backend\":");
    print_json_string(status.message_store_backend ? status.message_store_backend : "nvs");
    printf(",\"dm_store_backend\":");
    print_json_string(status.dm_store_backend ? status.dm_store_backend : "nvs");
    printf(",\"packet_log_backend\":");
    print_json_string(status.packet_log_backend ? status.packet_log_backend : "nvs");
    printf(",\"route_store_backend\":");
    print_json_string(status.route_store_backend ? status.route_store_backend : "nvs");
    printf(",\"map_tile_backend\":");
    print_json_string(status.map_tile_backend ? status.map_tile_backend : "unavailable");
    printf(",\"map_tile_cache_ready\":%s,\"map_tile_cache_policy\":",
           bool_json(d1l_map_tile_store_sd_ready(&status)));
    print_json_string(D1L_MAP_TILE_CACHE_POLICY);
    printf(",\"map_tile_cache_path_template\":");
    print_json_string(D1L_MAP_TILE_CACHE_PATH_TEMPLATE);
    printf(",\"map_tile_download_supported\":%s,\"map_tile_source\":",
           bool_json(connectivity.wifi_build_enabled &&
                     d1l_map_tile_store_sd_ready(&status)));
    print_json_string(D1L_MAP_TILE_SOURCE_ID);
    printf(",\"map_tile_download_state\":");
    print_json_string(!d1l_map_tile_store_sd_ready(&status) ? "sd_cache_required" :
                      !connectivity.wifi_connected ? "wifi_required" :
                      D1L_MAP_TILE_DOWNLOAD_STATE);
    printf(",\"map_tile_policy\":");
    print_json_string(D1L_MAP_TILE_PROVIDER_POLICY);
    printf(",\"map_tile_attribution\":");
    print_json_string(D1L_MAP_TILE_ATTRIBUTION);
    printf(",\"export_backend\":");
    print_json_string(status.export_backend ? status.export_backend : "serial");
    printf(",\"retained_nvs\":{\"partition\":\"d1l_retained\",\"marker_ready\":%s,\"markers_complete\":%s,\"anchor_ready\":%s,\"sentinel_ready\":%s,\"external_init_required\":%s,\"initialized_this_boot\":%s,\"ready\":%s,\"init_error\":\"%s\",\"migrated_keys\":%lu,\"migration_error\":\"%s\"}",
           bool_json(d1l_retained_blob_store_nvs_marker_ready()),
           bool_json(d1l_retained_blob_store_nvs_markers_complete()),
           bool_json(d1l_retained_blob_store_nvs_anchor_ready()),
           bool_json(d1l_retained_blob_store_nvs_sentinel_ready()),
           bool_json(d1l_retained_blob_store_nvs_external_init_required()),
           bool_json(d1l_retained_blob_store_nvs_initialized_this_boot()),
           bool_json(d1l_retained_blob_store_nvs_ready()),
           esp_err_to_name(d1l_retained_blob_store_nvs_error()),
           (unsigned long)d1l_retained_blob_store_nvs_migrated_keys(),
           esp_err_to_name(d1l_retained_blob_store_nvs_migration_error()));
    printf(",\"retained_sd\":{\"degraded\":%s,\"backup_degraded\":%s,\"note\":",
           bool_json(status.retained_sd_degraded),
           bool_json(status.retained_backup_degraded));
    print_json_string(status.retained_sd_degraded ?
                      D1L_RETAINED_BLOB_STORE_SD_DEGRADED_NOTE : "");
    printf(",\"stores\":{\"messages\":");
    print_retained_sd_store_json(
        &status.retained_sd_stats[D1L_RETAINED_BLOB_STORE_PUBLIC_MESSAGES]);
    printf(",\"dm\":");
    print_retained_sd_store_json(
        &status.retained_sd_stats[D1L_RETAINED_BLOB_STORE_DM_MESSAGES]);
    printf(",\"routes\":");
    print_retained_sd_store_json(
        &status.retained_sd_stats[D1L_RETAINED_BLOB_STORE_ROUTES]);
    printf(",\"packets\":");
    print_retained_sd_store_json(
        &status.retained_sd_stats[D1L_RETAINED_BLOB_STORE_PACKET_LOG]);
    printf("}}");
    printf(",\"stores\":{\"settings\":\"nvs\",\"identity\":\"nvs\",\"messages\":");
    print_json_string(status.message_store_backend ? status.message_store_backend : "nvs");
    printf(",\"dm\":");
    print_json_string(status.dm_store_backend ? status.dm_store_backend : "nvs");
    printf(",\"packets\":");
    print_json_string(status.packet_log_backend ? status.packet_log_backend : "nvs");
    printf(",\"routes\":");
    print_json_string(status.route_store_backend ? status.route_store_backend : "nvs");
    printf(",\"contacts\":\"nvs\",\"read_state\":\"nvs\",\"crashlog\":\"nvs\",\"map_tiles\":");
    print_json_string(status.map_tile_backend ? status.map_tile_backend : "unavailable");
    printf(",\"exports\":");
    print_json_string(status.export_backend ? status.export_backend : "serial");
    printf("},\"setup_required\":%s,\"setup_supported\":%s,\"setup_action\":",
           bool_json(status.setup_required), bool_json(status.setup_supported));
    print_json_string(status.setup_action ? status.setup_action : "not_available");
    printf(",\"fallback\":\"nvs\",\"note\":");
    print_json_string(status.note ? status.note : "");
    printf("}\n");
}

static void cmd_storage_mount(void)
{
    esp_err_t ret = d1l_storage_status_mount(D1L_STORAGE_RP2040_SD_PROBE_TIMEOUT_MS);
    d1l_storage_status_t status = {0};
    d1l_storage_status(&status);
    printf("{\"schema\":%d,\"ok\":%s,\"cmd\":\"storage mount\",\"code\":\"%s\",\"sd\":{\"state\":",
           D1L_CONSOLE_SCHEMA,
           bool_json(ret == ESP_OK),
           esp_err_to_name(ret));
    print_json_string(status.sd_state ? status.sd_state : "unknown");
    printf(",\"interface\":");
    print_json_string(status.sd_interface ? status.sd_interface : "unknown");
    printf(",\"filesystem\":");
    print_json_string(status.sd_filesystem ? status.sd_filesystem : "unknown");
    printf(",\"rp2040_bridge_ready\":%s,\"rp2040_protocol_supported\":%s,\"present\":%s,\"mounted\":%s,\"data_root_ready\":%s,\"needs_fat32\":%s,\"capacity_kb\":%lu,\"free_kb\":%lu,\"file_ops\":%s,\"file_line_max\":%lu,\"file_chunk_max\":%lu,\"path_max\":%lu,\"atomic_rename\":%s,\"response_truncated\":%s,\"probe_power\":",
           bool_json(status.rp2040_bridge_ready),
           bool_json(status.rp2040_sd_protocol_supported),
           bool_json(status.sd_present),
           bool_json(status.sd_mounted),
           bool_json(status.sd_data_root_ready),
           bool_json(status.sd_needs_fat32),
           (unsigned long)status.capacity_kb,
           (unsigned long)status.free_kb,
           bool_json(status.file_ops_supported),
           (unsigned long)status.file_line_max,
           (unsigned long)status.file_chunk_max,
           (unsigned long)status.path_max,
           bool_json(status.atomic_rename_supported),
           bool_json(status.response_truncated));
    print_json_string(status.sd_probe_power[0] ? status.sd_probe_power : "unknown");
    printf(",\"probe_mode\":");
    print_json_string(status.sd_probe_mode[0] ? status.sd_probe_mode : "unknown");
    printf(",\"probe_error\":%lu,\"probe_data\":%lu,\"mount_error\":%lu,\"mount_data\":%lu,\"last_error\":\"%s\"},\"setup_required\":%s,\"setup_supported\":%s,\"setup_action\":",
           (unsigned long)status.sd_probe_error,
           (unsigned long)status.sd_probe_data,
           (unsigned long)status.sd_mount_error,
           (unsigned long)status.sd_mount_data,
           esp_err_to_name(status.last_error),
           bool_json(status.setup_required),
           bool_json(status.setup_supported));
    print_json_string(status.setup_action ? status.setup_action : "not_available");
    printf(",\"public_rf_tx\":false,\"formats_sd\":false,\"note\":");
    print_json_string(status.note ? status.note : "");
    printf("}\n");
}

static void print_storage_manager_result(const char *cmd, esp_err_t ret,
                                         bool retained_worker_quiesce_acquired)
{
    d1l_storage_status_t status = {0};
    d1l_storage_status(&status);
    printf("{\"schema\":%d,\"ok\":%s,\"cmd\":",
           D1L_CONSOLE_SCHEMA,
           bool_json(ret == ESP_OK));
    print_json_string(cmd);
    printf(",\"code\":\"%s\",\"retained_worker_quiesce_acquired\":%s,\"manager\":{\"running\":%s,\"state\":",
           esp_err_to_name(ret),
           bool_json(retained_worker_quiesce_acquired),
           bool_json(status.manager_running));
    print_json_string(status.manager_state ? status.manager_state : "BRIDGE_WAIT");
    printf(",\"attempt\":%lu,\"backoff_ms\":%lu,\"force_nvs\":%s,"
           "\"initial_ping_timeouts\":%lu,\"bridge_response_seen\":%s,"
           "\"auto_reset_pending\":%s,\"auto_reset_attempted\":%s},\"sd\":{\"state\":",
           (unsigned long)status.manager_attempt,
           (unsigned long)status.manager_backoff_ms,
           bool_json(status.force_nvs),
           (unsigned long)status.manager_initial_ping_timeouts,
           bool_json(status.manager_bridge_response_seen),
           bool_json(status.manager_auto_reset_pending),
           bool_json(status.manager_auto_reset_attempted));
    print_json_string(status.sd_state ? status.sd_state : "unknown");
    printf(",\"rp2040_bridge_ready\":%s,\"rp2040_protocol_supported\":%s,\"present\":%s,\"presence_stale\":%s,\"mounted\":%s,\"data_root_ready\":%s,\"status_stale\":%s,\"refresh_failures\":%lu},\"setup_action\":",
           bool_json(status.rp2040_bridge_ready),
           bool_json(status.rp2040_sd_protocol_supported),
           bool_json(status.sd_present),
           bool_json(status.sd_presence_stale),
           bool_json(status.sd_mounted),
           bool_json(status.sd_data_root_ready),
           bool_json(status.bridge_status_stale),
           (unsigned long)status.bridge_status_refresh_failures);
    print_json_string(status.setup_action ? status.setup_action : "not_available");
    printf(",\"public_rf_tx\":false,\"formats_sd\":false,\"note\":");
    print_json_string(status.note ? status.note : "");
    printf("}\n");
}

static void cmd_storage_remount(void)
{
    bool retained_worker_quiesce_acquired = false;
    esp_err_t ret =
        d1l_storage_status_remount_blocking(
            D1L_STORAGE_REMOUNT_TIMEOUT_MS,
            &retained_worker_quiesce_acquired);
    print_storage_manager_result("storage remount", ret,
                                 retained_worker_quiesce_acquired);
}

static void cmd_storage_reset_bridge(void)
{
    esp_err_t ret = d1l_storage_manager_reset_bridge();
    print_storage_manager_result("storage reset-bridge", ret, false);
}

static void cmd_storage_force_nvs(const char *line)
{
    bool force = true;
    if (strcmp(line, "storage force-nvs off") == 0) {
        force = false;
    } else if (strcmp(line, "storage force-nvs") != 0 &&
               strcmp(line, "storage force-nvs on") != 0) {
        err_result("storage force-nvs", "INVALID_MODE",
                   "usage: storage force-nvs [on|off]");
        return;
    }
    d1l_storage_manager_force_nvs(force);
    esp_err_t ret = d1l_storage_manager_start();
    print_storage_manager_result(force ? "storage force-nvs" : "storage force-nvs off",
                                 ret, false);
}

static void print_storage_diag_probe(const char *name,
                                     bool present,
                                     uint32_t error,
                                     uint32_t data,
                                     uint32_t capacity_kb)
{
    printf(",\"");
    printf("%s", name);
    printf("\":{\"present\":%s,\"error\":%lu,\"data\":%lu,\"capacity_kb\":%lu}",
           bool_json(present),
           (unsigned long)error,
           (unsigned long)data,
           (unsigned long)capacity_kb);
}

static d1l_rp2040_sd_diag_t s_console_storage_diag;

static void cmd_storage_diag(void)
{
    d1l_rp2040_sd_diag_t *diag = &s_console_storage_diag;
    esp_err_t ret = d1l_rp2040_bridge_sd_diag(diag, D1L_STORAGE_RP2040_SD_PROBE_TIMEOUT_MS);
    printf("{\"schema\":%d,\"ok\":%s,\"cmd\":\"storage diag\",\"code\":\"%s\",\"bridge_ready\":%s,\"diag_supported\":%s,\"response_truncated\":%s,\"pins\":",
            D1L_CONSOLE_SCHEMA,
            bool_json(ret == ESP_OK),
            esp_err_to_name(ret),
            bool_json(diag->bridge_ready),
            bool_json(diag->protocol_supported),
            bool_json(diag->response_truncated));
    print_json_string(diag->pins[0] ? diag->pins : "unknown");
    printf(",\"spi_hz\":%lu,\"selected_power\":",
            (unsigned long)diag->spi_hz);
    print_json_string(diag->selected_power[0] ? diag->selected_power : "unknown");
    printf(",\"selected_mode\":");
    print_json_string(diag->selected_mode[0] ? diag->selected_mode : "unknown");
    printf(",\"mount_selected\":%s,\"probes\":{",
            bool_json(diag->mount_selected));
    printf("\"high_dedicated\":{\"present\":%s,\"error\":%lu,\"data\":%lu,\"capacity_kb\":%lu}",
            bool_json(diag->high_dedicated_present),
            (unsigned long)diag->high_dedicated_error,
            (unsigned long)diag->high_dedicated_data,
            (unsigned long)diag->high_dedicated_capacity_kb);
    print_storage_diag_probe("high_shared", diag->high_shared_present,
                             diag->high_shared_error, diag->high_shared_data,
                             diag->high_shared_capacity_kb);
    print_storage_diag_probe("low_dedicated", diag->low_dedicated_present,
                             diag->low_dedicated_error, diag->low_dedicated_data,
                             diag->low_dedicated_capacity_kb);
    print_storage_diag_probe("low_shared", diag->low_shared_present,
                             diag->low_shared_error, diag->low_shared_data,
                             diag->low_shared_capacity_kb);
    printf("},\"formats_sd\":false,\"public_rf_tx\":false,\"note\":");
    print_json_string(diag->note[0] ? diag->note : "");
    printf("}\n");
}

static void cmd_storage_diag_raw(void)
{
    d1l_rp2040_sd_diag_t *diag = &s_console_storage_diag;
    esp_err_t ret = d1l_rp2040_bridge_sd_diag(diag, D1L_STORAGE_RP2040_SD_PROBE_TIMEOUT_MS);
    printf("{\"schema\":%d,\"ok\":%s,\"cmd\":\"storage diag raw\",\"code\":\"%s\",\"bridge_ready\":%s,\"diag_supported\":%s,\"response_truncated\":%s,\"raw_line\":",
            D1L_CONSOLE_SCHEMA,
            bool_json(ret == ESP_OK),
            esp_err_to_name(ret),
            bool_json(diag->bridge_ready),
            bool_json(diag->protocol_supported),
            bool_json(diag->response_truncated));
    print_json_string(diag->raw_line[0] ? diag->raw_line : "");
    printf(",\"formats_sd\":false,\"public_rf_tx\":false,\"note\":");
    print_json_string(diag->note[0] ? diag->note : "");
    printf("}\n");
}

static void cmd_storage_map_policy(void)
{
    d1l_storage_status_t status = {0};
    d1l_storage_status(&status);
    d1l_connectivity_status_t connectivity = {0};
    d1l_connectivity_status(&connectivity);
    char sample_path[D1L_RP2040_FILE_PATH_MAX + 1U] = {0};
    const bool sample_path_ok = d1l_map_tile_store_path(0U, 0U, 0U,
                                                        sample_path,
                                                        sizeof(sample_path));
    const bool cache_ready = d1l_map_tile_store_sd_ready(&status);

    ok_begin("storage map-policy");
    printf(",\"kind\":\"map_tile_policy\",\"map_page_supported\":true,\"tile_cache_ready\":%s,\"tile_cache_backend\":",
           bool_json(cache_ready));
    print_json_string(status.map_tile_backend ? status.map_tile_backend : "unavailable");
    printf(",\"cache_policy\":");
    print_json_string(D1L_MAP_TILE_CACHE_POLICY);
    printf(",\"cache_path_template\":");
    print_json_string(D1L_MAP_TILE_CACHE_PATH_TEMPLATE);
    printf(",\"sample_path\":");
    print_json_string(sample_path_ok ? sample_path : "");
    const bool download_supported = connectivity.wifi_build_enabled && cache_ready;
    const bool live_network_download = download_supported && connectivity.wifi_connected;
    const char *download_state = !cache_ready ? "sd_cache_required" :
                                 !connectivity.wifi_connected ? "wifi_required" :
                                 D1L_MAP_TILE_DOWNLOAD_STATE;
    printf(",\"source\":");
    print_json_string(D1L_MAP_TILE_SOURCE_ID);
    printf(",\"url_template\":");
    print_json_string(D1L_MAP_TILE_SOURCE_URL_TEMPLATE);
    printf(",\"attribution\":");
    print_json_string(D1L_MAP_TILE_ATTRIBUTION);
    printf(",\"license_url\":");
    print_json_string(D1L_MAP_TILE_LICENSE_URL);
    printf(",\"built_in\":true,\"current_view_only\":true,\"max_current_view_tiles\":%u,\"min_cache_days\":%u,\"zoom\":%u,\"default_zoom\":%u,\"min_zoom\":%u,\"max_zoom\":%u,\"sideload_supported\":false,\"canary_command\":\"storage map-tile-canary <token>\",\"download_supported\":%s,\"live_network_download\":%s,\"download_state\":",
           (unsigned)D1L_MAP_VIEW_MAX_TILES,
           (unsigned)D1L_MAP_TILE_MIN_CACHE_DAYS,
           (unsigned)D1L_MAP_VIEW_DEFAULT_ZOOM,
           (unsigned)D1L_MAP_VIEW_DEFAULT_ZOOM,
           (unsigned)D1L_MAP_VIEW_MIN_ZOOM,
           (unsigned)D1L_MAP_VIEW_MAX_ZOOM,
           bool_json(download_supported),
           bool_json(live_network_download));
    print_json_string(download_state);
    printf(",\"download_requires\":");
    print_json_string(D1L_MAP_TILE_DOWNLOAD_REQUIRES);
    printf(",\"policy\":");
    print_json_string(D1L_MAP_TILE_PROVIDER_POLICY);
    printf(",\"wifi_state\":");
    print_json_string(connectivity.wifi_state ? connectivity.wifi_state : "off");
    printf(",\"wifi_build_enabled\":%s,\"public_rf_tx\":false,\"formats_sd\":false,\"note\":\"Set a location and enable Wi-Fi; the built-in OpenStreetMap source fetches only the visible current view into the persistent SD cache\"}\n",
           bool_json(connectivity.wifi_build_enabled));
}

static void cmd_map_tiles_status(void)
{
    d1l_map_view_status_t status = {0};
    d1l_map_view_service_status(&status);
    ok_begin("map tiles status");
    printf(",\"source\":");
    print_json_string(D1L_MAP_TILE_SOURCE_ID);
    printf(",\"render_style\":");
    print_json_string(D1L_MAP_RENDER_STYLE_ID);
    printf(",\"attribution\":");
    print_json_string(D1L_MAP_TILE_ATTRIBUTION);
    printf(",\"initialized\":%s,\"visible\":%s,\"worker_running\":%s,\"frame_ready\":%s,\"sd_cache_ready\":%s,\"wifi_connected\":%s,\"rate_limited\":%s,\"current_view_only\":%s,\"generation\":%lu,\"frame_revision\":%lu,\"retry_after_sec\":%lu,\"decode_total_us\":%lu,\"decode_max_us\":%lu,\"decode_samples\":%u,\"lat_e7\":%ld,\"lon_e7\":%ld,\"width\":%u,\"height\":%u,\"zoom\":%u,\"planned_tiles\":%u,\"attempted_tiles\":%u,\"cache_hits\":%u,\"network_requests\":%u,\"downloaded_tiles\":%u,\"rendered_tiles\":%u,\"failed_tiles\":%u,\"phase\":",
           bool_json(status.initialized), bool_json(status.visible),
           bool_json(status.worker_running), bool_json(status.frame_ready),
           bool_json(status.sd_cache_ready), bool_json(status.wifi_connected),
           bool_json(status.rate_limited), bool_json(status.current_view_only),
           (unsigned long)status.generation, (unsigned long)status.frame_revision,
           (unsigned long)status.retry_after_sec,
           (unsigned long)status.decode_total_us,
           (unsigned long)status.decode_max_us,
           (unsigned)status.decode_samples, (long)status.lat_e7,
           (long)status.lon_e7, (unsigned)status.width, (unsigned)status.height,
           (unsigned)status.zoom, (unsigned)status.planned_tiles,
           (unsigned)status.attempted_tiles, (unsigned)status.cache_hits,
           (unsigned)status.network_requests, (unsigned)status.downloaded_tiles,
           (unsigned)status.rendered_tiles, (unsigned)status.failed_tiles);
    print_json_string(status.phase);
    printf(",\"message\":");
    print_json_string(status.message);
    printf(",\"public_rf_tx\":false,\"formats_sd\":false}\n");
}

static void print_storage_setup_payload(const d1l_storage_status_t *status)
{
    printf(",\"available\":%s,\"setup_required\":%s,\"setup_supported\":%s,\"setup_action\":",
           bool_json(status->rp2040_sd_protocol_supported || status->sd_present),
           bool_json(status->setup_required),
           bool_json(status->setup_supported));
    print_json_string(status->setup_action ? status->setup_action : "not_available");
    printf(",\"needs_fat32\":%s,\"will_format\":false,\"format_requested\":false,\"format_performed\":false,\"policy\":\"no_device_format\",\"data_backend\":",
           bool_json(status->sd_needs_fat32));
    print_json_string(status->data_backend ? status->data_backend : "nvs");
    printf(",\"fallback\":\"nvs\",\"note\":");
    print_json_string(status->note ? status->note : "");
    printf("}\n");
}

static void print_storage_filecanary_error(const char *step,
                                           esp_err_t ret,
                                           const d1l_storage_status_t *status,
                                           const d1l_rp2040_file_result_t *file,
                                           const char *hint)
{
    printf("{\"schema\":%d,\"ok\":false,\"cmd\":\"storage filecanary\",\"code\":\"%s\",\"step\":",
           D1L_CONSOLE_SCHEMA, esp_err_to_name(ret));
    print_json_string(step ? step : "unknown");
    printf(",\"sd_state\":");
    print_json_string(status && status->sd_state ? status->sd_state : "unknown");
    printf(",\"data_backend\":");
    print_json_string(status && status->data_backend ? status->data_backend : "nvs");
    printf(",\"packet_log_backend\":");
    print_json_string(status && status->packet_log_backend ? status->packet_log_backend : "nvs");
    printf(",\"file_op\":");
    print_json_string(file && file->op[0] ? file->op : "");
    printf(",\"file_error\":");
    print_json_string(file && file->err[0] ? file->err : "");
    printf(",\"file_note\":");
    print_json_string(file && file->note[0] ? file->note : "");
    printf(",\"hint\":");
    print_json_string(hint ? hint : "SD file-operation canary failed; no format was performed");
    printf("}\n");
}

static void print_storage_filecanary_error_and_resume(const char *step,
                                                      esp_err_t ret,
                                                      const d1l_storage_status_t *status,
                                                      const d1l_rp2040_file_result_t *file,
                                                      const char *hint)
{
    d1l_storage_manager_resume();
    print_storage_filecanary_error(step, ret, status, file, hint);
}

static bool storage_filecanary_ready(const d1l_storage_status_t *status)
{
    return status &&
           status->file_ops_supported &&
           status->atomic_rename_supported &&
           status->file_line_max >= D1L_RP2040_FILE_LINE_MAX &&
           status->file_chunk_max >= D1L_RP2040_FILE_CHUNK_MAX &&
           status->path_max >= D1L_RP2040_FILE_PATH_MAX;
}

static void cmd_storage_filecanary(void)
{
    d1l_storage_status_t status = {0};
    d1l_storage_status(&status);
    if (!storage_filecanary_ready(&status)) {
        (void)d1l_storage_status_mount(D1L_STORAGE_RP2040_SD_PROBE_TIMEOUT_MS);
        d1l_storage_status(&status);
    }

    if (!storage_filecanary_ready(&status)) {
        print_storage_filecanary_error(
            "preflight",
            ESP_ERR_NOT_SUPPORTED,
            &status,
            NULL,
            "requires a ready RP2040 SD bridge with file_ops and atomic rename; retained-store migration is tested by storage retained-canary");
        return;
    }

    static const uint8_t payload[] = "DeskOS SD file canary v1";
    const size_t payload_len = sizeof(payload) - 1U;
    const uint32_t token = esp_random();
    const uint32_t tick = (uint32_t)xTaskGetTickCount();
    char tmp_path[D1L_RP2040_FILE_PATH_MAX + 1U];
    char final_path[D1L_RP2040_FILE_PATH_MAX + 1U];
    uint8_t read_buf[D1L_RP2040_FILE_CHUNK_MAX];
    d1l_rp2040_file_result_t file = {0};

    int path_len = snprintf(tmp_path, sizeof(tmp_path), "canary/fc-%08lx-%08lx.tmp",
                            (unsigned long)token, (unsigned long)tick);
    int final_len = snprintf(final_path, sizeof(final_path), "canary/fc-%08lx-%08lx.bin",
                             (unsigned long)token, (unsigned long)tick);
    if (path_len <= 0 || (size_t)path_len >= sizeof(tmp_path) ||
        final_len <= 0 || (size_t)final_len >= sizeof(final_path)) {
        print_storage_filecanary_error(
            "path",
            ESP_ERR_INVALID_SIZE,
            &status,
            NULL,
            "could not build unique file canary paths");
        return;
    }

    d1l_storage_manager_pause(D1L_STORAGE_FILE_CANARY_MANAGER_PAUSE_MS);

    esp_err_t ret = d1l_rp2040_bridge_file_write(tmp_path, 0U, payload, payload_len, true,
                                                 &file, D1L_STORAGE_FILE_CANARY_OP_TIMEOUT_MS);
    if (ret != ESP_OK || file.length != payload_len || file.size != payload_len) {
        print_storage_filecanary_error_and_resume("write_tmp", ret == ESP_OK ? ESP_FAIL : ret,
                                                  &status, &file, "temp write failed");
        return;
    }

    memset(read_buf, 0, sizeof(read_buf));
    ret = d1l_rp2040_bridge_file_read(tmp_path, 0U, read_buf, payload_len,
                                      &file, D1L_STORAGE_FILE_CANARY_OP_TIMEOUT_MS);
    if (ret != ESP_OK || file.length != payload_len ||
        memcmp(read_buf, payload, payload_len) != 0) {
        print_storage_filecanary_error_and_resume("read_tmp", ret == ESP_OK ? ESP_FAIL : ret,
                                                  &status, &file, "temp read-back did not match");
        return;
    }

    ret = d1l_rp2040_bridge_file_rename(tmp_path, final_path, true, &file,
                                        D1L_STORAGE_FILE_CANARY_OP_TIMEOUT_MS);
    if (ret != ESP_OK) {
        print_storage_filecanary_error_and_resume("rename_final", ret, &status, &file,
                                                  "rename replace commit failed");
        return;
    }

    ret = d1l_rp2040_bridge_file_stat(final_path, &file,
                                      D1L_STORAGE_FILE_CANARY_OP_TIMEOUT_MS);
    if (ret != ESP_OK || !file.exists || file.is_directory || file.size != payload_len) {
        print_storage_filecanary_error_and_resume("stat_final", ret == ESP_OK ? ESP_FAIL : ret,
                                                  &status, &file, "final canary stat did not match");
        return;
    }

    memset(read_buf, 0, sizeof(read_buf));
    ret = d1l_rp2040_bridge_file_read(final_path, 0U, read_buf, payload_len,
                                      &file, D1L_STORAGE_FILE_CANARY_OP_TIMEOUT_MS);
    if (ret != ESP_OK || file.length != payload_len ||
        memcmp(read_buf, payload, payload_len) != 0) {
        print_storage_filecanary_error_and_resume("read_final", ret == ESP_OK ? ESP_FAIL : ret,
                                                  &status, &file, "final read-back did not match");
        return;
    }

    ret = d1l_rp2040_bridge_file_delete(final_path, &file,
                                        D1L_STORAGE_FILE_CANARY_OP_TIMEOUT_MS);
    if (ret != ESP_OK) {
        print_storage_filecanary_error_and_resume("delete_final", ret, &status, &file,
                                                  "could not delete final canary file");
        return;
    }

    ret = d1l_rp2040_bridge_file_stat(final_path, &file,
                                      D1L_STORAGE_FILE_CANARY_OP_TIMEOUT_MS);
    if (ret != ESP_OK || file.exists) {
        print_storage_filecanary_error_and_resume("stat_deleted", ret == ESP_OK ? ESP_FAIL : ret,
                                                  &status, &file, "final canary file still exists after delete");
        return;
    }

    d1l_storage_manager_resume();
    ok_begin("storage filecanary");
    printf(",\"path\":");
    print_json_string(final_path);
    printf(",\"tmp_path\":");
    print_json_string(tmp_path);
    printf(",\"bytes\":%u,\"write_tmp\":true,\"read_tmp\":true,\"rename_replace\":true,\"stat_final\":true,\"read_final\":true,\"delete_final\":true,\"stat_deleted\":true,\"data_backend\":",
           (unsigned)payload_len);
    print_json_string(status.data_backend ? status.data_backend : "nvs");
    printf(",\"packet_log_backend\":");
    print_json_string(status.packet_log_backend ? status.packet_log_backend : "nvs");
    printf(",\"note\":\"Serial-only RP2040 SD file-operation canary passed; no Public RF or format command was used\"}\n");
}

static bool text_equals(const char *lhs, const char *rhs)
{
    return lhs && rhs && strcmp(lhs, rhs) == 0;
}

static bool storage_retained_history_sd_ready(const d1l_storage_status_t *status)
{
    return status &&
           status->data_enabled &&
           text_equals(status->data_backend, "mixed") &&
           text_equals(status->message_store_backend, "sd") &&
           text_equals(status->dm_store_backend, "sd") &&
           text_equals(status->route_store_backend, "sd") &&
           text_equals(status->packet_log_backend, "sd") &&
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

static bool copy_storage_canary_token(char *dest, size_t dest_size, const char *src)
{
    if (!dest || dest_size == 0 || !src) {
        return false;
    }
    while (*src == ' ') {
        src++;
    }
    size_t out = 0;
    while (*src && out + 1U < dest_size) {
        const unsigned char ch = (unsigned char)*src++;
        if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.') {
            dest[out++] = (char)ch;
        } else if (isspace(ch)) {
            break;
        } else {
            return false;
        }
    }
    dest[out] = '\0';
    return out > 0 && *src == '\0';
}

static bool read_storage_word(const char **cursor, char *dest, size_t dest_size)
{
    if (!cursor || !*cursor || !dest || dest_size == 0) {
        return false;
    }
    while (**cursor == ' ') {
        (*cursor)++;
    }
    size_t out = 0;
    while (**cursor != '\0' && **cursor != ' ' && out + 1U < dest_size) {
        const unsigned char ch = (unsigned char)**cursor;
        if (ch < 32 || ch > 126 || ch == '"' || ch == '\\') {
            return false;
        }
        dest[out++] = (char)ch;
        (*cursor)++;
    }
    dest[out] = '\0';
    while (**cursor == ' ') {
        (*cursor)++;
    }
    return out > 0;
}

static bool parse_u32_word(const char *word, uint32_t *out_value)
{
    if (!word || !out_value || word[0] == '\0') {
        return false;
    }
    char *end = NULL;
    unsigned long value = strtoul(word, &end, 10);
    if (!end || *end != '\0' || value > UINT32_MAX) {
        return false;
    }
    *out_value = (uint32_t)value;
    return true;
}

static bool parse_next_u32_arg(const char **cursor, uint32_t *out_value)
{
    if (!cursor || !*cursor || !out_value) {
        return false;
    }
    while (**cursor == ' ') {
        (*cursor)++;
    }
    if (**cursor == '\0') {
        return false;
    }
    char *end = NULL;
    unsigned long value = strtoul(*cursor, &end, 10);
    if (end == *cursor || !end || (*end != '\0' && *end != ' ') || value > UINT32_MAX) {
        return false;
    }
    *out_value = (uint32_t)value;
    *cursor = end;
    while (**cursor == ' ') {
        (*cursor)++;
    }
    return true;
}

static bool parse_message_offset_clause(const char *arg, size_t *out_offset)
{
    if (!arg || !out_offset) {
        return false;
    }
    while (*arg == ' ') {
        arg++;
    }
    if (strncmp(arg, "offset", 6) != 0 ||
        (arg[6] != ' ' && arg[6] != '\0')) {
        return false;
    }
    arg += 6;
    while (*arg == ' ') {
        arg++;
    }
    if (*arg == '\0') {
        return false;
    }
    char *end = NULL;
    unsigned long value = strtoul(arg, &end, 10);
    if (end == arg) {
        return false;
    }
    while (end && *end == ' ') {
        end++;
    }
    if (!end || *end != '\0') {
        return false;
    }
    *out_offset = (size_t)value;
    return true;
}

static bool strip_message_offset_suffix(char *text, size_t *out_offset)
{
    if (!text || !out_offset) {
        return false;
    }
    char *candidate = NULL;
    for (char *p = strstr(text, " offset "); p; p = strstr(p + 1, " offset ")) {
        candidate = p;
    }
    if (!candidate) {
        return true;
    }
    if (!parse_message_offset_clause(candidate + 1, out_offset)) {
        return false;
    }
    *candidate = '\0';
    trim_line(text);
    return true;
}

static void print_storage_export_error(const char *cmd,
                                       const char *step,
                                       esp_err_t ret,
                                       const d1l_storage_status_t *status,
                                       const d1l_export_canary_result_t *canary,
                                       const char *hint)
{
    printf("{\"schema\":%d,\"ok\":false,\"cmd\":", D1L_CONSOLE_SCHEMA);
    print_json_string(cmd ? cmd : "storage export");
    printf(",\"code\":\"%s\",\"step\":", esp_err_to_name(ret));
    print_json_string(step ? step : "unknown");
    printf(",\"sd_state\":");
    print_json_string(status && status->sd_state ? status->sd_state : "unknown");
    printf(",\"export_backend\":");
    print_json_string(status && status->export_backend ? status->export_backend : "serial");
    printf(",\"path\":");
    print_json_string(canary ? canary->path : "");
    printf(",\"tmp_path\":");
    print_json_string(canary ? canary->tmp_path : "");
    printf(",\"file_op\":");
    print_json_string(canary && canary->file.op[0] ? canary->file.op : "");
    printf(",\"file_error\":");
    print_json_string(canary && canary->file.err[0] ? canary->file.err : "");
    printf(",\"public_rf_tx\":false,\"formats_sd\":false,\"hint\":");
    print_json_string(hint ? hint : "requires a ready RP2040 SD bridge with file_ops and atomic rename; no Public RF or format command was used");
    printf("}\n");
}

static void print_storage_map_tile_error(const char *cmd,
                                         const char *step,
                                         esp_err_t ret,
                                         const d1l_storage_status_t *status,
                                         const d1l_map_tile_canary_result_t *canary,
                                         const char *hint)
{
    printf("{\"schema\":%d,\"ok\":false,\"cmd\":", D1L_CONSOLE_SCHEMA);
    print_json_string(cmd ? cmd : "storage map-tile-canary");
    printf(",\"code\":\"%s\",\"step\":", esp_err_to_name(ret));
    print_json_string(step ? step : "unknown");
    printf(",\"sd_state\":");
    print_json_string(status && status->sd_state ? status->sd_state : "unknown");
    printf(",\"map_tile_backend\":");
    print_json_string(status && status->map_tile_backend ? status->map_tile_backend : "unavailable");
    printf(",\"path\":");
    print_json_string(canary ? canary->path : "");
    printf(",\"tmp_path\":");
    print_json_string(canary ? canary->tmp_path : "");
    printf(",\"file_op\":");
    print_json_string(canary && canary->file.op[0] ? canary->file.op : "");
    printf(",\"file_error\":");
    print_json_string(canary && canary->file.err[0] ? canary->file.err : "");
    printf(",\"public_rf_tx\":false,\"formats_sd\":false,\"hint\":");
    print_json_string(hint ? hint : "requires a ready RP2040 SD bridge with file_ops and atomic rename; no Public RF or format command was used");
    printf("}\n");
}

static void cmd_storage_map_tile_canary(const char *line)
{
    static const char prefix[] = "storage map-tile-canary ";
    char token[D1L_MAP_TILE_CANARY_TOKEN_MAX + 1U] = {0};
    if (strncmp(line, prefix, strlen(prefix)) != 0 ||
        !copy_storage_canary_token(token, sizeof(token), line + strlen(prefix))) {
        err_result("storage map-tile-canary", "INVALID_TOKEN",
                   "usage: storage map-tile-canary <token-with-a-z-0-9-dot-dash-underscore>");
        return;
    }

    (void)d1l_storage_status_refresh(D1L_STORAGE_RP2040_SD_PROBE_TIMEOUT_MS);
    d1l_storage_status_t status = {0};
    d1l_storage_status(&status);

    d1l_map_tile_canary_result_t canary = {0};
    esp_err_t ret = d1l_map_tile_store_write_canary(token, &status, &canary);
    if (ret != ESP_OK) {
        print_storage_map_tile_error(
            "storage map-tile-canary", canary.step, ret, &status, &canary,
            "requires a ready RP2040 SD bridge with file_ops and atomic rename; no Public RF or format command was used");
        return;
    }

    d1l_storage_status(&status);
    ok_begin("storage map-tile-canary");
    printf(",\"token\":");
    print_json_string(canary.token);
    printf(",\"path\":");
    print_json_string(canary.path);
    printf(",\"tmp_path\":");
    print_json_string(canary.tmp_path);
    printf(",\"z\":%u,\"x\":%lu,\"y\":%lu,\"bytes\":%u,\"write_tmp\":%s,\"read_tmp\":%s,\"rename_replace\":%s,\"stat_final\":%s,\"read_final\":%s,\"public_rf_tx\":false,\"formats_sd\":false,\"map_tile_backend\":",
           (unsigned)canary.z,
           (unsigned long)canary.x,
           (unsigned long)canary.y,
           (unsigned)canary.bytes,
           bool_json(canary.write_tmp),
           bool_json(canary.read_tmp),
           bool_json(canary.rename_replace),
           bool_json(canary.stat_final),
           bool_json(canary.read_final));
    print_json_string(status.map_tile_backend ? status.map_tile_backend : "unavailable");
    printf(",\"note\":\"Map tile SD cache canary committed by temp write and atomic rename; no Public RF or format command was used\"}\n");
}

static void cmd_storage_map_tile_check(const char *line)
{
    static const char prefix[] = "storage map-tile-check ";
    char token[D1L_MAP_TILE_CANARY_TOKEN_MAX + 1U] = {0};
    if (strncmp(line, prefix, strlen(prefix)) != 0 ||
        !copy_storage_canary_token(token, sizeof(token), line + strlen(prefix))) {
        err_result("storage map-tile-check", "INVALID_TOKEN",
                   "usage: storage map-tile-check <token-with-a-z-0-9-dot-dash-underscore>");
        return;
    }

    (void)d1l_storage_status_refresh(D1L_STORAGE_RP2040_SD_PROBE_TIMEOUT_MS);
    d1l_storage_status_t status = {0};
    d1l_storage_status(&status);

    d1l_map_tile_canary_result_t canary = {0};
    esp_err_t ret = d1l_map_tile_store_check_canary(token, &status, &canary);
    if (ret != ESP_OK) {
        print_storage_map_tile_error(
            "storage map-tile-check", canary.step, ret, &status, &canary,
            "requires the previous map tile canary to survive SD reboot/remount; no Public RF or format command was used");
        return;
    }

    d1l_storage_status(&status);
    ok_begin("storage map-tile-check");
    printf(",\"token\":");
    print_json_string(canary.token);
    printf(",\"path\":");
    print_json_string(canary.path);
    printf(",\"z\":%u,\"x\":%lu,\"y\":%lu,\"bytes\":%u,\"stat_final\":%s,\"read_final\":%s,\"public_rf_tx\":false,\"formats_sd\":false,\"map_tile_backend\":",
           (unsigned)canary.z,
           (unsigned long)canary.x,
           (unsigned long)canary.y,
           (unsigned)canary.bytes,
           bool_json(canary.stat_final),
           bool_json(canary.read_final));
    print_json_string(status.map_tile_backend ? status.map_tile_backend : "unavailable");
    printf(",\"note\":\"Map tile SD cache canary verified read-only after remount; no Public RF or format command was used\"}\n");
}

static void cmd_storage_export_canary(const char *line)
{
    static const char prefix[] = "storage export-canary ";
    char token[D1L_EXPORT_CANARY_TOKEN_MAX + 1U] = {0};
    if (strncmp(line, prefix, strlen(prefix)) != 0 ||
        !copy_storage_canary_token(token, sizeof(token), line + strlen(prefix))) {
        err_result("storage export-canary", "INVALID_TOKEN",
                   "usage: storage export-canary <token-with-a-z-0-9-dot-dash-underscore>");
        return;
    }

    (void)d1l_storage_status_refresh(D1L_STORAGE_RP2040_SD_PROBE_TIMEOUT_MS);
    d1l_storage_status_t status = {0};
    d1l_storage_status(&status);

    d1l_export_canary_result_t canary = {0};
    esp_err_t ret = d1l_export_store_write_canary(token, &status, &canary);
    if (ret != ESP_OK) {
        print_storage_export_error("storage export-canary", canary.step, ret, &status,
                                   &canary,
                                   "requires a ready RP2040 SD bridge with file_ops and atomic rename; no Public RF or format command was used");
        return;
    }

    d1l_storage_status(&status);
    ok_begin("storage export-canary");
    printf(",\"token\":");
    print_json_string(canary.token);
    printf(",\"path\":");
    print_json_string(canary.path);
    printf(",\"tmp_path\":");
    print_json_string(canary.tmp_path);
    printf(",\"bytes\":%u,\"write_tmp\":%s,\"read_tmp\":%s,\"rename_replace\":%s,\"stat_final\":%s,\"read_final\":%s,\"public_rf_tx\":false,\"formats_sd\":false,\"export_backend\":",
           (unsigned)canary.bytes,
           bool_json(canary.write_tmp),
           bool_json(canary.read_tmp),
           bool_json(canary.rename_replace),
           bool_json(canary.stat_final),
           bool_json(canary.read_final));
    print_json_string(status.export_backend ? status.export_backend : "serial");
    printf(",\"note\":\"Diagnostic export SD canary committed by temp write and atomic rename; no Public RF or format command was used\"}\n");
}

static bool append_export_json(char *dest, size_t dest_size, size_t *used,
                               const char *fmt, ...)
{
    if (!dest || !used || !fmt || *used >= dest_size) {
        return false;
    }
    va_list args;
    va_start(args, fmt);
    const int ret = vsnprintf(dest + *used, dest_size - *used, fmt, args);
    va_end(args);
    if (ret < 0 || (size_t)ret >= dest_size - *used) {
        return false;
    }
    *used += (size_t)ret;
    return true;
}

static bool append_export_json_string(char *dest, size_t dest_size, size_t *used,
                                      const char *text)
{
    if (!dest || !used || *used >= dest_size) {
        return false;
    }
    if (!append_export_json(dest, dest_size, used, "\"")) {
        return false;
    }
    if (text) {
        for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
            switch (*p) {
            case '"':
                if (!append_export_json(dest, dest_size, used, "\\\"")) {
                    return false;
                }
                break;
            case '\\':
                if (!append_export_json(dest, dest_size, used, "\\\\")) {
                    return false;
                }
                break;
            case '\b':
                if (!append_export_json(dest, dest_size, used, "\\b")) {
                    return false;
                }
                break;
            case '\f':
                if (!append_export_json(dest, dest_size, used, "\\f")) {
                    return false;
                }
                break;
            case '\n':
                if (!append_export_json(dest, dest_size, used, "\\n")) {
                    return false;
                }
                break;
            case '\r':
                if (!append_export_json(dest, dest_size, used, "\\r")) {
                    return false;
                }
                break;
            case '\t':
                if (!append_export_json(dest, dest_size, used, "\\t")) {
                    return false;
                }
                break;
            default:
                if (*p < 0x20U) {
                    if (!append_export_json(dest, dest_size, used, "\\u%04x",
                                            (unsigned)*p)) {
                        return false;
                    }
                } else {
                    if (!append_export_json(dest, dest_size, used, "%c", (int)*p)) {
                        return false;
                    }
                }
                break;
            }
        }
    }
    return append_export_json(dest, dest_size, used, "\"");
}

static bool append_export_json_string_field(char *dest, size_t dest_size, size_t *used,
                                            const char *key, const char *value)
{
    return append_export_json(dest, dest_size, used, "\"%s\":", key) &&
           append_export_json_string(dest, dest_size, used, value);
}

static bool build_diagnostic_export_payload(const char *token,
                                            const d1l_storage_status_t *status,
                                            char *dest,
                                            size_t dest_size,
                                            size_t *out_len)
{
    if (!token || !status || !dest || !out_len || dest_size == 0) {
        return false;
    }
    size_t used = 0;
    d1l_health_snapshot_t h = d1l_health_snapshot();
    d1l_crash_log_stats_t crash_stats = d1l_crash_log_stats();
    d1l_crash_log_entry_t crash_entries[D1L_CRASH_LOG_CAPACITY];
    const size_t copied = d1l_crash_log_copy_recent(crash_entries, D1L_CRASH_LOG_CAPACITY);

    if (!append_export_json(dest, dest_size, &used,
                            "{\"schema\":1,\"kind\":\"diagnostic_export\",\"token\":\"%s\","
                            "\"public_rf_tx\":false,\"formats_sd\":false,"
                            "\"storage\":{\"sd_state\":\"%s\",\"data_backend\":\"%s\","
                            "\"export_backend\":\"%s\",\"message_store_backend\":\"%s\","
                            "\"dm_store_backend\":\"%s\",\"route_store_backend\":\"%s\","
                            "\"packet_log_backend\":\"%s\",\"map_tile_backend\":\"%s\","
                            "\"file_ops\":%s,\"atomic_rename\":%s},",
                            token,
                            status->sd_state ? status->sd_state : "unknown",
                            status->data_backend ? status->data_backend : "nvs",
                            status->export_backend ? status->export_backend : "serial",
                            status->message_store_backend ? status->message_store_backend : "nvs",
                            status->dm_store_backend ? status->dm_store_backend : "nvs",
                            status->route_store_backend ? status->route_store_backend : "nvs",
                            status->packet_log_backend ? status->packet_log_backend : "nvs",
                            status->map_tile_backend ? status->map_tile_backend : "unavailable",
                            bool_json(status->file_ops_supported),
                            bool_json(status->atomic_rename_supported))) {
        return false;
    }
    if (!append_export_json(dest, dest_size, &used,
                            "\"health\":{\"boot_nonce\":%lu,\"uptime_ms\":%lu,\"heap_free\":%lu,"
                            "\"heap_min_free\":%lu,\"heap_largest_free\":%lu,"
                            "\"internal_heap_free\":%lu,\"internal_heap_min_free\":%lu,"
                            "\"internal_heap_largest_free\":%lu,"
                            "\"dma_heap_free\":%lu,\"dma_heap_largest_free\":%lu,"
                            "\"psram_free\":%lu,\"psram_min_free\":%lu,"
                            "\"psram_largest_free\":%lu,"
                            "\"current_task_stack_free_words\":%lu,"
                            "\"ui_task_stack_free_words\":%lu,"
                            "\"retained_task_stack_free_bytes\":%lu,"
                            "\"lvgl_free_bytes\":%lu,\"lvgl_largest_free_bytes\":%lu,"
                            "\"lvgl_used_pct\":%u,\"nvs_ready\":%s,\"nvs_error\":\"%s\",\"reset_reason\":\"%s\"},",
                            (unsigned long)h.boot_nonce,
                            (unsigned long)h.uptime_ms,
                            (unsigned long)h.heap_free,
                            (unsigned long)h.heap_min_free,
                            (unsigned long)h.heap_largest_free,
                            (unsigned long)h.internal_heap_free,
                            (unsigned long)h.internal_heap_min_free,
                            (unsigned long)h.internal_heap_largest_free,
                            (unsigned long)h.dma_heap_free,
                            (unsigned long)h.dma_heap_largest_free,
                            (unsigned long)h.psram_free,
                            (unsigned long)h.psram_min_free,
                            (unsigned long)h.psram_largest_free,
                            (unsigned long)h.current_task_stack_free_words,
                            (unsigned long)h.ui_task_stack_free_words,
                            (unsigned long)h.retained_task_stack_free_bytes,
                            (unsigned long)h.lvgl_free_bytes,
                            (unsigned long)h.lvgl_largest_free_bytes,
                            h.lvgl_used_pct,
                            bool_json(h.nvs_ready),
                            esp_err_to_name(h.nvs_error),
                            h.reset_reason ? h.reset_reason : "UNKNOWN")) {
        return false;
    }
    const bool map_tile_cache_ready = d1l_map_tile_store_sd_ready(status);
    if (!append_export_json(dest, dest_size, &used,
                            "\"limits\":{\"payload_max\":%u,\"file_chunk_max\":%u,"
                            "\"crashlog_capacity\":%u,\"sampled\":true},"
                            "\"map_tiles\":{\"exported\":false,\"cache_ready\":%s,"
                            "\"backend\":\"%s\",\"reason\":\"%s\"},"
                            "\"crashlog\":{\"count\":%u,\"capacity\":%u,"
                            "\"total_written\":%lu,\"dropped_oldest\":%lu,\"entries\":[",
                            (unsigned)D1L_EXPORT_DIAGNOSTIC_PAYLOAD_MAX,
                            (unsigned)D1L_RP2040_FILE_CHUNK_MAX,
                            (unsigned)D1L_CRASH_LOG_CAPACITY,
                            bool_json(map_tile_cache_ready),
                            status->map_tile_backend ? status->map_tile_backend : "unavailable",
                            map_tile_cache_ready ? "diagnostic_export_does_not_bundle_map_tiles" : "pending",
                            (unsigned)crash_stats.count,
                            (unsigned)crash_stats.capacity,
                            (unsigned long)crash_stats.total_written,
                            (unsigned long)crash_stats.dropped_oldest)) {
        return false;
    }
    for (size_t i = 0; i < copied; ++i) {
        const d1l_crash_log_entry_t *e = &crash_entries[i];
        if (!append_export_json(dest, dest_size, &used,
                                "%s{\"seq\":%lu,\"uptime_ms\":%lu,"
                                "\"reset_reason\":\"%s\",\"reset_reason_code\":%u,"
                                "\"crash_like\":%s,\"heap_free\":%lu,"
                                "\"heap_min_free\":%lu,\"psram_free\":%lu}",
                                i ? "," : "",
                                (unsigned long)e->seq,
                                (unsigned long)e->uptime_ms,
                                e->reset_reason,
                                e->reset_reason_code,
                                bool_json(e->crash_like),
                                (unsigned long)e->heap_free,
                                (unsigned long)e->heap_min_free,
                                (unsigned long)e->psram_free)) {
            return false;
        }
    }
    if (!append_export_json(dest, dest_size, &used, "]}}\n")) {
        return false;
    }
    *out_len = used;
    return true;
}

#define D1L_EXPORT_DATA_SAMPLE_MAX 4U

static bool append_data_export_message_entries(char *dest, size_t dest_size, size_t *used,
                                               const d1l_message_entry_t *entries,
                                               size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        const d1l_message_entry_t *e = &entries[i];
        if (!append_export_json(dest, dest_size, used,
                                "%s{\"seq\":%lu,\"uptime_ms\":%lu,",
                                i ? "," : "",
                                (unsigned long)e->seq,
                                (unsigned long)e->uptime_ms) ||
            !append_export_json_string_field(dest, dest_size, used, "direction", e->direction) ||
            !append_export_json(dest, dest_size, used, ",") ||
            !append_export_json_string_field(dest, dest_size, used, "author", e->author) ||
            !append_export_json(dest, dest_size, used, ",") ||
            !append_export_json_string_field(dest, dest_size, used, "text", e->text) ||
            !append_export_json(dest, dest_size, used,
                                ",\"rssi_dbm\":%d,\"snr_tenths\":%d,"
                                "\"path_hash_bytes\":%u,\"path_hops\":%u,"
                                "\"delivered\":%s}",
                                e->rssi_dbm,
                                e->snr_tenths,
                                (unsigned)e->path_hash_bytes,
                                (unsigned)e->path_hops,
                                bool_json(e->delivered))) {
            return false;
        }
    }
    return true;
}

static bool append_data_export_dm_entries(char *dest, size_t dest_size, size_t *used,
                                          const d1l_dm_entry_t *entries,
                                          size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        const d1l_dm_entry_t *e = &entries[i];
        if (!append_export_json(dest, dest_size, used,
                                "%s{\"seq\":%lu,\"uptime_ms\":%lu,",
                                i ? "," : "",
                                (unsigned long)e->seq,
                                (unsigned long)e->uptime_ms) ||
            !append_export_json_string_field(dest, dest_size, used, "contact_fingerprint", e->contact_fingerprint) ||
            !append_export_json(dest, dest_size, used, ",") ||
            !append_export_json_string_field(dest, dest_size, used, "contact_alias", e->contact_alias) ||
            !append_export_json(dest, dest_size, used, ",") ||
            !append_export_json_string_field(dest, dest_size, used, "direction", e->direction) ||
            !append_export_json(dest, dest_size, used, ",") ||
            !append_export_json_string_field(dest, dest_size, used, "text", e->text) ||
            !append_export_json(dest, dest_size, used,
                                ",\"rssi_dbm\":%d,\"snr_tenths\":%d,"
                                "\"path_hash_bytes\":%u,\"path_hops\":%u,"
                                "\"attempt\":%u,\"delivered\":%s,\"acked\":%s,"
                                "\"ack_hash\":%lu}",
                                e->rssi_dbm,
                                e->snr_tenths,
                                (unsigned)e->path_hash_bytes,
                                (unsigned)e->path_hops,
                                (unsigned)e->attempt,
                                bool_json(e->delivered),
                                bool_json(e->acked),
                                (unsigned long)e->ack_hash)) {
            return false;
        }
    }
    return true;
}

static bool append_data_export_route_entries(char *dest, size_t dest_size, size_t *used,
                                             const d1l_route_entry_t *entries,
                                             size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        const d1l_route_entry_t *e = &entries[i];
        if (!append_export_json(dest, dest_size, used,
                                "%s{\"seq\":%lu,\"first_seen_ms\":%lu,"
                                "\"last_seen_ms\":%lu,\"seen_count\":%lu,",
                                i ? "," : "",
                                (unsigned long)e->seq,
                                (unsigned long)e->first_seen_ms,
                                (unsigned long)e->last_seen_ms,
                                (unsigned long)e->seen_count) ||
            !append_export_json_string_field(dest, dest_size, used, "target", e->target) ||
            !append_export_json(dest, dest_size, used, ",") ||
            !append_export_json_string_field(dest, dest_size, used, "label", e->label) ||
            !append_export_json(dest, dest_size, used, ",") ||
            !append_export_json_string_field(dest, dest_size, used, "kind", e->kind) ||
            !append_export_json(dest, dest_size, used, ",") ||
            !append_export_json_string_field(dest, dest_size, used, "route", e->route) ||
            !append_export_json(dest, dest_size, used, ",") ||
            !append_export_json_string_field(dest, dest_size, used, "direction", e->direction) ||
            !append_export_json(dest, dest_size, used,
                                ",\"last_rssi_dbm\":%d,\"last_snr_tenths\":%d,"
                                "\"path_hash_bytes\":%u,\"path_hops\":%u,"
                                "\"confidence\":%u,\"payload_len\":%u}",
                                e->last_rssi_dbm,
                                e->last_snr_tenths,
                                (unsigned)e->path_hash_bytes,
                                (unsigned)e->path_hops,
                                (unsigned)e->confidence,
                                (unsigned)e->payload_len)) {
            return false;
        }
    }
    return true;
}

static bool append_data_export_packet_entries(char *dest, size_t dest_size, size_t *used,
                                              const d1l_packet_log_entry_t *entries,
                                              size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        const d1l_packet_log_entry_t *e = &entries[i];
        if (!append_export_json(dest, dest_size, used,
                                "%s{\"seq\":%lu,\"uptime_ms\":%lu,",
                                i ? "," : "",
                                (unsigned long)e->seq,
                                (unsigned long)e->uptime_ms) ||
            !append_export_json_string_field(dest, dest_size, used, "direction", e->direction) ||
            !append_export_json(dest, dest_size, used, ",") ||
            !append_export_json_string_field(dest, dest_size, used, "kind", e->kind) ||
            !append_export_json(dest, dest_size, used,
                                ",\"rssi_dbm\":%d,\"snr_tenths\":%d,"
                                "\"path_hash_bytes\":%u,\"path_hops\":%u,"
                                "\"payload_len\":%u,\"raw_len\":%u,"
                                "\"raw_truncated\":%s,",
                                e->rssi_dbm,
                                e->snr_tenths,
                                (unsigned)e->path_hash_bytes,
                                (unsigned)e->path_hops,
                                (unsigned)e->payload_len,
                                (unsigned)e->raw_len,
                                bool_json(e->raw_truncated)) ||
            !append_export_json_string_field(dest, dest_size, used, "note", e->note) ||
            !append_export_json(dest, dest_size, used, ",") ||
            !append_export_json_string_field(dest, dest_size, used, "raw_hex", e->raw_hex) ||
            !append_export_json(dest, dest_size, used, "}")) {
            return false;
        }
    }
    return true;
}

static bool append_data_export_contact_entries(char *dest, size_t dest_size, size_t *used,
                                               const d1l_contact_entry_t *entries,
                                               size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        const d1l_contact_entry_t *e = &entries[i];
        if (!append_export_json(dest, dest_size, used,
                                "%s{\"seq\":%lu,\"created_ms\":%lu,"
                                "\"updated_ms\":%lu,\"out_path_updated_ms\":%lu,",
                                i ? "," : "",
                                (unsigned long)e->seq,
                                (unsigned long)e->created_ms,
                                (unsigned long)e->updated_ms,
                                (unsigned long)e->out_path_updated_ms) ||
            !append_export_json_string_field(dest, dest_size, used, "fingerprint", e->fingerprint) ||
            !append_export_json(dest, dest_size, used, ",") ||
            !append_export_json_string_field(dest, dest_size, used, "public_key_hex", e->public_key_hex) ||
            !append_export_json(dest, dest_size, used, ",") ||
            !append_export_json_string_field(dest, dest_size, used, "alias", e->alias) ||
            !append_export_json(dest, dest_size, used, ",") ||
            !append_export_json_string_field(dest, dest_size, used, "heard_name", e->heard_name) ||
            !append_export_json(dest, dest_size, used, ",") ||
            !append_export_json_string_field(dest, dest_size, used, "type", e->type) ||
            !append_export_json(dest, dest_size, used,
                                ",\"last_rssi_dbm\":%d,\"last_snr_tenths\":%d,"
                                "\"path_hash_bytes\":%u,\"path_hops\":%u,"
                                "\"out_path_valid\":%s,\"out_path_len\":%u,"
                                "\"favorite\":%s,\"muted\":%s}",
                                e->last_rssi_dbm,
                                e->last_snr_tenths,
                                (unsigned)e->path_hash_bytes,
                                (unsigned)e->path_hops,
                                bool_json(e->out_path_valid),
                                (unsigned)e->out_path_len,
                                bool_json(e->favorite),
                                bool_json(e->muted))) {
            return false;
        }
    }
    return true;
}

static bool append_data_export_node_entries(char *dest, size_t dest_size, size_t *used,
                                            const d1l_node_entry_t *entries,
                                            size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        const d1l_node_entry_t *e = &entries[i];
        if (!append_export_json(dest, dest_size, used,
                                "%s{\"seq\":%lu,\"first_heard_ms\":%lu,"
                                "\"last_heard_ms\":%lu,\"advert_timestamp\":%lu,"
                                "\"heard_count\":%lu,",
                                i ? "," : "",
                                (unsigned long)e->seq,
                                (unsigned long)e->first_heard_ms,
                                (unsigned long)e->last_heard_ms,
                                (unsigned long)e->advert_timestamp,
                                (unsigned long)e->heard_count) ||
            !append_export_json_string_field(dest, dest_size, used, "fingerprint", e->fingerprint) ||
            !append_export_json(dest, dest_size, used, ",") ||
            !append_export_json_string_field(dest, dest_size, used, "public_key_hex", e->public_key_hex) ||
            !append_export_json(dest, dest_size, used, ",") ||
            !append_export_json_string_field(dest, dest_size, used, "name", e->name) ||
            !append_export_json(dest, dest_size, used, ",") ||
            !append_export_json_string_field(dest, dest_size, used, "type", e->type) ||
            !append_export_json(dest, dest_size, used,
                                ",\"rssi_dbm\":%d,\"snr_tenths\":%d,"
                                "\"path_hash_bytes\":%u,\"path_hops\":%u,"
                                "\"location_valid\":%s,\"lat_e6\":%ld,"
                                "\"lon_e6\":%ld,\"location_advert_timestamp\":%lu,"
                                "\"location_seq\":%lu}",
                                e->rssi_dbm,
                                e->snr_tenths,
                                (unsigned)e->path_hash_bytes,
                                (unsigned)e->path_hops,
                                bool_json(e->location_valid),
                                (long)e->lat_e6,
                                (long)e->lon_e6,
                                (unsigned long)e->location_advert_timestamp,
                                (unsigned long)e->location_seq)) {
            return false;
        }
    }
    return true;
}

static bool append_data_export_read_threads(char *dest, size_t dest_size, size_t *used,
                                            const d1l_read_state_dm_thread_t *threads,
                                            size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        const d1l_read_state_dm_thread_t *t = &threads[i];
        if (!append_export_json(dest, dest_size, used,
                                "%s{\"last_read_seq\":%lu,\"newest_rx_seq\":%lu,"
                                "\"unread_count\":%lu,\"muted\":%s,",
                                i ? "," : "",
                                (unsigned long)t->last_read_seq,
                                (unsigned long)t->newest_rx_seq,
                                (unsigned long)t->unread_count,
                                bool_json(t->muted)) ||
            !append_export_json_string_field(dest, dest_size, used, "fingerprint", t->fingerprint) ||
            !append_export_json(dest, dest_size, used, "}")) {
            return false;
        }
    }
    return true;
}

static bool build_data_export_payload(const char *token,
                                      const d1l_storage_status_t *status,
                                      char *dest,
                                      size_t dest_size,
                                      size_t *out_len)
{
    if (!token || !status || !dest || !out_len || dest_size == 0) {
        return false;
    }

    static d1l_message_entry_t messages[D1L_EXPORT_DATA_SAMPLE_MAX];
    static d1l_dm_entry_t dms[D1L_EXPORT_DATA_SAMPLE_MAX];
    static d1l_route_entry_t routes[D1L_EXPORT_DATA_SAMPLE_MAX];
    static d1l_packet_log_entry_t packets[D1L_EXPORT_DATA_SAMPLE_MAX];
    static d1l_contact_entry_t contacts[D1L_EXPORT_DATA_SAMPLE_MAX];
    static d1l_node_entry_t nodes[D1L_EXPORT_DATA_SAMPLE_MAX];
    static d1l_read_state_dm_thread_t read_threads[D1L_EXPORT_DATA_SAMPLE_MAX];

    const d1l_settings_t *settings = d1l_settings_current();
    const d1l_message_store_stats_t message_stats = d1l_message_store_stats();
    const d1l_dm_store_stats_t dm_stats = d1l_dm_store_stats();
    const d1l_route_store_stats_t route_stats = d1l_route_store_stats();
    const d1l_packet_log_stats_t packet_stats = d1l_packet_log_stats();
    const d1l_contact_store_stats_t contact_stats = d1l_contact_store_stats();
    const d1l_node_store_stats_t node_stats = d1l_node_store_stats();
    const d1l_read_state_stats_t read_stats = d1l_read_state_stats();
    const size_t message_count = d1l_message_store_copy_recent(messages, D1L_EXPORT_DATA_SAMPLE_MAX);
    const size_t dm_count = d1l_dm_store_copy_recent(dms, D1L_EXPORT_DATA_SAMPLE_MAX);
    const size_t route_count = d1l_route_store_copy_recent(routes, D1L_EXPORT_DATA_SAMPLE_MAX);
    const size_t packet_count = d1l_packet_log_copy_recent(packets, D1L_EXPORT_DATA_SAMPLE_MAX);
    const size_t contact_count = d1l_contact_store_copy_recent(contacts, D1L_EXPORT_DATA_SAMPLE_MAX);
    const size_t node_count = d1l_node_store_copy_recent(nodes, D1L_EXPORT_DATA_SAMPLE_MAX);
    const size_t read_thread_count = d1l_read_state_copy_dm_threads(read_threads,
                                                                    D1L_EXPORT_DATA_SAMPLE_MAX);
    size_t used = 0;

    if (!append_export_json(dest, dest_size, &used,
                            "{\"schema\":1,\"kind\":\"data_export\",\"token\":\"%s\","
                            "\"sampled\":true,\"public_rf_tx\":false,\"formats_sd\":false,"
                            "\"private_identity_exported\":false,"
                            "\"storage\":{\"sd_state\":\"%s\",\"data_backend\":\"%s\","
                            "\"export_backend\":\"%s\",\"stores\":{\"messages\":\"%s\","
                            "\"dm\":\"%s\",\"routes\":\"%s\",\"packets\":\"%s\","
                            "\"contacts\":\"onboard\",\"nodes\":\"onboard\","
                            "\"read_state\":\"onboard\",\"map_tiles\":\"%s\"},"
                            "\"file_ops\":%s,\"atomic_rename\":%s},"
                            "\"limits\":{\"payload_max\":%u,\"sample_max\":%u},",
                            token,
                            status->sd_state ? status->sd_state : "unknown",
                            status->data_backend ? status->data_backend : "nvs",
                            status->export_backend ? status->export_backend : "serial",
                            status->message_store_backend ? status->message_store_backend : "nvs",
                            status->dm_store_backend ? status->dm_store_backend : "nvs",
                            status->route_store_backend ? status->route_store_backend : "nvs",
                            status->packet_log_backend ? status->packet_log_backend : "nvs",
                            status->map_tile_backend ? status->map_tile_backend : "unavailable",
                            bool_json(status->file_ops_supported),
                            bool_json(status->atomic_rename_supported),
                            (unsigned)D1L_EXPORT_DATA_PAYLOAD_MAX,
                            (unsigned)D1L_EXPORT_DATA_SAMPLE_MAX)) {
        return false;
    }

    if (!append_export_json(dest, dest_size, &used,
                            "\"settings\":{\"role\":\"%s\",\"onboarding_complete\":%s,"
                            "\"region\":\"Canada/USA\",\"radio\":{\"frequency_hz\":%lu,"
                            "\"bandwidth_khz\":%.1f,\"sf\":%u,\"cr\":%u,"
                            "\"tx_power_dbm\":%d,\"rx_boost\":%s,\"tcxo\":\"%s\"},"
                            "\"map_location\":{\"set\":%s,\"source\":\"%s\","
                            "\"lat_e7\":%ld,\"lon_e7\":%ld},",
                            d1l_settings_role_name(settings->role),
                            bool_json(settings->onboarding_complete),
                            (unsigned long)settings->frequency_hz,
                            ((float)settings->bandwidth_tenths_khz) / 10.0f,
                            settings->spreading_factor,
                            settings->coding_rate,
                            settings->tx_power_dbm,
                            bool_json(settings->rx_boost),
                            d1l_settings_tcxo_name(settings->tcxo_mode),
                            bool_json(settings->map_location_set),
                            settings->map_location_set ? "manual" : "unset",
                            (long)(settings->map_location_set ? settings->map_lat_e7 : 0),
                            (long)(settings->map_location_set ? settings->map_lon_e7 : 0)) ||
        !append_export_json_string_field(dest, dest_size, &used, "node_name", settings->node_name) ||
        !append_export_json(dest, dest_size, &used, "},")) {
        return false;
    }

    if (!append_export_json(dest, dest_size, &used,
                            "\"messages\":{\"count\":%u,\"capacity\":%u,"
                            "\"total_written\":%lu,\"dropped_oldest\":%lu,"
                            "\"entries\":[",
                            (unsigned)message_stats.count,
                            (unsigned)message_stats.capacity,
                            (unsigned long)message_stats.total_written,
                            (unsigned long)message_stats.dropped_oldest) ||
        !append_data_export_message_entries(dest, dest_size, &used, messages, message_count) ||
        !append_export_json(dest, dest_size, &used, "]},")) {
        return false;
    }

    if (!append_export_json(dest, dest_size, &used,
                            "\"dm\":{\"count\":%u,\"capacity\":%u,"
                            "\"total_written\":%lu,\"dropped_oldest\":%lu,"
                            "\"entries\":[",
                            (unsigned)dm_stats.count,
                            (unsigned)dm_stats.capacity,
                            (unsigned long)dm_stats.total_written,
                            (unsigned long)dm_stats.dropped_oldest) ||
        !append_data_export_dm_entries(dest, dest_size, &used, dms, dm_count) ||
        !append_export_json(dest, dest_size, &used, "]},")) {
        return false;
    }

    if (!append_export_json(dest, dest_size, &used,
                            "\"routes\":{\"count\":%u,\"capacity\":%u,"
                            "\"total_written\":%lu,\"dropped_oldest\":%lu,"
                            "\"entries\":[",
                            (unsigned)route_stats.count,
                            (unsigned)route_stats.capacity,
                            (unsigned long)route_stats.total_written,
                            (unsigned long)route_stats.dropped_oldest) ||
        !append_data_export_route_entries(dest, dest_size, &used, routes, route_count) ||
        !append_export_json(dest, dest_size, &used, "]},")) {
        return false;
    }

    if (!append_export_json(dest, dest_size, &used,
                            "\"packets\":{\"count\":%u,\"capacity\":%u,"
                            "\"total_written\":%lu,\"dropped_oldest\":%lu,"
                            "\"entries\":[",
                            (unsigned)packet_stats.count,
                            (unsigned)packet_stats.capacity,
                            (unsigned long)packet_stats.total_written,
                            (unsigned long)packet_stats.dropped_oldest) ||
        !append_data_export_packet_entries(dest, dest_size, &used, packets, packet_count) ||
        !append_export_json(dest, dest_size, &used, "]},")) {
        return false;
    }

    if (!append_export_json(dest, dest_size, &used,
                            "\"contacts\":{\"count\":%u,\"capacity\":%u,"
                            "\"total_written\":%lu,\"dropped_oldest\":%lu,"
                            "\"entries\":[",
                            (unsigned)contact_stats.count,
                            (unsigned)contact_stats.capacity,
                            (unsigned long)contact_stats.total_written,
                            (unsigned long)contact_stats.dropped_oldest) ||
        !append_data_export_contact_entries(dest, dest_size, &used, contacts, contact_count) ||
        !append_export_json(dest, dest_size, &used, "]},")) {
        return false;
    }

    if (!append_export_json(dest, dest_size, &used,
                            "\"nodes\":{\"count\":%u,\"capacity\":%u,"
                            "\"total_written\":%lu,\"dropped_oldest\":%lu,"
                            "\"entries\":[",
                            (unsigned)node_stats.count,
                            (unsigned)node_stats.capacity,
                            (unsigned long)node_stats.total_written,
                            (unsigned long)node_stats.dropped_oldest) ||
        !append_data_export_node_entries(dest, dest_size, &used, nodes, node_count) ||
        !append_export_json(dest, dest_size, &used, "]},")) {
        return false;
    }

    if (!append_export_json(dest, dest_size, &used,
                            "\"read_state\":{\"last_public_read_seq\":%lu,"
                            "\"last_dm_read_seq\":%lu,\"newest_public_rx_seq\":%lu,"
                            "\"newest_dm_rx_seq\":%lu,\"public_unread_count\":%lu,"
                            "\"dm_unread_count\":%lu,\"muted_dm_unread_count\":%lu,"
                            "\"dm_thread_count\":%lu,\"mark_read_count\":%lu,"
                            "\"threads\":[",
                            (unsigned long)read_stats.last_public_read_seq,
                            (unsigned long)read_stats.last_dm_read_seq,
                            (unsigned long)read_stats.newest_public_rx_seq,
                            (unsigned long)read_stats.newest_dm_rx_seq,
                            (unsigned long)read_stats.public_unread_count,
                            (unsigned long)read_stats.dm_unread_count,
                            (unsigned long)read_stats.muted_dm_unread_count,
                            (unsigned long)read_stats.dm_thread_count,
                            (unsigned long)read_stats.mark_read_count) ||
        !append_data_export_read_threads(dest, dest_size, &used, read_threads, read_thread_count) ||
        !append_export_json(dest, dest_size, &used, "]}}\n")) {
        return false;
    }

    *out_len = used;
    return true;
}

static void cmd_storage_export_diagnostics(const char *line)
{
    static const char prefix[] = "storage export-diagnostics ";
    char token[D1L_EXPORT_CANARY_TOKEN_MAX + 1U] = {0};
    if (strncmp(line, prefix, strlen(prefix)) != 0 ||
        !copy_storage_canary_token(token, sizeof(token), line + strlen(prefix))) {
        err_result("storage export-diagnostics", "INVALID_TOKEN",
                   "usage: storage export-diagnostics <token-with-a-z-0-9-dot-dash-underscore>");
        return;
    }

    (void)d1l_storage_status_refresh(D1L_STORAGE_RP2040_SD_PROBE_TIMEOUT_MS);
    d1l_storage_status_t status = {0};
    d1l_storage_status(&status);

    static char payload[D1L_EXPORT_DIAGNOSTIC_PAYLOAD_MAX];
    size_t payload_len = 0;
    if (!build_diagnostic_export_payload(token, &status, payload, sizeof(payload),
                                         &payload_len)) {
        d1l_export_canary_result_t failed = {0};
        print_storage_export_error(
            "storage export-diagnostics", "payload", ESP_ERR_INVALID_SIZE,
            &status, &failed,
            "diagnostic export payload exceeded its bounded buffer; no SD write was attempted");
        return;
    }

    d1l_export_canary_result_t export_result = {0};
    esp_err_t ret = d1l_export_store_write_diagnostics(
        token, (const uint8_t *)payload, payload_len, &status, &export_result);
    if (ret != ESP_OK) {
        print_storage_export_error(
            "storage export-diagnostics", export_result.step, ret, &status,
            &export_result,
            "requires a ready RP2040 SD bridge with file_ops and atomic rename; no Public RF or format command was used");
        return;
    }

    d1l_storage_status(&status);
    ok_begin("storage export-diagnostics");
    printf(",\"token\":");
    print_json_string(export_result.token);
    printf(",\"kind\":\"diagnostic_export\",\"path\":");
    print_json_string(export_result.path);
    printf(",\"tmp_path\":");
    print_json_string(export_result.tmp_path);
    printf(",\"bytes\":%u,\"chunks_written\":%lu,\"chunks_verified_tmp\":%lu,\"chunks_verified_final\":%lu,\"tmp_verified_bytes\":%u,\"final_verified_bytes\":%u,\"write_tmp\":%s,\"read_tmp\":%s,\"rename_replace\":%s,\"stat_final\":%s,\"read_final\":%s,\"public_rf_tx\":false,\"formats_sd\":false,\"export_backend\":",
           (unsigned)export_result.bytes,
           (unsigned long)export_result.chunks_written,
           (unsigned long)export_result.chunks_verified_tmp,
           (unsigned long)export_result.chunks_verified_final,
           (unsigned)export_result.tmp_verified_bytes,
           (unsigned)export_result.final_verified_bytes,
           bool_json(export_result.write_tmp),
           bool_json(export_result.read_tmp),
           bool_json(export_result.rename_replace),
           bool_json(export_result.stat_final),
           bool_json(export_result.read_final));
    print_json_string(status.export_backend ? status.export_backend : "serial");
    printf(",\"note\":\"Diagnostic export JSON committed to SD by chunked temp write and atomic rename; no Public RF or format command was used\"}\n");
}

static void cmd_storage_export_data(const char *line)
{
    static const char prefix[] = "storage export-data ";
    char token[D1L_EXPORT_CANARY_TOKEN_MAX + 1U] = {0};
    if (strncmp(line, prefix, strlen(prefix)) != 0 ||
        !copy_storage_canary_token(token, sizeof(token), line + strlen(prefix))) {
        err_result("storage export-data", "INVALID_TOKEN",
                   "usage: storage export-data <token-with-a-z-0-9-dot-dash-underscore>");
        return;
    }

    (void)d1l_storage_status_refresh(D1L_STORAGE_RP2040_SD_PROBE_TIMEOUT_MS);
    d1l_storage_status_t status = {0};
    d1l_storage_status(&status);

    static char payload[D1L_EXPORT_DATA_PAYLOAD_MAX];
    size_t payload_len = 0;
    if (!build_data_export_payload(token, &status, payload, sizeof(payload),
                                   &payload_len)) {
        d1l_export_canary_result_t failed = {0};
        print_storage_export_error(
            "storage export-data", "payload", ESP_ERR_INVALID_SIZE,
            &status, &failed,
            "data export payload exceeded its bounded buffer; no SD write was attempted");
        return;
    }

    d1l_export_canary_result_t export_result = {0};
    esp_err_t ret = d1l_export_store_write_data(
        token, (const uint8_t *)payload, payload_len, &status, &export_result);
    if (ret != ESP_OK) {
        print_storage_export_error(
            "storage export-data", export_result.step, ret, &status,
            &export_result,
            "requires a ready RP2040 SD bridge with file_ops and atomic rename; no Public RF or format command was used");
        return;
    }

    d1l_storage_status(&status);
    ok_begin("storage export-data");
    printf(",\"token\":");
    print_json_string(export_result.token);
    printf(",\"kind\":\"data_export\",\"path\":");
    print_json_string(export_result.path);
    printf(",\"tmp_path\":");
    print_json_string(export_result.tmp_path);
    printf(",\"bytes\":%u,\"chunks_written\":%lu,\"chunks_verified_tmp\":%lu,\"chunks_verified_final\":%lu,\"tmp_verified_bytes\":%u,\"final_verified_bytes\":%u,\"write_tmp\":%s,\"read_tmp\":%s,\"rename_replace\":%s,\"stat_final\":%s,\"read_final\":%s,\"sampled\":true,\"private_identity_exported\":false,\"public_rf_tx\":false,\"formats_sd\":false,\"export_backend\":",
           (unsigned)export_result.bytes,
           (unsigned long)export_result.chunks_written,
           (unsigned long)export_result.chunks_verified_tmp,
           (unsigned long)export_result.chunks_verified_final,
           (unsigned)export_result.tmp_verified_bytes,
           (unsigned)export_result.final_verified_bytes,
           bool_json(export_result.write_tmp),
           bool_json(export_result.read_tmp),
           bool_json(export_result.rename_replace),
           bool_json(export_result.stat_final),
           bool_json(export_result.read_final));
    print_json_string(status.export_backend ? status.export_backend : "serial");
    printf(",\"note\":\"Sampled user data export JSON committed to SD by chunked temp write and atomic rename; no private identity material, Public RF, or format command was used\"}\n");
}

static uint32_t retained_canary_hash(const char *token)
{
    uint32_t hash = 2166136261UL;
    for (const unsigned char *p = (const unsigned char *)token; p && *p; ++p) {
        hash ^= (uint32_t)*p;
        hash *= 16777619UL;
    }
    return hash ? hash : 1UL;
}

static void retained_canary_fingerprint(const char *token,
                                        char *fingerprint,
                                        size_t fingerprint_size)
{
    const uint32_t hash = retained_canary_hash(token);
    snprintf(fingerprint, fingerprint_size, "%08lX%08lX",
             (unsigned long)hash,
             (unsigned long)(hash ^ 0xA5A5F00DUL));
}

static void cmd_ui_data_canary(const char *line)
{
    static const char prefix[] = "ui data-canary ";
    char token[32] = {0};
    if (strncmp(line, prefix, strlen(prefix)) != 0 ||
        !copy_storage_canary_token(token, sizeof(token), line + strlen(prefix))) {
        err_result("ui data-canary", "INVALID_TOKEN",
                   "usage: ui data-canary <token-with-a-z-0-9-dot-dash-underscore>");
        return;
    }

    char fingerprint[D1L_NODE_FINGERPRINT_LEN] = {0};
    char text[D1L_MESSAGE_TEXT_LEN] = {0};
    retained_canary_fingerprint(token, fingerprint, sizeof(fingerprint));
    snprintf(text, sizeof(text), "ui-data-canary %s", token);

    const uint32_t public_seq = d1l_message_store_stats().next_seq;
    esp_err_t ret = d1l_message_store_append_public_volatile("rx", "UI Canary", text,
                                                             0, 0, 1, 0, true);
    if (ret != ESP_OK) {
        err_result("ui data-canary", esp_err_to_name(ret),
                   "could not append public UI canary");
        return;
    }

    const uint32_t dm_seq = d1l_dm_store_stats().next_seq;
    ret = d1l_dm_store_append_volatile(fingerprint, "UI Canary", "rx", text,
                                       0, 0, 1, 0, 0, true, true,
                                       retained_canary_hash(token));
    if (ret != ESP_OK) {
        err_result("ui data-canary", esp_err_to_name(ret),
                   "could not append DM UI canary");
        return;
    }

    const uint32_t route_seq = d1l_route_store_stats().next_seq;
    ret = d1l_route_store_upsert_observation_volatile(fingerprint, "UI Canary",
                                                      "ui_canary", "local", "rx",
                                                      0, 0, 1, 0, (uint16_t)strlen(text));
    if (ret != ESP_OK) {
        err_result("ui data-canary", esp_err_to_name(ret),
                   "could not append route UI canary");
        return;
    }

    const uint32_t packet_seq = d1l_packet_log_stats().next_seq;
    char packet_note[D1L_PACKET_LOG_NOTE_LEN] = {0};
    snprintf(packet_note, sizeof(packet_note), "ui-canary %s", token);
    d1l_packet_log_entry_t packet = {
        .direction = "rx",
        .kind = "ui_canary",
        .rssi_dbm = 0,
        .snr_tenths = 0,
        .path_hash_bytes = 1,
        .path_hops = 0,
        .payload_len = (uint16_t)strlen(text),
    };
    strncpy(packet.note, packet_note, sizeof(packet.note) - 1U);
    if (!d1l_packet_log_append_raw_volatile(&packet, (const uint8_t *)text, strlen(text))) {
        err_result("ui data-canary", "ESP_FAIL",
                   "could not append packet UI canary");
        return;
    }

    ok_begin("ui data-canary");
    printf(",\"token\":");
    print_json_string(token);
    printf(",\"fingerprint\":\"%s\",\"public_seq\":%lu,\"dm_seq\":%lu,\"route_seq\":%lu,\"packet_seq\":%lu",
           fingerprint,
           (unsigned long)public_seq,
           (unsigned long)dm_seq,
           (unsigned long)route_seq,
           (unsigned long)packet_seq);
    printf(",\"storage_required\":false,\"retention\":\"volatile\",\"public_rf_tx\":false,\"formats_sd\":false");
    printf(",\"note\":\"Synthetic UI refresh canary rows appended to RAM only, without Public RF, SD readiness, or format command\"}\n");
}

static void cmd_storage_retained_canary(const char *line)
{
    static const char prefix[] = "storage retained-canary ";
    char token[32] = {0};
    if (strncmp(line, prefix, strlen(prefix)) != 0 ||
        !copy_storage_canary_token(token, sizeof(token), line + strlen(prefix))) {
        err_result("storage retained-canary", "INVALID_TOKEN",
                   "usage: storage retained-canary <token-with-a-z-0-9-dot-dash-underscore>");
        return;
    }

    const int64_t quiesce_started_us = esp_timer_get_time();
    uint32_t remaining_ms = deadline_remaining_ms(
        quiesce_started_us, D1L_RETAINED_CANARY_QUIESCE_TIMEOUT_MS);
    esp_err_t ret = remaining_ms > 0U ?
        d1l_storage_manager_quiesce_begin(remaining_ms) : ESP_ERR_TIMEOUT;
    if (ret != ESP_OK) {
        err_result("storage retained-canary", esp_err_to_name(ret),
                   "storage manager quiesce failed; retained canary cancelled");
        return;
    }

    remaining_ms = deadline_remaining_ms(
        quiesce_started_us, D1L_RETAINED_CANARY_QUIESCE_TIMEOUT_MS);
    ret = remaining_ms > 0U ?
        d1l_route_store_worker_quiesce_begin(remaining_ms) : ESP_ERR_TIMEOUT;
    if (ret != ESP_OK) {
        d1l_storage_manager_quiesce_end();
        err_result("storage retained-canary", esp_err_to_name(ret),
                   "retained worker quiesce failed; retained canary cancelled");
        return;
    }

    remaining_ms = deadline_remaining_ms(
        quiesce_started_us, D1L_RETAINED_CANARY_QUIESCE_TIMEOUT_MS);
    const uint32_t refresh_timeout_ms =
        remaining_ms < D1L_STORAGE_RP2040_SD_PROBE_TIMEOUT_MS ?
            remaining_ms : D1L_STORAGE_RP2040_SD_PROBE_TIMEOUT_MS;
    ret = refresh_timeout_ms > 0U ?
        d1l_storage_status_refresh(refresh_timeout_ms) : ESP_ERR_TIMEOUT;
    if (ret != ESP_OK) {
        d1l_route_store_worker_quiesce_end();
        d1l_storage_manager_quiesce_end();
        err_result("storage retained-canary", esp_err_to_name(ret),
                   "fresh storage status failed; retained canary cancelled");
        return;
    }

    d1l_storage_status_t status = {0};
    d1l_storage_status(&status);
    if (!storage_retained_history_sd_ready(&status)) {
        d1l_route_store_worker_quiesce_end();
        d1l_storage_manager_quiesce_end();
        err_result("storage retained-canary", "SD_RETAINED_HISTORY_NOT_READY",
                   "requires ready RP2040 file ops and sd backends for messages, dm, routes, and packets");
        return;
    }

    char fingerprint[D1L_NODE_FINGERPRINT_LEN] = {0};
    char text[D1L_MESSAGE_TEXT_LEN] = {0};
    retained_canary_fingerprint(token, fingerprint, sizeof(fingerprint));
    snprintf(text, sizeof(text), "sd-retained-canary %s", token);

    const uint32_t public_seq = d1l_message_store_stats().next_seq;
    ret = d1l_message_store_append_public("tx", "SD Canary", text,
                                          0, 0, 1, 0, true);
    if (ret != ESP_OK) {
        d1l_route_store_worker_quiesce_end();
        d1l_storage_manager_quiesce_end();
        err_result("storage retained-canary", esp_err_to_name(ret),
                   "could not append public retained-history canary");
        return;
    }

    const uint32_t dm_seq = d1l_dm_store_stats().next_seq;
    ret = d1l_dm_store_append(fingerprint, "SD Canary", "tx", text,
                              0, 0, 1, 0, 0, true, true,
                              retained_canary_hash(token));
    if (ret != ESP_OK) {
        d1l_route_store_worker_quiesce_end();
        d1l_storage_manager_quiesce_end();
        err_result("storage retained-canary", esp_err_to_name(ret),
                   "could not append DM retained-history canary");
        return;
    }

    const uint32_t route_seq = d1l_route_store_stats().next_seq;
    ret = d1l_route_store_upsert_observation(fingerprint, "SD Canary",
                                             "sd_canary", "local", "tx",
                                             0, 0, 1, 0, (uint16_t)strlen(text));
    if (ret != ESP_OK) {
        d1l_route_store_worker_quiesce_end();
        d1l_storage_manager_quiesce_end();
        err_result("storage retained-canary", esp_err_to_name(ret),
                   "could not append route retained-history canary");
        return;
    }

    const uint32_t packet_seq = d1l_packet_log_stats().next_seq;
    char packet_note[D1L_PACKET_LOG_NOTE_LEN] = {0};
    snprintf(packet_note, sizeof(packet_note), "sd-canary %s", token);
    d1l_packet_log_entry_t packet = {
        .direction = "tx",
        .kind = "sd_canary",
        .rssi_dbm = 0,
        .snr_tenths = 0,
        .path_hash_bytes = 1,
        .path_hops = 0,
        .payload_len = (uint16_t)strlen(text),
    };
    strncpy(packet.note, packet_note, sizeof(packet.note) - 1U);
    if (!d1l_packet_log_append_raw(&packet, (const uint8_t *)text, strlen(text))) {
        d1l_route_store_worker_quiesce_end();
        d1l_storage_manager_quiesce_end();
        err_result("storage retained-canary", "ESP_FAIL",
                   "could not append packet retained-history canary");
        return;
    }

    d1l_route_store_worker_quiesce_end();
    d1l_storage_manager_quiesce_end();
    ok_begin("storage retained-canary");
    printf(",\"token\":");
    print_json_string(token);
    printf(",\"fingerprint\":\"%s\",\"public_seq\":%lu,\"dm_seq\":%lu,\"route_seq\":%lu,\"packet_seq\":%lu",
           fingerprint,
           (unsigned long)public_seq,
           (unsigned long)dm_seq,
           (unsigned long)route_seq,
           (unsigned long)packet_seq);
    printf(",\"storage_manager_quiesced\":true,\"retained_worker_quiesced\":true,\"public_rf_tx\":false,\"formats_sd\":false,\"backends\":{\"messages\":\"%s\",\"dm\":\"%s\",\"routes\":\"%s\",\"packets\":\"%s\"}",
           status.message_store_backend,
           status.dm_store_backend,
           status.route_store_backend,
           status.packet_log_backend);
    printf(",\"note\":\"Synthetic retained-history SD canary rows appended without Public RF or format command\"}\n");
}

static void cmd_storage_setup(const char *line)
{
    (void)line;
    d1l_storage_status_t status = {0};
    d1l_storage_status(&status);

    ok_begin("storage setup");
    print_storage_setup_payload(&status);
}

static void print_packet_entry_json(const d1l_packet_log_entry_t *e)
{
    printf("{\"seq\":%lu,\"uptime_ms\":%lu,\"direction\":\"%s\",\"kind\":\"%s\",\"rssi_dbm\":%d,\"snr_tenths\":%d,\"path_hash_bytes\":%u,\"path_hops\":%u,\"payload_len\":%u,\"raw_len\":%u,\"raw_truncated\":%s,\"note\":\"%s\",\"raw_hex\":\"%s\"}",
           (unsigned long)e->seq, (unsigned long)e->uptime_ms,
           e->direction, e->kind, e->rssi_dbm, e->snr_tenths,
           e->path_hash_bytes, e->path_hops, e->payload_len, e->raw_len,
           bool_json(e->raw_truncated), e->note, e->raw_hex);
}

static void print_packet_entries_json(const char *cmd, const char *direction, const char *kind,
                                      const char *search_text,
                                      const d1l_packet_log_entry_t *entries, size_t copied,
                                      const d1l_packet_log_stats_t *stats)
{
    ok_begin(cmd);
    printf(",\"count\":%u,\"capacity\":%u,\"total_written\":%lu,\"dropped_oldest\":%lu",
           (unsigned)stats->count, (unsigned)stats->capacity,
           (unsigned long)stats->total_written, (unsigned long)stats->dropped_oldest);
    printf(",\"sd_history\":{\"enabled\":%s,\"records\":%lu,\"capacity\":%u,\"failed_writes\":%lu}",
           bool_json(stats->sd_history_enabled),
           (unsigned long)stats->sd_history_records,
           (unsigned)stats->sd_capacity,
           (unsigned long)stats->sd_history_failed_writes);
    if (direction || kind || search_text) {
        printf(",\"filter\":{\"direction\":\"%s\",\"kind\":\"%s\",\"search\":\"%s\"}",
               direction ? direction : "any", kind ? kind : "any", search_text ? search_text : "");
    }
    printf(",\"persistence\":{\"loaded\":%s,\"dirty\":%s,\"revision\":%llu,\"commits\":%lu,\"failures\":%lu,\"reconcile\":{\"pending\":%s,\"count\":%lu,\"failures\":%lu,\"last_error\":\"%s\"},\"sd\":{\"required\":%s,\"generation\":%lu,\"dirty\":%s,\"commits\":%lu,\"failures\":%lu,\"last_error\":\"%s\"},\"nvs\":{\"dirty\":%s,\"commits\":%lu,\"failures\":%lu,\"last_error\":\"%s\"},\"journal\":{\"dirty\":%s,\"commits\":%lu,\"failures\":%lu,\"last_error\":\"%s\"}}",
           bool_json(stats->loaded),
           bool_json(stats->persistence_dirty),
           (unsigned long long)stats->persistence_revision,
           (unsigned long)stats->persistence_commit_count,
           (unsigned long)stats->persistence_fail_count,
           bool_json(stats->sd_primary_reconcile_pending),
           (unsigned long)stats->sd_reconcile_count,
           (unsigned long)stats->sd_reconcile_fail_count,
           esp_err_to_name(stats->sd_reconcile_last_error),
           bool_json(stats->sd_history_enabled),
           (unsigned long)stats->sd_backend_generation,
           bool_json(stats->sd_primary_dirty),
           (unsigned long)stats->sd_primary_commit_count,
           (unsigned long)stats->sd_primary_fail_count,
           esp_err_to_name(stats->sd_primary_last_error),
           bool_json(stats->nvs_fallback_dirty),
           (unsigned long)stats->nvs_fallback_commit_count,
           (unsigned long)stats->nvs_fallback_fail_count,
           esp_err_to_name(stats->nvs_fallback_last_error),
           bool_json(stats->journal_dirty),
           (unsigned long)stats->journal_commit_count,
           (unsigned long)stats->journal_fail_count,
           esp_err_to_name(stats->journal_last_error));
    printf(",\"entries\":[");
    for (size_t i = 0; i < copied; ++i) {
        printf("%s", i ? "," : "");
        print_packet_entry_json(&entries[i]);
    }
    printf("],\"persisted\":%s,\"note\":\"Packet log records recent MeshCore RF TX/RX evidence\"}\n",
           bool_json(stats->loaded && !stats->persistence_dirty));
}

static bool read_packet_token(const char **cursor, char *dest, size_t dest_size)
{
    if (!cursor || !*cursor || !dest || dest_size == 0) {
        return false;
    }
    while (**cursor == ' ') {
        (*cursor)++;
    }
    size_t out = 0;
    while (**cursor != '\0' && **cursor != ' ' && out + 1U < dest_size) {
        unsigned char c = (unsigned char)**cursor;
        if (c < 32 || c > 126 || c == '"' || c == '\\') {
            c = '_';
        }
        dest[out++] = (char)c;
        (*cursor)++;
    }
    dest[out] = '\0';
    while (**cursor == ' ') {
        (*cursor)++;
    }
    return out > 0;
}

static void copy_packet_search(char *dest, size_t dest_size, const char *src)
{
    if (!dest || dest_size == 0) {
        return;
    }
    size_t out = 0;
    while (src && *src == ' ') {
        src++;
    }
    while (src && src[0] && out + 1U < dest_size) {
        unsigned char c = (unsigned char)*src++;
        if (c < 32 || c > 126 || c == '"' || c == '\\') {
            c = '_';
        }
        dest[out++] = (char)c;
    }
    while (out > 0 && dest[out - 1U] == ' ') {
        out--;
    }
    dest[out] = '\0';
}

static void cmd_packets(void)
{
    d1l_packet_log_stats_t stats = d1l_packet_log_stats();
    static d1l_packet_log_entry_t entries[8];
    size_t copied = d1l_packet_log_copy_recent(entries, 8);
    print_packet_entries_json("packets", NULL, NULL, NULL, entries, copied, &stats);
}

static void cmd_packets_filter(const char *line)
{
    const char *arg = line + strlen("packets filter ");
    char direction[8] = "any";
    char kind[16] = "any";
    if (!read_packet_token(&arg, direction, sizeof(direction)) ||
        !read_packet_token(&arg, kind, sizeof(kind)) ||
        arg[0] != '\0') {
        err_result("packets filter", "INVALID_FILTER", "usage: packets filter <any|rx|tx> <any|text|kind>");
        return;
    }

    d1l_packet_log_stats_t stats = d1l_packet_log_stats();
    static d1l_packet_log_entry_t entries[8];
    size_t copied = d1l_packet_log_query(entries, 8, direction, kind, NULL);
    print_packet_entries_json("packets filter", direction, kind, NULL, entries, copied, &stats);
}

static void cmd_packets_search(const char *line)
{
    char search[D1L_PACKET_LOG_QUERY_TEXT_LEN] = {0};
    copy_packet_search(search, sizeof(search), line + strlen("packets search "));
    if (search[0] == '\0') {
        err_result("packets search", "INVALID_QUERY", "usage: packets search <text>");
        return;
    }

    d1l_packet_log_stats_t stats = d1l_packet_log_stats();
    static d1l_packet_log_entry_t entries[8];
    size_t copied = d1l_packet_log_query(entries, 8, "any", "any", search);
    print_packet_entries_json("packets search", "any", "any", search, entries, copied, &stats);
}

static void cmd_packets_detail(const char *line)
{
    const char *arg = line + strlen("packets detail ");
    while (*arg == ' ') {
        arg++;
    }
    char *end = NULL;
    unsigned long seq = strtoul(arg, &end, 10);
    while (end && *end == ' ') {
        end++;
    }
    if (arg[0] == '\0' || end == arg || (end && *end != '\0') || seq == 0 || seq > UINT32_MAX) {
        err_result("packets detail", "INVALID_SEQ", "usage: packets detail <seq>");
        return;
    }

    d1l_packet_log_entry_t entry = {0};
    esp_err_t ret = d1l_packet_log_find_by_seq((uint32_t)seq, &entry);
    if (ret != ESP_OK) {
        err_result("packets detail", esp_err_to_name(ret), "packet sequence not found");
        return;
    }

    ok_begin("packets detail");
    printf(",\"entry\":");
    print_packet_entry_json(&entry);
    printf("}\n");
}

static void cmd_packets_raw(const char *line)
{
    const char *arg = line + strlen("packets raw ");
    while (*arg == ' ') {
        arg++;
    }
    char *end = NULL;
    unsigned long seq = strtoul(arg, &end, 10);
    while (end && *end == ' ') {
        end++;
    }
    if (arg[0] == '\0' || end == arg || (end && *end != '\0') || seq == 0 || seq > UINT32_MAX) {
        err_result("packets raw", "INVALID_SEQ", "usage: packets raw <seq>");
        return;
    }

    d1l_packet_log_entry_t entry = {0};
    esp_err_t ret = d1l_packet_log_find_by_seq((uint32_t)seq, &entry);
    if (ret != ESP_OK) {
        err_result("packets raw", esp_err_to_name(ret), "packet sequence not found");
        return;
    }

    ok_begin("packets raw");
    printf(",\"seq\":%lu,\"raw_len\":%u,\"raw_preview_bytes\":%u,\"raw_truncated\":%s,\"raw_hex\":\"%s\",\"entry\":",
           (unsigned long)entry.seq, entry.raw_len, D1L_PACKET_LOG_RAW_PREVIEW_BYTES,
           bool_json(entry.raw_truncated), entry.raw_hex);
    print_packet_entry_json(&entry);
    printf("}\n");
}

static void cmd_packets_clear(void)
{
    esp_err_t ret = d1l_packet_log_clear();
    if (ret != ESP_OK) {
        err_result("packets clear", esp_err_to_name(ret), "could not clear packet log");
        return;
    }
    ok_begin("packets clear");
    printf(",\"persisted\":true,\"count\":0}\n");
}

static void print_public_message_entry_json(const d1l_message_entry_t *e)
{
    printf("{\"seq\":%lu,\"uptime_ms\":%lu,\"direction\":",
           (unsigned long)e->seq, (unsigned long)e->uptime_ms);
    print_json_string(e->direction);
    printf(",\"author\":");
    print_json_string(e->author);
    printf(",\"text\":");
    print_json_string(e->text);
    printf(",\"rssi_dbm\":%d,\"snr_tenths\":%d,\"path_hash_bytes\":%u,\"path_hops\":%u,\"delivered\":%s}",
           e->rssi_dbm, e->snr_tenths, e->path_hash_bytes, e->path_hops,
           bool_json(e->delivered));
}

static void cmd_messages_public(const char *line)
{
    d1l_message_store_stats_t stats = d1l_message_store_stats();
    static d1l_message_entry_t entries[D1L_MESSAGE_STORE_CAPACITY];
    char search[D1L_MESSAGE_TEXT_LEN] = {0};
    bool filtered = false;
    size_t offset = 0;

    const char *args = line + strlen("messages public");
    while (*args == ' ') {
        args++;
    }
    if (*args == '\0') {
        /* Newest page of retained history. */
    } else if (strncmp(args, "offset", 6) == 0) {
        if (!parse_message_offset_clause(args, &offset)) {
            err_result("messages public", "INVALID_OFFSET",
                       "usage: messages public [offset <n>] | messages public search <text> [offset <n>]");
            return;
        }
    } else if (strncmp(args, "search ", 7) == 0) {
        copy_packet_search(search, sizeof(search), args + 7);
        if (!strip_message_offset_suffix(search, &offset)) {
            err_result("messages public", "INVALID_OFFSET",
                       "usage: messages public [offset <n>] | messages public search <text> [offset <n>]");
            return;
        }
        if (search[0] == '\0') {
            err_result("messages public", "INVALID_QUERY",
                       "usage: messages public [offset <n>] | messages public search <text> [offset <n>]");
            return;
        }
        filtered = true;
    } else {
        err_result("messages public", "INVALID_TARGET",
                   "usage: messages public [offset <n>] | messages public search <text> [offset <n>]");
        return;
    }

    size_t total_matches = 0;
    const size_t copied = d1l_message_store_query_page(entries,
                                                       D1L_CONSOLE_MESSAGE_PAGE_SIZE,
                                                       offset, search, &total_matches);
    const bool has_older = total_matches > offset + copied;
    const size_t next_offset = has_older ? offset + copied : offset;
    ok_begin("messages public");
    printf(",\"count\":%u,\"capacity\":%u,\"total_written\":%lu,\"dropped_oldest\":%lu,\"filtered\":%s,\"offset\":%u,\"page_size\":%u,\"page_count\":%u,\"total_matches\":%u,\"has_older\":%s,\"next_offset\":%u",
           (unsigned)stats.count, (unsigned)stats.capacity,
           (unsigned long)stats.total_written, (unsigned long)stats.dropped_oldest,
           bool_json(filtered), (unsigned)offset,
           (unsigned)D1L_CONSOLE_MESSAGE_PAGE_SIZE, (unsigned)copied,
           (unsigned)total_matches, bool_json(has_older), (unsigned)next_offset);
    if (filtered) {
        printf(",\"search\":");
        print_json_string(search);
    }
    printf(",\"retained_epoch\":%lu,\"content_revision\":%lu",
           (unsigned long)stats.epoch,
           (unsigned long)stats.content_revision);
    printf(",\"persistence\":{\"loaded\":%s,\"dirty\":%s,\"revision\":%llu,\"commits\":%lu,\"failures\":%lu,\"stale_snapshots\":%lu,\"sd\":{\"required\":%s,\"generation\":%lu,\"dirty\":%s,\"reconcile_pending\":%s,\"commits\":%lu,\"failures\":%lu,\"last_error\":\"%s\"},\"nvs\":{\"dirty\":%s,\"commits\":%lu,\"failures\":%lu,\"last_error\":\"%s\"}}",
           bool_json(stats.loaded),
           bool_json(stats.persistence_dirty),
           (unsigned long long)stats.persistence_revision,
           (unsigned long)stats.persistence_commit_count,
           (unsigned long)stats.persistence_fail_count,
           (unsigned long)stats.persistence_stale_snapshot_count,
           bool_json(stats.sd_primary_required),
           (unsigned long)stats.sd_backend_generation,
           bool_json(stats.sd_primary_dirty),
           bool_json(stats.sd_primary_reconcile_pending),
           (unsigned long)stats.sd_primary_commit_count,
           (unsigned long)stats.sd_primary_fail_count,
           esp_err_to_name(stats.sd_primary_last_error),
           bool_json(stats.nvs_fallback_dirty),
           (unsigned long)stats.nvs_fallback_commit_count,
           (unsigned long)stats.nvs_fallback_fail_count,
           esp_err_to_name(stats.nvs_fallback_last_error));
    printf(",\"entries\":[");
    for (size_t i = 0; i < copied; ++i) {
        printf("%s", i ? "," : "");
        print_public_message_entry_json(&entries[i]);
    }
    printf("],\"persisted\":%s,\"note\":\"Public messages are kept in bounded retained storage; optional search filters retained rows and offset pages older rows\"}\n",
           bool_json(stats.loaded && !stats.persistence_dirty));
}

static void cmd_messages_clear(void)
{
    esp_err_t ret = d1l_message_store_clear();
    if (ret != ESP_OK) {
        err_result("messages clear", esp_err_to_name(ret), "could not clear public message store");
        return;
    }
    ret = d1l_read_state_mark_public_read();
    if (ret != ESP_OK) {
        err_result("messages clear", esp_err_to_name(ret), "public messages cleared but read state did not persist");
        return;
    }
    ok_begin("messages clear");
    printf(",\"persisted\":true,\"count\":0}\n");
}

static void print_dm_entry_json(const d1l_dm_entry_t *e)
{
    printf("{\"seq\":%lu,\"uptime_ms\":%lu,\"fingerprint\":\"%s\",\"alias\":\"%s\",\"direction\":\"%s\",\"text\":\"%s\",\"rssi_dbm\":%d,\"snr_tenths\":%d,\"path_hash_bytes\":%u,\"path_hops\":%u,\"attempt\":%u,\"delivered\":%s,\"acked\":%s,\"ack_hash\":%lu}",
           (unsigned long)e->seq, (unsigned long)e->uptime_ms,
           e->contact_fingerprint, e->contact_alias, e->direction, e->text,
           e->rssi_dbm, e->snr_tenths, e->path_hash_bytes, e->path_hops,
           e->attempt, bool_json(e->delivered), bool_json(e->acked),
           (unsigned long)e->ack_hash);
}

static void cmd_messages_dm(const char *line)
{
    d1l_dm_store_stats_t stats = d1l_dm_store_stats();
    static d1l_dm_entry_t entries[D1L_DM_STORE_CAPACITY];
    char thread_fingerprint[D1L_NODE_FINGERPRINT_LEN] = {0};
    bool filtered = false;
    size_t offset = 0;
    size_t total_matches = 0;
    size_t copied = 0;

    const char *args = line + strlen("messages dm");
    while (*args == ' ') {
        args++;
    }
    if (*args == '\0') {
        copied = d1l_dm_store_copy_recent_page(entries, D1L_CONSOLE_MESSAGE_PAGE_SIZE,
                                              offset, &total_matches);
    } else if (strncmp(args, "offset", 6) == 0) {
        if (!parse_message_offset_clause(args, &offset)) {
            err_result("messages dm", "INVALID_OFFSET",
                       "usage: messages dm [offset <n>] [fingerprint [offset <n>]]");
            return;
        }
        copied = d1l_dm_store_copy_recent_page(entries, D1L_CONSOLE_MESSAGE_PAGE_SIZE,
                                              offset, &total_matches);
    } else if (parse_fingerprint_token(args, thread_fingerprint,
                                     sizeof(thread_fingerprint))) {
        filtered = true;
        args += D1L_NODE_FINGERPRINT_LEN - 1U;
        while (*args == ' ') {
            args++;
        }
        if (*args != '\0' && !parse_message_offset_clause(args, &offset)) {
            err_result("messages dm", "INVALID_OFFSET",
                       "usage: messages dm [offset <n>] [fingerprint [offset <n>]]");
            return;
        }
        copied = d1l_dm_store_copy_thread_page(thread_fingerprint, entries,
                                               D1L_CONSOLE_MESSAGE_PAGE_SIZE,
                                               offset, &total_matches);
    } else {
        err_result("messages dm", "INVALID_TARGET",
                   "usage: messages dm [offset <n>] [fingerprint [offset <n>]]");
        return;
    }

    const bool has_older = total_matches > offset + copied;
    const size_t next_offset = has_older ? offset + copied : offset;
    ok_begin("messages dm");
    printf(",\"count\":%u,\"capacity\":%u,\"total_written\":%lu,\"dropped_oldest\":%lu,\"filtered\":%s,\"offset\":%u,\"page_size\":%u,\"page_count\":%u,\"total_matches\":%u,\"has_older\":%s,\"next_offset\":%u",
           (unsigned)stats.count, (unsigned)stats.capacity,
           (unsigned long)stats.total_written, (unsigned long)stats.dropped_oldest,
           bool_json(filtered), (unsigned)offset,
           (unsigned)D1L_CONSOLE_MESSAGE_PAGE_SIZE, (unsigned)copied,
           (unsigned)total_matches, bool_json(has_older), (unsigned)next_offset);
    if (filtered) {
        printf(",\"fingerprint\":\"%s\",\"thread_count\":%u",
               thread_fingerprint, (unsigned)total_matches);
    }
    printf(",\"retained_epoch\":%lu,\"content_revision\":%lu",
           (unsigned long)stats.epoch,
           (unsigned long)stats.content_revision);
    printf(",\"persistence\":{\"loaded\":%s,\"dirty\":%s,\"revision\":%llu,\"commits\":%lu,\"failures\":%lu,\"stale_snapshots\":%lu,\"sd\":{\"required\":%s,\"generation\":%lu,\"dirty\":%s,\"reconcile_pending\":%s,\"commits\":%lu,\"failures\":%lu,\"last_error\":\"%s\"},\"nvs\":{\"dirty\":%s,\"commits\":%lu,\"failures\":%lu,\"last_error\":\"%s\"}}",
           bool_json(stats.loaded),
           bool_json(stats.persistence_dirty),
           (unsigned long long)stats.persistence_revision,
           (unsigned long)stats.persistence_commit_count,
           (unsigned long)stats.persistence_fail_count,
           (unsigned long)stats.persistence_stale_snapshot_count,
           bool_json(stats.sd_primary_required),
           (unsigned long)stats.sd_backend_generation,
           bool_json(stats.sd_primary_dirty),
           bool_json(stats.sd_primary_reconcile_pending),
           (unsigned long)stats.sd_primary_commit_count,
           (unsigned long)stats.sd_primary_fail_count,
           esp_err_to_name(stats.sd_primary_last_error),
           bool_json(stats.nvs_fallback_dirty),
           (unsigned long)stats.nvs_fallback_commit_count,
           (unsigned long)stats.nvs_fallback_fail_count,
           esp_err_to_name(stats.nvs_fallback_last_error));
    printf(",\"entries\":[");
    for (size_t i = 0; i < copied; ++i) {
        printf("%s", i ? "," : "");
        print_dm_entry_json(&entries[i]);
    }
    printf("],\"persisted\":%s,\"note\":\"MeshCore direct-message rows are kept in bounded retained storage; optional fingerprint filters one retained thread and offset pages older rows\"}\n",
           bool_json(stats.loaded && !stats.persistence_dirty));
}

static void cmd_messages_dm_clear(void)
{
    esp_err_t ret = d1l_dm_store_clear();
    if (ret != ESP_OK) {
        err_result("messages dm clear", esp_err_to_name(ret), "could not clear DM store");
        return;
    }
    ret = d1l_read_state_mark_dm_read();
    if (ret != ESP_OK) {
        err_result("messages dm clear", esp_err_to_name(ret), "DM rows cleared but read state did not persist");
        return;
    }
    ok_begin("messages dm clear");
    printf(",\"persisted\":true,\"count\":0}\n");
}

static void cmd_messages_unread(void)
{
    d1l_read_state_stats_t stats = d1l_read_state_stats();
    static d1l_read_state_dm_thread_t threads[8];
    size_t thread_count = d1l_read_state_copy_dm_threads(threads, 8);
    ok_begin("messages unread");
    printf(",\"public_unread\":%lu,\"dm_unread\":%lu,\"muted_dm_unread\":%lu,\"dm_thread_count\":%lu,\"last_public_read_seq\":%lu,\"last_dm_read_seq\":%lu,\"newest_public_rx_seq\":%lu,\"newest_dm_rx_seq\":%lu,\"mark_read_count\":%lu,\"dm_threads\":[",
           (unsigned long)stats.public_unread_count,
           (unsigned long)stats.dm_unread_count,
           (unsigned long)stats.muted_dm_unread_count,
           (unsigned long)stats.dm_thread_count,
           (unsigned long)stats.last_public_read_seq,
           (unsigned long)stats.last_dm_read_seq,
           (unsigned long)stats.newest_public_rx_seq,
           (unsigned long)stats.newest_dm_rx_seq,
           (unsigned long)stats.mark_read_count);
    for (size_t i = 0; i < thread_count; ++i) {
        const d1l_read_state_dm_thread_t *thread = &threads[i];
        printf("%s{\"fingerprint\":\"%s\",\"last_read_seq\":%lu,\"newest_rx_seq\":%lu,\"unread\":%lu,\"muted\":%s}",
               i ? "," : "", thread->fingerprint,
               (unsigned long)thread->last_read_seq,
               (unsigned long)thread->newest_rx_seq,
               (unsigned long)thread->unread_count,
               bool_json(thread->muted));
    }
    printf("],\"persisted\":true,\"note\":\"Unread counters are derived from persisted RX rows; DM cursors are tracked per thread\"}\n");
}

static void cmd_messages_read(const char *line)
{
    const char *arg = line + strlen("messages read ");
    while (*arg == ' ') {
        arg++;
    }

    esp_err_t ret = ESP_ERR_INVALID_ARG;
    const char *target = "invalid";
    char thread_fingerprint[D1L_NODE_FINGERPRINT_LEN] = {0};
    if (strcmp(arg, "public") == 0) {
        target = "public";
        ret = d1l_read_state_mark_public_read();
    } else if (strcmp(arg, "dm") == 0) {
        target = "dm";
        ret = d1l_read_state_mark_dm_read();
    } else if (strncmp(arg, "dm ", 3) == 0) {
        const char *fingerprint = arg + 3;
        while (*fingerprint == ' ') {
            fingerprint++;
        }
        if (parse_fingerprint_token(fingerprint, thread_fingerprint, sizeof(thread_fingerprint))) {
            target = "dm_thread";
            ret = d1l_read_state_mark_dm_thread_read(thread_fingerprint);
        }
    } else if (strcmp(arg, "all") == 0) {
        target = "all";
        ret = d1l_read_state_mark_all_read();
    }
    if (ret != ESP_OK) {
        err_result("messages read",
                   ret == ESP_ERR_INVALID_ARG ? "INVALID_TARGET" : esp_err_to_name(ret),
                   "usage: messages read <public|dm|dm <fingerprint>|all>");
        return;
    }

    d1l_read_state_stats_t stats = d1l_read_state_stats();
    ok_begin("messages read");
    printf(",\"target\":\"%s\"", target);
    if (thread_fingerprint[0] != '\0') {
        printf(",\"fingerprint\":\"%s\"", thread_fingerprint);
    }
    printf(",\"persisted\":true,\"public_unread\":%lu,\"dm_unread\":%lu,\"muted_dm_unread\":%lu,\"dm_thread_count\":%lu,\"last_public_read_seq\":%lu,\"last_dm_read_seq\":%lu}\n",
           (unsigned long)stats.public_unread_count,
           (unsigned long)stats.dm_unread_count,
           (unsigned long)stats.muted_dm_unread_count,
           (unsigned long)stats.dm_thread_count,
           (unsigned long)stats.last_public_read_seq,
           (unsigned long)stats.last_dm_read_seq);
}

static void cmd_nodes(void)
{
    d1l_node_store_stats_t stats = d1l_node_store_stats();
    const uint32_t marker_generation = d1l_node_store_marker_generation();
    static d1l_node_view_t entries[8];
    const d1l_node_query_t query = {
        .filter = D1L_NODE_FILTER_ALL,
        .sort = D1L_NODE_SORT_LAST_HEARD,
        .text = NULL,
        .keyed_only = false,
        .reachable_only = false,
    };
    size_t copied = d1l_node_store_query(&query, entries, 8);
    ok_begin("nodes");
    printf(",\"count\":%u,\"capacity\":%u,\"active_capacity\":%u,\"sd_history_capacity\":%u,\"total_written\":%lu,\"dropped_oldest\":%lu,\"marker_generation\":%lu,\"filter\":\"all\",\"sort\":\"last_heard\",\"entries\":[",
           (unsigned)stats.count, (unsigned)stats.capacity,
           (unsigned)D1L_NODE_RAM_ACTIVE_CAPACITY,
           (unsigned)D1L_NODE_SD_HISTORY_CAPACITY,
           (unsigned long)stats.total_written, (unsigned long)stats.dropped_oldest,
           (unsigned long)marker_generation);
    for (size_t i = 0; i < copied; ++i) {
        const d1l_node_view_t *view = &entries[i];
        const d1l_node_entry_t *e = &view->node;
        printf("%s{\"seq\":%lu,\"first_heard_ms\":%lu,\"last_heard_ms\":%lu,\"advert_timestamp\":%lu,\"heard_count\":%lu,\"fingerprint\":\"%s\",\"public_key\":\"%s\",\"name\":\"%s\",\"display_name\":\"%s\",\"type\":\"%s\",\"role\":\"%s\",\"favorite\":%s,\"muted\":%s,\"keyed\":%s,\"reachable\":%s,\"rssi_dbm\":%d,\"snr_tenths\":%d,\"path_hash_bytes\":%u,\"path_hops\":%u,\"location_valid\":%s,\"lat_e6\":%ld,\"lon_e6\":%ld,\"location_advert_timestamp\":%lu,\"location_seq\":%lu}",
               i ? "," : "", (unsigned long)e->seq, (unsigned long)e->first_heard_ms,
               (unsigned long)e->last_heard_ms, (unsigned long)e->advert_timestamp,
               (unsigned long)e->heard_count, e->fingerprint, e->public_key_hex,
               e->name, view->display_name, e->type, view->role,
               bool_json(view->favorite), bool_json(view->muted),
               bool_json(view->keyed), bool_json(view->reachable),
               e->rssi_dbm, e->snr_tenths, e->path_hash_bytes, e->path_hops,
               bool_json(e->location_valid), (long)e->lat_e6, (long)e->lon_e6,
               (unsigned long)e->location_advert_timestamp,
               (unsigned long)e->location_seq);
    }
    printf("],\"persisted\":true,\"note\":\"Verified MeshCore adverts populate this bounded heard-node store; enriched query rows expose role, favorite, keyed, and reachability state\"}\n");
}

static void cmd_nodes_clear(void)
{
    esp_err_t ret = d1l_node_store_clear();
    if (ret != ESP_OK) {
        err_result("nodes clear", esp_err_to_name(ret), "could not clear heard-node store");
        return;
    }
    ok_begin("nodes clear");
    printf(",\"persisted\":true,\"count\":0}\n");
}

static void cmd_contacts(void)
{
    d1l_contact_store_stats_t stats = d1l_contact_store_stats();
    static d1l_contact_entry_t entries[8];
    size_t copied = d1l_contact_store_copy_recent(entries, 8);
    ok_begin("contacts");
    printf(",\"count\":%u,\"capacity\":%u,\"total_written\":%lu,\"dropped_oldest\":%lu,\"entries\":[",
           (unsigned)stats.count, (unsigned)stats.capacity,
           (unsigned long)stats.total_written, (unsigned long)stats.dropped_oldest);
    for (size_t i = 0; i < copied; ++i) {
        const d1l_contact_entry_t *e = &entries[i];
        printf("%s{\"seq\":%lu,\"created_ms\":%lu,\"updated_ms\":%lu,\"fingerprint\":\"%s\",\"public_key\":\"%s\",\"alias\":\"%s\",\"heard_name\":\"%s\",\"type\":\"%s\",\"last_rssi_dbm\":%d,\"last_snr_tenths\":%d,\"path_hash_bytes\":%u,\"path_hops\":%u,\"out_path_known\":%s,\"out_path_len\":%u,\"out_path_updated_ms\":%lu,\"favorite\":%s,\"muted\":%s}",
               i ? "," : "", (unsigned long)e->seq, (unsigned long)e->created_ms,
               (unsigned long)e->updated_ms, e->fingerprint, e->public_key_hex,
               e->alias, e->heard_name, e->type, e->last_rssi_dbm,
               e->last_snr_tenths, e->path_hash_bytes, e->path_hops,
               bool_json(e->out_path_valid), e->out_path_len,
               (unsigned long)e->out_path_updated_ms, bool_json(e->favorite),
               bool_json(e->muted));
    }
    printf("],\"persisted\":true,\"note\":\"Contacts are promoted from heard nodes into a bounded NVS store\"}\n");
}

static void print_contact_export_json(const d1l_contact_entry_t *e, bool leading_comma)
{
    char uri[D1L_CONTACT_EXPORT_URI_LEN] = {0};
    esp_err_t ret = d1l_contact_store_export_uri(e, uri, sizeof(uri));
    printf("%s{\"seq\":%lu,\"fingerprint\":\"%s\",\"alias\":\"%s\",\"type\":\"%s\",\"type_id\":%u,\"public_key\":\"%s\",\"shareable\":%s,\"meshcore_uri\":\"%s\"}",
           leading_comma ? "," : "", (unsigned long)e->seq, e->fingerprint, e->alias,
           e->type, (unsigned)d1l_contact_store_meshcore_type_id(e->type),
           e->public_key_hex, bool_json(ret == ESP_OK), ret == ESP_OK ? uri : "");
}

static void cmd_contacts_export(const char *line)
{
    const char *arg = line + strlen("contacts export");
    while (*arg == ' ') {
        arg++;
    }

    if (*arg == '\0') {
        d1l_contact_store_stats_t stats = d1l_contact_store_stats();
        static d1l_contact_entry_t entries[8];
        size_t copied = d1l_contact_store_copy_recent(entries, 8);
        ok_begin("contacts export");
        printf(",\"count\":%u,\"capacity\":%u,\"entries\":[",
               (unsigned)stats.count, (unsigned)stats.capacity);
        for (size_t i = 0; i < copied; ++i) {
            print_contact_export_json(&entries[i], i > 0);
        }
        printf("],\"format\":\"meshcore://contact/add\",\"note\":\"QR-compatible contact export requires a retained 64-hex public key\"}\n");
        return;
    }

    char fingerprint[D1L_NODE_FINGERPRINT_LEN] = {0};
    if (!parse_fingerprint_token(arg, fingerprint, sizeof(fingerprint))) {
        err_result("contacts export", "INVALID_FINGERPRINT",
                   "usage: contacts export [16-hex-fingerprint]");
        return;
    }

    d1l_contact_entry_t contact = {0};
    if (!d1l_contact_store_find_by_fingerprint(fingerprint, &contact)) {
        err_result("contacts export", "ESP_ERR_NOT_FOUND", "contact is not promoted");
        return;
    }

    char uri[D1L_CONTACT_EXPORT_URI_LEN] = {0};
    esp_err_t ret = d1l_contact_store_export_uri(&contact, uri, sizeof(uri));
    if (ret != ESP_OK) {
        err_result("contacts export", "NO_PUBLIC_KEY",
                   "contact export requires a retained 64-hex public key from a signed advert");
        return;
    }

    ok_begin("contacts export");
    printf(",\"fingerprint\":\"%s\",\"alias\":\"%s\",\"type\":\"%s\",\"type_id\":%u,\"public_key\":\"%s\",\"meshcore_uri\":\"%s\",\"format\":\"meshcore://contact/add\",\"qr_compatible\":true}\n",
           contact.fingerprint, contact.alias, contact.type,
           (unsigned)d1l_contact_store_meshcore_type_id(contact.type),
           contact.public_key_hex, uri);
}

static void cmd_contacts_clear(void)
{
    esp_err_t ret = d1l_contact_store_clear();
    if (ret != ESP_OK) {
        err_result("contacts clear", esp_err_to_name(ret), "could not clear contact store");
        return;
    }
    ok_begin("contacts clear");
    printf(",\"persisted\":true,\"count\":0}\n");
}

static void cmd_contacts_add(const char *line)
{
    const char *arg = line + strlen("contacts add ");
    while (*arg == ' ') {
        arg++;
    }
    char fingerprint[D1L_NODE_FINGERPRINT_LEN] = {0};
    if (!parse_fingerprint_token(arg, fingerprint, sizeof(fingerprint))) {
        err_result("contacts add", "INVALID_FINGERPRINT", "usage: contacts add <16-hex-fingerprint> [alias]");
        return;
    }
    const char *alias = arg + strlen(fingerprint);
    while (*alias == ' ') {
        alias++;
    }

    d1l_node_entry_t heard = {0};
    bool heard_found = d1l_node_store_find_by_fingerprint(fingerprint, &heard);
    esp_err_t ret = d1l_contact_store_upsert_from_node(fingerprint, alias, heard_found ? &heard : NULL);
    if (ret != ESP_OK) {
        err_result("contacts add", esp_err_to_name(ret), "could not persist contact");
        return;
    }

    d1l_contact_entry_t contact = {0};
    d1l_contact_store_find_by_fingerprint(fingerprint, &contact);
    ok_begin("contacts add");
    printf(",\"persisted\":true,\"source\":\"%s\",\"fingerprint\":\"%s\",\"public_key\":\"%s\",\"alias\":\"%s\",\"heard_name\":\"%s\",\"type\":\"%s\"}\n",
           heard_found ? "heard_node" : "manual", contact.fingerprint,
           contact.public_key_hex, contact.alias, contact.heard_name, contact.type);
}

static void cmd_contacts_set(const char *line)
{
    const char *arg = line + strlen("contacts set ");
    while (*arg == ' ') {
        arg++;
    }
    char fingerprint[D1L_NODE_FINGERPRINT_LEN] = {0};
    if (!parse_fingerprint_token(arg, fingerprint, sizeof(fingerprint))) {
        err_result("contacts set", "INVALID_FINGERPRINT",
                   "usage: contacts set <fingerprint> <favorite|mute> <0|1>");
        return;
    }
    arg += strlen(fingerprint);
    while (*arg == ' ') {
        arg++;
    }

    char field[12] = {0};
    size_t field_len = 0;
    while (arg[field_len] != '\0' && !isspace((unsigned char)arg[field_len]) &&
           field_len + 1U < sizeof(field)) {
        field[field_len] = (char)tolower((unsigned char)arg[field_len]);
        field_len++;
    }
    field[field_len] = '\0';
    arg += field_len;
    while (*arg == ' ') {
        arg++;
    }

    char value_text[8] = {0};
    size_t value_len = 0;
    while (arg[value_len] != '\0' && !isspace((unsigned char)arg[value_len]) &&
           value_len + 1U < sizeof(value_text)) {
        value_text[value_len] = (char)tolower((unsigned char)arg[value_len]);
        value_len++;
    }
    value_text[value_len] = '\0';

    bool value = false;
    if (!parse_bool_token(value_text, &value)) {
        err_result("contacts set", "INVALID_VALUE",
                   "usage: contacts set <fingerprint> <favorite|mute> <0|1>");
        return;
    }

    d1l_contact_entry_t contact = {0};
    if (!d1l_contact_store_find_by_fingerprint(fingerprint, &contact)) {
        err_result("contacts set", "ESP_ERR_NOT_FOUND", "contact is not promoted");
        return;
    }
    if (strcmp(field, "favorite") == 0 || strcmp(field, "fav") == 0) {
        contact.favorite = value;
    } else if (strcmp(field, "mute") == 0 || strcmp(field, "muted") == 0) {
        contact.muted = value;
    } else {
        err_result("contacts set", "INVALID_FIELD",
                   "usage: contacts set <fingerprint> <favorite|mute> <0|1>");
        return;
    }

    esp_err_t ret = d1l_contact_store_set_flags(fingerprint, contact.favorite, contact.muted, &contact);
    if (ret != ESP_OK) {
        err_result("contacts set", esp_err_to_name(ret), "could not persist contact flags");
        return;
    }

    ok_begin("contacts set");
    printf(",\"persisted\":true,\"fingerprint\":\"%s\",\"favorite\":%s,\"muted\":%s}\n",
           contact.fingerprint, bool_json(contact.favorite), bool_json(contact.muted));
}

static void cmd_contacts_rename(const char *line)
{
    const char *arg = line + strlen("contacts rename ");
    while (*arg == ' ') {
        arg++;
    }
    char fingerprint[D1L_NODE_FINGERPRINT_LEN] = {0};
    if (!parse_fingerprint_token(arg, fingerprint, sizeof(fingerprint))) {
        err_result("contacts rename", "INVALID_FINGERPRINT",
                   "usage: contacts rename <fingerprint> <alias>");
        return;
    }
    const char *alias = arg + strlen(fingerprint);
    while (*alias == ' ') {
        alias++;
    }
    if (alias[0] == '\0') {
        err_result("contacts rename", "EMPTY_ALIAS",
                   "usage: contacts rename <fingerprint> <alias>");
        return;
    }

    d1l_contact_entry_t contact = {0};
    esp_err_t ret = d1l_contact_store_rename(fingerprint, alias, &contact);
    if (ret != ESP_OK) {
        err_result("contacts rename", esp_err_to_name(ret), "could not persist contact alias");
        return;
    }

    ok_begin("contacts rename");
    printf(",\"persisted\":true,\"fingerprint\":\"%s\",\"alias\":\"%s\",\"updated_ms\":%lu}\n",
           contact.fingerprint, contact.alias, (unsigned long)contact.updated_ms);
}

static void cmd_contacts_delete(const char *line)
{
    const char *arg = line + strlen("contacts delete ");
    while (*arg == ' ') {
        arg++;
    }
    char fingerprint[D1L_NODE_FINGERPRINT_LEN] = {0};
    if (!parse_fingerprint_token(arg, fingerprint, sizeof(fingerprint))) {
        err_result("contacts delete", "INVALID_FINGERPRINT",
                   "usage: contacts delete <fingerprint>");
        return;
    }

    d1l_contact_entry_t contact = {0};
    esp_err_t ret = d1l_contact_store_delete(fingerprint, &contact);
    if (ret != ESP_OK) {
        err_result("contacts delete", esp_err_to_name(ret), "could not delete contact");
        return;
    }

    d1l_contact_store_stats_t stats = d1l_contact_store_stats();
    ok_begin("contacts delete");
    printf(",\"persisted\":true,\"fingerprint\":\"%s\",\"alias\":\"%s\",\"count\":%u,\"history_retained\":true}\n",
           contact.fingerprint, contact.alias, (unsigned)stats.count);
}

static void cmd_mesh_send_dm(const char *line)
{
    const char *arg = line + strlen("mesh send dm ");
    while (*arg == ' ') {
        arg++;
    }
    char fingerprint[D1L_NODE_FINGERPRINT_LEN] = {0};
    if (!parse_fingerprint_token(arg, fingerprint, sizeof(fingerprint))) {
        err_result("mesh send dm", "INVALID_FINGERPRINT", "usage: mesh send dm <16-hex-fingerprint> <text>");
        return;
    }
    const char *text = arg + strlen(fingerprint);
    while (*text == ' ') {
        text++;
    }
    if (text[0] == '\0') {
        err_result("mesh send dm", "EMPTY_MESSAGE", "usage: mesh send dm <16-hex-fingerprint> <text>");
        return;
    }
    esp_err_t ret = d1l_meshcore_service_send_dm(fingerprint, text);
    if (ret != ESP_OK) {
        err_result("mesh send dm",
                   ret == ESP_ERR_INVALID_SIZE ? "MESSAGE_TOO_LONG" : esp_err_to_name(ret),
                   ret == ESP_ERR_INVALID_SIZE ? "max 138 characters" :
                   "DM requires a promoted contact with a retained public key");
        return;
    }
    ok_begin("mesh send dm");
    printf(",\"queued\":true,\"fingerprint\":\"%s\"}\n", fingerprint);
}

static void print_route_entry_json(const d1l_route_entry_t *e)
{
    printf("{\"seq\":%lu,\"first_seen_ms\":%lu,\"last_seen_ms\":%lu,\"seen_count\":%lu,\"target\":\"%s\",\"label\":\"%s\",\"kind\":\"%s\",\"route\":\"%s\",\"direction\":\"%s\",\"last_rssi_dbm\":%d,\"last_snr_tenths\":%d,\"path_hash_bytes\":%u,\"path_hops\":%u,\"confidence\":%u,\"payload_len\":%u}",
           (unsigned long)e->seq, (unsigned long)e->first_seen_ms,
           (unsigned long)e->last_seen_ms, (unsigned long)e->seen_count,
           e->target, e->label, e->kind, e->route, e->direction,
           e->last_rssi_dbm, e->last_snr_tenths, e->path_hash_bytes,
           e->path_hops, e->confidence, e->payload_len);
}

static void cmd_routes(void)
{
    d1l_route_store_stats_t stats = d1l_route_store_stats();
    static d1l_route_entry_t entries[8];
    size_t copied = d1l_route_store_copy_recent(entries, 8);
    ok_begin("routes");
    printf(",\"count\":%u,\"capacity\":%u,\"total_written\":%lu,\"dropped_oldest\":%lu,"
           "\"persistence\":{\"revision\":%llu,\"commit_count\":%lu,"
           "\"coalesced_count\":%lu,\"fail_count\":%lu,"
           "\"stale_snapshot_count\":%lu,\"dirty\":%s,"
           "\"clear_failure_latched\":%s,\"clear_fail_count\":%lu,"
           "\"clear_last_error\":\"%s\","
           "\"sd_primary\":{\"required\":%s,\"backend_generation\":%lu,\"dirty\":%s,\"reconcile_pending\":%s,\"commit_count\":%lu,"
           "\"fail_count\":%lu,\"last_error\":\"%s\"},"
           "\"nvs_fallback\":{\"dirty\":%s,\"commit_count\":%lu,"
           "\"fail_count\":%lu,\"last_error\":\"%s\"}},\"entries\":[",
           (unsigned)stats.count, (unsigned)stats.capacity,
           (unsigned long)stats.total_written, (unsigned long)stats.dropped_oldest,
           (unsigned long long)stats.persistence_revision,
           (unsigned long)stats.persistence_commit_count,
           (unsigned long)stats.persistence_coalesced_count,
           (unsigned long)stats.persistence_fail_count,
           (unsigned long)stats.persistence_stale_snapshot_count,
           bool_json(stats.persistence_dirty),
           bool_json(stats.clear_failure_latched),
           (unsigned long)stats.clear_fail_count,
           esp_err_to_name(stats.clear_last_error),
           bool_json(stats.sd_primary_required),
           (unsigned long)stats.sd_backend_generation,
           bool_json(stats.sd_primary_dirty),
           bool_json(stats.sd_primary_reconcile_pending),
           (unsigned long)stats.sd_primary_commit_count,
           (unsigned long)stats.sd_primary_fail_count,
           esp_err_to_name(stats.sd_primary_last_error),
           bool_json(stats.nvs_fallback_dirty),
           (unsigned long)stats.nvs_fallback_commit_count,
           (unsigned long)stats.nvs_fallback_fail_count,
           esp_err_to_name(stats.nvs_fallback_last_error));
    for (size_t i = 0; i < copied; ++i) {
        printf("%s", i ? "," : "");
        print_route_entry_json(&entries[i]);
    }
    printf("],\"persisted\":%s,\"note\":\"Routes are learned from MeshCore path metadata on Public and advert packets\"}\n",
           bool_json(!stats.persistence_dirty));
}

static void cmd_routes_detail(const char *line)
{
    const char *arg = line + strlen("routes detail ");
    while (*arg == ' ') {
        arg++;
    }
    char *end = NULL;
    unsigned long seq = strtoul(arg, &end, 10);
    while (end && *end == ' ') {
        end++;
    }
    if (arg[0] == '\0' || end == arg || (end && *end != '\0') || seq == 0 || seq > UINT32_MAX) {
        err_result("routes detail", "INVALID_SEQ", "usage: routes detail <seq>");
        return;
    }

    d1l_route_entry_t entry = {0};
    esp_err_t ret = d1l_route_store_find_by_seq((uint32_t)seq, &entry);
    if (ret != ESP_OK) {
        err_result("routes detail", esp_err_to_name(ret), "route sequence not found");
        return;
    }

    ok_begin("routes detail");
    printf(",\"entry\":");
    print_route_entry_json(&entry);
    printf("}\n");
}

static bool route_trace_is_direct(const d1l_route_entry_t *entry)
{
    return entry && (entry->path_hops == 0 ||
                     strcmp(entry->route, "direct") == 0);
}

static size_t route_trace_best_index(const d1l_route_entry_t *entries, size_t count)
{
    size_t best = 0;
    bool best_set = false;
    for (size_t i = 0; i < count; ++i) {
        const bool candidate_direct = route_trace_is_direct(&entries[i]);
        const bool best_direct = best_set && route_trace_is_direct(&entries[best]);
        if (!best_set ||
            (candidate_direct && !best_direct) ||
            (candidate_direct == best_direct && entries[i].confidence > entries[best].confidence) ||
            (candidate_direct == best_direct && entries[i].confidence == entries[best].confidence &&
             entries[i].seq > entries[best].seq)) {
            best = i;
            best_set = true;
        }
    }
    return best;
}

static void cmd_routes_trace(const char *line)
{
    const char *arg = line + strlen("routes trace ");
    while (*arg == ' ') {
        arg++;
    }
    char fingerprint[D1L_NODE_FINGERPRINT_LEN] = {0};
    if (!parse_fingerprint_token(arg, fingerprint, sizeof(fingerprint))) {
        err_result("routes trace", "INVALID_FINGERPRINT", "usage: routes trace <16-hex-fingerprint>");
        return;
    }
    const char *tail = arg + strlen(fingerprint);
    while (*tail == ' ') {
        tail++;
    }
    if (*tail != '\0') {
        err_result("routes trace", "INVALID_FINGERPRINT", "usage: routes trace <16-hex-fingerprint>");
        return;
    }

    d1l_contact_entry_t contact = {0};
    const bool known_contact = d1l_contact_store_find_by_fingerprint(fingerprint, &contact);
    static d1l_route_entry_t entries[D1L_ROUTE_STORE_CAPACITY];
    size_t copied = d1l_app_model_copy_route_trace(fingerprint, entries, D1L_ROUTE_STORE_CAPACITY);
    const size_t best = copied ? route_trace_best_index(entries, copied) : 0;

    ok_begin("routes trace");
    printf(",\"fingerprint\":\"%s\",\"known_contact\":%s,\"alias\":",
           fingerprint, bool_json(known_contact));
    print_json_string(known_contact && contact.alias[0] ? contact.alias : "");
    printf(",\"public_key_ready\":%s,\"contact_route\":{\"out_path_known\":%s,\"path_hops\":%u,\"last_rssi_dbm\":%d,\"last_snr_tenths\":%d}",
           bool_json(known_contact && contact.public_key_hex[0]),
           bool_json(known_contact && contact.out_path_valid),
           known_contact && contact.out_path_valid ? contact.path_hops : 0,
           known_contact ? contact.last_rssi_dbm : 0,
           known_contact ? contact.last_snr_tenths : 0);
    printf(",\"route_count\":%u,\"best_route\":\"%s\",\"best_seq\":%lu,\"best_confidence\":%u,\"best_hops\":%u,\"entries\":[",
           (unsigned)copied,
           copied ? entries[best].route : "unknown",
           copied ? (unsigned long)entries[best].seq : 0UL,
           copied ? entries[best].confidence : 0,
           copied ? entries[best].path_hops : 0);
    for (size_t i = 0; i < copied; ++i) {
        printf("%s", i ? "," : "");
        print_route_entry_json(&entries[i]);
    }
    printf("],\"active_probe_supported\":true,\"active_probe_command\":\"routes probe <fingerprint>\",\"note\":\"Trace combines retained route/contact evidence with optional DM-only active probes\"}\n");
}

static void cmd_routes_probe(const char *line)
{
    const char *arg = line + strlen("routes probe ");
    while (*arg == ' ') {
        arg++;
    }
    char fingerprint[D1L_NODE_FINGERPRINT_LEN] = {0};
    if (!parse_fingerprint_token(arg, fingerprint, sizeof(fingerprint))) {
        err_result("routes probe", "INVALID_FINGERPRINT", "usage: routes probe <16-hex-fingerprint>");
        return;
    }
    const char *tail = arg + strlen(fingerprint);
    while (*tail == ' ') {
        tail++;
    }
    if (*tail != '\0') {
        err_result("routes probe", "INVALID_FINGERPRINT", "usage: routes probe <16-hex-fingerprint>");
        return;
    }

    char token[D1L_MESSAGE_TEXT_LEN] = {0};
    esp_err_t ret = d1l_app_model_request_trace_probe(fingerprint, token, sizeof(token));
    if (ret != ESP_OK) {
        err_result("routes probe", esp_err_to_name(ret),
                   ret == ESP_ERR_INVALID_STATE ?
                   "trace probe is cooling down or radio is busy" :
                   "could not queue DM-only trace probe");
        return;
    }

    ok_begin("routes probe");
    printf(",\"fingerprint\":\"%s\",\"queued\":true,\"token\":\"%s\",\"dm_rf_tx\":true,\"public_rf_tx\":false,\"active_probe_supported\":true,\"note\":\"DM-only trace probe queued; ACK/PATH evidence will appear in messages, packets, and routes trace when received\"}\n",
           fingerprint, token);
}

static void cmd_routes_clear(void)
{
    esp_err_t ret = d1l_route_store_clear();
    if (ret != ESP_OK) {
        err_result("routes clear", esp_err_to_name(ret), "could not clear route store");
        return;
    }
    ok_begin("routes clear");
    printf(",\"persisted\":true,\"count\":0}\n");
}

static void print_snr_json(int snr_tenths)
{
    const int snr_abs = snr_tenths < 0 ? -snr_tenths : snr_tenths;
    printf("%s%d.%d", snr_tenths < 0 ? "-" : "", snr_abs / 10, snr_abs % 10);
}

static void print_room_server_json(const d1l_mesh_room_server_t *e)
{
    printf("{\"fingerprint\":\"%s\",\"name\":\"%s\",\"type\":\"%s\",\"rssi_dbm\":%d,\"snr_tenths\":%d,\"path_hops\":%u,\"heard_count\":%lu,\"last_heard_ms\":%lu}",
           e->fingerprint, e->name, e->type, e->rssi_dbm, e->snr_tenths,
           e->path_hops, (unsigned long)e->heard_count, (unsigned long)e->last_heard_ms);
}

static void print_repeater_candidate_json(const d1l_mesh_repeater_candidate_t *e)
{
    printf("{\"target\":\"%s\",\"label\":\"%s\",\"kind\":\"%s\",\"route\":\"%s\",\"source\":\"%s\",\"rssi_dbm\":%d,\"snr_tenths\":%d,\"path_hops\":%u,\"confidence\":%u,\"seen_count\":%lu,\"last_seen_ms\":%lu}",
           e->target, e->label, e->kind, e->route, e->source, e->rssi_dbm,
           e->snr_tenths, e->path_hops, e->confidence,
           (unsigned long)e->seen_count, (unsigned long)e->last_seen_ms);
}

static void cmd_signal(void)
{
    d1l_mesh_signal_summary_t summary = {0};
    d1l_mesh_inspector_signal_summary(&summary);
    ok_begin("signal");
    printf(",\"sample_count\":%lu,\"rx_packet_samples\":%lu,\"route_samples\":%lu,\"node_samples\":%lu,\"room_server_count\":%lu,\"repeater_candidate_count\":%lu",
           (unsigned long)summary.sample_count, (unsigned long)summary.rx_packet_samples,
           (unsigned long)summary.route_samples, (unsigned long)summary.node_samples,
           (unsigned long)summary.room_server_count,
           (unsigned long)summary.repeater_candidate_count);
    printf(",\"latest\":{\"label\":\"%s\",\"kind\":\"%s\",\"rssi_dbm\":%d,\"snr_tenths\":%d,\"snr_db\":",
           summary.latest_label, summary.latest_kind,
           summary.latest_rssi_dbm, summary.latest_snr_tenths);
    print_snr_json(summary.latest_snr_tenths);
    printf(",\"path_hops\":%u}", summary.latest_path_hops);
    printf(",\"strongest_rssi_dbm\":%d,\"weakest_rssi_dbm\":%d,\"best_snr_tenths\":%d,\"worst_snr_tenths\":%d,\"avg_rssi_dbm\":%d,\"avg_snr_tenths\":%d,\"note\":\"Signal is derived from recent packet, route, and heard-node evidence\"}\n",
           summary.strongest_rssi_dbm, summary.weakest_rssi_dbm,
           summary.best_snr_tenths, summary.worst_snr_tenths,
           summary.avg_rssi_dbm, summary.avg_snr_tenths);
}

static void cmd_roomservers(void)
{
    d1l_mesh_signal_summary_t summary = {0};
    d1l_mesh_inspector_signal_summary(&summary);
    static d1l_mesh_room_server_t entries[8];
    size_t copied = d1l_mesh_inspector_copy_room_servers(entries, 8);
    ok_begin("roomservers");
    printf(",\"count\":%u,\"total_known\":%lu,\"entries\":[", (unsigned)copied,
           (unsigned long)summary.room_server_count);
    for (size_t i = 0; i < copied; ++i) {
        printf("%s", i ? "," : "");
        print_room_server_json(&entries[i]);
    }
    printf("],\"persisted\":true,\"note\":\"Room servers are derived from signed heard-node adverts with room role\"}\n");
}

static void cmd_repeaters(void)
{
    d1l_mesh_signal_summary_t summary = {0};
    d1l_mesh_inspector_signal_summary(&summary);
    static d1l_mesh_repeater_candidate_t entries[8];
    size_t copied = d1l_mesh_inspector_copy_repeater_candidates(entries, 8);
    ok_begin("repeaters");
    printf(",\"count\":%u,\"total_known\":%lu,\"entries\":[", (unsigned)copied,
           (unsigned long)summary.repeater_candidate_count);
    for (size_t i = 0; i < copied; ++i) {
        printf("%s", i ? "," : "");
        print_repeater_candidate_json(&entries[i]);
    }
    printf("],\"persisted\":true,\"note\":\"Repeater candidates are inferred from nonzero path-hop route and heard-node evidence\"}\n");
}

static void print_crash_log_entry_json(const d1l_crash_log_entry_t *e)
{
    printf("{\"seq\":%lu,\"uptime_ms\":%lu,\"reset_reason\":\"%s\",\"reset_reason_code\":%u,\"crash_like\":%s,\"heap_free\":%lu,\"heap_min_free\":%lu,\"psram_free\":%lu}",
           (unsigned long)e->seq, (unsigned long)e->uptime_ms, e->reset_reason,
           e->reset_reason_code, bool_json(e->crash_like),
           (unsigned long)e->heap_free, (unsigned long)e->heap_min_free,
           (unsigned long)e->psram_free);
}

static void cmd_crashlog(void)
{
    d1l_crash_log_stats_t stats = d1l_crash_log_stats();
    static d1l_crash_log_entry_t entries[D1L_CRASH_LOG_CAPACITY];
    size_t copied = d1l_crash_log_copy_recent(entries, D1L_CRASH_LOG_CAPACITY);
    ok_begin("crashlog");
    printf(",\"count\":%u,\"capacity\":%u,\"total_written\":%lu,\"dropped_oldest\":%lu,\"entries\":[",
           (unsigned)stats.count, (unsigned)stats.capacity,
           (unsigned long)stats.total_written, (unsigned long)stats.dropped_oldest);
    for (size_t i = 0; i < copied; ++i) {
        printf("%s", i ? "," : "");
        print_crash_log_entry_json(&entries[i]);
    }
    printf("],\"persisted\":true,\"note\":\"Crashlog records recent boot reset reasons and early memory watermarks\"}\n");
}

static void cmd_crashlog_clear(void)
{
    esp_err_t ret = d1l_crash_log_clear();
    if (ret != ESP_OK) {
        err_result("crashlog clear", esp_err_to_name(ret), "could not clear crash/reset log");
        return;
    }
    ok_begin("crashlog clear");
    printf(",\"persisted\":true,\"count\":0}\n");
}

static void cmd_health(void)
{
    d1l_health_snapshot_t h = d1l_health_snapshot();
    ok_begin("health");
    printf(",\"boot_nonce\":%lu,\"uptime_ms\":%lu,\"heap_free\":%lu,\"heap_min_free\":%lu,\"heap_largest_free\":%lu,\"internal_heap_free\":%lu,\"internal_heap_min_free\":%lu,\"internal_heap_largest_free\":%lu,\"dma_heap_free\":%lu,\"dma_heap_largest_free\":%lu,\"psram_free\":%lu,\"psram_min_free\":%lu,\"psram_largest_free\":%lu,\"current_task_stack_free_words\":%lu,\"ui_task_stack_free_words\":%lu,\"retained_task_stack_free_bytes\":%lu,\"lvgl_free_bytes\":%lu,\"lvgl_largest_free_bytes\":%lu,\"lvgl_used_pct\":%u,\"nvs_ready\":%s,\"nvs_error\":\"%s\",\"reset_reason\":\"%s\",\"board_ready\":%s,\"ui_ready\":%s}\n",
           (unsigned long)h.boot_nonce,
           (unsigned long)h.uptime_ms,
           (unsigned long)h.heap_free, (unsigned long)h.heap_min_free,
           (unsigned long)h.heap_largest_free,
           (unsigned long)h.internal_heap_free,
           (unsigned long)h.internal_heap_min_free,
           (unsigned long)h.internal_heap_largest_free,
           (unsigned long)h.dma_heap_free,
           (unsigned long)h.dma_heap_largest_free,
           (unsigned long)h.psram_free, (unsigned long)h.psram_min_free,
           (unsigned long)h.psram_largest_free,
           (unsigned long)h.current_task_stack_free_words,
           (unsigned long)h.ui_task_stack_free_words,
           (unsigned long)h.retained_task_stack_free_bytes,
           (unsigned long)h.lvgl_free_bytes,
           (unsigned long)h.lvgl_largest_free_bytes,
           h.lvgl_used_pct, bool_json(h.nvs_ready), esp_err_to_name(h.nvs_error),
           h.reset_reason,
           d1l_app_model_get()->board_ready ? "true" : "false",
           d1l_app_model_get()->ui_ready ? "true" : "false");
}

static void cmd_wifi_status(void)
{
    d1l_connectivity_status_t status = {0};
    d1l_connectivity_status(&status);
    ok_begin("wifi status");
    printf(",\"setting_enabled\":%s,\"build_enabled\":%s,\"stack_active\":%s,\"connected\":%s,\"connecting\":%s,\"scan_supported\":%s,\"profile_saved\":%s,\"password_saved\":%s,\"ssid\":",
            bool_json(status.wifi_enabled_setting), bool_json(status.wifi_build_enabled),
            bool_json(status.wifi_stack_active), bool_json(status.wifi_connected),
            bool_json(status.wifi_connecting), bool_json(status.wifi_scan_supported),
            bool_json(status.wifi_profile_saved), bool_json(status.wifi_password_saved));
    print_json_string(status.wifi_profile_saved ? status.wifi_ssid : "");
    printf(",\"state\":");
    print_json_string(status.wifi_state ? status.wifi_state : "off");
    printf(",\"ip\":");
    print_json_string(status.wifi_connected && status.wifi_ip ? status.wifi_ip : "");
    printf(",\"rssi_dbm\":%d,\"channel\":%u,\"last_error\":",
           status.wifi_rssi_dbm, (unsigned)status.wifi_channel);
    print_json_string(status.wifi_last_error ? status.wifi_last_error : "none");
    printf(",\"policy\":\"%s\",\"live_network\":%s}\n",
           status.coexistence_policy, bool_json(status.wifi_stack_active));
}

static void cmd_wifi_off(void)
{
    esp_err_t ret = d1l_connectivity_set_wifi_enabled(false);
    if (ret != ESP_OK) {
        err_result("wifi off", esp_err_to_name(ret), "could not persist Wi-Fi disabled setting");
        return;
    }
    d1l_connectivity_status_t status = {0};
    d1l_connectivity_status(&status);
    ok_begin("wifi off");
    printf(",\"persisted\":true,\"setting_enabled\":%s,\"build_enabled\":%s,\"state\":\"%s\"}\n",
           bool_json(status.wifi_enabled_setting), bool_json(status.wifi_build_enabled),
           status.wifi_state);
}

static void cmd_wifi_on(void)
{
    d1l_connectivity_status_t status = {0};
    d1l_connectivity_status(&status);
    esp_err_t ret = d1l_connectivity_set_wifi_enabled(true);
    if (ret != ESP_OK) {
        err_result("wifi on",
                   ret == ESP_ERR_NOT_SUPPORTED ?
                   "WIFI_BUILD_DISABLED" :
                   esp_err_to_name(ret),
                   "Wi-Fi runtime could not be enabled on this release build");
        return;
    }
    cmd_wifi_status();
}

static void cmd_wifi_save(const char *line)
{
    const char *args = line + strlen("wifi save ");
    while (*args == ' ') {
        args++;
    }
    if (args[0] == '\0') {
        err_result("wifi save", "INVALID_ARG", "usage: wifi save <ssid> [password]");
        return;
    }
    char ssid[D1L_WIFI_SSID_LEN] = {0};
    char password[D1L_WIFI_PASSWORD_LEN] = {0};
    const char *space = strchr(args, ' ');
    const char *password_to_save = NULL;
    size_t ssid_len = space ? (size_t)(space - args) : strlen(args);
    if (ssid_len == 0 || ssid_len >= sizeof(ssid)) {
        err_result("wifi save", "INVALID_SSID", "SSID must be 1-32 printable characters without spaces");
        return;
    }
    memcpy(ssid, args, ssid_len);
    ssid[ssid_len] = '\0';
    if (space) {
        const char *password_arg = space + 1;
        while (*password_arg == ' ') {
            password_arg++;
        }
        if (strlen(password_arg) >= sizeof(password)) {
            err_result("wifi save", "INVALID_PASSWORD", "password must fit in 64 characters");
            return;
        }
        snprintf(password, sizeof(password), "%s", password_arg);
        if (password[0] != '\0') {
            password_to_save = password;
        }
    }
    esp_err_t ret = d1l_connectivity_save_wifi_profile(ssid, password_to_save);
    if (ret != ESP_OK) {
        err_result("wifi save", esp_err_to_name(ret), "could not persist Wi-Fi profile");
        return;
    }
    d1l_connectivity_status_t status = {0};
    d1l_connectivity_status(&status);
    ok_begin("wifi save");
    printf(",\"persisted\":true,\"profile_saved\":%s,\"password_saved\":%s,\"ssid\":",
           bool_json(status.wifi_profile_saved), bool_json(status.wifi_password_saved));
    print_json_string(status.wifi_ssid ? status.wifi_ssid : "");
    printf(",\"public_rf_tx\":false,\"note\":\"Wi-Fi profile saved for the measured runtime path; password is not printed\"}\n");
}

static void cmd_wifi_clear(void)
{
    esp_err_t ret = d1l_connectivity_clear_wifi_profile();
    if (ret != ESP_OK) {
        err_result("wifi clear", esp_err_to_name(ret), "could not clear Wi-Fi profile");
        return;
    }
    d1l_connectivity_status_t status = {0};
    d1l_connectivity_status(&status);
    ok_begin("wifi clear");
    printf(",\"persisted\":true,\"profile_saved\":%s,\"setting_enabled\":%s,\"ssid\":\"\"}\n",
           bool_json(status.wifi_profile_saved), bool_json(status.wifi_enabled_setting));
}

static void cmd_wifi_scan(void)
{
    d1l_wifi_scan_result_t scan = {0};
    (void)d1l_connectivity_wifi_scan(&scan);
    d1l_connectivity_status_t status = {0};
    d1l_connectivity_status(&status);
    ok_begin("wifi scan");
    printf(",\"scan_started\":%s,\"setting_enabled\":%s,\"build_enabled\":%s,\"scan_supported\":%s,\"profile_saved\":%s,\"state\":",
           bool_json(scan.scan_started),
           bool_json(status.wifi_enabled_setting), bool_json(status.wifi_build_enabled),
           bool_json(status.wifi_scan_supported), bool_json(status.wifi_profile_saved));
    print_json_string(status.wifi_state ? status.wifi_state : "off");
    printf(",\"reason\":");
    print_json_string(scan.reason ? scan.reason : "unknown");
    printf(",\"total_count\":%u,\"returned_count\":%u,\"truncated\":%s,\"networks\":[",
           (unsigned)scan.total_count, (unsigned)scan.returned_count,
           bool_json(scan.truncated));
    for (uint16_t i = 0; i < scan.returned_count; ++i) {
        if (i > 0U) {
            printf(",");
        }
        printf("{\"ssid\":");
        print_json_string(scan.aps[i].ssid);
        printf(",\"rssi_dbm\":%d,\"channel\":%u,\"auth\":",
               scan.aps[i].rssi_dbm, (unsigned)scan.aps[i].channel);
        print_json_string(scan.aps[i].auth ? scan.aps[i].auth : "unknown");
        printf("}");
    }
    printf("],\"public_rf_tx\":false,\"password_printed\":false}\n");
}

static void cmd_wifi_connect(void)
{
    esp_err_t ret = d1l_connectivity_wifi_connect();
    if (ret != ESP_OK) {
        d1l_connectivity_status_t status = {0};
        d1l_connectivity_status(&status);
        const char *code = esp_err_to_name(ret);
        if (ret == ESP_ERR_INVALID_STATE && !status.wifi_enabled_setting) {
            code = "WIFI_DISABLED";
        } else if (ret == ESP_ERR_INVALID_STATE && !status.wifi_profile_saved) {
            code = "WIFI_PROFILE_REQUIRED";
        } else if (ret == ESP_ERR_NOT_SUPPORTED) {
            code = status.wifi_build_enabled ? "WIFI_RUNTIME_UNAVAILABLE" : "WIFI_BUILD_DISABLED";
        }
        err_result("wifi connect", code,
                   "enable Wi-Fi and save a profile before connecting");
        return;
    }
    d1l_connectivity_status_t status = {0};
    d1l_connectivity_status(&status);
    ok_begin("wifi connect");
    printf(",\"initiated\":true,\"setting_enabled\":%s,\"profile_saved\":%s,\"state\":",
           bool_json(status.wifi_enabled_setting),
           bool_json(status.wifi_profile_saved));
    print_json_string(status.wifi_state ? status.wifi_state : "connecting");
    printf(",\"ssid\":");
    print_json_string(status.wifi_profile_saved ? status.wifi_ssid : "");
    printf(",\"password_printed\":false,\"public_rf_tx\":false}\n");
}

static void cmd_ble_status(void)
{
    d1l_connectivity_status_t status = {0};
    d1l_connectivity_status(&status);
    ok_begin("ble status");
    printf(",\"setting_enabled\":%s,\"build_enabled\":%s,\"stack_active\":%s,\"state\":\"%s\",\"policy\":\"%s\"}\n",
           bool_json(status.ble_companion_enabled_setting), bool_json(status.ble_build_enabled),
           bool_json(status.ble_stack_active), status.ble_state, status.coexistence_policy);
}

static void cmd_ble_off(void)
{
    esp_err_t ret = d1l_connectivity_set_ble_enabled(false);
    if (ret != ESP_OK) {
        err_result("ble off", esp_err_to_name(ret), "could not persist BLE disabled setting");
        return;
    }
    d1l_connectivity_status_t status = {0};
    d1l_connectivity_status(&status);
    ok_begin("ble off");
    printf(",\"persisted\":true,\"setting_enabled\":%s,\"build_enabled\":%s,\"state\":\"%s\"}\n",
           bool_json(status.ble_companion_enabled_setting), bool_json(status.ble_build_enabled),
           status.ble_state);
}

static void cmd_ble_on(void)
{
    d1l_connectivity_status_t status = {0};
    d1l_connectivity_status(&status);
    esp_err_t ret = d1l_connectivity_set_ble_enabled(true);
    if (ret != ESP_OK) {
        err_result("ble on",
                   ret == ESP_ERR_NOT_SUPPORTED ?
                   (status.ble_build_enabled ? "BLE_RUNTIME_PENDING" : "BLE_BUILD_DISABLED") :
                   esp_err_to_name(ret),
                   "BLE enable/start is pending a later measured connectivity build");
        return;
    }
    cmd_ble_status();
}

static void cmd_help(void)
{
    ok_begin("help");
    printf(",\"contact_probe_surfaces\":[\"contact_detail\",\"contact_options\","
           "\"contact_forget\",\"contact_route\"]");
    printf(",\"storage_probe_surfaces\":[\"storage\",\"storage_card\",\"storage_data\"]");
    printf(",\"map_probe_surfaces\":[\"map\",\"map_options\",\"map_location\",\"map_cache\"]");
    printf(",\"map_probes_read_only\":true");
    printf(",\"commands\":[\"help\",\"version\",\"board\",\"settings get\",\"settings reset\","
           "\"settings set name <name>\",\"settings set pathhash <1|2|3>\","
           "\"settings set location <lat> <lon>\",\"settings clear location\","
           "\"settings onboarding status\",\"settings onboarding complete <name>\","
           "\"settings onboarding reset\",\"identity status\",\"i2c\",\"display test\","
           "\"touch test\",\"touch raw\",\"button\",\"backlight <0-100>\",\"radiohw\","
           "\"radio get\",\"radio set preset uscan\",\"radio set freq 910.525\","
           "\"radio set bw 62.5\",\"radio set sf 7\",\"radio set cr 5\","
           "\"radio set txpower 20\",\"radio set rxboost <0|1>\",\"ui status\","
           "\"ui tab <home|messages|nodes|map|packets|settings>\","
           "\"ui scroll-probe <home|public_messages|dm_thread|nodes|contact_detail|contact_options|contact_forget|contact_route|mesh_roles|mesh_rooms|mesh_repeaters|packets|settings|storage|storage_card|storage_data|wifi|map|map_options|map_location|map_cache>\","
           "\"ui compose-probe <public|public-long|dm|dm-long|public-search|packet-search|contact-edit|onboarding|map-location|wifi-ssid|wifi-password>\","
           "\"ui data-canary <token>\",\"ui capture status\",\"ui capture begin\","
           "\"ui capture chunk <offset> <len>\",\"ui capture end\",\"map center\","
           "\"map center set <lat> <lon>\",\"map center clear\",\"map tiles status\","
           "\"mesh status\",\"companion status\",\"rp2040 status\","
           "\"rp2040 set-baud <baud>\",\"rp2040 baud-probe [timeout_ms]\","
           "\"rp2040 ping\",\"rp2040 bootloader\",\"rp2040 stock-probe\","
           "\"rp2040 double-reset [hold_ms gap_ms [settle_ms]]\",\"rp2040 reset\","
           "\"storage status\",\"storage mount\",\"storage remount\","
           "\"storage reset-bridge\",\"storage force-nvs [on|off]\",\"storage diag\","
           "\"storage diag raw\",\"storage map-policy\",\"storage setup\","
           "\"storage filecanary\",\"storage map-tile-canary <token>\","
           "\"storage map-tile-check <token>\",\"storage export-canary <token>\","
           "\"storage export-diagnostics <token>\",\"storage export-data <token>\","
           "\"storage retained-canary <token>\",\"mesh advert zero\",\"mesh advert flood\","
           "\"mesh send public <text>\",\"mesh send dm <fingerprint> <text>\","
           "\"messages public [offset <n>]\",\"messages public search <text> [offset <n>]\","
           "\"messages dm [offset <n>]\",\"messages dm <fingerprint> [offset <n>]\","
           "\"messages unread\",\"messages read <public|dm|dm <fingerprint>|all>\","
           "\"messages clear\",\"messages dm clear\",\"nodes\",\"nodes clear\",\"contacts\","
           "\"contacts export [fingerprint]\",\"contacts add <fingerprint> [alias]\","
           "\"contacts rename <fingerprint> <alias>\",\"contacts delete <fingerprint>\","
           "\"contacts set <fingerprint> <favorite|mute> <0|1>\",\"contacts clear\","
           "\"routes\",\"routes detail <seq>\",\"routes trace <fingerprint>\","
           "\"routes probe <fingerprint>\",\"routes clear\",\"packets\","
           "\"packets filter <any|rx|tx> <any|text|kind>\",\"packets search <text>\","
           "\"packets detail <seq>\",\"packets raw <seq>\",\"packets clear\",\"signal\","
           "\"roomservers\",\"repeaters\",\"health\",\"crashlog\",\"crashlog clear\","
           "\"wifi status\",\"wifi scan\",\"wifi save <ssid> [password]\","
           "\"wifi connect\",\"wifi clear\",\"wifi on\",\"wifi off\",\"ble status\","
           "\"ble on\",\"ble off\",\"reboot\",\"factory-reset-confirm\"]}\n");
}

static void handle_line(const char *line)
{
    if (strcmp(line, "help") == 0) {
        cmd_help();
    } else if (strcmp(line, "version") == 0) {
        cmd_version();
    } else if (strcmp(line, "board") == 0) {
        cmd_board();
    } else if (strcmp(line, "settings get") == 0) {
        cmd_settings_get();
    } else if (strcmp(line, "settings reset") == 0) {
        cmd_settings_reset();
    } else if (strncmp(line, "settings set name ", 18) == 0) {
        cmd_settings_set_name(line);
    } else if (strncmp(line, "settings set pathhash ", 22) == 0) {
        cmd_settings_set_pathhash(line);
    } else if (strncmp(line, "settings set location ", strlen("settings set location ")) == 0) {
        cmd_settings_set_location(line);
    } else if (strcmp(line, "settings clear location") == 0) {
        cmd_settings_clear_location();
    } else if (strcmp(line, "ui status") == 0) {
        cmd_ui_status();
    } else if (strncmp(line, "ui tab ", strlen("ui tab ")) == 0) {
        cmd_ui_tab(line);
    } else if (strncmp(line, "ui scroll-probe ", strlen("ui scroll-probe ")) == 0) {
        cmd_ui_scroll_probe(line);
    } else if (strncmp(line, "ui compose-probe ", strlen("ui compose-probe ")) == 0) {
        cmd_ui_compose_probe(line);
    } else if (strncmp(line, "ui data-canary ", strlen("ui data-canary ")) == 0) {
        cmd_ui_data_canary(line);
    } else if (strcmp(line, "ui capture status") == 0) {
        cmd_ui_capture_status();
    } else if (strcmp(line, "ui capture begin") == 0) {
        cmd_ui_capture_begin();
    } else if (strncmp(line, "ui capture chunk ", strlen("ui capture chunk ")) == 0) {
        cmd_ui_capture_chunk(line);
    } else if (strcmp(line, "ui capture end") == 0) {
        cmd_ui_capture_end();
    } else if (strcmp(line, "map center") == 0) {
        cmd_map_center();
    } else if (strncmp(line, "map center set ", strlen("map center set ")) == 0) {
        cmd_map_center_set(line);
    } else if (strcmp(line, "map center clear") == 0) {
        cmd_map_center_clear();
    } else if (strcmp(line, "map tiles status") == 0) {
        cmd_map_tiles_status();
    } else if (strcmp(line, "settings onboarding status") == 0) {
        cmd_settings_onboarding_status();
    } else if (strncmp(line, "settings onboarding complete ", 29) == 0) {
        cmd_settings_onboarding_complete(line);
    } else if (strcmp(line, "settings onboarding reset") == 0) {
        cmd_settings_onboarding_reset();
    } else if (strcmp(line, "identity status") == 0) {
        cmd_identity_status();
    } else if (strcmp(line, "i2c") == 0) {
        cmd_i2c();
    } else if (strcmp(line, "display test") == 0) {
        cmd_display_test();
    } else if (strcmp(line, "touch test") == 0) {
        cmd_touch_test();
    } else if (strcmp(line, "touch raw") == 0) {
        cmd_touch_raw();
    } else if (strcmp(line, "button") == 0) {
        cmd_button();
    } else if (strncmp(line, "backlight ", 10) == 0) {
        cmd_backlight(line);
    } else if (strcmp(line, "radiohw") == 0) {
        cmd_radiohw();
    } else if (strcmp(line, "radio get") == 0) {
        cmd_radio_get();
    } else if (strcmp(line, "radio set preset uscan") == 0) {
        cmd_radio_set_preset_uscan();
    } else if (strncmp(line, "radio set freq ", 15) == 0) {
        cmd_radio_set_freq(line);
    } else if (strncmp(line, "radio set bw ", 13) == 0) {
        cmd_radio_set_bw(line);
    } else if (strncmp(line, "radio set sf ", 13) == 0) {
        cmd_radio_set_sf(line);
    } else if (strncmp(line, "radio set cr ", 13) == 0) {
        cmd_radio_set_cr(line);
    } else if (strncmp(line, "radio set txpower ", 18) == 0) {
        cmd_radio_set_txpower(line);
    } else if (strncmp(line, "radio set rxboost ", 18) == 0) {
        cmd_radio_set_rxboost(line);
    } else if (strcmp(line, "mesh status") == 0) {
        cmd_mesh_status();
    } else if (strcmp(line, "companion status") == 0) {
        cmd_companion_status();
    } else if (strcmp(line, "rp2040 status") == 0) {
        cmd_rp2040_status();
    } else if (strncmp(line, "rp2040 set-baud ", strlen("rp2040 set-baud ")) == 0) {
        cmd_rp2040_set_baud(line);
    } else if (strncmp(line, "rp2040 baud-probe", strlen("rp2040 baud-probe")) == 0 &&
               (line[strlen("rp2040 baud-probe")] == '\0' ||
                line[strlen("rp2040 baud-probe")] == ' ')) {
        cmd_rp2040_baud_probe(line);
    } else if (strcmp(line, "rp2040 ping") == 0) {
        cmd_rp2040_ping();
    } else if (strcmp(line, "rp2040 bootloader") == 0) {
        cmd_rp2040_bootloader();
    } else if (strcmp(line, "rp2040 stock-probe") == 0) {
        cmd_rp2040_stock_probe();
    } else if (strncmp(line, "rp2040 double-reset", strlen("rp2040 double-reset")) == 0 &&
               (line[strlen("rp2040 double-reset")] == '\0' ||
                line[strlen("rp2040 double-reset")] == ' ')) {
        cmd_rp2040_double_reset(line);
    } else if (strcmp(line, "rp2040 reset") == 0) {
        cmd_rp2040_reset();
    } else if (strcmp(line, "storage status") == 0) {
        cmd_storage_status();
    } else if (strcmp(line, "storage mount") == 0) {
        cmd_storage_mount();
    } else if (strcmp(line, "storage remount") == 0) {
        cmd_storage_remount();
    } else if (strcmp(line, "storage reset-bridge") == 0) {
        cmd_storage_reset_bridge();
    } else if (strncmp(line, "storage force-nvs", strlen("storage force-nvs")) == 0) {
        cmd_storage_force_nvs(line);
    } else if (strcmp(line, "storage diag") == 0) {
        cmd_storage_diag();
    } else if (strcmp(line, "storage diag raw") == 0) {
        cmd_storage_diag_raw();
    } else if (strcmp(line, "storage map-policy") == 0) {
        cmd_storage_map_policy();
    } else if (strcmp(line, "storage setup") == 0) {
        cmd_storage_setup(line);
    } else if (strcmp(line, "storage filecanary") == 0) {
        cmd_storage_filecanary();
    } else if (strncmp(line, "storage map-tile-canary ", strlen("storage map-tile-canary ")) == 0) {
        cmd_storage_map_tile_canary(line);
    } else if (strncmp(line, "storage map-tile-check ", strlen("storage map-tile-check ")) == 0) {
        cmd_storage_map_tile_check(line);
    } else if (strncmp(line, "storage export-canary ", strlen("storage export-canary ")) == 0) {
        cmd_storage_export_canary(line);
    } else if (strncmp(line, "storage export-diagnostics ", strlen("storage export-diagnostics ")) == 0) {
        cmd_storage_export_diagnostics(line);
    } else if (strncmp(line, "storage export-data ", strlen("storage export-data ")) == 0) {
        cmd_storage_export_data(line);
    } else if (strncmp(line, "storage retained-canary ", strlen("storage retained-canary ")) == 0) {
        cmd_storage_retained_canary(line);
    } else if (strcmp(line, "packets") == 0) {
        cmd_packets();
    } else if (strncmp(line, "packets filter ", 15) == 0) {
        cmd_packets_filter(line);
    } else if (strncmp(line, "packets search ", 15) == 0) {
        cmd_packets_search(line);
    } else if (strncmp(line, "packets detail ", 15) == 0) {
        cmd_packets_detail(line);
    } else if (strncmp(line, "packets raw ", 12) == 0) {
        cmd_packets_raw(line);
    } else if (strcmp(line, "packets clear") == 0) {
        cmd_packets_clear();
    } else if (strcmp(line, "messages public") == 0 ||
               strncmp(line, "messages public ", 16) == 0) {
        cmd_messages_public(line);
    } else if (strcmp(line, "messages dm") == 0 ||
               strncmp(line, "messages dm ", 12) == 0) {
        if (strcmp(line, "messages dm clear") == 0) {
            cmd_messages_dm_clear();
        } else {
            cmd_messages_dm(line);
        }
    } else if (strcmp(line, "messages unread") == 0) {
        cmd_messages_unread();
    } else if (strncmp(line, "messages read ", 14) == 0) {
        cmd_messages_read(line);
    } else if (strcmp(line, "messages clear") == 0) {
        cmd_messages_clear();
    } else if (strcmp(line, "nodes") == 0) {
        cmd_nodes();
    } else if (strcmp(line, "nodes clear") == 0) {
        cmd_nodes_clear();
    } else if (strcmp(line, "contacts") == 0) {
        cmd_contacts();
    } else if (strcmp(line, "contacts export") == 0 ||
               strncmp(line, "contacts export ", 16) == 0) {
        cmd_contacts_export(line);
    } else if (strcmp(line, "contacts clear") == 0) {
        cmd_contacts_clear();
    } else if (strncmp(line, "contacts add ", 13) == 0) {
        cmd_contacts_add(line);
    } else if (strncmp(line, "contacts rename ", 16) == 0) {
        cmd_contacts_rename(line);
    } else if (strncmp(line, "contacts delete ", 16) == 0) {
        cmd_contacts_delete(line);
    } else if (strncmp(line, "contacts set ", 13) == 0) {
        cmd_contacts_set(line);
    } else if (strcmp(line, "routes") == 0) {
        cmd_routes();
    } else if (strncmp(line, "routes detail ", 14) == 0) {
        cmd_routes_detail(line);
    } else if (strncmp(line, "routes trace ", 13) == 0) {
        cmd_routes_trace(line);
    } else if (strncmp(line, "routes probe ", 13) == 0) {
        cmd_routes_probe(line);
    } else if (strcmp(line, "routes clear") == 0) {
        cmd_routes_clear();
    } else if (strcmp(line, "signal") == 0) {
        cmd_signal();
    } else if (strcmp(line, "roomservers") == 0) {
        cmd_roomservers();
    } else if (strcmp(line, "repeaters") == 0) {
        cmd_repeaters();
    } else if (strcmp(line, "health") == 0) {
        cmd_health();
    } else if (strcmp(line, "crashlog") == 0) {
        cmd_crashlog();
    } else if (strcmp(line, "crashlog clear") == 0) {
        cmd_crashlog_clear();
    } else if (strcmp(line, "wifi status") == 0) {
        cmd_wifi_status();
    } else if (strcmp(line, "wifi off") == 0) {
        cmd_wifi_off();
    } else if (strcmp(line, "wifi on") == 0) {
        cmd_wifi_on();
    } else if (strcmp(line, "wifi scan") == 0) {
        cmd_wifi_scan();
    } else if (strcmp(line, "wifi connect") == 0) {
        cmd_wifi_connect();
    } else if (strncmp(line, "wifi save ", strlen("wifi save ")) == 0) {
        cmd_wifi_save(line);
    } else if (strcmp(line, "wifi clear") == 0) {
        cmd_wifi_clear();
    } else if (strcmp(line, "ble status") == 0) {
        cmd_ble_status();
    } else if (strcmp(line, "ble off") == 0) {
        cmd_ble_off();
    } else if (strcmp(line, "ble on") == 0) {
        cmd_ble_on();
    } else if (strcmp(line, "mesh advert zero") == 0) {
        cmd_mesh_advert("mesh advert zero", false);
    } else if (strcmp(line, "mesh advert flood") == 0) {
        cmd_mesh_advert("mesh advert flood", true);
    } else if (strncmp(line, "mesh send public ", 17) == 0) {
        cmd_mesh_send_public(line);
    } else if (strncmp(line, "mesh send dm ", 13) == 0) {
        cmd_mesh_send_dm(line);
    } else if (strcmp(line, "reboot") == 0) {
        const int64_t reboot_started_us = esp_timer_get_time();
        uint32_t remaining_ms = reboot_deadline_remaining_ms(reboot_started_us);
        esp_err_t storage_quiesce_ret =
            d1l_storage_manager_quiesce_begin(remaining_ms);
        if (storage_quiesce_ret != ESP_OK) {
            err_result("reboot", esp_err_to_name(storage_quiesce_ret),
                       "storage manager quiesce failed; reboot cancelled");
            return;
        }

        remaining_ms = reboot_deadline_remaining_ms(reboot_started_us);
        esp_err_t retained_flush_ret = remaining_ms > 0U ?
            d1l_route_store_worker_force_flush(remaining_ms) : ESP_ERR_TIMEOUT;
        if (retained_flush_ret != ESP_OK) {
            d1l_storage_manager_quiesce_end();
            err_result("reboot", esp_err_to_name(retained_flush_ret),
                       "retained storage flush failed; reboot cancelled");
            return;
        }

        remaining_ms = reboot_deadline_remaining_ms(reboot_started_us);
        esp_err_t retained_quiesce_ret = remaining_ms > 0U ?
            d1l_route_store_worker_quiesce_begin(remaining_ms) : ESP_ERR_TIMEOUT;
        if (retained_quiesce_ret != ESP_OK) {
            d1l_storage_manager_quiesce_end();
            err_result("reboot", esp_err_to_name(retained_quiesce_ret),
                       "retained worker quiesce failed; reboot cancelled");
            return;
        }

        remaining_ms = reboot_deadline_remaining_ms(reboot_started_us);
        esp_err_t bridge_quiesce_ret = remaining_ms > 0U ?
            d1l_rp2040_bridge_quiesce_begin(remaining_ms) : ESP_ERR_TIMEOUT;
        if (bridge_quiesce_ret != ESP_OK) {
            d1l_route_store_worker_quiesce_end();
            d1l_storage_manager_quiesce_end();
            err_result("reboot", esp_err_to_name(bridge_quiesce_ret),
                       "RP2040 bridge quiesce failed; reboot cancelled");
            return;
        }

        if (reboot_deadline_remaining_ms(reboot_started_us) == 0U) {
            d1l_rp2040_bridge_quiesce_end();
            d1l_route_store_worker_quiesce_end();
            d1l_storage_manager_quiesce_end();
            err_result("reboot", esp_err_to_name(ESP_ERR_TIMEOUT),
                       "reboot quiesce deadline expired; reboot cancelled");
            return;
        }
        ok_begin("reboot");
        printf(",\"rebooting\":true,\"storage_manager_quiesced\":true,\"retained_worker_quiesced\":true,\"rp2040_bridge_quiesced\":true,\"retained_flush\":\"ESP_OK\",\"route_flush\":\"ESP_OK\"}\n");
        fflush(stdout);
        /* Keep all three quiesce locks held so neither retained persistence nor
         * a new UART2 SD exchange can begin while restart drains UART FIFOs. */
        esp_restart();
    } else if (strcmp(line, "factory-reset-confirm") == 0) {
        err_result("factory-reset-confirm", "SAFETY_NONCE_REQUIRED", "factory reset will require a generated nonce in a later phase");
    } else {
        err_result(line, "UNKNOWN_COMMAND", "send help for supported commands");
    }
}

void d1l_usb_console_run(void)
{
    static char line[256];
    size_t used = 0;
    bool dropping_overlong = false;
    cmd_help();
    bool prompt_pending = true;
    while (true) {
        if (prompt_pending) {
            printf(D1L_USB_CONSOLE_PROMPT);
            fflush(stdout);
            prompt_pending = false;
        }

        int ch = fgetc(stdin);
        if (ch == EOF) {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (ch == '\n' || ch == '\r') {
            if (dropping_overlong) {
                dropping_overlong = false;
                used = 0;
                prompt_pending = true;
                continue;
            }
            if (used == 0U) {
                continue;
            }
            line[used] = '\0';
            used = 0;
            trim_line(line);
            if (line[0] == '\0') {
                prompt_pending = true;
                continue;
            }
            printf("\n");
            handle_line(line);
            prompt_pending = true;
            continue;
        }

        if (dropping_overlong) {
            continue;
        }

        if (used + 1U >= sizeof(line)) {
            used = 0;
            dropping_overlong = true;
            printf("\n");
            err_result("console", "LINE_TOO_LONG", "USB console command exceeded the line buffer");
            prompt_pending = true;
            continue;
        }

        line[used++] = (char)ch;
    }
}
