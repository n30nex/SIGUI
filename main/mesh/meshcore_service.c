#include "meshcore_service.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "app/settings_model.h"
#include "bsp_sx126x.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/aes.h"
#include "mbedtls/md.h"
#include "mesh/meshcore_radio_profile.h"
#include "mesh/packet_log.h"
#include "radio.h"
#include "sx126x.h"

#define D1L_MESHCORE_ROUTE_FLOOD 0x01U
#define D1L_MESHCORE_PAYLOAD_GROUP_TEXT 0x05U
#define D1L_MESHCORE_HEADER_GROUP_TEXT_FLOOD \
    ((uint8_t)((D1L_MESHCORE_PAYLOAD_GROUP_TEXT << 2) | D1L_MESHCORE_ROUTE_FLOOD))
#define D1L_MESHCORE_PUB_KEY_SIZE 32U
#define D1L_MESHCORE_CIPHER_BLOCK_SIZE 16U
#define D1L_MESHCORE_CIPHER_MAC_SIZE 2U
#define D1L_MESHCORE_MAX_RAW_PACKET 255U
#define D1L_MESHCORE_MAX_TEXT_BYTES 160U
#define D1L_MESHCORE_BW_INDEX_62K5 3U
#define D1L_MESHCORE_PREAMBLE_LOW_SF 32U
#define D1L_MESHCORE_TX_TIMEOUT_MS 5000U

static const char *TAG = "d1l_mesh";
static d1l_meshcore_service_status_t s_status;
static bool s_radio_started;
static volatile bool s_tx_busy;
extern SX126x_t SX126x;

static const uint8_t s_public_secret[D1L_MESHCORE_PUB_KEY_SIZE] = {
    0x8b, 0x33, 0x87, 0xe9, 0xc5, 0xcd, 0xea, 0x6a,
    0xc9, 0xe5, 0xed, 0xba, 0xa1, 0x15, 0xcd, 0x72,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static uint8_t s_public_channel_hash;

static void d1l_meshcore_start_rx(void);

static void sanitize_note(char *dest, size_t dest_size, const char *src)
{
    if (dest_size == 0) {
        return;
    }
    size_t out = 0;
    while (src && src[0] && out + 1U < dest_size) {
        unsigned char c = (unsigned char)*src++;
        if (c < 32 || c > 126 || c == '"' || c == '\\') {
            c = '_';
        }
        dest[out++] = (char)c;
    }
    dest[out] = '\0';
}

static void append_packet_log(const char *direction, const char *kind, int rssi, int snr_quarters,
                              uint8_t path_hash_bytes, uint8_t path_hops, uint16_t payload_len,
                              const char *note)
{
    d1l_packet_log_entry_t entry = {
        .rssi_dbm = rssi,
        .snr_tenths = (snr_quarters * 10) / 4,
        .path_hash_bytes = path_hash_bytes,
        .path_hops = path_hops,
        .payload_len = payload_len,
    };
    strncpy(entry.direction, direction, sizeof(entry.direction) - 1U);
    strncpy(entry.kind, kind, sizeof(entry.kind) - 1U);
    sanitize_note(entry.note, sizeof(entry.note), note);
    d1l_packet_log_append(&entry);
}

static uint32_t meshcore_timestamp(void)
{
    time_t now = time(NULL);
    if (now > 1600000000) {
        return (uint32_t)now;
    }
    return (uint32_t)(esp_timer_get_time() / 1000000ULL);
}

static void write_le32(uint8_t *dest, uint32_t value)
{
    dest[0] = (uint8_t)(value & 0xffU);
    dest[1] = (uint8_t)((value >> 8) & 0xffU);
    dest[2] = (uint8_t)((value >> 16) & 0xffU);
    dest[3] = (uint8_t)((value >> 24) & 0xffU);
}

static bool path_len_valid(uint8_t path_len)
{
    const uint8_t hash_count = path_len & 63U;
    const uint8_t hash_size = (uint8_t)((path_len >> 6) + 1U);
    return hash_size < 4U && (hash_count * hash_size) <= 64U;
}

static uint8_t path_hash_size(uint8_t path_len)
{
    return (uint8_t)((path_len >> 6) + 1U);
}

static uint8_t path_hash_count(uint8_t path_len)
{
    return (uint8_t)(path_len & 63U);
}

static uint8_t path_byte_len(uint8_t path_len)
{
    return (uint8_t)(path_hash_size(path_len) * path_hash_count(path_len));
}

static bool bandwidth_to_driver_index(float bandwidth_khz, uint32_t *out_index,
                                      RadioLoRaBandwidths_t *out_sx1262_bw)
{
    const uint16_t tenths = (uint16_t)((bandwidth_khz * 10.0f) + 0.5f);
    switch (tenths) {
    case 625:
        *out_index = D1L_MESHCORE_BW_INDEX_62K5;
        *out_sx1262_bw = LORA_BW_062;
        return true;
    case 1250:
        *out_index = 0;
        *out_sx1262_bw = LORA_BW_125;
        return true;
    case 2500:
        *out_index = 1;
        *out_sx1262_bw = LORA_BW_250;
        return true;
    case 5000:
        *out_index = 2;
        *out_sx1262_bw = LORA_BW_500;
        return true;
    default:
        return false;
    }
}

static bool coding_rate_to_driver_value(uint8_t coding_rate, uint8_t *out_cr)
{
    if (coding_rate < 5 || coding_rate > 8) {
        return false;
    }
    *out_cr = (uint8_t)(coding_rate - 4U);
    return true;
}

static uint32_t lora_bw_hz(RadioLoRaBandwidths_t bw)
{
    switch (bw) {
    case LORA_BW_062:
        return 62500UL;
    case LORA_BW_125:
        return 125000UL;
    case LORA_BW_250:
        return 250000UL;
    case LORA_BW_500:
        return 500000UL;
    default:
        return 0;
    }
}

static uint8_t low_datarate_optimize(RadioLoRaBandwidths_t bw, uint8_t spreading_factor)
{
    const uint32_t bw_hz = lora_bw_hz(bw);
    if (bw_hz == 0 || spreading_factor >= 31) {
        return 0;
    }
    const uint32_t symbol_time_us = ((uint32_t)1U << spreading_factor) * 1000000UL / bw_hz;
    return symbol_time_us > 16000UL ? 1U : 0U;
}

static uint8_t sx1262_read_reg(uint16_t reg)
{
    uint8_t value = 0;
    SX126xReadRegisters(reg, &value, 1);
    return value;
}

static void sx1262_write_reg(uint16_t reg, uint8_t value)
{
    SX126xWriteRegisters(reg, &value, 1);
}

static void apply_sx1262_lora_params(const d1l_radio_profile_t *profile, RadioLoRaBandwidths_t bw,
                                     uint8_t cr_value)
{
    SX126x.ModulationParams.PacketType = PACKET_TYPE_LORA;
    SX126x.ModulationParams.Params.LoRa.SpreadingFactor =
        (RadioLoRaSpreadingFactors_t)profile->spreading_factor;
    SX126x.ModulationParams.Params.LoRa.Bandwidth = bw;
    SX126x.ModulationParams.Params.LoRa.CodingRate = (RadioLoRaCodingRates_t)cr_value;
    SX126x.ModulationParams.Params.LoRa.LowDatarateOptimize =
        low_datarate_optimize(bw, profile->spreading_factor);

    SX126x.PacketParams.PacketType = PACKET_TYPE_LORA;
    SX126x.PacketParams.Params.LoRa.PreambleLength = D1L_MESHCORE_PREAMBLE_LOW_SF;
    SX126x.PacketParams.Params.LoRa.HeaderType = LORA_PACKET_VARIABLE_LENGTH;
    SX126x.PacketParams.Params.LoRa.PayloadLength = 0xff;
    SX126x.PacketParams.Params.LoRa.CrcMode = LORA_CRC_ON;
    SX126x.PacketParams.Params.LoRa.InvertIQ = LORA_IQ_NORMAL;

    Radio.Standby();
    Radio.SetModem(MODEM_LORA);
    SX126xSetModulationParams(&SX126x.ModulationParams);
    SX126xSetPacketParams(&SX126x.PacketParams);
    SX126xSetLoRaSymbNumTimeout(0);

    sx1262_write_reg(REG_IQ_POLARITY, sx1262_read_reg(REG_IQ_POLARITY) | (1 << 2));
    if (bw == LORA_BW_500) {
        sx1262_write_reg(REG_TX_MODULATION, sx1262_read_reg(REG_TX_MODULATION) & ~(1 << 2));
    } else {
        sx1262_write_reg(REG_TX_MODULATION, sx1262_read_reg(REG_TX_MODULATION) | (1 << 2));
    }
}

static esp_err_t meshcore_encrypt_then_mac(uint8_t *dest, size_t dest_size, const uint8_t *src,
                                           size_t src_len, size_t *out_len)
{
    if (!dest || !src || !out_len || src_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t enc_len = ((src_len + D1L_MESHCORE_CIPHER_BLOCK_SIZE - 1U) /
                            D1L_MESHCORE_CIPHER_BLOCK_SIZE) *
                           D1L_MESHCORE_CIPHER_BLOCK_SIZE;
    if (D1L_MESHCORE_CIPHER_MAC_SIZE + enc_len > dest_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    int ret = mbedtls_aes_setkey_enc(&aes, s_public_secret, 128);
    if (ret != 0) {
        mbedtls_aes_free(&aes);
        return ESP_FAIL;
    }

    uint8_t block[D1L_MESHCORE_CIPHER_BLOCK_SIZE];
    uint8_t *ciphertext = dest + D1L_MESHCORE_CIPHER_MAC_SIZE;
    size_t offset = 0;
    while (offset < enc_len) {
        memset(block, 0, sizeof(block));
        const size_t remaining = src_len > offset ? src_len - offset : 0;
        const size_t copy_len = remaining > sizeof(block) ? sizeof(block) : remaining;
        if (copy_len > 0) {
            memcpy(block, src + offset, copy_len);
        }
        ret = mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, block, ciphertext + offset);
        if (ret != 0) {
            mbedtls_aes_free(&aes);
            return ESP_FAIL;
        }
        offset += sizeof(block);
    }
    mbedtls_aes_free(&aes);

    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    uint8_t hmac[32];
    ret = mbedtls_md_hmac(md, s_public_secret, sizeof(s_public_secret), ciphertext, enc_len, hmac);
    if (ret != 0) {
        return ESP_FAIL;
    }
    memcpy(dest, hmac, D1L_MESHCORE_CIPHER_MAC_SIZE);
    *out_len = D1L_MESHCORE_CIPHER_MAC_SIZE + enc_len;
    return ESP_OK;
}

static size_t meshcore_decrypt_after_mac(uint8_t *dest, size_t dest_size, const uint8_t *src, size_t src_len)
{
    if (!dest || !src || src_len <= D1L_MESHCORE_CIPHER_MAC_SIZE) {
        return 0;
    }
    const size_t enc_len = src_len - D1L_MESHCORE_CIPHER_MAC_SIZE;
    if ((enc_len % D1L_MESHCORE_CIPHER_BLOCK_SIZE) != 0 || enc_len > dest_size) {
        return 0;
    }

    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    uint8_t hmac[32];
    int ret = mbedtls_md_hmac(md, s_public_secret, sizeof(s_public_secret),
                              src + D1L_MESHCORE_CIPHER_MAC_SIZE, enc_len, hmac);
    if (ret != 0 || memcmp(hmac, src, D1L_MESHCORE_CIPHER_MAC_SIZE) != 0) {
        return 0;
    }

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    ret = mbedtls_aes_setkey_dec(&aes, s_public_secret, 128);
    if (ret != 0) {
        mbedtls_aes_free(&aes);
        return 0;
    }

    for (size_t offset = 0; offset < enc_len; offset += D1L_MESHCORE_CIPHER_BLOCK_SIZE) {
        ret = mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT,
                                    src + D1L_MESHCORE_CIPHER_MAC_SIZE + offset, dest + offset);
        if (ret != 0) {
            mbedtls_aes_free(&aes);
            return 0;
        }
    }
    mbedtls_aes_free(&aes);
    return enc_len;
}

static esp_err_t build_public_text_packet(const char *text, uint8_t path_hash_bytes, uint8_t *raw,
                                          size_t raw_size, uint8_t *out_len)
{
    if (!text || !raw || !out_len || text[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (path_hash_bytes < 1 || path_hash_bytes > 3) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t plain[D1L_MESHCORE_MAX_TEXT_BYTES];
    write_le32(plain, meshcore_timestamp());
    plain[4] = 0;

    int written = snprintf((char *)&plain[5], sizeof(plain) - 5U, "%s", text);
    if (written < 0) {
        return ESP_FAIL;
    }
    const size_t max_message_len = sizeof(plain) - 5U - 1U;
    size_t message_len = (size_t)written;
    if (message_len > max_message_len) {
        message_len = max_message_len;
    }
    const size_t plain_len = 5U + message_len;

    if (raw_size < 4U) {
        return ESP_ERR_INVALID_SIZE;
    }
    size_t i = 0;
    raw[i++] = D1L_MESHCORE_HEADER_GROUP_TEXT_FLOOD;
    raw[i++] = (uint8_t)((path_hash_bytes - 1U) << 6);
    raw[i++] = s_public_channel_hash;

    size_t mac_cipher_len = 0;
    esp_err_t ret = meshcore_encrypt_then_mac(&raw[i], raw_size - i, plain, plain_len, &mac_cipher_len);
    if (ret != ESP_OK) {
        return ret;
    }
    i += mac_cipher_len;
    if (i > UINT8_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    *out_len = (uint8_t)i;
    return ESP_OK;
}

static void parse_rx_public_packet(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr)
{
    if (!payload || size < 5U) {
        return;
    }
    const uint8_t header = payload[0];
    const uint8_t route = header & 0x03U;
    const uint8_t type = (header >> 2) & 0x0fU;
    if (type != D1L_MESHCORE_PAYLOAD_GROUP_TEXT) {
        return;
    }

    size_t i = 1;
    if (route == 0U || route == 3U) {
        if (size < 6U) {
            return;
        }
        i += 4U;
    }
    if (i >= size) {
        return;
    }
    const uint8_t path_len = payload[i++];
    if (!path_len_valid(path_len)) {
        return;
    }
    const uint8_t path_bytes = path_byte_len(path_len);
    if (i + path_bytes >= size) {
        return;
    }
    i += path_bytes;
    if (payload[i++] != s_public_channel_hash) {
        return;
    }

    uint8_t plain[D1L_MESHCORE_MAX_RAW_PACKET + 1U] = {0};
    const size_t plain_len = meshcore_decrypt_after_mac(plain, sizeof(plain) - 1U, &payload[i], size - i);
    if (plain_len < 6U || (plain[4] >> 2) != 0) {
        return;
    }
    plain[plain_len] = '\0';
    const char *message = (const char *)&plain[5];
    s_status.rx_packets++;
    append_packet_log("rx", "public_text", rssi, snr, path_hash_size(path_len),
                      path_hash_count(path_len), size, message);
}

static void on_tx_done(void)
{
    s_tx_busy = false;
    s_status.tx_packets++;
    s_status.state = D1L_MESHCORE_SERVICE_READY;
    d1l_meshcore_start_rx();
}

static void on_tx_timeout(void)
{
    s_tx_busy = false;
    s_status.state = D1L_MESHCORE_SERVICE_RADIO_ERROR;
    d1l_meshcore_start_rx();
}

static void on_rx_done(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr)
{
    parse_rx_public_packet(payload, size, rssi, snr);
    d1l_meshcore_start_rx();
}

static void on_rx_timeout(void)
{
    d1l_meshcore_start_rx();
}

static void on_rx_error(void)
{
    d1l_meshcore_start_rx();
}

static RadioEvents_t s_radio_events = {
    .TxDone = on_tx_done,
    .TxTimeout = on_tx_timeout,
    .RxDone = on_rx_done,
    .RxTimeout = on_rx_timeout,
    .RxError = on_rx_error,
};

static esp_err_t configure_radio_profile(const d1l_radio_profile_t *profile)
{
    uint32_t bw_index = 0;
    RadioLoRaBandwidths_t sx_bw = LORA_BW_125;
    uint8_t cr_value = 0;
    if (!bandwidth_to_driver_index(profile->bandwidth_khz, &bw_index, &sx_bw) ||
        !coding_rate_to_driver_value(profile->coding_rate, &cr_value)) {
        s_status.state = D1L_MESHCORE_SERVICE_RADIO_ERROR;
        return ESP_ERR_NOT_SUPPORTED;
    }

    const uint32_t wrapper_bw_index = bw_index == D1L_MESHCORE_BW_INDEX_62K5 ? 0 : bw_index;
    Radio.SetChannel(profile->frequency_hz);
    Radio.SetPublicNetwork(false);
    Radio.SetRxConfig(MODEM_LORA, wrapper_bw_index, profile->spreading_factor, cr_value, 0,
                      D1L_MESHCORE_PREAMBLE_LOW_SF, 0, false, 0, true, false, 0, false, true);
    Radio.SetTxConfig(MODEM_LORA, profile->tx_power_dbm, 0, wrapper_bw_index, profile->spreading_factor,
                      cr_value, D1L_MESHCORE_PREAMBLE_LOW_SF, false, true, false, 0, false,
                      D1L_MESHCORE_TX_TIMEOUT_MS);
    apply_sx1262_lora_params(profile, sx_bw, cr_value);
    return ESP_OK;
}

static esp_err_t ensure_radio_started(void)
{
    uint8_t hash[32] = {0};
    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (md == NULL || mbedtls_md(md, s_public_secret, 16, hash) != 0) {
        s_status.state = D1L_MESHCORE_SERVICE_RADIO_ERROR;
        return ESP_FAIL;
    }
    s_public_channel_hash = hash[0];

    if (!indicator_io_expander || !bsp_sx126x_spi_handle_get()) {
        s_status.state = D1L_MESHCORE_SERVICE_WAITING_FOR_RADIO;
        s_status.radio_ready = false;
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_radio_started) {
        Radio.Init(&s_radio_events);
        s_radio_started = true;
    }
    d1l_radio_profile_t profile = d1l_settings_radio_profile(NULL);
    esp_err_t ret = configure_radio_profile(&profile);
    if (ret != ESP_OK) {
        s_status.radio_ready = false;
        ESP_LOGW(TAG, "unsupported radio profile for MeshCore public RX/TX");
        return ret;
    }
    s_status.radio_ready = true;
    s_status.identity_ready = true;
    s_status.companion_framing_ready = true;
    if (!s_tx_busy) {
        s_status.state = D1L_MESHCORE_SERVICE_READY;
    }
    return ESP_OK;
}

static void d1l_meshcore_start_rx(void)
{
    if (!s_radio_started || !s_status.radio_ready) {
        return;
    }
    d1l_radio_profile_t profile = d1l_settings_radio_profile(NULL);
    if (configure_radio_profile(&profile) != ESP_OK) {
        return;
    }
    if (profile.rx_boost) {
        Radio.RxBoosted(0);
    } else {
        Radio.Rx(0);
    }
    if (!s_tx_busy) {
        s_status.state = D1L_MESHCORE_SERVICE_READY;
    }
}

void d1l_meshcore_service_init(void)
{
    const d1l_settings_t *settings = d1l_settings_current();
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = D1L_MESHCORE_SERVICE_WAITING_FOR_RADIO;
    s_status.path_hash_bytes = settings->path_hash_bytes;
    s_status.identity_ready = false;
    s_status.radio_ready = false;
    s_status.companion_framing_ready = true;
    s_radio_started = false;
    s_tx_busy = false;
}

d1l_meshcore_service_status_t d1l_meshcore_service_status(void)
{
    const d1l_settings_t *settings = d1l_settings_current();
    if (!s_tx_busy) {
        esp_err_t ret = ensure_radio_started();
        if (ret == ESP_OK) {
            d1l_meshcore_start_rx();
        }
    }
    s_status.path_hash_bytes = settings->path_hash_bytes;
    return s_status;
}

esp_err_t d1l_meshcore_service_request_advert(bool flood)
{
    (void)flood;
    s_status.rejected_commands++;
    return ESP_ERR_INVALID_STATE;
}

esp_err_t d1l_meshcore_service_send_public(const char *text)
{
    if (text == NULL || text[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = ensure_radio_started();
    if (ret != ESP_OK) {
        s_status.rejected_commands++;
        return ret;
    }
    if (s_tx_busy) {
        s_status.rejected_commands++;
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t raw[D1L_MESHCORE_MAX_RAW_PACKET];
    uint8_t raw_len = 0;
    const d1l_settings_t *settings = d1l_settings_current();
    ret = build_public_text_packet(text, settings->path_hash_bytes, raw, sizeof(raw), &raw_len);
    if (ret != ESP_OK) {
        s_status.rejected_commands++;
        return ret;
    }

    s_tx_busy = true;
    s_status.state = D1L_MESHCORE_SERVICE_TX_BUSY;
    Radio.Send(raw, raw_len);
    append_packet_log("tx", "public_text", 0, 0, settings->path_hash_bytes, 0, raw_len, text);
    return ESP_OK;
}

const char *d1l_meshcore_service_state_name(d1l_meshcore_service_state_t state)
{
    switch (state) {
    case D1L_MESHCORE_SERVICE_PHASE1_STUB:
        return "phase1_stub";
    case D1L_MESHCORE_SERVICE_WAITING_FOR_RADIO:
        return "waiting_for_radio";
    case D1L_MESHCORE_SERVICE_READY:
        return "ready";
    case D1L_MESHCORE_SERVICE_TX_BUSY:
        return "tx_busy";
    case D1L_MESHCORE_SERVICE_RADIO_ERROR:
        return "radio_error";
    default:
        return "unknown";
    }
}
