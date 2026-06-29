#include "usb_console.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_system.h"

#include "d1l_config.h"
#include "app/app_model.h"
#include "app/settings_model.h"
#include "comms/companion_3byte.h"
#include "diagnostics/health_monitor.h"
#include "hal/backlight.h"
#include "hal/indicator_board.h"
#include "hal/rp2040_bridge.h"
#include "hal/sx1262_indicator.h"
#include "mesh/packet_log.h"
#include "mesh/meshcore_radio_profile.h"
#include "mesh/meshcore_service.h"

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
    printf(",\"firmware\":\"%s\",\"version\":\"%s\",\"idf\":\"%s\",\"meshcore_ca_desk_mode\":true}\n",
           D1L_FIRMWARE_NAME, D1L_FIRMWARE_VERSION, esp_get_idf_version());
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
    ok_begin("settings get");
    printf(",\"node_name\":\"%s\",\"role\":\"%s\",\"wifi_enabled\":%s,\"ble_companion_enabled\":%s,\"observer_enabled\":%s,\"high_contrast\":%s,\"night_mode\":%s,\"path_hash_bytes\":%u}\n",
           settings->node_name, d1l_settings_role_name(settings->role),
           bool_json(settings->wifi_enabled), bool_json(settings->ble_companion_enabled),
           bool_json(settings->observer_enabled), bool_json(settings->high_contrast),
           bool_json(settings->night_mode), settings->path_hash_bytes);
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
    d1l_settings_t settings = *d1l_settings_current();
    snprintf(settings.node_name, sizeof(settings.node_name), "%s", arg);
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

static void cmd_identity_status(void)
{
    const d1l_settings_t *settings = d1l_settings_current();
    ok_begin("identity status");
    printf(",\"node_name\":\"%s\",\"role\":\"%s\",\"meshcore_local_identity\":\"pending_phase2_cpp_binding\",\"public_key_ready\":false,\"fingerprint\":null}\n",
           settings->node_name, d1l_settings_role_name(settings->role));
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
    printf("],\"expected\":[\"0x20\",\"0x38\"]}\n");
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
    uint8_t touches = 0;
    uint16_t x = 0;
    uint16_t y = 0;
    esp_err_t ret = d1l_board_touch_sample(&touches, &x, &y);
    if (ret != ESP_OK) {
        err_result("touch test", esp_err_to_name(ret), "touch sample requires initialized D1L touch controller");
        return;
    }
    ok_begin("touch test");
    printf(",\"touches\":%u,\"x\":%u,\"y\":%u,\"manual_confirm\":true}\n", touches, x, y);
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
    ok_begin(cmd);
    printf(",\"profile_id\":\"%s\",\"region\":\"%s\",\"frequency_hz\":%lu,\"bandwidth_khz\":%.1f,\"sf\":%u,\"cr\":%u,\"tx_power_dbm\":%d,\"tcxo\":\"%s\",\"rx_boost\":%s,\"persisted\":true,\"applied_to_radio\":false}\n",
           profile.profile_id, profile.region_label, (unsigned long)profile.frequency_hz,
           profile.bandwidth_khz, profile.spreading_factor, profile.coding_rate,
           profile.tx_power_dbm, profile.tcxo, bool_json(profile.rx_boost));
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

static void cmd_mesh_status(void)
{
    d1l_meshcore_service_status_t status = d1l_meshcore_service_status();
    ok_begin("mesh status");
    printf(",\"phase\":\"phase2_foundation\",\"state\":\"%s\",\"radio_profile\":\"uscan-meshcore-default\",\"identity_ready\":%s,\"radio_ready\":%s,\"companion_framing_ready\":%s,\"path_hash_bytes\":%u,\"rx_packets\":%lu,\"tx_packets\":%lu,\"rejected_commands\":%lu,\"note\":\"MeshCore C++ binding and RF RX/TX require Phase 2 hardware validation\"}\n",
           d1l_meshcore_service_state_name(status.state), bool_json(status.identity_ready),
           bool_json(status.radio_ready), bool_json(status.companion_framing_ready),
           status.path_hash_bytes, (unsigned long)status.rx_packets,
           (unsigned long)status.tx_packets, (unsigned long)status.rejected_commands);
}

static void cmd_mesh_advert(const char *cmd, bool flood)
{
    esp_err_t ret = d1l_meshcore_service_request_advert(flood);
    if (ret != ESP_OK) {
        err_result(cmd, "MESHCORE_RADIO_NOT_READY", "MeshCore radio binding is not enabled until Phase 2 RF validation");
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
        err_result("mesh send public", ret == ESP_ERR_INVALID_ARG ? "EMPTY_MESSAGE" : "MESHCORE_RADIO_NOT_READY",
                   "MeshCore radio binding is not enabled until Phase 2 RF validation");
        return;
    }
    ok_begin("mesh send public");
    printf(",\"queued\":true}\n");
}

static void cmd_companion_status(void)
{
    ok_begin("companion status");
    printf(",\"framing\":\"meshcore-3byte\",\"header_bytes\":%u,\"app_to_radio\":\"%c\",\"radio_to_app\":\"%c\",\"length\":\"uint16_le\",\"max_payload_bytes\":%u,\"transport_codec\":\"ready\",\"meshcore_bridge\":\"phase2_stub\",\"path_hash_bytes_supported\":[1,2,3],\"default_path_hash_bytes\":1}\n",
           D1L_COMPANION3_HEADER_SIZE, D1L_COMPANION3_APP_TO_RADIO,
           D1L_COMPANION3_RADIO_TO_APP, D1L_COMPANION3_MAX_FRAME_SIZE);
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

static void cmd_packets(void)
{
    d1l_packet_log_stats_t stats = d1l_packet_log_stats();
    d1l_packet_log_entry_t entries[8];
    size_t copied = d1l_packet_log_copy_recent(entries, 8);
    ok_begin("packets");
    printf(",\"count\":%u,\"capacity\":%u,\"total_written\":%lu,\"dropped_oldest\":%lu,\"entries\":[",
           (unsigned)stats.count, (unsigned)stats.capacity,
           (unsigned long)stats.total_written, (unsigned long)stats.dropped_oldest);
    for (size_t i = 0; i < copied; ++i) {
        const d1l_packet_log_entry_t *e = &entries[i];
        printf("%s{\"seq\":%lu,\"uptime_ms\":%lu,\"direction\":\"%s\",\"kind\":\"%s\",\"rssi_dbm\":%d,\"snr_tenths\":%d,\"path_hash_bytes\":%u,\"path_hops\":%u,\"payload_len\":%u}",
               i ? "," : "", (unsigned long)e->seq, (unsigned long)e->uptime_ms,
               e->direction, e->kind, e->rssi_dbm, e->snr_tenths,
               e->path_hash_bytes, e->path_hops, e->payload_len);
    }
    printf("],\"note\":\"Phase 1 ring is ready; real packets require Phase 2 MeshCore radio integration\"}\n");
}

static void cmd_health(void)
{
    d1l_health_snapshot_t h = d1l_health_snapshot();
    ok_begin("health");
    printf(",\"uptime_ms\":%lu,\"heap_free\":%lu,\"heap_min_free\":%lu,\"psram_free\":%lu,\"reset_reason\":\"%s\",\"board_ready\":%s,\"ui_ready\":%s}\n",
           (unsigned long)h.uptime_ms, (unsigned long)h.heap_free, (unsigned long)h.heap_min_free,
           (unsigned long)h.psram_free, h.reset_reason,
           d1l_app_model_get()->board_ready ? "true" : "false",
           d1l_app_model_get()->ui_ready ? "true" : "false");
}

static void cmd_help(void)
{
    ok_begin("help");
    printf(",\"commands\":[\"help\",\"version\",\"board\",\"settings get\",\"settings reset\",\"settings set name <name>\",\"settings set pathhash <1|2|3>\",\"identity status\",\"i2c\",\"display test\",\"touch test\",\"button\",\"backlight <0-100>\",\"radiohw\",\"radio get\",\"radio set preset uscan\",\"radio set freq 910.525\",\"radio set bw 62.5\",\"radio set sf 7\",\"radio set cr 5\",\"mesh status\",\"companion status\",\"rp2040 status\",\"mesh advert zero\",\"mesh advert flood\",\"mesh send public <text>\",\"nodes\",\"packets\",\"health\",\"crashlog\",\"wifi scan\",\"wifi off\",\"ble status\",\"reboot\",\"factory-reset-confirm\"]}\n");
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
    } else if (strcmp(line, "identity status") == 0) {
        cmd_identity_status();
    } else if (strcmp(line, "i2c") == 0) {
        cmd_i2c();
    } else if (strcmp(line, "display test") == 0) {
        cmd_display_test();
    } else if (strcmp(line, "touch test") == 0) {
        cmd_touch_test();
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
    } else if (strcmp(line, "mesh status") == 0) {
        cmd_mesh_status();
    } else if (strcmp(line, "companion status") == 0) {
        cmd_companion_status();
    } else if (strcmp(line, "rp2040 status") == 0) {
        cmd_rp2040_status();
    } else if (strcmp(line, "packets") == 0) {
        cmd_packets();
    } else if (strcmp(line, "wifi off") == 0) {
        ok_begin("wifi off");
        printf(",\"wifi\":\"off\",\"persisted\":true}\n");
    } else if (strcmp(line, "wifi scan") == 0) {
        err_result("wifi scan", "WIFI_DISABLED_PHASE1", "Wi-Fi defaults off during Phase 1; enable in later connectivity phase");
    } else if (strcmp(line, "ble status") == 0) {
        ok_begin("ble status");
        printf(",\"ble\":\"off\",\"reason\":\"Phase 1 keeps BLE disabled until radio/UI heap headroom is measured\"}\n");
    } else if (strcmp(line, "mesh advert zero") == 0) {
        cmd_mesh_advert("mesh advert zero", false);
    } else if (strcmp(line, "mesh advert flood") == 0) {
        cmd_mesh_advert("mesh advert flood", true);
    } else if (strncmp(line, "mesh send public ", 17) == 0) {
        cmd_mesh_send_public(line);
    } else if (strcmp(line, "nodes") == 0 || strcmp(line, "crashlog") == 0) {
        err_result(line, "PHASE2_STUB", "MeshCore protocol and persistence commands are scheduled for Phase 2");
    } else if (strcmp(line, "reboot") == 0) {
        ok_begin("reboot");
        printf(",\"rebooting\":true}\n");
        fflush(stdout);
        esp_restart();
    } else if (strcmp(line, "factory-reset-confirm") == 0) {
        err_result("factory-reset-confirm", "SAFETY_NONCE_REQUIRED", "factory reset will require a generated nonce in a later phase");
    } else {
        err_result(line, "UNKNOWN_COMMAND", "send help for supported commands");
    }
}

void d1l_usb_console_run(void)
{
    char line[256];
    cmd_help();
    while (true) {
        printf(D1L_USB_CONSOLE_PROMPT);
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) {
            continue;
        }
        trim_line(line);
        if (line[0] == '\0') {
            continue;
        }
        handle_line(line);
    }
}
