#include "rp2040_bridge.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#include "hal/indicator_pins.h"

#define D1L_RP2040_UART_BUF_SIZE 512
#define D1L_RP2040_SD_QUERY "DESKOS_SD_STATUS\n"
#define D1L_RP2040_SD_REPLY_PREFIX "DESKOS_SD_STATUS"
#define D1L_RP2040_SD_LINE_MAX 192U

static d1l_rp2040_status_t s_status = {
    .uart_ready = false,
    .init_result = ESP_ERR_INVALID_STATE,
    .buffered_bytes = 0,
};

static void init_sd_status(d1l_rp2040_sd_status_t *status, esp_err_t err)
{
    memset(status, 0, sizeof(*status));
    status->bridge_ready = s_status.uart_ready;
    status->last_error = err;
    snprintf(status->state, sizeof(status->state), "%s",
             s_status.uart_ready ? "protocol_pending" : "bridge_unavailable");
    snprintf(status->filesystem, sizeof(status->filesystem), "unknown");
    snprintf(status->note, sizeof(status->note), "%s",
             s_status.uart_ready ?
             "RP2040 UART is ready but DeskOS SD status protocol has not answered" :
             "RP2040 UART bridge is unavailable");
}

static bool parse_bool_token(const char *line, const char *key, bool *out_value)
{
    const char *p = strstr(line, key);
    if (!p || !out_value) {
        return false;
    }
    p += strlen(key);
    if (*p == '1' || strncmp(p, "true", 4) == 0 || strncmp(p, "yes", 3) == 0) {
        *out_value = true;
        return true;
    }
    if (*p == '0' || strncmp(p, "false", 5) == 0 || strncmp(p, "no", 2) == 0) {
        *out_value = false;
        return true;
    }
    return false;
}

static bool parse_u32_token(const char *line, const char *key, uint32_t *out_value)
{
    const char *p = strstr(line, key);
    if (!p || !out_value) {
        return false;
    }
    p += strlen(key);
    unsigned long value = 0;
    if (sscanf(p, "%lu", &value) != 1) {
        return false;
    }
    *out_value = (uint32_t)value;
    return true;
}

static void parse_word_token(const char *line, const char *key, char *dest, size_t dest_size)
{
    const char *p = strstr(line, key);
    if (!p || !dest || dest_size == 0) {
        return;
    }
    p += strlen(key);
    size_t out = 0;
    while (*p && *p != ' ' && *p != '\r' && *p != '\n' && out + 1U < dest_size) {
        dest[out++] = *p++;
    }
    dest[out] = '\0';
}

static esp_err_t parse_sd_status_line(const char *line, d1l_rp2040_sd_status_t *status)
{
    if (!line || !status || strncmp(line, D1L_RP2040_SD_REPLY_PREFIX,
                                    strlen(D1L_RP2040_SD_REPLY_PREFIX)) != 0) {
        return ESP_FAIL;
    }

    init_sd_status(status, ESP_OK);
    status->protocol_supported = true;
    parse_word_token(line, "state=", status->state, sizeof(status->state));
    parse_word_token(line, "fs=", status->filesystem, sizeof(status->filesystem));
    parse_word_token(line, "note=", status->note, sizeof(status->note));
    (void)parse_bool_token(line, "present=", &status->card_present);
    (void)parse_bool_token(line, "mounted=", &status->filesystem_mounted);
    (void)parse_bool_token(line, "deskos=", &status->deskos_root_ready);
    (void)parse_bool_token(line, "format_required=", &status->format_required);
    (void)parse_bool_token(line, "format_supported=", &status->format_supported);
    (void)parse_u32_token(line, "capacity_kb=", &status->capacity_kb);
    (void)parse_u32_token(line, "free_kb=", &status->free_kb);

    if (status->card_present && !status->filesystem_mounted) {
        status->format_required = true;
    }
    status->data_ready = status->card_present &&
                         status->filesystem_mounted &&
                         status->deskos_root_ready &&
                         !status->format_required;
    if (status->state[0] == '\0') {
        snprintf(status->state, sizeof(status->state), "%s",
                 status->data_ready ? "ready" :
                 status->card_present ? "setup_required" : "no_card");
    }
    if (status->note[0] == '\0') {
        snprintf(status->note, sizeof(status->note), "%s",
                 status->data_ready ? "SD card is ready for DeskOS data" :
                 status->format_required ? "SD card requires explicit setup before use" :
                 "SD card is not ready for DeskOS data");
    }
    return ESP_OK;
}

esp_err_t d1l_rp2040_bridge_init(void)
{
    const d1l_rp2040_pins_t *pins = d1l_rp2040_pins();
    s_status.uart_port = pins->uart_port;
    s_status.tx_gpio = pins->esp_tx_gpio;
    s_status.rx_gpio = pins->esp_rx_gpio;
    s_status.baud_rate = pins->baud_rate;
    s_status.reset_expander_pin = pins->expander_reset;

    uart_config_t uart_config = {
        .baud_rate = pins->baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_driver_install((uart_port_t)pins->uart_port, D1L_RP2040_UART_BUF_SIZE, 0, 0, NULL, 0);
    if (ret == ESP_OK || ret == ESP_ERR_INVALID_STATE) {
        ret = uart_param_config((uart_port_t)pins->uart_port, &uart_config);
    }
    if (ret == ESP_OK) {
        ret = uart_set_pin((uart_port_t)pins->uart_port, pins->esp_tx_gpio, pins->esp_rx_gpio,
                           UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }

    s_status.init_result = ret;
    s_status.uart_ready = (ret == ESP_OK);
    return ret;
}

esp_err_t d1l_rp2040_bridge_status(d1l_rp2040_status_t *out_status)
{
    if (out_status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_status.uart_ready) {
        size_t buffered = 0;
        esp_err_t ret = uart_get_buffered_data_len((uart_port_t)s_status.uart_port, &buffered);
        if (ret == ESP_OK) {
            s_status.buffered_bytes = (uint32_t)buffered;
        } else {
            s_status.init_result = ret;
        }
    }
    *out_status = s_status;
    return s_status.init_result;
}

esp_err_t d1l_rp2040_bridge_probe_sd(d1l_rp2040_sd_status_t *out_status, uint32_t timeout_ms)
{
    if (out_status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_status.uart_ready) {
        init_sd_status(out_status, s_status.init_result);
        return s_status.init_result;
    }

    init_sd_status(out_status, ESP_ERR_TIMEOUT);
    uart_flush_input((uart_port_t)s_status.uart_port);
    const int written = uart_write_bytes((uart_port_t)s_status.uart_port,
                                         D1L_RP2040_SD_QUERY,
                                         strlen(D1L_RP2040_SD_QUERY));
    if (written <= 0) {
        out_status->last_error = ESP_FAIL;
        snprintf(out_status->state, sizeof(out_status->state), "query_failed");
        snprintf(out_status->note, sizeof(out_status->note), "Could not write SD status query to RP2040 UART");
        return ESP_FAIL;
    }

    const int64_t deadline_us = esp_timer_get_time() + ((int64_t)timeout_ms * 1000LL);
    char line[D1L_RP2040_SD_LINE_MAX];
    size_t used = 0;
    while (esp_timer_get_time() < deadline_us) {
        uint8_t ch = 0;
        int len = uart_read_bytes((uart_port_t)s_status.uart_port, &ch, 1, pdMS_TO_TICKS(10));
        if (len <= 0) {
            continue;
        }
        if (ch == '\n') {
            line[used] = '\0';
            if (strncmp(line, D1L_RP2040_SD_REPLY_PREFIX,
                        strlen(D1L_RP2040_SD_REPLY_PREFIX)) == 0) {
                esp_err_t ret = parse_sd_status_line(line, out_status);
                out_status->last_error = ret;
                return ret;
            }
            used = 0;
            continue;
        }
        if (ch == '\r') {
            continue;
        }
        if (used + 1U < sizeof(line)) {
            line[used++] = (char)ch;
        } else {
            out_status->response_truncated = true;
            used = 0;
        }
    }

    out_status->last_error = ESP_ERR_TIMEOUT;
    return ESP_ERR_TIMEOUT;
}
