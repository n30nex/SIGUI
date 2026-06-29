#include "meshcore_service.h"

#include <stdio.h>
#include <string.h>

#include "app/settings_model.h"
#include "bsp_sx126x.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/aes.h"
#include "mbedtls/md.h"
#include "ed_25519.h"
#include "mesh/meshcore_radio_profile.h"
#include "mesh/message_store.h"
#include "mesh/node_store.h"
#include "mesh/packet_log.h"
#include "radio.h"
#include "sx126x.h"

#define D1L_MESHCORE_ROUTE_TRANSPORT_FLOOD 0x00U
#define D1L_MESHCORE_ROUTE_FLOOD 0x01U
#define D1L_MESHCORE_ROUTE_DIRECT 0x02U
#define D1L_MESHCORE_ROUTE_TRANSPORT_DIRECT 0x03U
#define D1L_MESHCORE_PAYLOAD_ADVERT 0x04U
#define D1L_MESHCORE_PAYLOAD_GROUP_TEXT 0x05U
#define D1L_MESHCORE_HEADER_GROUP_TEXT_FLOOD \
    ((uint8_t)((D1L_MESHCORE_PAYLOAD_GROUP_TEXT << 2) | D1L_MESHCORE_ROUTE_FLOOD))
#define D1L_MESHCORE_PUB_KEY_SIZE 32U
#define D1L_MESHCORE_SIGNATURE_SIZE 64U
#define D1L_MESHCORE_SEED_SIZE 32U
#define D1L_MESHCORE_ADVERT_MIN_PAYLOAD \
    (D1L_MESHCORE_PUB_KEY_SIZE + 4U + D1L_MESHCORE_SIGNATURE_SIZE)
#define D1L_MESHCORE_MAX_ADVERT_DATA 32U
#define D1L_MESHCORE_CIPHER_BLOCK_SIZE 16U
#define D1L_MESHCORE_CIPHER_MAC_SIZE 2U
#define D1L_MESHCORE_MAX_RAW_PACKET 255U
#define D1L_MESHCORE_MAX_TEXT_BYTES 160U
#define D1L_MESHCORE_BW_INDEX_62K5 3U
#define D1L_MESHCORE_PREAMBLE_LOW_SF 32U
#define D1L_MESHCORE_TX_TIMEOUT_MS 5000U
#define D1L_MESHCORE_ADVERT_TYPE_CHAT 0x01U
#define D1L_MESHCORE_ADVERT_NAME_MASK 0x80U

static const char *TAG = "d1l_mesh";
static d1l_meshcore_service_status_t s_status;
static bool s_radio_started;
static volatile bool s_tx_busy;
static bool s_pending_public_tx;
static char s_pending_public_text[D1L_MESSAGE_TEXT_LEN];
extern SX126x_t SX126x;

static const uint8_t s_public_secret[D1L_MESHCORE_PUB_KEY_SIZE] = {
    0x8b, 0x33, 0x87, 0xe9, 0xc5, 0xcd, 0xea, 0x6a,
    0xc9, 0xe5, 0xed, 0xba, 0xa1, 0x15, 0xcd, 0x72,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static uint8_t s_public_channel_hash;

static void d1l_meshcore_start_rx(void);

typedef struct {
    uint8_t header;
    uint8_t route;
    uint8_t type;
    uint8_t path_len;
    uint8_t path_hash_bytes;
    uint8_t path_hops;
    const uint8_t *payload;
    uint16_t payload_len;
} d1l_meshcore_wire_packet_t;

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

static void append_public_message_store_rx(const char *message, int rssi, int snr_quarters,
                                           uint8_t path_hash_bytes, uint8_t path_hops)
{
    char author[D1L_MESSAGE_AUTHOR_LEN] = "Public";
    char body[D1L_MESSAGE_TEXT_LEN] = {0};
    const char *body_src = message;
    const char *colon = message ? strchr(message, ':') : NULL;
    const size_t author_len = colon ? (size_t)(colon - message) : 0;
    if (colon && author_len > 0 && author_len < sizeof(author)) {
        const char *after = colon + 1;
        if (*after == ' ') {
            after++;
        }
        if (*after != '\0') {
            memcpy(author, message, author_len);
            author[author_len] = '\0';
            body_src = after;
        }
    }
    sanitize_note(body, sizeof(body), body_src);
    esp_err_t ret = d1l_message_store_append_public("rx", author, body, rssi,
                                                    (snr_quarters * 10) / 4,
                                                    path_hash_bytes, path_hops, true);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "message store append rx failed: %s", esp_err_to_name(ret));
    }
}

static void append_public_message_store_tx(const char *message)
{
    const d1l_settings_t *settings = d1l_settings_current();
    esp_err_t ret = d1l_message_store_append_public("tx", settings->node_name, message, 0, 0,
                                                    settings->path_hash_bytes, 0, false);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "message store append tx failed: %s", esp_err_to_name(ret));
    }
}

static void remember_pending_public_tx(const char *message)
{
    sanitize_note(s_pending_public_text, sizeof(s_pending_public_text), message);
    s_pending_public_tx = s_pending_public_text[0] != '\0';
}

static void flush_pending_public_tx(void)
{
    if (!s_pending_public_tx) {
        return;
    }
    append_public_message_store_tx(s_pending_public_text);
    s_pending_public_tx = false;
    s_pending_public_text[0] = '\0';
}

static void write_le32(uint8_t *dest, uint32_t value)
{
    dest[0] = (uint8_t)(value & 0xffU);
    dest[1] = (uint8_t)((value >> 8) & 0xffU);
    dest[2] = (uint8_t)((value >> 16) & 0xffU);
    dest[3] = (uint8_t)((value >> 24) & 0xffU);
}

static uint32_t read_le32(const uint8_t *src)
{
    return (uint32_t)src[0] |
           ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) |
           ((uint32_t)src[3] << 24);
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

static bool parse_wire_packet(const uint8_t *raw, uint16_t size, d1l_meshcore_wire_packet_t *out)
{
    if (!raw || !out || size < 3U) {
        return false;
    }

    size_t i = 0;
    memset(out, 0, sizeof(*out));
    out->header = raw[i++];
    out->route = out->header & 0x03U;
    out->type = (out->header >> 2) & 0x0fU;
    if (out->route == D1L_MESHCORE_ROUTE_TRANSPORT_FLOOD ||
        out->route == D1L_MESHCORE_ROUTE_TRANSPORT_DIRECT) {
        if (i + 4U >= size) {
            return false;
        }
        i += 4U;
    }
    out->path_len = raw[i++];
    if (!path_len_valid(out->path_len)) {
        return false;
    }
    const uint8_t path_bytes = path_byte_len(out->path_len);
    if (i + path_bytes >= size) {
        return false;
    }
    i += path_bytes;
    out->path_hash_bytes = path_hash_size(out->path_len);
    out->path_hops = path_hash_count(out->path_len);
    out->payload = &raw[i];
    out->payload_len = (uint16_t)(size - i);
    return true;
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

static esp_err_t build_public_text_packet(const char *text, uint8_t path_hash_bytes,
                                          uint32_t tx_timestamp, uint8_t *raw,
                                          size_t raw_size, uint8_t *out_len)
{
    if (!text || !raw || !out_len || text[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (path_hash_bytes < 1 || path_hash_bytes > 3) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t plain[D1L_MESHCORE_MAX_TEXT_BYTES];
    write_le32(plain, tx_timestamp);
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
    d1l_meshcore_wire_packet_t packet;
    if (!parse_wire_packet(payload, size, &packet) ||
        packet.type != D1L_MESHCORE_PAYLOAD_GROUP_TEXT ||
        packet.payload_len < 3U ||
        packet.payload[0] != s_public_channel_hash) {
        return;
    }

    uint8_t plain[D1L_MESHCORE_MAX_RAW_PACKET + 1U] = {0};
    const size_t plain_len = meshcore_decrypt_after_mac(plain, sizeof(plain) - 1U,
                                                        &packet.payload[1],
                                                        packet.payload_len - 1U);
    if (plain_len < 6U || (plain[4] >> 2) != 0) {
        return;
    }
    plain[plain_len] = '\0';
    const char *message = (const char *)&plain[5];
    s_status.rx_packets++;
    append_packet_log("rx", "public_text", rssi, snr, packet.path_hash_bytes,
                      packet.path_hops, size, message);
    append_public_message_store_rx(message, rssi, snr, packet.path_hash_bytes, packet.path_hops);
}

static char advert_type_code(uint8_t flags)
{
    switch (flags & 0x0fU) {
    case 1:
        return 'C';
    case 2:
        return 'R';
    case 3:
        return 'O';
    case 4:
        return 'S';
    default:
        return 'N';
    }
}

static bool parse_advert_name(const uint8_t *app_data, size_t app_data_len, char *name, size_t name_size)
{
    if (!name || name_size == 0) {
        return false;
    }
    name[0] = '\0';
    if (!app_data || app_data_len == 0) {
        return true;
    }

    const uint8_t flags = app_data[0];
    size_t i = 1;
    if ((flags & 0x10U) != 0) {
        i += 8U;
    }
    if ((flags & 0x20U) != 0) {
        i += 2U;
    }
    if ((flags & 0x40U) != 0) {
        i += 2U;
    }
    if (i > app_data_len) {
        return false;
    }
    if ((flags & 0x80U) == 0 || i == app_data_len) {
        return true;
    }

    char raw_name[D1L_PACKET_LOG_NOTE_LEN] = {0};
    size_t out = 0;
    while (i < app_data_len && out + 1U < sizeof(raw_name)) {
        raw_name[out++] = (char)app_data[i++];
    }
    raw_name[out] = '\0';
    sanitize_note(name, name_size, raw_name);
    return true;
}

static bool verify_advert_signature(const uint8_t *pub_key, const uint8_t *timestamp,
                                    const uint8_t *signature, const uint8_t *app_data,
                                    size_t app_data_len)
{
    if (!pub_key || !timestamp || !signature || app_data_len > D1L_MESHCORE_MAX_ADVERT_DATA) {
        return false;
    }
    uint8_t message[D1L_MESHCORE_PUB_KEY_SIZE + 4U + D1L_MESHCORE_MAX_ADVERT_DATA];
    size_t len = 0;
    memcpy(&message[len], pub_key, D1L_MESHCORE_PUB_KEY_SIZE);
    len += D1L_MESHCORE_PUB_KEY_SIZE;
    memcpy(&message[len], timestamp, 4U);
    len += 4U;
    if (app_data_len > 0) {
        memcpy(&message[len], app_data, app_data_len);
        len += app_data_len;
    }
    return ed25519_verify(signature, message, len, pub_key) == 1;
}

static uint8_t build_advert_app_data(const char *name, uint8_t *app_data, size_t app_data_size)
{
    if (!app_data || app_data_size == 0) {
        return 0;
    }
    app_data[0] = D1L_MESHCORE_ADVERT_TYPE_CHAT;
    uint8_t len = 1;
    if (name && name[0] != '\0') {
        app_data[0] |= D1L_MESHCORE_ADVERT_NAME_MASK;
        while (name[0] && len < app_data_size && len < D1L_MESHCORE_MAX_ADVERT_DATA) {
            unsigned char c = (unsigned char)*name++;
            if (c < 32 || c > 126 || c == '"' || c == '\\') {
                c = '_';
            }
            app_data[len++] = c;
        }
    }
    return len;
}

static esp_err_t build_advert_packet(const d1l_settings_t *settings, bool flood,
                                     uint32_t tx_timestamp,
                                     uint8_t *raw, size_t raw_size, uint8_t *out_len)
{
    if (!settings || !settings->identity_ready || !raw || !out_len || raw_size < 2U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (settings->path_hash_bytes < 1 || settings->path_hash_bytes > 3) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t app_data[D1L_MESHCORE_MAX_ADVERT_DATA] = {0};
    const uint8_t app_data_len =
        build_advert_app_data(settings->node_name, app_data, sizeof(app_data));
    const size_t payload_len = D1L_MESHCORE_ADVERT_MIN_PAYLOAD + app_data_len;
    const size_t raw_len = 2U + payload_len;
    if (raw_len > raw_size || raw_len > UINT8_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t i = 0;
    raw[i++] = (uint8_t)((D1L_MESHCORE_PAYLOAD_ADVERT << 2) |
                         (flood ? D1L_MESHCORE_ROUTE_FLOOD : D1L_MESHCORE_ROUTE_DIRECT));
    raw[i++] = flood ? (uint8_t)((settings->path_hash_bytes - 1U) << 6) : 0;
    memcpy(&raw[i], settings->identity_public_key, D1L_MESHCORE_PUB_KEY_SIZE);
    i += D1L_MESHCORE_PUB_KEY_SIZE;
    write_le32(&raw[i], tx_timestamp);
    uint8_t *timestamp = &raw[i];
    i += 4U;
    uint8_t *signature = &raw[i];
    i += D1L_MESHCORE_SIGNATURE_SIZE;
    memcpy(&raw[i], app_data, app_data_len);
    i += app_data_len;

    uint8_t message[D1L_MESHCORE_PUB_KEY_SIZE + 4U + D1L_MESHCORE_MAX_ADVERT_DATA];
    size_t msg_len = 0;
    memcpy(&message[msg_len], settings->identity_public_key, D1L_MESHCORE_PUB_KEY_SIZE);
    msg_len += D1L_MESHCORE_PUB_KEY_SIZE;
    memcpy(&message[msg_len], timestamp, 4U);
    msg_len += 4U;
    memcpy(&message[msg_len], app_data, app_data_len);
    msg_len += app_data_len;
    ed25519_sign(signature, message, msg_len, settings->identity_public_key,
                 settings->identity_private_key);

    *out_len = (uint8_t)i;
    return ESP_OK;
}

static void parse_rx_advert_packet(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr)
{
    d1l_meshcore_wire_packet_t packet;
    if (!parse_wire_packet(payload, size, &packet) ||
        packet.type != D1L_MESHCORE_PAYLOAD_ADVERT ||
        packet.payload_len < D1L_MESHCORE_ADVERT_MIN_PAYLOAD) {
        return;
    }

    const uint8_t *pub_key = &packet.payload[0];
    const uint8_t *timestamp = &packet.payload[D1L_MESHCORE_PUB_KEY_SIZE];
    const uint8_t *signature = &packet.payload[D1L_MESHCORE_PUB_KEY_SIZE + 4U];
    const uint8_t *app_data = &packet.payload[D1L_MESHCORE_ADVERT_MIN_PAYLOAD];
    size_t app_data_len = packet.payload_len - D1L_MESHCORE_ADVERT_MIN_PAYLOAD;
    if (app_data_len > D1L_MESHCORE_MAX_ADVERT_DATA) {
        app_data_len = D1L_MESHCORE_MAX_ADVERT_DATA;
    }

    char pub_prefix[17] = {0};
    hex_prefix(pub_prefix, sizeof(pub_prefix), pub_key, 8U);
    const bool valid_signature =
        verify_advert_signature(pub_key, timestamp, signature, app_data, app_data_len);
    if (!valid_signature) {
        char note[D1L_PACKET_LOG_NOTE_LEN] = {0};
        snprintf(note, sizeof(note), "bad_sig pub=%s", pub_prefix);
        append_packet_log("rx", "advert_bad_sig", rssi, snr, packet.path_hash_bytes,
                          packet.path_hops, size, note);
        return;
    }

    char name[D1L_PACKET_LOG_NOTE_LEN] = {0};
    bool app_data_valid = parse_advert_name(app_data, app_data_len, name, sizeof(name));
    const char type = app_data_len > 0 ? advert_type_code(app_data[0]) : 'N';
    char note[D1L_PACKET_LOG_NOTE_LEN] = {0};
    if (name[0]) {
        char short_name[13] = {0};
        sanitize_note(short_name, sizeof(short_name), name);
        snprintf(note, sizeof(note), "adv %c %s %.8s", type, short_name, pub_prefix);
    } else {
        snprintf(note, sizeof(note), "adv %c %.8s%s", type, pub_prefix,
                 app_data_valid ? "" : " app_bad");
    }
    s_status.rx_packets++;
    s_status.rx_adverts++;
    esp_err_t ret = d1l_node_store_upsert_advert(pub_prefix, name, type, rssi,
                                                 (snr * 10) / 4,
                                                 packet.path_hash_bytes, packet.path_hops,
                                                 read_le32(timestamp));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "node store upsert failed: %s", esp_err_to_name(ret));
    }
    append_packet_log("rx", "advert", rssi, snr, packet.path_hash_bytes,
                      packet.path_hops, size, note);
}

static void on_tx_done(void)
{
    flush_pending_public_tx();
    s_tx_busy = false;
    s_status.tx_packets++;
    s_status.state = D1L_MESHCORE_SERVICE_READY;
    d1l_meshcore_start_rx();
}

static void on_tx_timeout(void)
{
    s_pending_public_tx = false;
    s_pending_public_text[0] = '\0';
    s_tx_busy = false;
    s_status.state = D1L_MESHCORE_SERVICE_RADIO_ERROR;
    d1l_meshcore_start_rx();
}

static void on_rx_done(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr)
{
    parse_rx_public_packet(payload, size, rssi, snr);
    parse_rx_advert_packet(payload, size, rssi, snr);
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
    s_status.identity_ready = settings->identity_ready;
    s_status.radio_ready = false;
    s_status.companion_framing_ready = true;
    s_radio_started = false;
    s_tx_busy = false;
    s_pending_public_tx = false;
    s_pending_public_text[0] = '\0';
}

esp_err_t d1l_meshcore_service_ensure_identity(void)
{
    d1l_settings_t settings = *d1l_settings_current();
    if (settings.identity_ready) {
        s_status.identity_ready = true;
        return ESP_OK;
    }

    uint8_t seed[D1L_MESHCORE_SEED_SIZE] = {0};
    for (int attempt = 0; attempt < 8; ++attempt) {
        esp_fill_random(seed, sizeof(seed));
        ed25519_create_keypair(settings.identity_public_key, settings.identity_private_key, seed);
        if (settings.identity_public_key[0] != 0x00 && settings.identity_public_key[0] != 0xff) {
            settings.identity_ready = true;
            break;
        }
    }
    memset(seed, 0, sizeof(seed));
    if (!settings.identity_ready) {
        s_status.identity_ready = false;
        return ESP_FAIL;
    }

    esp_err_t ret = d1l_settings_save(&settings);
    s_status.identity_ready = ret == ESP_OK;
    return ret;
}

d1l_meshcore_service_status_t d1l_meshcore_service_status(void)
{
    const d1l_settings_t *settings = d1l_settings_current();
    if (!settings->identity_ready) {
        (void)d1l_meshcore_service_ensure_identity();
    } else {
        s_status.identity_ready = true;
    }
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
    esp_err_t ret = d1l_meshcore_service_ensure_identity();
    if (ret != ESP_OK) {
        s_status.rejected_commands++;
        return ret;
    }
    ret = ensure_radio_started();
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
    uint32_t tx_timestamp = 0;
    ret = d1l_settings_next_mesh_timestamp(&tx_timestamp);
    if (ret != ESP_OK) {
        s_status.rejected_commands++;
        return ret;
    }
    ret = build_advert_packet(settings, flood, tx_timestamp, raw, sizeof(raw), &raw_len);
    if (ret != ESP_OK) {
        s_status.rejected_commands++;
        return ret;
    }

    char pub_prefix[17] = {0};
    hex_prefix(pub_prefix, sizeof(pub_prefix), settings->identity_public_key, 8U);
    char note[D1L_PACKET_LOG_NOTE_LEN] = {0};
    snprintf(note, sizeof(note), "%s %.8s", flood ? "flood" : "zero", pub_prefix);

    s_tx_busy = true;
    s_status.state = D1L_MESHCORE_SERVICE_TX_BUSY;
    Radio.Send(raw, raw_len);
    append_packet_log("tx", "advert", 0, 0, settings->path_hash_bytes, 0, raw_len, note);
    return ESP_OK;
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
    uint32_t tx_timestamp = 0;
    ret = d1l_settings_next_mesh_timestamp(&tx_timestamp);
    if (ret != ESP_OK) {
        s_status.rejected_commands++;
        return ret;
    }
    ret = build_public_text_packet(text, settings->path_hash_bytes, tx_timestamp,
                                   raw, sizeof(raw), &raw_len);
    if (ret != ESP_OK) {
        s_status.rejected_commands++;
        return ret;
    }

    s_tx_busy = true;
    s_status.state = D1L_MESHCORE_SERVICE_TX_BUSY;
    remember_pending_public_tx(text);
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
