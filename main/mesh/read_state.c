#include "read_state.h"

#include <stdbool.h>
#include <string.h>

#include "nvs.h"

#include "mesh/contact_store.h"
#include "mesh/dm_store.h"
#include "mesh/message_store.h"

#define D1L_READ_STATE_NAMESPACE "d1l_read"
#define D1L_READ_STATE_KEY "state"
#define D1L_READ_STATE_SCHEMA 1U

typedef struct {
    uint32_t schema;
    uint32_t last_public_read_seq;
    uint32_t last_dm_read_seq;
    uint32_t mark_read_count;
} d1l_read_state_blob_t;

static d1l_read_state_blob_t s_state;
static bool s_loaded;
static d1l_message_entry_t s_message_scratch[D1L_MESSAGE_STORE_CAPACITY];
static d1l_dm_entry_t s_dm_scratch[D1L_DM_STORE_CAPACITY];

static void clear_ram(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_state.schema = D1L_READ_STATE_SCHEMA;
}

static bool blob_is_valid(const d1l_read_state_blob_t *blob, size_t len)
{
    return blob && len == sizeof(*blob) && blob->schema == D1L_READ_STATE_SCHEMA;
}

static esp_err_t persist_state(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(D1L_READ_STATE_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_set_blob(handle, D1L_READ_STATE_KEY, &s_state, sizeof(s_state));
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

static esp_err_t ensure_loaded(void)
{
    if (s_loaded) {
        return ESP_OK;
    }
    return d1l_read_state_init();
}

esp_err_t d1l_read_state_init(void)
{
    clear_ram();

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(D1L_READ_STATE_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        s_loaded = false;
        return ret;
    }

    size_t len = sizeof(s_state);
    d1l_read_state_blob_t loaded = {0};
    ret = nvs_get_blob(handle, D1L_READ_STATE_KEY, &loaded, &len);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ret = ESP_OK;
    } else if (ret == ESP_OK && blob_is_valid(&loaded, len)) {
        s_state = loaded;
    } else if (ret == ESP_OK) {
        clear_ram();
        ret = nvs_erase_key(handle, D1L_READ_STATE_KEY);
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            ret = ESP_OK;
        }
        if (ret == ESP_OK) {
            ret = nvs_commit(handle);
        }
    }
    nvs_close(handle);
    s_loaded = (ret == ESP_OK);
    return ret;
}

esp_err_t d1l_read_state_clear(void)
{
    clear_ram();
    s_loaded = true;
    return persist_state();
}

d1l_read_state_stats_t d1l_read_state_stats(void)
{
    if (!s_loaded) {
        (void)d1l_read_state_init();
    }

    d1l_read_state_stats_t stats = {
        .last_public_read_seq = s_state.last_public_read_seq,
        .last_dm_read_seq = s_state.last_dm_read_seq,
        .mark_read_count = s_state.mark_read_count,
    };

    d1l_message_store_stats_t message_stats = d1l_message_store_stats();
    d1l_dm_store_stats_t dm_stats = d1l_dm_store_stats();
    if (stats.last_public_read_seq >= message_stats.next_seq) {
        stats.last_public_read_seq = 0;
    }
    if (stats.last_dm_read_seq >= dm_stats.next_seq) {
        stats.last_dm_read_seq = 0;
    }

    size_t copied = d1l_message_store_copy_recent(s_message_scratch, D1L_MESSAGE_STORE_CAPACITY);
    for (size_t i = 0; i < copied; ++i) {
        const d1l_message_entry_t *entry = &s_message_scratch[i];
        if (entry->direction[0] != 'r') {
            continue;
        }
        if (entry->seq > stats.newest_public_rx_seq) {
            stats.newest_public_rx_seq = entry->seq;
        }
        if (entry->seq > stats.last_public_read_seq) {
            stats.public_unread_count++;
        }
    }

    copied = d1l_dm_store_copy_recent(s_dm_scratch, D1L_DM_STORE_CAPACITY);
    for (size_t i = 0; i < copied; ++i) {
        const d1l_dm_entry_t *entry = &s_dm_scratch[i];
        if (entry->direction[0] != 'r') {
            continue;
        }
        if (entry->seq > stats.newest_dm_rx_seq) {
            stats.newest_dm_rx_seq = entry->seq;
        }
        if (entry->seq <= stats.last_dm_read_seq) {
            continue;
        }

        d1l_contact_entry_t contact = {0};
        const bool muted = d1l_contact_store_find_by_fingerprint(entry->contact_fingerprint, &contact) &&
                           contact.muted;
        if (muted) {
            stats.muted_dm_unread_count++;
        } else {
            stats.dm_unread_count++;
        }
    }

    return stats;
}

esp_err_t d1l_read_state_mark_public_read(void)
{
    esp_err_t ret = ensure_loaded();
    if (ret != ESP_OK) {
        return ret;
    }

    d1l_read_state_stats_t stats = d1l_read_state_stats();
    s_state.last_public_read_seq = stats.newest_public_rx_seq;
    s_state.mark_read_count++;
    return persist_state();
}

esp_err_t d1l_read_state_mark_dm_read(void)
{
    esp_err_t ret = ensure_loaded();
    if (ret != ESP_OK) {
        return ret;
    }

    d1l_read_state_stats_t stats = d1l_read_state_stats();
    s_state.last_dm_read_seq = stats.newest_dm_rx_seq;
    s_state.mark_read_count++;
    return persist_state();
}

esp_err_t d1l_read_state_mark_all_read(void)
{
    esp_err_t ret = ensure_loaded();
    if (ret != ESP_OK) {
        return ret;
    }

    d1l_read_state_stats_t stats = d1l_read_state_stats();
    s_state.last_public_read_seq = stats.newest_public_rx_seq;
    s_state.last_dm_read_seq = stats.newest_dm_rx_seq;
    s_state.mark_read_count++;
    return persist_state();
}
