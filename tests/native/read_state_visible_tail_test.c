#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "mesh/read_state.h"
#include "nvs.h"

#define TEST_VISIBLE_DM_CAPACITY (D1L_DM_STORE_CAPACITY + 1U)

static d1l_dm_entry_t s_rows[TEST_VISIBLE_DM_CAPACITY];
static size_t s_row_count;
static d1l_message_entry_t s_public_rows[D1L_MESSAGE_STORE_CAPACITY];
static size_t s_public_row_count;
static uint32_t s_message_next_seq = 1U;
static size_t s_last_dm_copy_max;
static bool s_contact_muted;
static uint8_t s_nvs_blob[1024U];
static size_t s_nvs_blob_len;
static bool s_nvs_blob_valid;

esp_err_t nvs_open(const char *namespace_name, nvs_open_mode_t open_mode,
                   nvs_handle_t *out_handle)
{
    (void)namespace_name;
    (void)open_mode;
    if (!out_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_handle = 1U;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle)
{
    (void)handle;
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out_value,
                       size_t *length)
{
    (void)handle;
    (void)key;
    if (!length) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_nvs_blob_valid) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (!out_value) {
        *length = s_nvs_blob_len;
        return ESP_OK;
    }
    if (*length < s_nvs_blob_len) {
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    memcpy(out_value, s_nvs_blob, s_nvs_blob_len);
    *length = s_nvs_blob_len;
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key,
                       const void *value, size_t length)
{
    (void)handle;
    (void)key;
    if (!value || length > sizeof(s_nvs_blob)) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(s_nvs_blob, value, length);
    s_nvs_blob_len = length;
    s_nvs_blob_valid = true;
    return ESP_OK;
}

esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key)
{
    (void)handle;
    (void)key;
    s_nvs_blob_valid = false;
    s_nvs_blob_len = 0U;
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    (void)handle;
    return ESP_OK;
}

d1l_message_store_stats_t d1l_message_store_stats(void)
{
    return (d1l_message_store_stats_t) {.next_seq = s_message_next_seq};
}

size_t d1l_message_store_copy_recent(d1l_message_entry_t *out_entries,
                                     size_t max_entries)
{
    if (!out_entries || max_entries == 0U) {
        return 0U;
    }
    const size_t copied = s_public_row_count < max_entries ?
        s_public_row_count : max_entries;
    const size_t first = s_public_row_count - copied;
    memcpy(out_entries, &s_public_rows[first],
           copied * sizeof(out_entries[0]));
    return copied;
}

d1l_dm_store_stats_t d1l_dm_store_stats(void)
{
    return (d1l_dm_store_stats_t) {.next_seq = 18U};
}

size_t d1l_dm_store_copy_recent(d1l_dm_entry_t *out_entries,
                                size_t max_entries)
{
    s_last_dm_copy_max = max_entries;
    if (!out_entries || max_entries == 0U) {
        return 0U;
    }
    const size_t copied = s_row_count < max_entries ? s_row_count : max_entries;
    const size_t first = s_row_count - copied;
    memcpy(out_entries, &s_rows[first], copied * sizeof(out_entries[0]));
    return copied;
}

bool d1l_contact_store_find_by_fingerprint(const char *fingerprint,
                                           d1l_contact_entry_t *out_entry)
{
    if (!fingerprint || strcmp(fingerprint, "aaaaaaaaaaaaaaaa") != 0) {
        return false;
    }
    if (out_entry) {
        memset(out_entry, 0, sizeof(*out_entry));
        out_entry->muted = s_contact_muted;
    }
    return true;
}

static d1l_dm_entry_t row(uint32_t seq, const char *fingerprint,
                          const char *direction)
{
    d1l_dm_entry_t entry = {.seq = seq};
    snprintf(entry.contact_fingerprint, sizeof(entry.contact_fingerprint),
             "%s", fingerprint);
    snprintf(entry.direction, sizeof(entry.direction), "%s", direction);
    return entry;
}

int main(void)
{
    s_public_rows[0] = (d1l_message_entry_t){
        .seq = 10U,
        .channel_id = UINT64_C(1),
    };
    snprintf(s_public_rows[0].direction,
             sizeof(s_public_rows[0].direction), "rx");
    s_public_row_count = 1U;
    /* Private-channel rows can advance the shared global sequence through 99,
     * but the compatibility copy is exact-Public filtered. */
    s_message_next_seq = 100U;
    s_rows[0] = row(1U, "aaaaaaaaaaaaaaaa", "rx");
    for (size_t i = 1U; i < D1L_DM_STORE_CAPACITY; ++i) {
        char fingerprint[D1L_NODE_FINGERPRINT_LEN];
        snprintf(fingerprint, sizeof(fingerprint), "%016u", (unsigned)i);
        s_rows[i] = row((uint32_t)i + 1U, fingerprint, "tx");
    }
    /* Volatile newest TX for A makes the oldest durable RX invisible to a
     * 16-row copy while still representing the same visible conversation. */
    s_rows[D1L_DM_STORE_CAPACITY] =
        row(17U, "aaaaaaaaaaaaaaaa", "tx");
    s_row_count = TEST_VISIBLE_DM_CAPACITY;
    s_contact_muted = true;

    assert(d1l_read_state_init() == ESP_OK);
    d1l_read_state_stats_t stats = d1l_read_state_stats();
    assert(stats.public_unread_count == 1U);
    assert(stats.newest_public_rx_seq == 10U);
    assert(s_last_dm_copy_max == TEST_VISIBLE_DM_CAPACITY);
    assert(stats.dm_unread_count == 0U);
    assert(stats.muted_dm_unread_count == 1U);
    assert(stats.dm_thread_count == 1U);
    assert(d1l_read_state_dm_entry_is_unread(&s_rows[0]));

    d1l_read_state_dm_thread_t threads[TEST_VISIBLE_DM_CAPACITY] = {0};
    assert(d1l_read_state_copy_dm_threads(
               threads, TEST_VISIBLE_DM_CAPACITY) == 1U);
    assert(threads[0].unread_count == 1U && threads[0].muted);

    assert(d1l_read_state_mark_dm_thread_read("aaaaaaaaaaaaaaaa") == ESP_OK);
    assert(s_last_dm_copy_max == TEST_VISIBLE_DM_CAPACITY);
    assert(!d1l_read_state_dm_entry_is_unread(&s_rows[0]));
    stats = d1l_read_state_stats();
    assert(stats.dm_unread_count == 0U);
    assert(stats.muted_dm_unread_count == 0U);
    assert(stats.mark_read_count == 1U);

    assert(d1l_read_state_mark_public_read() == ESP_OK);
    stats = d1l_read_state_stats();
    assert(stats.last_public_read_seq == 10U &&
           stats.public_unread_count == 0U);

    /* Public-only clear preserves private rows/global sequencing. An empty
     * exact-Public view is an idempotent mark-read operation: retaining the
     * monotonic cursor avoids reclassifying restored older rows after an SD
     * merge, while the next admitted Public row still receives a higher
     * shared-global sequence. */
    s_public_row_count = 0U;
    assert(d1l_read_state_mark_public_read() == ESP_OK);
    stats = d1l_read_state_stats();
    assert(stats.last_public_read_seq == 10U &&
           stats.public_unread_count == 0U);
    s_public_rows[0].seq = 100U;
    s_public_row_count = 1U;
    s_message_next_seq = 101U;
    stats = d1l_read_state_stats();
    assert(stats.newest_public_rx_seq == 100U &&
           stats.public_unread_count == 1U);

    puts("native read-state visible tail: ok");
    return 0;
}
