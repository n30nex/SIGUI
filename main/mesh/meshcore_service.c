#include "meshcore_service.h"

#include <stdio.h>
#include <string.h>

#include "app/settings_model.h"
#include "bsp_sx126x.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/aes.h"
#include "mbedtls/md.h"
#include "ed_25519.h"
#include "mesh/advert_data.h"
#include "mesh/contact_store.h"
#include "mesh/dm_store.h"
#include "mesh/ed25519_canonical.h"
#include "mesh/meshcore_ack_dispatch.h"
#include "mesh/meshcore_radio_profile.h"
#include "mesh/meshcore_route_selection.h"
#include "mesh/meshcore_trace.h"
#include "mesh/meshcore_wire.h"
#include "mesh/message_store.h"
#include "mesh/node_store.h"
#include "mesh/packet_log.h"
#include "mesh/route_store.h"
#include "mesh/store_lock.h"
#include "radio.h"
#include "sx126x.h"

#define D1L_MESHCORE_PUB_KEY_SIZE 32U
#define D1L_MESHCORE_SIGNATURE_SIZE 64U
#define D1L_MESHCORE_SEED_SIZE 32U
#define D1L_MESHCORE_ADVERT_MIN_PAYLOAD \
    (D1L_MESHCORE_PUB_KEY_SIZE + 4U + D1L_MESHCORE_SIGNATURE_SIZE)
#define D1L_MESHCORE_MAX_ADVERT_DATA D1L_ADVERT_DATA_MAX_LEN
#define D1L_MESHCORE_CIPHER_BLOCK_SIZE 16U
#define D1L_MESHCORE_CIPHER_MAC_SIZE 2U
#define D1L_MESHCORE_MAX_TEXT_BYTES 160U
#define D1L_MESHCORE_USER_TEXT_MAX D1L_MESSAGE_MAX_CHARS
#define D1L_MESHCORE_BW_INDEX_62K5 3U
#define D1L_MESHCORE_PREAMBLE_LOW_SF 32U
#define D1L_MESHCORE_TX_TIMEOUT_MS 5000U
#define D1L_MESHCORE_ADVERT_TYPE_CHAT 0x01U
#define D1L_MESHCORE_ADVERT_NAME_MASK 0x80U
#define D1L_MESHCORE_TXT_TYPE_PLAIN 0U
#define D1L_MESHCORE_PATH_PROBE_COOLDOWN_MS 30000U
#define D1L_MESHCORE_SERVICE_TASK_STACK_BYTES 4096U
#define D1L_MESHCORE_SERVICE_QUEUE_LEN 6U
#define D1L_MESHCORE_SERVICE_COMMAND_TIMEOUT_MS 1500U
_Static_assert(D1L_MESHCORE_USER_TEXT_MAX == 138U,
               "MeshCore user text limit must reject 139+ chars");
_Static_assert(D1L_MESHCORE_USER_TEXT_MAX <= (D1L_MESHCORE_MAX_TEXT_BYTES - 5U),
               "MeshCore plaintext buffer must fit the user text limit");
_Static_assert(D1L_MESHCORE_PUB_KEY_SIZE == D1L_MESHCORE_DM_IDENTITY_SENDER_BYTES,
               "DM identity sender size must match the MeshCore public key");
_Static_assert(D1L_MESHCORE_DM_ACK_DEDUPE_DIGEST_BYTES ==
                   D1L_DM_IDENTITY_DIGEST_BYTES,
               "DM identity digest sizes must match retained storage");
_Static_assert(D1L_MESHCORE_DM_ACK_MAX_DISPATCHES ==
                   D1L_DM_ACK_DISPATCH_MAX,
               "DM ACK dispatch limits must match retained storage");
_Static_assert(D1L_ADVERT_DATA_NAME_LEN == D1L_HEARD_NODE_NAME_LEN,
               "Advert parser and heard-node name bounds must match");

static const char *TAG = "d1l_mesh";
static d1l_meshcore_service_status_t s_status;
static SemaphoreHandle_t s_status_mutex;
static QueueHandle_t s_service_queue;
static TaskHandle_t s_service_task;
static bool s_service_initialized;
static bool s_radio_started;
static volatile bool s_tx_busy;
static volatile bool s_active_tx_ack_response;
static bool s_pending_public_tx;
static char s_pending_public_text[D1L_MESSAGE_TEXT_LEN];
static uint32_t s_last_path_probe_ms;
static char s_last_path_probe_fingerprint[D1L_NODE_FINGERPRINT_LEN];
static bool s_radio_profile_applied;
static d1l_radio_profile_t s_applied_radio_profile;
static d1l_meshcore_trace_tracker_t s_trace_tracker;
static int s_trace_last_rssi_dbm;
static int s_trace_last_radio_snr_quarter_db;
static bool s_trace_last_retention_attempted;
static bool s_trace_last_route_summary_accepted;
static bool s_trace_last_packet_preview_retained;
static d1l_store_lock_t s_trace_lock = D1L_STORE_LOCK_INITIALIZER;

typedef struct {
    bool active;
    char fingerprint[D1L_NODE_FINGERPRINT_LEN];
    char alias[D1L_CONTACT_ALIAS_LEN];
    char text[D1L_MESSAGE_TEXT_LEN];
    uint8_t path_hash_bytes;
    uint8_t path_hops;
    uint8_t attempt;
    uint32_t ack_hash;
} d1l_pending_dm_tx_t;

static d1l_pending_dm_tx_t s_pending_dm_tx;

typedef struct {
    d1l_meshcore_route_selection_t selection;
    char fingerprint[D1L_NODE_FINGERPRINT_LEN];
    char alias[D1L_CONTACT_ALIAS_LEN];
    uint8_t raw_len;
} d1l_dm_send_result_t;

typedef struct {
    bool active;
    uint32_t ack_hash;
    uint32_t row_seq;
    uint8_t identity_digest[D1L_MESHCORE_DM_ACK_DEDUPE_DIGEST_BYTES];
    char fingerprint[D1L_NODE_FINGERPRINT_LEN];
    char alias[D1L_CONTACT_ALIAS_LEN];
    d1l_meshcore_ack_dispatch_kind_t kind;
    uint8_t route;
    uint8_t path_hash_bytes;
    uint8_t path_hops;
    uint8_t raw[D1L_MESHCORE_MAX_RAW_PACKET];
    uint8_t raw_len;
} d1l_pending_ack_tx_t;

static d1l_pending_ack_tx_t s_pending_ack_tx;
static d1l_meshcore_ack_dedupe_t s_ack_dedupe;
static d1l_dm_entry_t s_ack_restore_scan[D1L_DM_STORE_CAPACITY];
static d1l_contact_entry_t s_contact_scan[D1L_CONTACT_STORE_CAPACITY];

typedef struct {
    char fingerprint[D1L_NODE_FINGERPRINT_LEN];
    uint8_t path_len;
    uint8_t path[D1L_MESHCORE_MAX_PATH_BYTES];
    uint32_t learned_at_ms;
    bool valid;
} d1l_boot_route_t;

static d1l_boot_route_t s_boot_routes[D1L_CONTACT_STORE_CAPACITY];
static uint8_t s_boot_route_next;
static d1l_store_lock_t s_boot_route_lock = D1L_STORE_LOCK_INITIALIZER;
extern SX126x_t SX126x;

typedef enum {
    D1L_MESHCORE_SERVICE_CMD_START_RX,
    D1L_MESHCORE_SERVICE_CMD_SEND_RAW,
} d1l_meshcore_service_cmd_type_t;

typedef struct {
    d1l_meshcore_service_cmd_type_t type;
    uint8_t raw[D1L_MESHCORE_MAX_RAW_PACKET];
    uint8_t raw_len;
    uint16_t delay_ms;
    bool ack_response;
    TaskHandle_t reply_task;
} d1l_meshcore_service_cmd_t;

static const uint8_t s_public_secret[D1L_MESHCORE_PUB_KEY_SIZE] = {
    0x8b, 0x33, 0x87, 0xe9, 0xc5, 0xcd, 0xea, 0x6a,
    0xc9, 0xe5, 0xed, 0xba, 0xa1, 0x15, 0xcd, 0x72,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static uint8_t s_public_channel_hash;

static void d1l_meshcore_start_rx(void);
static esp_err_t meshcore_service_start_task(void);
static void meshcore_service_request_rx_async(void);
static esp_err_t meshcore_service_send_ack_async(
    const d1l_contact_entry_t *contact,
    uint32_t ack_hash,
    uint32_t row_seq,
    const uint8_t digest[D1L_MESHCORE_DM_ACK_DEDUPE_DIGEST_BYTES],
    const d1l_meshcore_ack_dispatch_plan_t *plan,
    const uint8_t *raw,
    uint8_t raw_len);

static void status_lock(void)
{
    if (s_status_mutex) {
        (void)xSemaphoreTake(s_status_mutex, portMAX_DELAY);
    }
}

static void status_unlock(void)
{
    if (s_status_mutex) {
        (void)xSemaphoreGive(s_status_mutex);
    }
}

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

static esp_err_t validate_user_text(const char *text)
{
    if (text == NULL || text[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i <= D1L_MESHCORE_USER_TEXT_MAX; ++i) {
        if (text[i] == '\0') {
            return ESP_OK;
        }
    }
    return ESP_ERR_INVALID_SIZE;
}

static uint16_t radio_profile_bandwidth_tenths(const d1l_radio_profile_t *profile)
{
    return (uint16_t)((profile->bandwidth_khz * 10.0f) + 0.5f);
}

static bool radio_profile_strings_match(const char *lhs, const char *rhs)
{
    if (!lhs || !rhs) {
        return lhs == rhs;
    }
    return strcmp(lhs, rhs) == 0;
}

static bool radio_profiles_match(const d1l_radio_profile_t *lhs,
                                 const d1l_radio_profile_t *rhs)
{
    return lhs && rhs &&
           lhs->frequency_hz == rhs->frequency_hz &&
           radio_profile_bandwidth_tenths(lhs) == radio_profile_bandwidth_tenths(rhs) &&
           lhs->spreading_factor == rhs->spreading_factor &&
           lhs->coding_rate == rhs->coding_rate &&
           lhs->tx_power_dbm == rhs->tx_power_dbm &&
           radio_profile_strings_match(lhs->tcxo, rhs->tcxo) &&
           lhs->rx_boost == rhs->rx_boost;
}

static void mark_radio_apply_result(const d1l_radio_profile_t *profile, esp_err_t ret)
{
    status_lock();
    s_status.radio_apply_error = ret;
    if (ret == ESP_OK && profile) {
        s_applied_radio_profile = *profile;
        s_radio_profile_applied = true;
        s_status.radio_applied = true;
        s_status.radio_apply_pending = false;
        status_unlock();
        return;
    }
    s_status.radio_applied = false;
    s_status.radio_apply_pending = true;
    status_unlock();
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

static bool hex_to_bytes(uint8_t *dest, size_t dest_len, const char *src_hex)
{
    if (!dest || !src_hex) {
        return false;
    }
    for (size_t i = 0; i < dest_len; ++i) {
        int hi = hex_nibble(src_hex[i * 2U]);
        int lo = hex_nibble(src_hex[i * 2U + 1U]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        dest[i] = (uint8_t)((hi << 4) | lo);
    }
    return src_hex[dest_len * 2U] == '\0';
}

static bool append_packet_log(const char *direction, const char *kind, int rssi, int snr_quarters,
                              uint8_t path_hash_bytes, uint8_t path_hops, uint16_t payload_len,
                              const uint8_t *raw, size_t raw_len, const char *note)
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
    return d1l_packet_log_append_raw(&entry, raw, raw_len);
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

static bool append_dm_store_tx(const d1l_pending_dm_tx_t *pending)
{
    if (!pending || !pending->active) {
        return true;
    }
    esp_err_t ret = d1l_dm_store_append(pending->fingerprint, pending->alias, "tx",
                                        pending->text, 0, 0, pending->path_hash_bytes,
                                        pending->path_hops, pending->attempt, false, false,
                                        pending->ack_hash);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "DM tx store append failed: %s", esp_err_to_name(ret));
        return false;
    }
    return true;
}

static void clear_pending_dm_tx(void)
{
    memset(&s_pending_dm_tx, 0, sizeof(s_pending_dm_tx));
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

static void flush_pending_tx(void)
{
    flush_pending_public_tx();
    (void)append_dm_store_tx(&s_pending_dm_tx);
    clear_pending_dm_tx();
}

static void remember_pending_dm_tx(const d1l_contact_entry_t *contact, const char *text,
                                   uint8_t path_hash_bytes, uint8_t path_hops, uint8_t attempt,
                                   uint32_t ack_hash)
{
    memset(&s_pending_dm_tx, 0, sizeof(s_pending_dm_tx));
    if (!contact || !text || text[0] == '\0') {
        return;
    }
    s_pending_dm_tx.active = true;
    sanitize_note(s_pending_dm_tx.fingerprint, sizeof(s_pending_dm_tx.fingerprint),
                  contact->fingerprint);
    sanitize_note(s_pending_dm_tx.alias, sizeof(s_pending_dm_tx.alias),
                  contact->alias[0] ? contact->alias : contact->fingerprint);
    sanitize_note(s_pending_dm_tx.text, sizeof(s_pending_dm_tx.text), text);
    s_pending_dm_tx.path_hash_bytes = path_hash_bytes;
    s_pending_dm_tx.path_hops = path_hops;
    s_pending_dm_tx.attempt = attempt;
    s_pending_dm_tx.ack_hash = ack_hash;
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

static const char *route_name(uint8_t route)
{
    switch (route) {
    case D1L_MESHCORE_ROUTE_TRANSPORT_FLOOD:
        return "transport_flood";
    case D1L_MESHCORE_ROUTE_FLOOD:
        return "flood";
    case D1L_MESHCORE_ROUTE_DIRECT:
        return "direct";
    case D1L_MESHCORE_ROUTE_TRANSPORT_DIRECT:
        return "transport_direct";
    default:
        return "unknown";
    }
}

static void clear_boot_routes(void)
{
    d1l_store_lock_take(&s_boot_route_lock);
    memset(s_boot_routes, 0, sizeof(s_boot_routes));
    s_boot_route_next = 0U;
    d1l_store_lock_give(&s_boot_route_lock);
}

static void remember_boot_route(const char *fingerprint,
                                const uint8_t *path,
                                uint8_t path_len,
                                uint32_t learned_at_ms)
{
    if (!fingerprint || fingerprint[0] == '\0' ||
        !d1l_meshcore_wire_path_len_valid(path_len)) {
        return;
    }
    const uint8_t path_bytes = d1l_meshcore_wire_path_byte_len(path_len);
    if (path_bytes > 0U && !path) {
        return;
    }

    d1l_store_lock_take(&s_boot_route_lock);
    size_t slot = D1L_CONTACT_STORE_CAPACITY;
    size_t empty = D1L_CONTACT_STORE_CAPACITY;
    for (size_t i = 0U; i < D1L_CONTACT_STORE_CAPACITY; ++i) {
        if (s_boot_routes[i].valid &&
            strncmp(s_boot_routes[i].fingerprint, fingerprint,
                    sizeof(s_boot_routes[i].fingerprint)) == 0) {
            slot = i;
            break;
        }
        if (!s_boot_routes[i].valid && empty == D1L_CONTACT_STORE_CAPACITY) {
            empty = i;
        }
    }
    if (slot == D1L_CONTACT_STORE_CAPACITY) {
        slot = empty < D1L_CONTACT_STORE_CAPACITY ? empty : s_boot_route_next;
        s_boot_route_next = (uint8_t)((slot + 1U) % D1L_CONTACT_STORE_CAPACITY);
    }
    d1l_boot_route_t *record = &s_boot_routes[slot];
    memset(record, 0, sizeof(*record));
    snprintf(record->fingerprint, sizeof(record->fingerprint), "%s",
             fingerprint);
    record->path_len = path_len;
    if (path_bytes > 0U) {
        memcpy(record->path, path, path_bytes);
    }
    record->learned_at_ms = learned_at_ms;
    record->valid = true;
    d1l_store_lock_give(&s_boot_route_lock);
}

static bool lookup_boot_route(const char *fingerprint,
                              const uint8_t *path,
                              uint8_t path_len,
                              uint32_t *out_learned_at_ms)
{
    if (!fingerprint || fingerprint[0] == '\0' || !out_learned_at_ms ||
        !d1l_meshcore_wire_path_len_valid(path_len)) {
        return false;
    }
    const uint8_t path_bytes = d1l_meshcore_wire_path_byte_len(path_len);
    if (path_bytes > 0U && !path) {
        return false;
    }

    bool found = false;
    d1l_store_lock_take(&s_boot_route_lock);
    for (size_t i = 0U; i < D1L_CONTACT_STORE_CAPACITY; ++i) {
        const d1l_boot_route_t *record = &s_boot_routes[i];
        if (record->valid && record->path_len == path_len &&
            strncmp(record->fingerprint, fingerprint,
                    sizeof(record->fingerprint)) == 0 &&
            (path_bytes == 0U ||
             memcmp(record->path, path, path_bytes) == 0)) {
            *out_learned_at_ms = record->learned_at_ms;
            found = true;
            break;
        }
    }
    d1l_store_lock_give(&s_boot_route_lock);
    return found;
}

static void record_dm_route_selection(
    const d1l_meshcore_route_selection_t *selection)
{
    if (!selection) {
        return;
    }
    if (selection->route == D1L_MESHCORE_ROUTE_DIRECT) {
        s_status.dm_route_direct_selected++;
    } else {
        s_status.dm_route_flood_selected++;
    }
    switch (selection->reason) {
    case D1L_MESHCORE_ROUTE_SELECTION_FLOOD_NO_PATH:
        s_status.dm_route_missing_fallback++;
        break;
    case D1L_MESHCORE_ROUTE_SELECTION_FLOOD_PREBOOT_PATH:
        s_status.dm_route_preboot_fallback++;
        break;
    case D1L_MESHCORE_ROUTE_SELECTION_FLOOD_STALE_PATH:
        s_status.dm_route_stale_fallback++;
        break;
    case D1L_MESHCORE_ROUTE_SELECTION_FLOOD_MALFORMED_PATH:
        s_status.dm_route_malformed_fallback++;
        break;
    default:
        break;
    }
    s_status.dm_route_last_reason = (uint8_t)selection->reason;
    s_status.dm_route_last_path_age_ms = selection->path_age_ms;
}

static void clear_pending_ack_tx(void)
{
    memset(&s_pending_ack_tx, 0, sizeof(s_pending_ack_tx));
}

static bool pending_ack_identity_matches(
    const uint8_t digest[D1L_MESHCORE_DM_ACK_DEDUPE_DIGEST_BYTES])
{
    return s_pending_ack_tx.active && digest &&
           memcmp(s_pending_ack_tx.identity_digest, digest,
                  D1L_MESHCORE_DM_ACK_DEDUPE_DIGEST_BYTES) == 0;
}

static void record_dm_ack_failure(uint32_t ack_hash, esp_err_t error)
{
    s_status.ack_tx_failed++;
    s_status.ack_tx_last_hash = ack_hash;
    s_status.ack_tx_last_error = error;
}

static bool remember_ack_identity_state(const d1l_dm_entry_t *entry,
                                        bool durable)
{
    if (!entry || !entry->identity_digest_valid ||
        entry->ack_dispatch_count > D1L_MESHCORE_DM_ACK_MAX_DISPATCHES) {
        return false;
    }
    if (!d1l_meshcore_ack_dedupe_contains(&s_ack_dedupe,
                                          entry->identity_digest)) {
        return d1l_meshcore_ack_dedupe_remember_state(
            &s_ack_dedupe, entry->identity_digest, durable,
            entry->ack_dispatch_count);
    }
    return d1l_meshcore_ack_dedupe_set_dispatch_count(
               &s_ack_dedupe, entry->identity_digest,
               entry->ack_dispatch_count,
               D1L_MESHCORE_DM_ACK_MAX_DISPATCHES) &&
           d1l_meshcore_ack_dedupe_mark_durable(
               &s_ack_dedupe, entry->identity_digest, durable);
}

static void restore_ack_dedupe_from_store(void)
{
    memset(&s_ack_dedupe, 0, sizeof(s_ack_dedupe));
    const d1l_dm_store_stats_t stats = d1l_dm_store_stats();
    const size_t copied = d1l_dm_store_copy_recent(
        s_ack_restore_scan, D1L_DM_STORE_CAPACITY);
    const bool durable = stats.loaded && !stats.persistence_dirty;
    for (size_t i = 0U; i < copied; ++i) {
        if (s_ack_restore_scan[i].identity_digest_valid) {
            (void)remember_ack_identity_state(&s_ack_restore_scan[i], durable);
        }
    }
}

static void remember_pending_ack_tx(const d1l_contact_entry_t *contact,
                                    uint32_t ack_hash,
                                    uint32_t row_seq,
                                    const uint8_t digest[
                                        D1L_MESHCORE_DM_ACK_DEDUPE_DIGEST_BYTES],
                                    const d1l_meshcore_ack_dispatch_plan_t *plan,
                                    const uint8_t *raw,
                                    uint8_t raw_len)
{
    clear_pending_ack_tx();
    if (!contact || row_seq == 0U || !digest || !plan || !raw ||
        raw_len == 0U) {
        return;
    }

    s_pending_ack_tx.active = true;
    s_pending_ack_tx.ack_hash = ack_hash;
    s_pending_ack_tx.row_seq = row_seq;
    memcpy(s_pending_ack_tx.identity_digest, digest,
           sizeof(s_pending_ack_tx.identity_digest));
    sanitize_note(s_pending_ack_tx.fingerprint, sizeof(s_pending_ack_tx.fingerprint),
                  contact->fingerprint);
    sanitize_note(s_pending_ack_tx.alias, sizeof(s_pending_ack_tx.alias),
                  contact->alias[0] ? contact->alias : contact->fingerprint);
    s_pending_ack_tx.kind = plan->kind;
    s_pending_ack_tx.route = plan->route;
    s_pending_ack_tx.path_hash_bytes = d1l_meshcore_wire_path_hash_size(plan->path_len);
    s_pending_ack_tx.path_hops = d1l_meshcore_wire_path_hash_count(plan->path_len);
    memcpy(s_pending_ack_tx.raw, raw, raw_len);
    s_pending_ack_tx.raw_len = raw_len;

    s_status.ack_tx_last_hash = ack_hash;
    s_status.ack_tx_last_error = ESP_OK;
}

static void complete_pending_ack_tx(bool sent, esp_err_t error)
{
    if (!s_pending_ack_tx.active) {
        return;
    }

    const esp_err_t persist_ret = d1l_dm_store_complete_ack_dispatch(
        s_pending_ack_tx.row_seq, s_pending_ack_tx.identity_digest,
        sent, sent ? ESP_OK : error);
    (void)d1l_meshcore_ack_dedupe_mark_durable(
        &s_ack_dedupe, s_pending_ack_tx.identity_digest,
        persist_ret == ESP_OK);

    char note[D1L_PACKET_LOG_NOTE_LEN] = {0};
    snprintf(note, sizeof(note), "%s %lu %.12s",
             d1l_meshcore_ack_dispatch_kind_name(s_pending_ack_tx.kind),
             (unsigned long)s_pending_ack_tx.ack_hash,
             s_pending_ack_tx.alias);
    if (sent) {
        s_status.ack_tx_done++;
        s_status.ack_tx_last_error = persist_ret;
        esp_err_t route_ret = d1l_route_store_upsert_observation(
            s_pending_ack_tx.fingerprint, s_pending_ack_tx.alias,
            s_pending_ack_tx.kind == D1L_MESHCORE_ACK_DISPATCH_FLOOD_ACK_PATH ?
                "dm_ack_path" : "dm_ack",
            route_name(s_pending_ack_tx.route), "tx", 0, 0,
            s_pending_ack_tx.path_hash_bytes, s_pending_ack_tx.path_hops,
            s_pending_ack_tx.raw_len);
        if (route_ret != ESP_OK) {
            ESP_LOGW(TAG, "route store DM ACK tx failed: %s", esp_err_to_name(route_ret));
        }
        append_packet_log(
            "tx",
            s_pending_ack_tx.kind == D1L_MESHCORE_ACK_DISPATCH_FLOOD_ACK_PATH ?
                "dm_ack_path" : "dm_ack",
            0, 0, s_pending_ack_tx.path_hash_bytes, s_pending_ack_tx.path_hops,
            s_pending_ack_tx.raw_len, s_pending_ack_tx.raw,
            s_pending_ack_tx.raw_len, note);
    } else {
        s_status.ack_tx_failed++;
        s_status.ack_tx_last_error = persist_ret == ESP_OK ? error : persist_ret;
        append_packet_log("tx_fail", "dm_ack_failed", 0, 0,
                          s_pending_ack_tx.path_hash_bytes,
                          s_pending_ack_tx.path_hops,
                          s_pending_ack_tx.raw_len,
                          s_pending_ack_tx.raw,
                          s_pending_ack_tx.raw_len, note);
    }
    if (persist_ret != ESP_OK) {
        s_status.ack_tx_failed++;
        s_status.ack_tx_last_error = persist_ret;
        ESP_LOGW(TAG, "DM ACK completion persistence failed: %s",
                 esp_err_to_name(persist_ret));
    }
    clear_pending_ack_tx();
}

static void complete_unqueued_ack_reservation(
    uint32_t row_seq,
    const uint8_t digest[D1L_MESHCORE_DM_ACK_DEDUPE_DIGEST_BYTES],
    esp_err_t error)
{
    const esp_err_t persist_ret = d1l_dm_store_complete_ack_dispatch(
        row_seq, digest, false, error);
    (void)d1l_meshcore_ack_dedupe_mark_durable(
        &s_ack_dedupe, digest, persist_ret == ESP_OK);
    if (persist_ret != ESP_OK) {
        s_status.ack_tx_failed++;
        s_status.ack_tx_last_error = persist_ret;
        ESP_LOGW(TAG, "unqueued DM ACK state persistence failed: %s",
                 esp_err_to_name(persist_ret));
    }
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

static esp_err_t meshcore_encrypt_then_mac(const uint8_t *secret, uint8_t *dest, size_t dest_size,
                                           const uint8_t *src, size_t src_len, size_t *out_len)
{
    if (!secret || !dest || !src || !out_len || src_len == 0) {
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
    int ret = mbedtls_aes_setkey_enc(&aes, secret, 128);
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
    if (md == NULL) {
        return ESP_FAIL;
    }
    uint8_t hmac[32];
    ret = mbedtls_md_hmac(md, secret, D1L_MESHCORE_PUB_KEY_SIZE, ciphertext, enc_len, hmac);
    if (ret != 0) {
        return ESP_FAIL;
    }
    memcpy(dest, hmac, D1L_MESHCORE_CIPHER_MAC_SIZE);
    *out_len = D1L_MESHCORE_CIPHER_MAC_SIZE + enc_len;
    return ESP_OK;
}

static size_t meshcore_decrypt_after_mac(const uint8_t *secret, uint8_t *dest, size_t dest_size,
                                         const uint8_t *src, size_t src_len)
{
    if (!secret || !dest || !src || src_len <= D1L_MESHCORE_CIPHER_MAC_SIZE) {
        return 0;
    }
    const size_t enc_len = src_len - D1L_MESHCORE_CIPHER_MAC_SIZE;
    if ((enc_len % D1L_MESHCORE_CIPHER_BLOCK_SIZE) != 0 || enc_len > dest_size) {
        return 0;
    }

    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (md == NULL) {
        return 0;
    }
    uint8_t hmac[32];
    int ret = mbedtls_md_hmac(md, secret, D1L_MESHCORE_PUB_KEY_SIZE,
                              src + D1L_MESHCORE_CIPHER_MAC_SIZE, enc_len, hmac);
    if (ret != 0 || memcmp(hmac, src, D1L_MESHCORE_CIPHER_MAC_SIZE) != 0) {
        return 0;
    }

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    ret = mbedtls_aes_setkey_dec(&aes, secret, 128);
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
    if (!raw || !out_len) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t text_ret = validate_user_text(text);
    if (text_ret != ESP_OK) {
        return text_ret;
    }
    if (path_hash_bytes < 1 || path_hash_bytes > 3) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t plain[D1L_MESHCORE_MAX_TEXT_BYTES] = {0};
    write_le32(plain, tx_timestamp);
    plain[4] = 0;
    const size_t message_len = strlen(text);
    memcpy(&plain[5], text, message_len);
    const size_t plain_len = 5U + message_len;

    size_t i = 0;
    if (!d1l_meshcore_wire_write_prefix(
            D1L_MESHCORE_HEADER_GROUP_TEXT_FLOOD, 0U, 0U,
            (uint8_t)((path_hash_bytes - 1U) << 6), NULL,
            raw, raw_size, &i) || i >= raw_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    raw[i++] = s_public_channel_hash;

    size_t mac_cipher_len = 0;
    esp_err_t ret = meshcore_encrypt_then_mac(s_public_secret, &raw[i], raw_size - i,
                                              plain, plain_len, &mac_cipher_len);
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

static esp_err_t calc_dm_identity_digest(
    uint8_t out_digest[D1L_MESHCORE_DM_ACK_DEDUPE_DIGEST_BYTES],
    const uint8_t *plain,
    size_t plain_len,
    size_t message_len,
    const uint8_t sender_pub[D1L_MESHCORE_PUB_KEY_SIZE])
{
    if (!out_digest || !plain || !sender_pub) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t material[D1L_MESHCORE_DM_IDENTITY_PREFIX_BYTES +
                     D1L_MESHCORE_MAX_TEXT_BYTES] = {0};
    size_t material_len = 0U;
    if (!d1l_meshcore_dm_identity_material(
            sender_pub, plain, plain_len, message_len,
            material, sizeof(material), &material_len)) {
        return ESP_ERR_INVALID_SIZE;
    }
    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (md == NULL || mbedtls_md(md, material, material_len, out_digest) != 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t calc_dm_ack_hash(uint32_t *out_hash, const uint8_t *plain, size_t plain_len,
                                  const uint8_t *sender_pub_key)
{
    if (!out_hash || !plain || !sender_pub_key) {
        return ESP_ERR_INVALID_ARG;
    }
    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (md == NULL) {
        return ESP_FAIL;
    }
    uint8_t hash[32] = {0};
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    int ret = mbedtls_md_setup(&ctx, md, 0);
    if (ret == 0) {
        ret = mbedtls_md_starts(&ctx);
    }
    if (ret == 0) {
        ret = mbedtls_md_update(&ctx, plain, plain_len);
    }
    if (ret == 0) {
        ret = mbedtls_md_update(&ctx, sender_pub_key, D1L_MESHCORE_PUB_KEY_SIZE);
    }
    if (ret == 0) {
        ret = mbedtls_md_finish(&ctx, hash);
    }
    mbedtls_md_free(&ctx);
    if (ret != 0) {
        return ESP_FAIL;
    }
    memcpy(out_hash, hash, sizeof(*out_hash));
    return ESP_OK;
}

static esp_err_t build_dm_text_packet(const d1l_settings_t *settings,
                                      const d1l_contact_entry_t *contact,
                                      const char *text,
                                      const d1l_meshcore_route_selection_t *selection,
                                      uint32_t tx_timestamp, uint8_t *raw,
                                      size_t raw_size, uint8_t *out_len,
                                      uint32_t *out_ack_hash)
{
    if (!settings || !settings->identity_ready || !contact || !selection ||
        !raw || !out_len || !out_ack_hash) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t text_ret = validate_user_text(text);
    if (text_ret != ESP_OK) {
        return text_ret;
    }
    if (contact->public_key_hex[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    const bool use_direct = selection->route == D1L_MESHCORE_ROUTE_DIRECT;
    const bool use_flood = selection->route == D1L_MESHCORE_ROUTE_FLOOD;
    if ((!use_direct && !use_flood) ||
        !d1l_meshcore_wire_path_len_valid(selection->path_len) ||
        selection->path_byte_len !=
            d1l_meshcore_wire_path_byte_len(selection->path_len) ||
        selection->path_hash_bytes !=
            d1l_meshcore_wire_path_hash_size(selection->path_len) ||
        selection->path_hops !=
            d1l_meshcore_wire_path_hash_count(selection->path_len) ||
        (use_flood && (selection->path_byte_len != 0U ||
                       selection->path_hops != 0U))) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t dest_pub[D1L_MESHCORE_PUB_KEY_SIZE] = {0};
    if (!hex_to_bytes(dest_pub, sizeof(dest_pub), contact->public_key_hex)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t secret[D1L_MESHCORE_PUB_KEY_SIZE] = {0};
    ed25519_key_exchange(secret, dest_pub, settings->identity_private_key);

    uint8_t plain[D1L_MESHCORE_MAX_TEXT_BYTES] = {0};
    write_le32(plain, tx_timestamp);
    const uint8_t attempt = 0;
    plain[4] = (uint8_t)((D1L_MESHCORE_TXT_TYPE_PLAIN << 2) | attempt);
    const size_t message_len = strlen(text);
    memcpy(&plain[5], text, message_len);
    const size_t plain_len = 5U + message_len;

    esp_err_t ret = calc_dm_ack_hash(out_ack_hash, plain, plain_len,
                                     settings->identity_public_key);
    if (ret != ESP_OK) {
        return ret;
    }

    size_t i = 0;
    if (!d1l_meshcore_wire_write_prefix(
            use_direct ? D1L_MESHCORE_HEADER_DM_TEXT_DIRECT :
                         D1L_MESHCORE_HEADER_DM_TEXT_FLOOD,
            0U, 0U,
            selection->path_len,
            selection->path_byte_len > 0U ? selection->path : NULL,
            raw, raw_size, &i) || raw_size - i < 2U) {
        return ESP_ERR_INVALID_SIZE;
    }
    raw[i++] = dest_pub[0];
    raw[i++] = settings->identity_public_key[0];

    size_t mac_cipher_len = 0;
    ret = meshcore_encrypt_then_mac(secret, &raw[i], raw_size - i,
                                    plain, plain_len, &mac_cipher_len);
    memset(secret, 0, sizeof(secret));
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

static esp_err_t build_dm_ack_response(
    const d1l_settings_t *settings,
    const uint8_t sender_pub[D1L_MESHCORE_PUB_KEY_SIZE],
    const uint8_t secret[D1L_MESHCORE_PUB_KEY_SIZE],
    const d1l_meshcore_ack_dispatch_plan_t *plan,
    uint8_t *raw,
    size_t raw_size,
    uint8_t *out_len)
{
    if (!settings || !settings->identity_ready || !sender_pub || !secret ||
        !plan || !raw || !out_len) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t index = 0U;
    if (plan->kind == D1L_MESHCORE_ACK_DISPATCH_FLOOD_ACK_PATH) {
        const uint8_t header = (uint8_t)(
            (D1L_MESHCORE_PAYLOAD_PATH << 2U) | D1L_MESHCORE_ROUTE_FLOOD);
        if (!d1l_meshcore_wire_write_prefix(
                header, 0U, 0U, plan->path_len, NULL,
                raw, raw_size, &index) || raw_size - index < 2U) {
            return ESP_ERR_INVALID_SIZE;
        }
        raw[index++] = sender_pub[0];
        raw[index++] = settings->identity_public_key[0];

        uint8_t plain[1U + D1L_MESHCORE_MAX_PATH_BYTES + 1U +
                      D1L_MESHCORE_DM_ACK_WIRE_BYTES] = {0};
        size_t plain_len = 0U;
        plain[plain_len++] = plan->return_path_len;
        if (plan->return_path_byte_len > 0U) {
            memcpy(&plain[plain_len], plan->return_path,
                   plan->return_path_byte_len);
            plain_len += plan->return_path_byte_len;
        }
        plain[plain_len++] = D1L_MESHCORE_PAYLOAD_ACK;
        memcpy(&plain[plain_len], plan->ack, sizeof(plan->ack));
        plain_len += sizeof(plan->ack);

        size_t cipher_len = 0U;
        esp_err_t ret = meshcore_encrypt_then_mac(
            secret, &raw[index], raw_size - index,
            plain, plain_len, &cipher_len);
        if (ret != ESP_OK) {
            return ret;
        }
        index += cipher_len;
    } else if (plan->kind == D1L_MESHCORE_ACK_DISPATCH_DIRECT_ACK ||
               plan->kind == D1L_MESHCORE_ACK_DISPATCH_FLOOD_ACK) {
        const uint8_t header = (uint8_t)(
            (D1L_MESHCORE_PAYLOAD_ACK << 2U) | plan->route);
        const uint8_t *path = plan->path_byte_len > 0U ? plan->path : NULL;
        if (!d1l_meshcore_wire_write_prefix(
                header, 0U, 0U, plan->path_len, path,
                raw, raw_size, &index) ||
            raw_size - index < D1L_MESHCORE_DM_ACK_WIRE_BYTES) {
            return ESP_ERR_INVALID_SIZE;
        }
        memcpy(&raw[index], plan->ack, sizeof(plan->ack));
        index += sizeof(plan->ack);
    } else {
        return ESP_ERR_INVALID_ARG;
    }

    if (index > UINT8_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    *out_len = (uint8_t)index;
    return ESP_OK;
}

static bool dispatch_bounded_dm_ack(
    const d1l_contact_entry_t *contact,
    uint32_t ack_hash,
    const d1l_meshcore_ack_dispatch_plan_t *plan,
    const uint8_t *raw,
    uint8_t raw_len,
    const uint8_t digest[D1L_MESHCORE_DM_ACK_DEDUPE_DIGEST_BYTES])
{
    d1l_dm_entry_t retained = {0};
    const d1l_dm_store_stats_t dm_stats = d1l_dm_store_stats();
    if (!d1l_dm_store_find_rx_identity(digest, &retained) ||
        !remember_ack_identity_state(
            &retained, dm_stats.loaded && !dm_stats.persistence_dirty)) {
        record_dm_ack_failure(ack_hash, ESP_ERR_NOT_FOUND);
        return false;
    }
    if (!d1l_meshcore_ack_dedupe_is_durable(&s_ack_dedupe, digest)) {
        const esp_err_t flush_ret = d1l_dm_store_flush();
        if (flush_ret != ESP_OK) {
            record_dm_ack_failure(ack_hash, flush_ret);
            return false;
        }
        (void)d1l_meshcore_ack_dedupe_mark_durable(
            &s_ack_dedupe, digest, true);
        if (!d1l_dm_store_find_rx_identity(digest, &retained) ||
            !remember_ack_identity_state(&retained, true)) {
            record_dm_ack_failure(ack_hash, ESP_ERR_NOT_FOUND);
            return false;
        }
    }

    d1l_dm_ack_reservation_t reservation = {0};
    if (retained.ack_state == D1L_DM_ACK_STATE_PENDING) {
        if (retained.ack_dispatch_count == 0U ||
            pending_ack_identity_matches(digest)) {
            return false;
        }
        if (retained.ack_dispatch_kind != (uint8_t)plan->kind) {
            const esp_err_t rebind_ret =
                d1l_dm_store_rebind_pending_ack_dispatch(
                    retained.seq, digest, (uint8_t)plan->kind);
            (void)d1l_meshcore_ack_dedupe_mark_durable(
                &s_ack_dedupe, digest, rebind_ret == ESP_OK);
            if (rebind_ret != ESP_OK) {
                record_dm_ack_failure(ack_hash, rebind_ret);
                return false;
            }
            retained.ack_dispatch_kind = (uint8_t)plan->kind;
        }
        reservation.reserved = true;
        reservation.durable = true;
        reservation.row_seq = retained.seq;
        reservation.dispatch_count = retained.ack_dispatch_count;
        reservation.error = ESP_OK;
    } else {
        if (!d1l_meshcore_ack_dedupe_dispatch_allowed(
                &s_ack_dedupe, digest,
                D1L_MESHCORE_DM_ACK_MAX_DISPATCHES)) {
            return false;
        }
        const esp_err_t reserve_ret = d1l_dm_store_reserve_ack_dispatch(
            digest, (uint8_t)plan->kind, &reservation);
        if (reserve_ret != ESP_OK) {
            if (reservation.reserved) {
                (void)d1l_meshcore_ack_dedupe_set_dispatch_count(
                    &s_ack_dedupe, digest, reservation.dispatch_count,
                    D1L_MESHCORE_DM_ACK_MAX_DISPATCHES);
                (void)d1l_meshcore_ack_dedupe_mark_durable(
                    &s_ack_dedupe, digest, false);
            }
            if (reservation.reserved || reserve_ret != ESP_ERR_INVALID_STATE) {
                record_dm_ack_failure(ack_hash, reserve_ret);
            }
            return false;
        }
    }
    if (!d1l_meshcore_ack_dedupe_set_dispatch_count(
            &s_ack_dedupe, digest, reservation.dispatch_count,
            D1L_MESHCORE_DM_ACK_MAX_DISPATCHES) ||
        !d1l_meshcore_ack_dedupe_mark_durable(
            &s_ack_dedupe, digest, reservation.durable)) {
        record_dm_ack_failure(ack_hash, ESP_ERR_INVALID_STATE);
        return false;
    }

    const esp_err_t dispatch_ret = meshcore_service_send_ack_async(
        contact, ack_hash, reservation.row_seq, digest, plan, raw, raw_len);
    if (dispatch_ret != ESP_OK) {
        ESP_LOGW(TAG, "DM ACK dispatch failed: %s", esp_err_to_name(dispatch_ret));
        return false;
    }
    return true;
}

static void parse_rx_public_packet(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr)
{
    d1l_meshcore_wire_packet_t packet;
    if (!d1l_meshcore_wire_decode_v1(payload, size, &packet) ||
        packet.type != D1L_MESHCORE_PAYLOAD_GROUP_TEXT ||
        packet.payload_len < 3U ||
        packet.payload[0] != s_public_channel_hash) {
        return;
    }

    uint8_t plain[D1L_MESHCORE_MAX_RAW_PACKET + 1U] = {0};
    const size_t plain_len = meshcore_decrypt_after_mac(s_public_secret, plain, sizeof(plain) - 1U,
                                                        &packet.payload[1],
                                                        packet.payload_len - 1U);
    if (plain_len < 6U || (plain[4] >> 2) != 0) {
        return;
    }
    plain[plain_len] = '\0';
    const char *message = (const char *)&plain[5];
    s_status.rx_packets++;
    esp_err_t route_ret = d1l_route_store_upsert_observation("public", "Public", "public_text",
                                                             route_name(packet.route), "rx", rssi,
                                                             (snr * 10) / 4,
                                                             packet.path_hash_bytes,
                                                             packet.path_hops, size);
    if (route_ret != ESP_OK) {
        ESP_LOGW(TAG, "route store public rx failed: %s", esp_err_to_name(route_ret));
    }
    append_packet_log("rx", "public_text", rssi, snr, packet.path_hash_bytes,
                      packet.path_hops, size, payload, size, message);
    append_public_message_store_rx(message, rssi, snr, packet.path_hash_bytes, packet.path_hops);
}

static bool parse_rx_dm_packet(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr)
{
    d1l_meshcore_wire_packet_t packet;
    if (!d1l_meshcore_wire_decode_v1(payload, size, &packet) ||
        packet.type != D1L_MESHCORE_PAYLOAD_TEXT ||
        packet.payload_len <= (2U + D1L_MESHCORE_CIPHER_MAC_SIZE)) {
        return false;
    }

    const d1l_settings_t *settings = d1l_settings_current();
    if (!settings->identity_ready || packet.payload[0] != settings->identity_public_key[0]) {
        return false;
    }

    size_t copied = d1l_contact_store_copy_recent(s_contact_scan, D1L_CONTACT_STORE_CAPACITY);
    for (size_t i = 0; i < copied; ++i) {
        d1l_contact_entry_t *contact = &s_contact_scan[i];
        uint8_t sender_pub[D1L_MESHCORE_PUB_KEY_SIZE] = {0};
        if (contact->public_key_hex[0] == '\0' ||
            !hex_to_bytes(sender_pub, sizeof(sender_pub), contact->public_key_hex) ||
            sender_pub[0] != packet.payload[1]) {
            continue;
        }

        uint8_t secret[D1L_MESHCORE_PUB_KEY_SIZE] = {0};
        ed25519_key_exchange(secret, sender_pub, settings->identity_private_key);
        uint8_t plain[D1L_MESHCORE_MAX_RAW_PACKET + 1U] = {0};
        const size_t plain_len =
            meshcore_decrypt_after_mac(secret, plain, sizeof(plain) - 1U,
                                       &packet.payload[2], packet.payload_len - 2U);
        if (plain_len < 6U) {
            memset(secret, 0, sizeof(secret));
            continue;
        }

        const uint8_t txt_type = plain[4] >> 2;
        if (txt_type != D1L_MESHCORE_TXT_TYPE_PLAIN) {
            memset(secret, 0, sizeof(secret));
            continue;
        }
        plain[plain_len] = '\0';
        const char *message = (const char *)&plain[5];
        const size_t message_len = strlen(message);
        const size_t extended_attempt_index = 6U + message_len;
        const uint8_t extended_attempt =
            extended_attempt_index < plain_len ? plain[extended_attempt_index] : 0U;
        uint32_t ack_hash = 0;
        const esp_err_t ack_hash_ret =
            calc_dm_ack_hash(&ack_hash, plain, 5U + message_len, sender_pub);
        uint8_t identity_digest[D1L_MESHCORE_DM_ACK_DEDUPE_DIGEST_BYTES] = {0};
        const esp_err_t digest_ret = calc_dm_identity_digest(
            identity_digest, plain, plain_len, message_len, sender_pub);
        d1l_dm_entry_t retained_identity = {0};
        const bool duplicate = digest_ret == ESP_OK &&
            d1l_dm_store_find_rx_identity(identity_digest, &retained_identity);
        if (duplicate) {
            const d1l_dm_store_stats_t dm_stats = d1l_dm_store_stats();
            (void)remember_ack_identity_state(
                &retained_identity,
                dm_stats.loaded && !dm_stats.persistence_dirty);
        }

        d1l_meshcore_ack_dispatch_plan_t ack_plan = {0};
        uint8_t ack_raw[D1L_MESHCORE_MAX_RAW_PACKET] = {0};
        uint8_t ack_raw_len = 0U;
        esp_err_t ack_build_ret =
            ack_hash_ret == ESP_OK ? digest_ret : ack_hash_ret;
        if (ack_build_ret == ESP_OK) {
            uint8_t ack_hash_bytes[4] = {0};
            write_le32(ack_hash_bytes, ack_hash);
            uint32_t ack_route_learned_at_ms = 0U;
            const bool ack_route_learned_this_boot = contact->out_path_valid &&
                lookup_boot_route(contact->fingerprint, contact->out_path,
                                  contact->out_path_len,
                                  &ack_route_learned_at_ms);
            const uint32_t ack_route_now_ms =
                (uint32_t)(esp_timer_get_time() / 1000ULL);
            d1l_meshcore_route_selection_t ack_route_selection = {0};
            const bool selected = d1l_meshcore_route_select(
                contact->out_path_valid, ack_route_learned_this_boot,
                contact->out_path, contact->out_path_len,
                ack_route_learned_at_ms, ack_route_now_ms,
                settings->path_hash_bytes, &ack_route_selection);
            const bool ack_direct = selected &&
                ack_route_selection.route == D1L_MESHCORE_ROUTE_DIRECT;
            const bool planned = selected && d1l_meshcore_ack_dispatch_plan(
                &packet, settings->path_hash_bytes, ack_direct,
                ack_direct ? ack_route_selection.path : NULL,
                ack_direct ? ack_route_selection.path_len : 0U,
                ack_hash_bytes, extended_attempt, (uint8_t)esp_random(),
                &ack_plan);
            ack_build_ret = planned ?
                build_dm_ack_response(settings, sender_pub, secret, &ack_plan,
                                      ack_raw, sizeof(ack_raw), &ack_raw_len) :
                ESP_ERR_INVALID_ARG;
        }
        memset(secret, 0, sizeof(secret));

        s_status.rx_packets++;
        if (duplicate) {
            s_status.ack_tx_duplicate_rows_suppressed++;
            s_status.ack_tx_last_hash = ack_hash;
            s_status.ack_tx_last_error = ack_build_ret;
            char duplicate_note[D1L_PACKET_LOG_NOTE_LEN] = {0};
            snprintf(duplicate_note, sizeof(duplicate_note), "%.12s: %.24s",
                     contact->alias, message);
            append_packet_log("rx", "dm_text_duplicate", rssi, snr,
                              packet.path_hash_bytes, packet.path_hops,
                              size, payload, size, duplicate_note);
            if (ack_build_ret == ESP_OK) {
                return dispatch_bounded_dm_ack(
                    contact, ack_hash, &ack_plan, ack_raw, ack_raw_len,
                    identity_digest);
            }
            record_dm_ack_failure(ack_hash, ack_build_ret);
            ESP_LOGW(TAG, "duplicate DM ACK build failed: %s",
                     esp_err_to_name(ack_build_ret));
            return false;
        }

        d1l_dm_store_append_outcome_t store_outcome = {0};
        esp_err_t store_ret = digest_ret == ESP_OK ?
            d1l_dm_store_append_rx_identity(
                contact->fingerprint, contact->alias, message, rssi,
                (snr * 10) / 4, packet.path_hash_bytes, packet.path_hops,
                plain[4] & 0x03U, ack_hash, identity_digest,
                &store_outcome) : digest_ret;
        if (store_ret != ESP_OK) {
            ESP_LOGW(TAG, "DM rx store append failed: %s", esp_err_to_name(store_ret));
        }
        esp_err_t route_ret =
            d1l_route_store_upsert_observation(contact->fingerprint, contact->alias, "dm_text",
                                               route_name(packet.route), "rx", rssi,
                                               (snr * 10) / 4, packet.path_hash_bytes,
                                               packet.path_hops, size);
        if (route_ret != ESP_OK) {
            ESP_LOGW(TAG, "route store DM rx failed: %s", esp_err_to_name(route_ret));
        }
        char note[D1L_PACKET_LOG_NOTE_LEN] = {0};
        snprintf(note, sizeof(note), "%.12s: %.24s", contact->alias, message);
        append_packet_log("rx", "dm_text", rssi, snr, packet.path_hash_bytes,
                          packet.path_hops, size, payload, size, note);
        if (store_ret != ESP_OK) {
            if (store_outcome.inserted &&
                d1l_dm_store_find_rx_identity(identity_digest,
                                              &retained_identity)) {
                (void)remember_ack_identity_state(&retained_identity, false);
            }
            record_dm_ack_failure(ack_hash, store_ret);
            return false;
        }
        if (digest_ret == ESP_OK) {
            if (!d1l_dm_store_find_rx_identity(identity_digest,
                                               &retained_identity) ||
                !remember_ack_identity_state(&retained_identity, true)) {
                record_dm_ack_failure(ack_hash, ESP_ERR_INVALID_STATE);
                ESP_LOGE(TAG, "DM ACK dedupe remember failed");
                return false;
            }
            if (ack_build_ret == ESP_OK) {
                return dispatch_bounded_dm_ack(
                    contact, ack_hash, &ack_plan, ack_raw, ack_raw_len,
                    identity_digest);
            }
        }
        if (ack_build_ret != ESP_OK) {
            record_dm_ack_failure(ack_hash, ack_build_ret);
            ESP_LOGW(TAG, "DM ACK build failed: %s", esp_err_to_name(ack_build_ret));
        }
        return false;
    }
    return false;
}

static void record_dm_ack(uint32_t ack_hash, const d1l_meshcore_wire_packet_t *packet,
                          int16_t rssi, int8_t snr, uint16_t size, const uint8_t *raw,
                          size_t raw_len, const char *source)
{
    d1l_dm_entry_t acked = {0};
    esp_err_t ret = d1l_dm_store_mark_acked(ack_hash, &acked);
    char note[D1L_PACKET_LOG_NOTE_LEN] = {0};
    if (ret == ESP_OK) {
        snprintf(note, sizeof(note), "ack %lu %.12s", (unsigned long)ack_hash,
                 acked.contact_alias);
        esp_err_t route_ret =
            d1l_route_store_upsert_observation(acked.contact_fingerprint, acked.contact_alias,
                                               "dm_ack", route_name(packet->route), "rx", rssi,
                                               (snr * 10) / 4, packet->path_hash_bytes,
                                               packet->path_hops, size);
        if (route_ret != ESP_OK) {
            ESP_LOGW(TAG, "route store DM ACK rx failed: %s", esp_err_to_name(route_ret));
        }
        append_packet_log("rx", "dm_ack", rssi, snr, packet->path_hash_bytes,
                          packet->path_hops, size, raw, raw_len, note);
    } else {
        snprintf(note, sizeof(note), "%s unmatched %lu", source ? source : "ack",
                 (unsigned long)ack_hash);
        append_packet_log("rx", "dm_ack_unmatched", rssi, snr, packet->path_hash_bytes,
                          packet->path_hops, size, raw, raw_len, note);
    }
}

static void parse_rx_ack_packet(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr)
{
    d1l_meshcore_wire_packet_t packet;
    if (!d1l_meshcore_wire_decode_v1(payload, size, &packet)) {
        return;
    }

    uint32_t ack_hash = 0;
    const char *source = "ack";
    if (packet.type == D1L_MESHCORE_PAYLOAD_ACK && packet.payload_len >= 4U) {
        ack_hash = read_le32(packet.payload);
    } else if (packet.type == D1L_MESHCORE_PAYLOAD_MULTIPART &&
               packet.payload_len >= 5U &&
               (packet.payload[0] & 0x0fU) == D1L_MESHCORE_PAYLOAD_ACK) {
        ack_hash = read_le32(&packet.payload[1]);
        source = "multi_ack";
    } else {
        return;
    }

    s_status.rx_packets++;
    record_dm_ack(ack_hash, &packet, rssi, snr, size, payload, size, source);
}

static void parse_rx_path_packet(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr)
{
    d1l_meshcore_wire_packet_t packet;
    if (!d1l_meshcore_wire_decode_v1(payload, size, &packet) ||
        packet.type != D1L_MESHCORE_PAYLOAD_PATH ||
        packet.payload_len <= (2U + D1L_MESHCORE_CIPHER_MAC_SIZE)) {
        return;
    }

    const d1l_settings_t *settings = d1l_settings_current();
    if (!settings->identity_ready || packet.payload[0] != settings->identity_public_key[0]) {
        return;
    }

    const uint8_t src_hash = packet.payload[1];
    size_t copied = d1l_contact_store_copy_recent(s_contact_scan, D1L_CONTACT_STORE_CAPACITY);
    for (size_t i = 0; i < copied; ++i) {
        d1l_contact_entry_t *contact = &s_contact_scan[i];
        uint8_t sender_pub[D1L_MESHCORE_PUB_KEY_SIZE] = {0};
        if (contact->public_key_hex[0] == '\0' ||
            !hex_to_bytes(sender_pub, sizeof(sender_pub), contact->public_key_hex) ||
            sender_pub[0] != src_hash) {
            continue;
        }

        uint8_t secret[D1L_MESHCORE_PUB_KEY_SIZE] = {0};
        ed25519_key_exchange(secret, sender_pub, settings->identity_private_key);
        uint8_t plain[D1L_MESHCORE_MAX_RAW_PACKET + 1U] = {0};
        const size_t plain_len =
            meshcore_decrypt_after_mac(secret, plain, sizeof(plain) - 1U,
                                       &packet.payload[2], packet.payload_len - 2U);
        memset(secret, 0, sizeof(secret));
        if (plain_len < 2U) {
            continue;
        }

        const uint8_t out_path_len = plain[0];
        if (!d1l_meshcore_wire_path_len_valid(out_path_len)) {
            continue;
        }
        const uint8_t out_path_bytes = d1l_meshcore_wire_path_byte_len(out_path_len);
        if ((size_t)(2U + out_path_bytes) > plain_len) {
            continue;
        }

        const uint8_t *out_path = out_path_bytes > 0 ? &plain[1] : NULL;
        const uint8_t extra_type = plain[1U + out_path_bytes];
        const uint8_t *extra = &plain[2U + out_path_bytes];
        const size_t extra_len = plain_len - (2U + out_path_bytes);
        esp_err_t ret = d1l_contact_store_update_path(contact->fingerprint, out_path, out_path_len);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "contact path update failed: %s", esp_err_to_name(ret));
            return;
        }
        remember_boot_route(
            contact->fingerprint, out_path, out_path_len,
            (uint32_t)(esp_timer_get_time() / 1000ULL));

        const uint8_t out_hash_bytes = d1l_meshcore_wire_path_hash_size(out_path_len);
        const uint8_t out_hops = d1l_meshcore_wire_path_hash_count(out_path_len);
        s_status.rx_packets++;
        esp_err_t route_ret =
            d1l_route_store_upsert_observation(contact->fingerprint, contact->alias, "path_return",
                                               route_name(packet.route), "rx", rssi,
                                               (snr * 10) / 4, out_hash_bytes, out_hops, size);
        if (route_ret != ESP_OK) {
            ESP_LOGW(TAG, "route store path rx failed: %s", esp_err_to_name(route_ret));
        }
        char note[D1L_PACKET_LOG_NOTE_LEN] = {0};
        snprintf(note, sizeof(note), "path %.12s hops=%u", contact->alias, out_hops);
        append_packet_log("rx", "path_return", rssi, snr, out_hash_bytes, out_hops, size,
                          payload, size, note);

        if (extra_type == D1L_MESHCORE_PAYLOAD_ACK && extra_len >= 4U) {
            record_dm_ack(read_le32(extra), &packet, rssi, snr, size, payload, size, "path_ack");
        }
        return;
    }
}

static void parse_rx_trace_packet(uint8_t *payload, uint16_t size,
                                  int16_t rssi, int8_t snr)
{
    if (!payload || size == 0U ||
        ((payload[0] >> 2U) & 0x0fU) != D1L_MESHCORE_PAYLOAD_TRACE) {
        return;
    }

    d1l_meshcore_trace_terminal_t terminal = {0};
    const d1l_meshcore_trace_frame_kind_t frame_kind =
        d1l_meshcore_trace_classify(payload, size, &terminal);
    if (frame_kind != D1L_MESHCORE_TRACE_FRAME_TERMINAL) {
        status_lock();
        switch (frame_kind) {
        case D1L_MESHCORE_TRACE_FRAME_SOURCE:
            s_status.trace_rx_source_ignored++;
            break;
        case D1L_MESHCORE_TRACE_FRAME_IN_FLIGHT:
            s_status.trace_rx_in_flight_ignored++;
            break;
        case D1L_MESHCORE_TRACE_FRAME_UNSUPPORTED:
            s_status.trace_rx_unsupported++;
            break;
        case D1L_MESHCORE_TRACE_FRAME_MALFORMED:
        default:
            s_status.trace_rx_malformed++;
            break;
        }
        status_unlock();
        return;
    }

    const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    d1l_store_lock_take(&s_trace_lock);
    const bool pending_expired = s_trace_tracker.pending &&
        (uint32_t)(now_ms - s_trace_tracker.pending_started_ms) >=
            D1L_MESHCORE_TRACE_PENDING_TIMEOUT_MS;
    const d1l_meshcore_trace_correlation_t correlation =
        d1l_meshcore_trace_tracker_consume(&s_trace_tracker, &terminal, now_ms);
    if (correlation == D1L_MESHCORE_TRACE_CORRELATION_MATCHED) {
        s_trace_last_rssi_dbm = rssi;
        s_trace_last_radio_snr_quarter_db = snr;
        s_trace_last_retention_attempted = false;
        s_trace_last_route_summary_accepted = false;
        s_trace_last_packet_preview_retained = false;
    }
    d1l_store_lock_give(&s_trace_lock);

    status_lock();
    if (pending_expired) {
        s_status.trace_pending_expired++;
    }
    switch (correlation) {
    case D1L_MESHCORE_TRACE_CORRELATION_MATCHED:
        s_status.trace_rx_matched++;
        s_status.rx_packets++;
        break;
    case D1L_MESHCORE_TRACE_CORRELATION_DUPLICATE:
        s_status.trace_rx_duplicates++;
        break;
    case D1L_MESHCORE_TRACE_CORRELATION_EXPIRED:
        s_status.trace_rx_expired++;
        break;
    case D1L_MESHCORE_TRACE_CORRELATION_AUTH_MISMATCH:
        s_status.trace_rx_auth_mismatch++;
        break;
    case D1L_MESHCORE_TRACE_CORRELATION_PATH_MISMATCH:
        s_status.trace_rx_path_mismatch++;
        break;
    case D1L_MESHCORE_TRACE_CORRELATION_UNMATCHED:
    default:
        s_status.trace_rx_unmatched++;
        break;
    }
    status_unlock();

    if (correlation != D1L_MESHCORE_TRACE_CORRELATION_MATCHED) {
        return;
    }

    char target[D1L_ROUTE_TARGET_LEN] = {0};
    char note[D1L_PACKET_LOG_NOTE_LEN] = {0};
    snprintf(target, sizeof(target), "%s",
             d1l_meshcore_trace_retained_target_for_tag(terminal.tag));
    snprintf(note, sizeof(note), "tag=%08lX hops=%u",
             (unsigned long)terminal.tag, terminal.path_hops);
    const esp_err_t route_ret = d1l_route_store_upsert_observation(
        target, "Explicit TRACE", "trace_reply", "direct", "rx", rssi,
        (snr * 10) / 4, 1U, terminal.path_hops, size);
    if (route_ret != ESP_OK) {
        ESP_LOGW(TAG, "route store TRACE reply failed: %s",
                 esp_err_to_name(route_ret));
    }
    const bool packet_retained = append_packet_log(
        "rx", "trace_reply", rssi, snr, 1U, terminal.path_hops, size,
        payload, size, note);
    if (!packet_retained) {
        ESP_LOGW(TAG, "packet log TRACE reply retention failed");
    }
    d1l_store_lock_take(&s_trace_lock);
    if (s_trace_tracker.completed &&
        s_trace_tracker.last_result.tag == terminal.tag &&
        s_trace_tracker.last_result.auth_code == terminal.auth_code &&
        d1l_meshcore_trace_path_matches(
            &terminal, s_trace_tracker.last_result.path_hops,
            s_trace_tracker.last_result.path_hashes)) {
        s_trace_last_retention_attempted = true;
        s_trace_last_route_summary_accepted = route_ret == ESP_OK;
        s_trace_last_packet_preview_retained = packet_retained;
    }
    d1l_store_lock_give(&s_trace_lock);
}

static bool verify_advert_signature(const uint8_t *pub_key, const uint8_t *timestamp,
                                    const uint8_t *signature, const uint8_t *app_data,
                                    size_t app_data_len)
{
    if (!pub_key || !timestamp || !signature || app_data_len > D1L_MESHCORE_MAX_ADVERT_DATA ||
        !d1l_ed25519_encoded_point_is_strict(pub_key) ||
        !d1l_ed25519_encoded_point_is_strict(signature) ||
        !d1l_ed25519_signature_s_is_canonical(signature)) {
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
    if (!d1l_meshcore_wire_write_prefix(
            (uint8_t)((D1L_MESHCORE_PAYLOAD_ADVERT << 2) |
                      (flood ? D1L_MESHCORE_ROUTE_FLOOD :
                               D1L_MESHCORE_ROUTE_DIRECT)),
            0U, 0U,
            flood ? (uint8_t)((settings->path_hash_bytes - 1U) << 6) : 0U,
            NULL, raw, raw_size, &i)) {
        return ESP_ERR_INVALID_SIZE;
    }
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
    if (!d1l_meshcore_wire_decode_v1(payload, size, &packet) ||
        packet.type != D1L_MESHCORE_PAYLOAD_ADVERT ||
        packet.payload_len < D1L_MESHCORE_ADVERT_MIN_PAYLOAD) {
        return;
    }

    const uint8_t *pub_key = &packet.payload[0];
    const uint8_t *timestamp = &packet.payload[D1L_MESHCORE_PUB_KEY_SIZE];
    const uint8_t *signature = &packet.payload[D1L_MESHCORE_PUB_KEY_SIZE + 4U];
    const uint8_t *app_data = &packet.payload[D1L_MESHCORE_ADVERT_MIN_PAYLOAD];
    const size_t app_data_len = packet.payload_len - D1L_MESHCORE_ADVERT_MIN_PAYLOAD;

    char pub_prefix[17] = {0};
    hex_prefix(pub_prefix, sizeof(pub_prefix), pub_key, 8U);
    char pub_key_hex[D1L_NODE_PUBLIC_KEY_HEX_LEN] = {0};
    hex_prefix(pub_key_hex, sizeof(pub_key_hex), pub_key, D1L_MESHCORE_PUB_KEY_SIZE);
    if (app_data_len > D1L_MESHCORE_MAX_ADVERT_DATA) {
        char note[D1L_PACKET_LOG_NOTE_LEN] = {0};
        snprintf(note, sizeof(note), "app_oversize pub=%s", pub_prefix);
        append_packet_log("rx", "advert_bad_app", rssi, snr, packet.path_hash_bytes,
                          packet.path_hops, size, payload, size, note);
        return;
    }
    const bool valid_signature =
        verify_advert_signature(pub_key, timestamp, signature, app_data, app_data_len);
    if (!valid_signature) {
        char note[D1L_PACKET_LOG_NOTE_LEN] = {0};
        snprintf(note, sizeof(note), "bad_sig pub=%s", pub_prefix);
        append_packet_log("rx", "advert_bad_sig", rssi, snr, packet.path_hash_bytes,
                          packet.path_hops, size, payload, size, note);
        return;
    }

    const d1l_settings_t *settings = d1l_settings_current();
    if (settings->identity_ready &&
        memcmp(pub_key, settings->identity_public_key, D1L_MESHCORE_PUB_KEY_SIZE) == 0) {
        char note[D1L_PACKET_LOG_NOTE_LEN] = {0};
        snprintf(note, sizeof(note), "self pub=%s", pub_prefix);
        append_packet_log("rx", "advert_self", rssi, snr, packet.path_hash_bytes,
                          packet.path_hops, size, payload, size, note);
        return;
    }

    d1l_advert_data_t advert = {0};
    if (!d1l_advert_data_parse(app_data, app_data_len, &advert)) {
        char note[D1L_PACKET_LOG_NOTE_LEN] = {0};
        snprintf(note, sizeof(note), "app_invalid pub=%s", pub_prefix);
        append_packet_log("rx", "advert_bad_app", rssi, snr, packet.path_hash_bytes,
                          packet.path_hops, size, payload, size, note);
        return;
    }

    char note[D1L_PACKET_LOG_NOTE_LEN] = {0};
    if (advert.name[0]) {
        char short_name[13] = {0};
        sanitize_note(short_name, sizeof(short_name), advert.name);
        snprintf(note, sizeof(note), "adv %c %s %.8s", advert.type_code, short_name, pub_prefix);
    } else {
        snprintf(note, sizeof(note), "adv %c %.8s", advert.type_code, pub_prefix);
    }
    s_status.rx_packets++;
    const uint32_t advert_timestamp = read_le32(timestamp);
    bool advert_stale = false;
    esp_err_t ret = d1l_node_store_upsert_advert(pub_prefix, pub_key_hex, advert.name,
                                                 advert.type_code, rssi,
                                                 (snr * 10) / 4,
                                                 packet.path_hash_bytes, packet.path_hops,
                                                 advert_timestamp, advert.location_valid,
                                                 advert.lat_e6, advert.lon_e6,
                                                 &advert_stale);
    if (advert_stale) {
        snprintf(note, sizeof(note), "replay %.8s ts=%lu", pub_prefix,
                 (unsigned long)advert_timestamp);
        append_packet_log("rx", "advert_replay", rssi, snr, packet.path_hash_bytes,
                          packet.path_hops, size, payload, size, note);
        return;
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "node store upsert failed: %s", esp_err_to_name(ret));
    }
    s_status.rx_adverts++;
    esp_err_t route_ret =
        d1l_route_store_upsert_observation(pub_prefix,
                                           advert.name[0] ? advert.name : pub_prefix, "advert",
                                           route_name(packet.route), "rx", rssi,
                                           (snr * 10) / 4, packet.path_hash_bytes,
                                           packet.path_hops, size);
    if (route_ret != ESP_OK) {
        ESP_LOGW(TAG, "route store advert rx failed: %s", esp_err_to_name(route_ret));
    }
    append_packet_log("rx", "advert", rssi, snr, packet.path_hash_bytes,
                      packet.path_hops, size, payload, size, note);
}

static void on_tx_done(void)
{
    if (s_active_tx_ack_response) {
        complete_pending_ack_tx(true, ESP_OK);
    } else {
        flush_pending_tx();
    }
    s_active_tx_ack_response = false;
    s_tx_busy = false;
    s_status.tx_packets++;
    s_status.state = D1L_MESHCORE_SERVICE_READY;
    meshcore_service_request_rx_async();
}

static void on_tx_timeout(void)
{
    if (s_active_tx_ack_response) {
        complete_pending_ack_tx(false, ESP_ERR_TIMEOUT);
    } else {
        s_pending_public_tx = false;
        s_pending_public_text[0] = '\0';
        memset(&s_pending_dm_tx, 0, sizeof(s_pending_dm_tx));
    }
    s_active_tx_ack_response = false;
    s_tx_busy = false;
    s_status.state = D1L_MESHCORE_SERVICE_RADIO_ERROR;
    meshcore_service_request_rx_async();
}

static void on_rx_done(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr)
{
    parse_rx_public_packet(payload, size, rssi, snr);
    const bool ack_queued = parse_rx_dm_packet(payload, size, rssi, snr);
    parse_rx_path_packet(payload, size, rssi, snr);
    parse_rx_ack_packet(payload, size, rssi, snr);
    parse_rx_trace_packet(payload, size, rssi, snr);
    parse_rx_advert_packet(payload, size, rssi, snr);
    if (!ack_queued) {
        meshcore_service_request_rx_async();
    }
}

static void on_rx_timeout(void)
{
    meshcore_service_request_rx_async();
}

static void on_rx_error(void)
{
    meshcore_service_request_rx_async();
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
    mark_radio_apply_result(&profile, ret);
    if (ret != ESP_OK) {
        s_status.radio_ready = false;
        ESP_LOGW(TAG, "unsupported radio profile for MeshCore public RX/TX");
        return ret;
    }
    s_status.radio_ready = true;
    s_status.identity_ready = d1l_settings_current()->identity_ready;
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
    const esp_err_t ret = configure_radio_profile(&profile);
    mark_radio_apply_result(&profile, ret);
    if (ret != ESP_OK) {
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

static esp_err_t meshcore_service_handle_start_rx(void)
{
    if (s_tx_busy) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = ensure_radio_started();
    if (ret == ESP_OK) {
        d1l_meshcore_start_rx();
    }
    return ret;
}

static esp_err_t meshcore_service_handle_send_raw(const d1l_meshcore_service_cmd_t *cmd)
{
    if (!cmd || cmd->raw_len == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_tx_busy) {
        return ESP_ERR_INVALID_STATE;
    }

    if (cmd->delay_ms > 0U) {
        vTaskDelay(pdMS_TO_TICKS(cmd->delay_ms));
        if (s_tx_busy) {
            return ESP_ERR_INVALID_STATE;
        }
    }

    esp_err_t ret = ensure_radio_started();
    if (ret != ESP_OK) {
        return ret;
    }

    s_active_tx_ack_response = cmd->ack_response;
    s_tx_busy = true;
    s_status.state = D1L_MESHCORE_SERVICE_TX_BUSY;
    Radio.Send(cmd->raw, cmd->raw_len);
    return ESP_OK;
}

static void meshcore_service_reply(const d1l_meshcore_service_cmd_t *cmd, esp_err_t ret)
{
    if (cmd && cmd->reply_task) {
        (void)xTaskNotify(cmd->reply_task, (uint32_t)ret, eSetValueWithOverwrite);
    }
}

static void meshcore_service_task(void *arg)
{
    (void)arg;
    d1l_meshcore_service_cmd_t cmd = {0};
    for (;;) {
        if (xQueueReceive(s_service_queue, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        esp_err_t ret = ESP_ERR_INVALID_ARG;
        switch (cmd.type) {
        case D1L_MESHCORE_SERVICE_CMD_START_RX:
            ret = meshcore_service_handle_start_rx();
            break;
        case D1L_MESHCORE_SERVICE_CMD_SEND_RAW:
            ret = meshcore_service_handle_send_raw(&cmd);
            break;
        default:
            ret = ESP_ERR_INVALID_ARG;
            break;
        }
        if (cmd.type == D1L_MESHCORE_SERVICE_CMD_START_RX &&
            cmd.reply_task == NULL && ret != ESP_OK) {
            ESP_LOGW(TAG, "asynchronous MeshCore RX start failed: %s", esp_err_to_name(ret));
        }
        if (cmd.type == D1L_MESHCORE_SERVICE_CMD_SEND_RAW &&
            cmd.ack_response && ret != ESP_OK) {
            complete_pending_ack_tx(false, ret);
            meshcore_service_request_rx_async();
        }
        meshcore_service_reply(&cmd, ret);
    }
}

static esp_err_t meshcore_service_start_task(void)
{
    if (!s_status_mutex) {
        s_status_mutex = xSemaphoreCreateMutex();
        if (!s_status_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (!s_service_queue) {
        s_service_queue =
            xQueueCreate(D1L_MESHCORE_SERVICE_QUEUE_LEN, sizeof(d1l_meshcore_service_cmd_t));
        if (!s_service_queue) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (!s_service_task) {
        BaseType_t created = xTaskCreate(meshcore_service_task,
                                         "meshcore_service",
                                         D1L_MESHCORE_SERVICE_TASK_STACK_BYTES,
                                         NULL,
                                         4,
                                         &s_service_task);
        if (created != pdPASS) {
            s_service_task = NULL;
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

static esp_err_t meshcore_service_send_command(d1l_meshcore_service_cmd_t *cmd,
                                               uint32_t timeout_ms)
{
    if (!cmd) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = meshcore_service_start_task();
    if (ret != ESP_OK) {
        return ret;
    }

    cmd->reply_task = xTaskGetCurrentTaskHandle();
    (void)xTaskNotifyStateClear(NULL);
    const TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    if (xQueueSend(s_service_queue, cmd, timeout_ticks) != pdTRUE) {
        cmd->reply_task = NULL;
        return ESP_ERR_TIMEOUT;
    }
    uint32_t notify_value = (uint32_t)ESP_ERR_TIMEOUT;
    if (xTaskNotifyWait(0, UINT32_MAX, &notify_value, timeout_ticks) == pdTRUE) {
        ret = (esp_err_t)notify_value;
    } else {
        ret = ESP_ERR_TIMEOUT;
    }
    cmd->reply_task = NULL;
    return ret;
}

static void meshcore_service_request_rx_async(void)
{
    if (!s_service_queue) {
        return;
    }
    d1l_meshcore_service_cmd_t cmd = {
        .type = D1L_MESHCORE_SERVICE_CMD_START_RX,
    };
    (void)xQueueSend(s_service_queue, &cmd, 0);
}

esp_err_t d1l_meshcore_service_start_rx_async(void)
{
    esp_err_t ret = meshcore_service_start_task();
    if (ret != ESP_OK) {
        return ret;
    }
    d1l_meshcore_service_cmd_t cmd = {
        .type = D1L_MESHCORE_SERVICE_CMD_START_RX,
    };
    return xQueueSend(s_service_queue, &cmd, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

static esp_err_t meshcore_service_send_raw(const uint8_t *raw,
                                           uint8_t raw_len,
                                           uint32_t timeout_ms)
{
    if (!raw || raw_len == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    d1l_meshcore_service_cmd_t cmd = {
        .type = D1L_MESHCORE_SERVICE_CMD_SEND_RAW,
        .raw_len = raw_len,
    };
    memcpy(cmd.raw, raw, raw_len);
    return meshcore_service_send_command(&cmd, timeout_ms);
}

static esp_err_t meshcore_service_send_ack_async(
    const d1l_contact_entry_t *contact,
    uint32_t ack_hash,
    uint32_t row_seq,
    const uint8_t digest[D1L_MESHCORE_DM_ACK_DEDUPE_DIGEST_BYTES],
    const d1l_meshcore_ack_dispatch_plan_t *plan,
    const uint8_t *raw,
    uint8_t raw_len)
{
    if (!contact || row_seq == 0U || !digest || !plan || !raw || raw_len == 0U ||
        plan->delay_ms > UINT16_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = meshcore_service_start_task();
    if (ret != ESP_OK) {
        s_status.ack_tx_failed++;
        s_status.ack_tx_last_hash = ack_hash;
        s_status.ack_tx_last_error = ret;
        complete_unqueued_ack_reservation(row_seq, digest, ret);
        return ret;
    }
    if (s_tx_busy || s_pending_ack_tx.active) {
        s_status.ack_tx_failed++;
        s_status.ack_tx_last_hash = ack_hash;
        s_status.ack_tx_last_error = ESP_ERR_INVALID_STATE;
        complete_unqueued_ack_reservation(
            row_seq, digest, ESP_ERR_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }

    remember_pending_ack_tx(contact, ack_hash, row_seq, digest, plan, raw,
                            raw_len);
    d1l_meshcore_service_cmd_t cmd = {
        .type = D1L_MESHCORE_SERVICE_CMD_SEND_RAW,
        .raw_len = raw_len,
        .delay_ms = (uint16_t)plan->delay_ms,
        .ack_response = true,
    };
    memcpy(cmd.raw, raw, raw_len);
    if (xQueueSendToFront(s_service_queue, &cmd, 0) != pdTRUE) {
        complete_pending_ack_tx(false, ESP_ERR_TIMEOUT);
        return ESP_ERR_TIMEOUT;
    }
    s_status.ack_tx_queued++;
    return ESP_OK;
}

void d1l_meshcore_service_init(void)
{
    const d1l_settings_t *settings = d1l_settings_current();
    const esp_err_t task_ret = meshcore_service_start_task();
    if (!s_service_initialized) {
        d1l_store_lock_take(&s_trace_lock);
        memset(&s_trace_tracker, 0, sizeof(s_trace_tracker));
        s_trace_last_rssi_dbm = 0;
        s_trace_last_radio_snr_quarter_db = 0;
        s_trace_last_retention_attempted = false;
        s_trace_last_route_summary_accepted = false;
        s_trace_last_packet_preview_retained = false;
        d1l_store_lock_give(&s_trace_lock);
    }
    status_lock();
    // Runtime settings flows reuse init; preserve the live radio and queued work.
    if (s_service_initialized) {
        s_status.path_hash_bytes = settings->path_hash_bytes;
        s_status.identity_ready = settings->identity_ready;
        status_unlock();
        return;
    }
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = D1L_MESHCORE_SERVICE_WAITING_FOR_RADIO;
    s_status.path_hash_bytes = settings->path_hash_bytes;
    s_status.identity_ready = settings->identity_ready;
    s_status.radio_ready = false;
    s_status.radio_applied = false;
    s_status.radio_apply_pending = true;
    s_status.radio_apply_error = ESP_ERR_INVALID_STATE;
    s_status.companion_framing_ready = true;
    s_radio_profile_applied = false;
    s_radio_started = false;
    s_tx_busy = false;
    s_active_tx_ack_response = false;
    s_pending_public_tx = false;
    s_pending_public_text[0] = '\0';
    memset(&s_pending_dm_tx, 0, sizeof(s_pending_dm_tx));
    clear_pending_ack_tx();
    clear_boot_routes();
    restore_ack_dedupe_from_store();
    s_status.ack_tx_last_error = ESP_OK;
    s_service_initialized = task_ret == ESP_OK;
    status_unlock();
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
    d1l_radio_profile_t current_profile = d1l_settings_radio_profile(settings);
    d1l_radio_profile_t applied_profile = {0};
    bool applied_valid = false;
    status_lock();
    d1l_meshcore_service_status_t snapshot = s_status;
    applied_profile = s_applied_radio_profile;
    applied_valid = s_radio_profile_applied;
    status_unlock();
    snapshot.identity_ready = settings->identity_ready || snapshot.identity_ready;
    snapshot.path_hash_bytes = settings->path_hash_bytes;
    snapshot.radio_applied = snapshot.radio_ready &&
                             applied_valid &&
                             snapshot.radio_apply_error == ESP_OK &&
                             radio_profiles_match(&applied_profile, &current_profile);
    snapshot.radio_apply_pending = !snapshot.radio_applied;
    return snapshot;
}

void d1l_meshcore_service_trace_snapshot(
    d1l_meshcore_trace_snapshot_t *out_snapshot)
{
    if (!out_snapshot) {
        return;
    }

    d1l_meshcore_trace_snapshot_t snapshot = {0};
    const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    d1l_store_lock_take(&s_trace_lock);
    if (s_trace_tracker.pending) {
        snapshot.pending = true;
        snapshot.pending_tag = s_trace_tracker.pending_tag;
        snapshot.pending_age_ms =
            (uint32_t)(now_ms - s_trace_tracker.pending_started_ms);
        snapshot.pending_expired =
            snapshot.pending_age_ms >= D1L_MESHCORE_TRACE_PENDING_TIMEOUT_MS;
        snapshot.pending_path_hops = s_trace_tracker.pending_path_hops;
        memcpy(snapshot.pending_path_hashes,
               s_trace_tracker.pending_path_hashes,
               snapshot.pending_path_hops);
    }
    if (s_trace_tracker.completed) {
        snapshot.last_result_valid = true;
        snapshot.last_tag = s_trace_tracker.last_result.tag;
        snapshot.last_age_ms =
            (uint32_t)(now_ms - s_trace_tracker.completed_at_ms);
        snapshot.last_path_hops = s_trace_tracker.last_result.path_hops;
        memcpy(snapshot.last_path_hashes,
               s_trace_tracker.last_result.path_hashes,
               snapshot.last_path_hops);
        memcpy(snapshot.last_path_snrs_quarter_db,
               s_trace_tracker.last_result.path_snrs_quarter_db,
               snapshot.last_path_hops);
        snapshot.last_rssi_dbm = s_trace_last_rssi_dbm;
        snapshot.last_radio_snr_quarter_db =
            s_trace_last_radio_snr_quarter_db;
        snapshot.last_retention_attempted =
            s_trace_last_retention_attempted;
        snapshot.last_route_summary_accepted =
            s_trace_last_route_summary_accepted;
        snapshot.last_packet_preview_retained =
            s_trace_last_packet_preview_retained;
    }
    d1l_store_lock_give(&s_trace_lock);
    *out_snapshot = snapshot;
}

esp_err_t d1l_meshcore_service_request_advert(bool flood)
{
    esp_err_t ret = d1l_meshcore_service_ensure_identity();
    if (ret != ESP_OK) {
        s_status.rejected_commands++;
        return ret;
    }
    d1l_meshcore_service_cmd_t start_cmd = {
        .type = D1L_MESHCORE_SERVICE_CMD_START_RX,
    };
    ret = meshcore_service_send_command(&start_cmd, D1L_MESHCORE_SERVICE_COMMAND_TIMEOUT_MS);
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

    ret = meshcore_service_send_raw(raw, raw_len, D1L_MESHCORE_SERVICE_COMMAND_TIMEOUT_MS);
    if (ret != ESP_OK) {
        s_status.rejected_commands++;
        return ret;
    }
    esp_err_t route_ret =
        d1l_route_store_upsert_observation(pub_prefix, settings->node_name, "advert",
                                           route_name(flood ? D1L_MESHCORE_ROUTE_FLOOD :
                                                      D1L_MESHCORE_ROUTE_DIRECT),
                                           "tx", 0, 0, settings->path_hash_bytes, 0, raw_len);
    if (route_ret != ESP_OK) {
        ESP_LOGW(TAG, "route store advert tx failed: %s", esp_err_to_name(route_ret));
    }
    append_packet_log("tx", "advert", 0, 0, settings->path_hash_bytes, 0, raw_len,
                      raw, raw_len, note);
    return ESP_OK;
}

esp_err_t d1l_meshcore_service_send_public(const char *text)
{
    esp_err_t ret = validate_user_text(text);
    if (ret != ESP_OK) {
        return ret;
    }
    d1l_meshcore_service_cmd_t start_cmd = {
        .type = D1L_MESHCORE_SERVICE_CMD_START_RX,
    };
    ret = meshcore_service_send_command(&start_cmd, D1L_MESHCORE_SERVICE_COMMAND_TIMEOUT_MS);
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

    remember_pending_public_tx(text);
    ret = meshcore_service_send_raw(raw, raw_len, D1L_MESHCORE_SERVICE_COMMAND_TIMEOUT_MS);
    if (ret != ESP_OK) {
        s_pending_public_tx = false;
        s_pending_public_text[0] = '\0';
        s_status.rejected_commands++;
        return ret;
    }
    esp_err_t route_ret =
        d1l_route_store_upsert_observation("public", "Public", "public_text",
                                           route_name(D1L_MESHCORE_ROUTE_FLOOD), "tx",
                                           0, 0, settings->path_hash_bytes, 0, raw_len);
    if (route_ret != ESP_OK) {
        ESP_LOGW(TAG, "route store public tx failed: %s", esp_err_to_name(route_ret));
    }
    append_packet_log("tx", "public_text", 0, 0, settings->path_hash_bytes, 0, raw_len,
                      raw, raw_len, text);
    return ESP_OK;
}

static esp_err_t meshcore_service_send_dm_with_result(
    const char *fingerprint, const char *text,
    d1l_dm_send_result_t *out_result)
{
    if (out_result) {
        memset(out_result, 0, sizeof(*out_result));
    }
    if (!fingerprint || fingerprint[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = validate_user_text(text);
    if (ret != ESP_OK) {
        return ret;
    }

    /* Resolve and authorize the target before identity/radio side effects.
     * Only canonical chat adverts are direct-message endpoints. */
    d1l_contact_entry_t contact = {0};
    if (!d1l_contact_store_find_by_fingerprint(fingerprint, &contact) ||
        contact.public_key_hex[0] == '\0') {
        s_status.rejected_commands++;
        return ESP_ERR_NOT_FOUND;
    }
    if (strcmp(contact.type, "chat") != 0) {
        s_status.rejected_commands++;
        return ESP_ERR_INVALID_STATE;
    }

    ret = d1l_meshcore_service_ensure_identity();
    if (ret != ESP_OK) {
        s_status.rejected_commands++;
        return ret;
    }
    d1l_meshcore_service_cmd_t start_cmd = {
        .type = D1L_MESHCORE_SERVICE_CMD_START_RX,
    };
    ret = meshcore_service_send_command(&start_cmd, D1L_MESHCORE_SERVICE_COMMAND_TIMEOUT_MS);
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
    uint32_t ack_hash = 0;
    const d1l_settings_t *settings = d1l_settings_current();
    const uint32_t route_now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    uint32_t route_learned_at_ms = 0U;
    const bool route_learned_this_boot = contact.out_path_valid &&
        lookup_boot_route(contact.fingerprint, contact.out_path,
                          contact.out_path_len, &route_learned_at_ms);
    d1l_meshcore_route_selection_t selection = {0};
    if (!d1l_meshcore_route_select(
            contact.out_path_valid, route_learned_this_boot,
            contact.out_path, contact.out_path_len,
            route_learned_at_ms, route_now_ms, settings->path_hash_bytes,
            &selection)) {
        s_status.rejected_commands++;
        return ESP_ERR_INVALID_STATE;
    }
    uint32_t tx_timestamp = 0;
    ret = d1l_settings_next_mesh_timestamp(&tx_timestamp);
    if (ret != ESP_OK) {
        s_status.rejected_commands++;
        return ret;
    }
    ret = build_dm_text_packet(settings, &contact, text, &selection,
                               tx_timestamp, raw, sizeof(raw), &raw_len, &ack_hash);
    if (ret != ESP_OK) {
        s_status.rejected_commands++;
        return ret;
    }

    remember_pending_dm_tx(&contact, text, selection.path_hash_bytes,
                           selection.path_hops, 0, ack_hash);
    ret = meshcore_service_send_raw(raw, raw_len, D1L_MESHCORE_SERVICE_COMMAND_TIMEOUT_MS);
    if (ret != ESP_OK) {
        clear_pending_dm_tx();
        s_status.rejected_commands++;
        return ret;
    }
    (void)append_dm_store_tx(&s_pending_dm_tx);
    clear_pending_dm_tx();
    record_dm_route_selection(&selection);
    esp_err_t route_ret =
        d1l_route_store_upsert_observation(contact.fingerprint, contact.alias, "dm_text",
                                           route_name(selection.route),
                                           "tx", 0, 0, selection.path_hash_bytes,
                                           selection.path_hops,
                                           raw_len);
    if (route_ret != ESP_OK) {
        ESP_LOGW(TAG, "route store DM tx failed: %s", esp_err_to_name(route_ret));
    }
    char route_note[D1L_PACKET_LOG_NOTE_LEN] = {0};
    snprintf(route_note, sizeof(route_note), "%s age=%lu %.20s",
             d1l_meshcore_route_selection_reason_name(selection.reason),
             (unsigned long)selection.path_age_ms, text);
    append_packet_log("tx", "dm_text", 0, 0, selection.path_hash_bytes,
                      selection.path_hops, raw_len, raw, raw_len, route_note);
    if (out_result) {
        out_result->selection = selection;
        snprintf(out_result->fingerprint, sizeof(out_result->fingerprint), "%s",
                 contact.fingerprint);
        snprintf(out_result->alias, sizeof(out_result->alias), "%s",
                 contact.alias[0] ? contact.alias : contact.fingerprint);
        out_result->raw_len = raw_len;
    }
    return ESP_OK;
}

esp_err_t d1l_meshcore_service_send_dm(const char *fingerprint, const char *text)
{
    return meshcore_service_send_dm_with_result(fingerprint, text, NULL);
}

esp_err_t d1l_meshcore_service_request_path_discovery_probe(
    const char *fingerprint,
    char *out_token,
    size_t out_token_size)
{
    if (!fingerprint || fingerprint[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    if (s_last_path_probe_fingerprint[0] != '\0' &&
        strncmp(s_last_path_probe_fingerprint, fingerprint,
                sizeof(s_last_path_probe_fingerprint)) == 0 &&
        (uint32_t)(now_ms - s_last_path_probe_ms) <
            D1L_MESHCORE_PATH_PROBE_COOLDOWN_MS) {
        s_status.rejected_commands++;
        return ESP_ERR_INVALID_STATE;
    }

    char token[D1L_MESSAGE_TEXT_LEN] = {0};
    const int written = snprintf(token, sizeof(token), "path_%08lX",
                                 (unsigned long)esp_random());
    if (written <= 0 || (size_t)written >= sizeof(token)) {
        s_status.rejected_commands++;
        return ESP_ERR_INVALID_SIZE;
    }

    d1l_dm_send_result_t send_result = {0};
    esp_err_t ret = meshcore_service_send_dm_with_result(
        fingerprint, token, &send_result);
    if (ret != ESP_OK) {
        return ret;
    }

    esp_err_t route_ret =
        d1l_route_store_upsert_observation(
            send_result.fingerprint, send_result.alias, "path_probe",
            route_name(send_result.selection.route), "tx", 0, 0,
            send_result.selection.path_hash_bytes,
            send_result.selection.path_hops, send_result.raw_len);
    if (route_ret != ESP_OK) {
        ESP_LOGW(TAG, "route store path probe tx failed: %s", esp_err_to_name(route_ret));
    }

    snprintf(s_last_path_probe_fingerprint, sizeof(s_last_path_probe_fingerprint),
             "%s", fingerprint);
    s_last_path_probe_ms = now_ms;
    if (out_token && out_token_size > 0) {
        snprintf(out_token, out_token_size, "%s", token);
    }
    return ESP_OK;
}

esp_err_t d1l_meshcore_service_send_trace_loop(const uint8_t *path_hashes,
                                               size_t path_hops,
                                               uint32_t *out_tag)
{
    if (!path_hashes || path_hops == 0U ||
        path_hops > D1L_MESHCORE_TRACE_MAX_HOPS) {
        status_lock();
        s_status.rejected_commands++;
        status_unlock();
        return ESP_ERR_INVALID_ARG;
    }
    const uint32_t tag = esp_random();
    const uint32_t auth_code = esp_random();
    d1l_meshcore_trace_source_t source = {0};
    if (!d1l_meshcore_trace_build_source(tag, auth_code, path_hashes,
                                         path_hops, &source)) {
        status_lock();
        s_status.rejected_commands++;
        status_unlock();
        return ESP_ERR_INVALID_SIZE;
    }

    const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    d1l_store_lock_take(&s_trace_lock);
    const bool expired = d1l_meshcore_trace_tracker_expire_pending(
        &s_trace_tracker, now_ms);
    const bool began = d1l_meshcore_trace_tracker_begin(
        &s_trace_tracker, tag, auth_code, path_hashes, path_hops, now_ms);
    d1l_store_lock_give(&s_trace_lock);
    if (expired) {
        status_lock();
        s_status.trace_pending_expired++;
        status_unlock();
    }
    if (!began) {
        status_lock();
        s_status.rejected_commands++;
        status_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t ret = meshcore_service_send_raw(
        source.raw, source.raw_len, D1L_MESHCORE_SERVICE_COMMAND_TIMEOUT_MS);
    if (ret != ESP_OK) {
        d1l_store_lock_take(&s_trace_lock);
        (void)d1l_meshcore_trace_tracker_cancel(&s_trace_tracker, tag,
                                                auth_code);
        d1l_store_lock_give(&s_trace_lock);
        status_lock();
        s_status.rejected_commands++;
        status_unlock();
        return ret;
    }

    char target[D1L_ROUTE_TARGET_LEN] = {0};
    char note[D1L_PACKET_LOG_NOTE_LEN] = {0};
    snprintf(target, sizeof(target), "trace_%08lX", (unsigned long)tag);
    snprintf(note, sizeof(note), "tag=%08lX loop_hops=%u",
             (unsigned long)tag, (unsigned)path_hops);
    const esp_err_t route_ret = d1l_route_store_upsert_observation_volatile(
        target, "Explicit TRACE", "trace_request", "direct", "tx", 0, 0,
        1U, (uint8_t)path_hops, source.raw_len);
    if (route_ret != ESP_OK) {
        ESP_LOGW(TAG, "volatile TRACE request route failed: %s",
                 esp_err_to_name(route_ret));
    }
    append_packet_log("tx", "trace_request", 0, 0, 1U,
                      (uint8_t)path_hops, source.raw_len, source.raw,
                      source.raw_len, note);
    status_lock();
    s_status.trace_tx_queued++;
    status_unlock();
    if (out_tag) {
        *out_tag = tag;
    }
    return ESP_OK;
}

const char *d1l_meshcore_service_state_name(d1l_meshcore_service_state_t state)
{
    switch (state) {
    case D1L_MESHCORE_SERVICE_INITIALIZING:
        return "initializing";
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
