#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "mesh/read_state.h"
#include "nvs.h"

#define TEST_DM_ROWS 8U

static d1l_message_entry_t s_public_rows[4U];
static size_t s_public_count;
static d1l_dm_entry_t s_dm_rows[TEST_DM_ROWS];
static size_t s_dm_count;
static uint8_t s_nvs_blob[1024U];
static size_t s_nvs_blob_len;
static bool s_nvs_blob_valid;
static uint8_t s_nvs_pending_blob[1024U];
static size_t s_nvs_pending_blob_len;
static bool s_nvs_pending_blob_valid;
static bool s_fail_commit;

typedef struct {
    char fingerprint[D1L_NODE_FINGERPRINT_LEN];
    uint32_t last_read_seq;
} test_dm_cursor_t;

typedef struct {
    uint32_t schema;
    uint32_t last_public_read_seq;
    uint32_t last_dm_read_seq;
    uint32_t mark_read_count;
    uint32_t dm_cursor_count;
    test_dm_cursor_t dm_cursors[D1L_READ_STATE_DM_THREAD_CAPACITY];
} test_read_state_blob_t;

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
    if (!value || length > sizeof(s_nvs_pending_blob)) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(s_nvs_pending_blob, value, length);
    s_nvs_pending_blob_len = length;
    s_nvs_pending_blob_valid = true;
    return ESP_OK;
}

esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key)
{
    (void)handle;
    (void)key;
    s_nvs_blob_valid = false;
    s_nvs_blob_len = 0U;
    s_nvs_pending_blob_valid = false;
    s_nvs_pending_blob_len = 0U;
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    (void)handle;
    if (s_fail_commit) {
        s_nvs_pending_blob_valid = false;
        s_nvs_pending_blob_len = 0U;
        return ESP_FAIL;
    }
    if (s_nvs_pending_blob_valid) {
        memcpy(s_nvs_blob, s_nvs_pending_blob, s_nvs_pending_blob_len);
        s_nvs_blob_len = s_nvs_pending_blob_len;
        s_nvs_blob_valid = true;
        s_nvs_pending_blob_valid = false;
        s_nvs_pending_blob_len = 0U;
    }
    return ESP_OK;
}

d1l_message_store_stats_t d1l_message_store_stats(void)
{
    uint32_t next_seq = 1U;
    for (size_t i = 0U; i < s_public_count; ++i) {
        if (s_public_rows[i].seq >= next_seq) {
            next_seq = s_public_rows[i].seq + 1U;
        }
    }
    return (d1l_message_store_stats_t) {
        .next_seq = next_seq,
    };
}

size_t d1l_message_store_copy_recent(d1l_message_entry_t *out_entries,
                                     size_t max_entries)
{
    if (!out_entries || max_entries == 0U) {
        return 0U;
    }
    const size_t copied = s_public_count < max_entries ? s_public_count : max_entries;
    memcpy(out_entries, &s_public_rows[s_public_count - copied],
           copied * sizeof(out_entries[0]));
    return copied;
}

d1l_dm_store_stats_t d1l_dm_store_stats(void)
{
    uint32_t next_seq = 1U;
    for (size_t i = 0U; i < s_dm_count; ++i) {
        if (s_dm_rows[i].seq >= next_seq) {
            next_seq = s_dm_rows[i].seq + 1U;
        }
    }
    return (d1l_dm_store_stats_t) {
        .next_seq = next_seq,
    };
}

size_t d1l_dm_store_copy_recent(d1l_dm_entry_t *out_entries,
                                size_t max_entries)
{
    if (!out_entries || max_entries == 0U) {
        return 0U;
    }
    const size_t copied = s_dm_count < max_entries ? s_dm_count : max_entries;
    memcpy(out_entries, &s_dm_rows[s_dm_count - copied],
           copied * sizeof(out_entries[0]));
    return copied;
}

bool d1l_contact_store_find_by_fingerprint(const char *fingerprint,
                                           d1l_contact_entry_t *out_entry)
{
    if (!fingerprint) {
        return false;
    }
    if (out_entry) {
        memset(out_entry, 0, sizeof(*out_entry));
        out_entry->muted = strcmp(fingerprint, "bbbbbbbbbbbbbbbb") == 0;
    }
    return true;
}

static d1l_message_entry_t public_row(uint32_t seq, const char *direction)
{
    d1l_message_entry_t entry = {.seq = seq};
    snprintf(entry.direction, sizeof(entry.direction), "%s", direction);
    return entry;
}

static d1l_dm_entry_t dm_row(uint32_t seq, const char *fingerprint,
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
    s_public_rows[0] = public_row(10U, "rx");
    s_public_rows[1] = public_row(11U, "tx");
    s_public_count = 2U;

    s_dm_rows[0] = dm_row(1U, "aaaaaaaaaaaaaaaa", "rx");
    s_dm_rows[1] = dm_row(2U, "bbbbbbbbbbbbbbbb", "rx");
    s_dm_rows[2] = dm_row(3U, "aaaaaaaaaaaaaaaa", "rx");
    s_dm_rows[3] = dm_row(4U, "cccccccccccccccc", "rx");
    s_dm_rows[4] = dm_row(5U, "aaaaaaaaaaaaaaaa", "tx");
    s_dm_count = 5U;

    assert(d1l_read_state_init() == ESP_OK);
    d1l_read_state_stats_t stats = d1l_read_state_stats();
    assert(stats.public_unread_count == 1U);
    assert(stats.dm_unread_count == 3U);
    assert(stats.muted_dm_unread_count == 1U);
    assert(stats.mark_read_count == 0U);

    s_fail_commit = true;
    assert(d1l_read_state_mark_public_read() == ESP_FAIL);
    stats = d1l_read_state_stats();
    assert(stats.public_unread_count == 1U);
    assert(stats.dm_unread_count == 3U);
    assert(stats.muted_dm_unread_count == 1U);
    assert(stats.mark_read_count == 0U);
    s_fail_commit = false;
    assert(d1l_read_state_mark_public_read() == ESP_OK);
    stats = d1l_read_state_stats();
    assert(stats.public_unread_count == 0U);
    assert(stats.dm_unread_count == 3U);
    assert(stats.muted_dm_unread_count == 1U);
    assert(stats.mark_read_count == 1U);
    assert(d1l_read_state_mark_public_read() == ESP_OK);
    assert(d1l_read_state_stats().mark_read_count == 1U);

    s_fail_commit = true;
    assert(d1l_read_state_mark_dm_thread_read("aaaaaaaaaaaaaaaa") == ESP_FAIL);
    stats = d1l_read_state_stats();
    assert(stats.dm_unread_count == 3U);
    assert(stats.muted_dm_unread_count == 1U);
    assert(stats.mark_read_count == 1U);
    assert(d1l_read_state_dm_entry_is_unread(&s_dm_rows[0]));
    assert(d1l_read_state_dm_entry_is_unread(&s_dm_rows[2]));
    s_fail_commit = false;
    assert(d1l_read_state_mark_dm_thread_read("aaaaaaaaaaaaaaaa") == ESP_OK);
    stats = d1l_read_state_stats();
    assert(stats.dm_unread_count == 1U);
    assert(stats.muted_dm_unread_count == 1U);
    assert(stats.mark_read_count == 2U);
    assert(!d1l_read_state_dm_entry_is_unread(&s_dm_rows[0]));
    assert(!d1l_read_state_dm_entry_is_unread(&s_dm_rows[2]));
    assert(d1l_read_state_dm_entry_is_unread(&s_dm_rows[1]));
    assert(d1l_read_state_dm_entry_is_unread(&s_dm_rows[3]));
    assert(d1l_read_state_mark_dm_thread_read("aaaaaaaaaaaaaaaa") == ESP_OK);
    assert(d1l_read_state_stats().mark_read_count == 2U);

    /* A new incoming row re-opens only A's exact notification. */
    s_dm_rows[s_dm_count++] = dm_row(6U, "aaaaaaaaaaaaaaaa", "rx");
    stats = d1l_read_state_stats();
    assert(stats.dm_unread_count == 2U);
    assert(stats.muted_dm_unread_count == 1U);
    assert(d1l_read_state_dm_entry_is_unread(&s_dm_rows[5]));

    /* A shared prefix is not an identity and cannot clear any cursor. */
    assert(d1l_read_state_mark_dm_thread_read("aaaaaaaaaaaaaaa") ==
           ESP_ERR_NOT_FOUND);
    stats = d1l_read_state_stats();
    assert(stats.dm_unread_count == 2U);
    assert(stats.muted_dm_unread_count == 1U);
    assert(stats.mark_read_count == 2U);

    assert(d1l_read_state_mark_dm_thread_read("bbbbbbbbbbbbbbbb") == ESP_OK);
    stats = d1l_read_state_stats();
    assert(stats.dm_unread_count == 2U);
    assert(stats.muted_dm_unread_count == 0U);
    assert(stats.mark_read_count == 3U);
    assert(d1l_read_state_dm_entry_is_unread(&s_dm_rows[3]));

    /* Explicit store generation resets must not let an old high cursor hide a
     * newly retained seq=1 event. */
    s_public_rows[0] = public_row(1U, "rx");
    s_public_count = 1U;
    s_dm_rows[0] = dm_row(1U, "aaaaaaaaaaaaaaaa", "rx");
    s_dm_count = 1U;
    stats = d1l_read_state_stats();
    assert(stats.public_unread_count == 1U);
    assert(stats.dm_unread_count == 1U);
    assert(d1l_read_state_dm_entry_is_unread(&s_dm_rows[0]));
    assert(d1l_read_state_mark_public_read() == ESP_OK);
    assert(d1l_read_state_mark_dm_thread_read("aaaaaaaaaaaaaaaa") == ESP_OK);
    stats = d1l_read_state_stats();
    assert(stats.public_unread_count == 0U);
    assert(stats.dm_unread_count == 0U);
    assert(stats.mark_read_count == 5U);

    /* Cursor revision is diagnostic metadata and must saturate, never wrap. */
    assert(s_nvs_blob_len == sizeof(test_read_state_blob_t));
    test_read_state_blob_t saturated = {0};
    memcpy(&saturated, s_nvs_blob, sizeof(saturated));
    saturated.mark_read_count = UINT32_MAX;
    memcpy(s_nvs_blob, &saturated, sizeof(saturated));
    assert(d1l_read_state_init() == ESP_OK);
    s_public_rows[1] = public_row(2U, "rx");
    s_public_count = 2U;
    assert(d1l_read_state_stats().public_unread_count == 1U);
    assert(d1l_read_state_mark_public_read() == ESP_OK);
    stats = d1l_read_state_stats();
    assert(stats.public_unread_count == 0U);
    assert(stats.mark_read_count == UINT32_MAX);

    puts("native read-state cursor invariants: ok");
    return 0;
}
