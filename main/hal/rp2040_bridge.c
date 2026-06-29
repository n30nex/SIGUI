#include "rp2040_bridge.h"

#include <stddef.h>

#include "driver/uart.h"

#include "hal/indicator_pins.h"

#define D1L_RP2040_UART_BUF_SIZE 512

static d1l_rp2040_status_t s_status = {
    .uart_ready = false,
    .init_result = ESP_ERR_INVALID_STATE,
    .buffered_bytes = 0,
};

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
