#include "sx1262_indicator.h"

#include <string.h>

#include "driver/spi_master.h"
#include "esp_rom_sys.h"
#include "bsp_sx126x.h"
#include "sdkconfig.h"
#include "tca9535.h"

static void fill_failure(d1l_radiohw_status_t *status, const char *code)
{
    memset(status, 0, sizeof(*status));
    status->tcxo_default = "NONE";
    status->failure_code = code;
}

static esp_err_t read_radio_pins(uint16_t *pins)
{
#if CONFIG_LCD_BOARD_SENSECAP_INDICATOR_D1L
    return tca9535_read_input_pins(pins);
#else
    uint8_t pins8 = 0;
    esp_err_t ret = indicator_io_expander->read_input_pins(&pins8);
    *pins = pins8;
    return ret;
#endif
}

esp_err_t d1l_sx1262_probe(d1l_radiohw_status_t *status)
{
    if (!status) {
        return ESP_ERR_INVALID_ARG;
    }
    fill_failure(status, "UNPROBED");

    if (!indicator_io_expander) {
        fill_failure(status, "EXPANDER_NOT_READY");
        return ESP_ERR_INVALID_STATE;
    }

    uint16_t pins = 0;
    esp_err_t ret = read_radio_pins(&pins);
    if (ret != ESP_OK) {
        fill_failure(status, "EXPANDER_READ_FAILED");
        return ret;
    }

    status->expander_ready = true;
    status->busy = (pins & (1U << EXPANDER_IO_RADIO_BUSY)) ? 1 : 0;
    status->dio1 = (pins & (1U << EXPANDER_IO_RADIO_DIO_1)) ? 1 : 0;
    status->ver_pin = (pins & (1U << EXPANDER_IO_RADIO_VER)) ? 1 : 0;
    status->tcxo_default = "NONE";

    spi_device_handle_t spi = bsp_sx126x_spi_handle_get();
    if (!spi) {
        status->failure_code = "SPI_NOT_READY";
        return ESP_ERR_INVALID_STATE;
    }

    for (int i = 0; i < 50; ++i) {
        ret = read_radio_pins(&pins);
        if (ret != ESP_OK) {
            status->failure_code = "EXPANDER_READ_FAILED";
            return ret;
        }
        if ((pins & (1U << EXPANDER_IO_RADIO_BUSY)) == 0) {
            break;
        }
        esp_rom_delay_us(1000);
    }

    uint8_t tx[2] = {0xC0, 0x00}; /* SX126x GET_STATUS without touching TCXO. */
    uint8_t rx[2] = {0};
    spi_transaction_t t = {
        .length = sizeof(tx) * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };

    indicator_io_expander->set_level(EXPANDER_IO_RADIO_NSS, 0);
    ret = spi_device_transmit(spi, &t);
    indicator_io_expander->set_level(EXPANDER_IO_RADIO_NSS, 1);
    if (ret != ESP_OK) {
        status->failure_code = "SPI_STATUS_FAILED";
        return ret;
    }

    status->status_byte = rx[1];
    status->present = true;
    status->failure_code = NULL;
    return ESP_OK;
}
