#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hal/rp2040_bridge.h"
#include "esp_partition.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "storage/retained_blob_store.h"

#define TEST_RETAINED_PARTITION "d1l_retained"
#define TEST_RETAINED_META_PARTITION "d1l_ret_meta"
#define TEST_RETAINED_META_SIZE 0x1000U
#define TEST_RETAINED_NVS_SIZE 0x1f000U
#define TEST_RETAINED_META_MAGIC 0x44314C52U
#define TEST_RETAINED_META_VERSION 1U
#define TEST_NVS_ENTRY_COUNT 8U
#define TEST_NVS_HANDLE_COUNT 4U
#define TEST_NVS_NAME_MAX 31U
#define TEST_NVS_BLOB_MAX 128U
#define TEST_NVS_EVENT_COUNT 128U

typedef enum {
    TEST_NVS_EVENT_INIT_PARTITION = 0,
    TEST_NVS_EVENT_OPEN_DEDICATED,
    TEST_NVS_EVENT_OPEN_LEGACY,
    TEST_NVS_EVENT_GET_DEDICATED,
    TEST_NVS_EVENT_GET_LEGACY,
    TEST_NVS_EVENT_SET_DEDICATED,
    TEST_NVS_EVENT_SET_LEGACY,
    TEST_NVS_EVENT_ERASE_DEDICATED,
    TEST_NVS_EVENT_ERASE_LEGACY,
    TEST_NVS_EVENT_COMMIT_DEDICATED,
    TEST_NVS_EVENT_COMMIT_LEGACY,
    TEST_NVS_EVENT_FIND_META,
    TEST_NVS_EVENT_FIND_RETAINED,
    TEST_NVS_EVENT_READ_META,
    TEST_NVS_EVENT_READ_RETAINED,
    TEST_NVS_EVENT_ERASE_META,
    TEST_NVS_EVENT_ERASE_RETAINED,
    TEST_NVS_EVENT_WRITE_META,
} test_nvs_event_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t magic_inverse;
    uint32_t version_inverse;
} test_retained_marker_t;

typedef enum {
    TEST_NVS_PENDING_NONE = 0,
    TEST_NVS_PENDING_SET,
    TEST_NVS_PENDING_ERASE,
} test_nvs_pending_t;

typedef struct {
    bool used;
    bool dedicated;
    char namespace_name[TEST_NVS_NAME_MAX + 1U];
    char key[TEST_NVS_NAME_MAX + 1U];
    uint8_t blob[TEST_NVS_BLOB_MAX];
    size_t blob_len;
} test_nvs_entry_t;

typedef struct {
    bool active;
    bool dedicated;
    char namespace_name[TEST_NVS_NAME_MAX + 1U];
    test_nvs_pending_t pending;
    char pending_key[TEST_NVS_NAME_MAX + 1U];
    uint8_t pending_blob[TEST_NVS_BLOB_MAX];
    size_t pending_blob_len;
} test_nvs_handle_t;

static bool s_toggle_backend_during_write;
static bool s_toggle_backend_during_delete;
static uint32_t s_rename_count;
static uint32_t s_delete_count;
static bool s_nvs_enabled;
static esp_err_t s_partition_init_result = ESP_OK;
static esp_err_t s_dedicated_commit_result = ESP_OK;
static esp_err_t s_legacy_commit_result = ESP_OK;
static char s_init_partition[TEST_NVS_NAME_MAX + 1U];
static test_nvs_entry_t s_nvs_entries[TEST_NVS_ENTRY_COUNT];
static test_nvs_handle_t s_nvs_handles[TEST_NVS_HANDLE_COUNT];
static test_nvs_event_t s_nvs_events[TEST_NVS_EVENT_COUNT];
static size_t s_nvs_event_count;
static uint8_t s_retained_meta[TEST_RETAINED_META_SIZE];
static bool s_retained_partition_erased;
static size_t s_retained_partition_read_count;
static const esp_partition_t s_meta_partition = {
    .type = ESP_PARTITION_TYPE_DATA,
    .subtype = 0x40U,
    .address = 0x7e0000U,
    .size = TEST_RETAINED_META_SIZE,
    .label = TEST_RETAINED_META_PARTITION,
};
static const esp_partition_t s_retained_partition = {
    .type = ESP_PARTITION_TYPE_DATA,
    .subtype = ESP_PARTITION_SUBTYPE_DATA_NVS,
    .address = 0x7e1000U,
    .size = TEST_RETAINED_NVS_SIZE,
    .label = TEST_RETAINED_PARTITION,
};

static test_retained_marker_t valid_retained_marker(void)
{
    const test_retained_marker_t marker = {
        .magic = TEST_RETAINED_META_MAGIC,
        .version = TEST_RETAINED_META_VERSION,
        .magic_inverse = ~TEST_RETAINED_META_MAGIC,
        .version_inverse = ~TEST_RETAINED_META_VERSION,
    };
    return marker;
}

static void seed_valid_retained_markers(void)
{
    const test_retained_marker_t marker = valid_retained_marker();
    memset(s_retained_meta, 0xff, sizeof(s_retained_meta));
    memcpy(s_retained_meta, &marker, sizeof(marker));
    memcpy(s_retained_meta + sizeof(s_retained_meta) - sizeof(marker),
           &marker, sizeof(marker));
}

static void copy_name(char *dst, const char *src)
{
    assert(dst);
    assert(src);
    assert(strlen(src) <= TEST_NVS_NAME_MAX);
    strcpy(dst, src);
}

static void note_nvs_event(test_nvs_event_t event)
{
    assert(s_nvs_event_count < TEST_NVS_EVENT_COUNT);
    s_nvs_events[s_nvs_event_count++] = event;
}

static size_t count_nvs_event(test_nvs_event_t event)
{
    size_t count = 0U;
    for (size_t i = 0; i < s_nvs_event_count; ++i) {
        if (s_nvs_events[i] == event) {
            count++;
        }
    }
    return count;
}

static size_t first_nvs_event(test_nvs_event_t event)
{
    for (size_t i = 0; i < s_nvs_event_count; ++i) {
        if (s_nvs_events[i] == event) {
            return i;
        }
    }
    return SIZE_MAX;
}

static test_nvs_entry_t *find_nvs_entry(bool dedicated,
                                        const char *namespace_name,
                                        const char *key)
{
    for (size_t i = 0; i < TEST_NVS_ENTRY_COUNT; ++i) {
        test_nvs_entry_t *entry = &s_nvs_entries[i];
        if (entry->used && entry->dedicated == dedicated &&
            strcmp(entry->namespace_name, namespace_name) == 0 &&
            strcmp(entry->key, key) == 0) {
            return entry;
        }
    }
    return NULL;
}

static test_nvs_entry_t *allocate_nvs_entry(bool dedicated,
                                            const char *namespace_name,
                                            const char *key)
{
    test_nvs_entry_t *entry = find_nvs_entry(dedicated, namespace_name, key);
    if (entry) {
        return entry;
    }
    for (size_t i = 0; i < TEST_NVS_ENTRY_COUNT; ++i) {
        entry = &s_nvs_entries[i];
        if (!entry->used) {
            entry->used = true;
            entry->dedicated = dedicated;
            copy_name(entry->namespace_name, namespace_name);
            copy_name(entry->key, key);
            return entry;
        }
    }
    assert(false && "native NVS entry capacity exhausted");
    return NULL;
}

static void seed_nvs_blob(bool dedicated, const char *namespace_name,
                          const char *key, const void *blob, size_t blob_len)
{
    assert(blob);
    assert(blob_len <= TEST_NVS_BLOB_MAX);
    test_nvs_entry_t *entry = allocate_nvs_entry(dedicated, namespace_name, key);
    memcpy(entry->blob, blob, blob_len);
    entry->blob_len = blob_len;
    if (dedicated) {
        s_retained_partition_erased = false;
    }
}

static void assert_nvs_blob(bool dedicated, const char *namespace_name,
                            const char *key, const void *blob, size_t blob_len)
{
    test_nvs_entry_t *entry = find_nvs_entry(dedicated, namespace_name, key);
    assert(entry);
    assert(entry->blob_len == blob_len);
    assert(memcmp(entry->blob, blob, blob_len) == 0);
}

static void clear_nvs_case(void)
{
    memset(s_nvs_entries, 0, sizeof(s_nvs_entries));
    memset(s_nvs_handles, 0, sizeof(s_nvs_handles));
    memset(s_nvs_events, 0, sizeof(s_nvs_events));
    s_nvs_event_count = 0U;
    s_dedicated_commit_result = ESP_OK;
    s_legacy_commit_result = ESP_OK;
    s_partition_init_result = ESP_OK;
    s_retained_partition_erased = false;
    s_retained_partition_read_count = 0U;
    seed_valid_retained_markers();
}

const esp_partition_t *esp_partition_find_first(
    esp_partition_type_t type,
    esp_partition_subtype_t subtype,
    const char *label)
{
    if (type != ESP_PARTITION_TYPE_DATA || !label) {
        return NULL;
    }
    if (strcmp(label, TEST_RETAINED_META_PARTITION) == 0 &&
        (subtype == ESP_PARTITION_SUBTYPE_ANY || subtype == 0x40U)) {
        note_nvs_event(TEST_NVS_EVENT_FIND_META);
        return &s_meta_partition;
    }
    if (strcmp(label, TEST_RETAINED_PARTITION) == 0 &&
        (subtype == ESP_PARTITION_SUBTYPE_ANY ||
         subtype == ESP_PARTITION_SUBTYPE_DATA_NVS)) {
        note_nvs_event(TEST_NVS_EVENT_FIND_RETAINED);
        return &s_retained_partition;
    }
    return NULL;
}

esp_err_t esp_partition_read(const esp_partition_t *partition,
                             size_t src_offset,
                             void *dst,
                             size_t size)
{
    if (!partition || !dst || src_offset > partition->size ||
        size > partition->size - src_offset) {
        return ESP_ERR_INVALID_ARG;
    }
    if (partition == &s_meta_partition) {
        note_nvs_event(TEST_NVS_EVENT_READ_META);
        memcpy(dst, s_retained_meta + src_offset, size);
        return ESP_OK;
    }
    if (partition == &s_retained_partition) {
        if (s_retained_partition_read_count++ == 0U) {
            note_nvs_event(TEST_NVS_EVENT_READ_RETAINED);
        }
        memset(dst, s_retained_partition_erased ? 0xff : 0x00, size);
        return ESP_OK;
    }
    return ESP_ERR_INVALID_ARG;
}

esp_err_t esp_partition_write(const esp_partition_t *partition,
                              size_t dst_offset,
                              const void *src,
                              size_t size)
{
    if (partition != &s_meta_partition || !src ||
        dst_offset > sizeof(s_retained_meta) ||
        size > sizeof(s_retained_meta) - dst_offset) {
        return ESP_ERR_INVALID_ARG;
    }
    note_nvs_event(TEST_NVS_EVENT_WRITE_META);
    memcpy(s_retained_meta + dst_offset, src, size);
    return ESP_OK;
}

esp_err_t esp_partition_erase_range(const esp_partition_t *partition,
                                    size_t offset,
                                    size_t size)
{
    if (!partition || offset != 0U || size != partition->size) {
        return ESP_ERR_INVALID_ARG;
    }
    if (partition == &s_meta_partition) {
        note_nvs_event(TEST_NVS_EVENT_ERASE_META);
        memset(s_retained_meta, 0xff, sizeof(s_retained_meta));
        return ESP_OK;
    }
    if (partition == &s_retained_partition) {
        note_nvs_event(TEST_NVS_EVENT_ERASE_RETAINED);
        for (size_t i = 0U; i < TEST_NVS_ENTRY_COUNT; ++i) {
            if (s_nvs_entries[i].used && s_nvs_entries[i].dedicated) {
                memset(&s_nvs_entries[i], 0, sizeof(s_nvs_entries[i]));
            }
        }
        return ESP_OK;
    }
    return ESP_ERR_INVALID_ARG;
}

static esp_err_t open_nvs_handle(bool dedicated, const char *namespace_name,
                                 nvs_open_mode_t open_mode,
                                 nvs_handle_t *out_handle)
{
    if (!s_nvs_enabled) {
        return ESP_FAIL;
    }
    if (!namespace_name || !out_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    note_nvs_event(dedicated ? TEST_NVS_EVENT_OPEN_DEDICATED
                             : TEST_NVS_EVENT_OPEN_LEGACY);
    if (open_mode == NVS_READONLY) {
        bool namespace_found = false;
        for (size_t i = 0U; i < TEST_NVS_ENTRY_COUNT; ++i) {
            namespace_found = namespace_found ||
                (s_nvs_entries[i].used &&
                 s_nvs_entries[i].dedicated == dedicated &&
                 strcmp(s_nvs_entries[i].namespace_name,
                        namespace_name) == 0);
        }
        if (!namespace_found) {
            return ESP_ERR_NVS_NOT_FOUND;
        }
    }
    for (size_t i = 0; i < TEST_NVS_HANDLE_COUNT; ++i) {
        test_nvs_handle_t *handle = &s_nvs_handles[i];
        if (!handle->active) {
            memset(handle, 0, sizeof(*handle));
            handle->active = true;
            handle->dedicated = dedicated;
            copy_name(handle->namespace_name, namespace_name);
            *out_handle = (nvs_handle_t)(i + 1U);
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

static test_nvs_handle_t *get_nvs_handle(nvs_handle_t handle)
{
    if (handle == 0U || handle > TEST_NVS_HANDLE_COUNT) {
        return NULL;
    }
    test_nvs_handle_t *result = &s_nvs_handles[handle - 1U];
    return result->active ? result : NULL;
}

esp_err_t nvs_flash_init_partition(const char *partition_label)
{
    if (!s_nvs_enabled || !partition_label) {
        return ESP_FAIL;
    }
    copy_name(s_init_partition, partition_label);
    note_nvs_event(TEST_NVS_EVENT_INIT_PARTITION);
    if (s_partition_init_result == ESP_OK) {
        s_retained_partition_erased = false;
    }
    return s_partition_init_result;
}

esp_err_t nvs_open(const char *namespace_name, nvs_open_mode_t open_mode,
                   nvs_handle_t *out_handle)
{
    assert(open_mode == NVS_READONLY || open_mode == NVS_READWRITE);
    return open_nvs_handle(false, namespace_name, open_mode, out_handle);
}

esp_err_t nvs_open_from_partition(const char *part_name,
                                  const char *namespace_name,
                                  nvs_open_mode_t open_mode,
                                  nvs_handle_t *out_handle)
{
    assert(open_mode == NVS_READONLY || open_mode == NVS_READWRITE);
    if (!part_name || strcmp(part_name, TEST_RETAINED_PARTITION) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return open_nvs_handle(true, namespace_name, open_mode, out_handle);
}

void nvs_close(nvs_handle_t handle)
{
    test_nvs_handle_t *context = get_nvs_handle(handle);
    assert(context);
    memset(context, 0, sizeof(*context));
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out_value,
                       size_t *length)
{
    test_nvs_handle_t *context = get_nvs_handle(handle);
    assert(context);
    assert(key);
    assert(length);
    note_nvs_event(context->dedicated ? TEST_NVS_EVENT_GET_DEDICATED
                                      : TEST_NVS_EVENT_GET_LEGACY);
    test_nvs_entry_t *entry = find_nvs_entry(
        context->dedicated, context->namespace_name, key);
    if (!entry) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (!out_value) {
        *length = entry->blob_len;
        return ESP_OK;
    }
    if (*length < entry->blob_len) {
        *length = entry->blob_len;
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    memcpy(out_value, entry->blob, entry->blob_len);
    *length = entry->blob_len;
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value,
                       size_t length)
{
    test_nvs_handle_t *context = get_nvs_handle(handle);
    assert(context);
    assert(key);
    assert(value);
    assert(length <= TEST_NVS_BLOB_MAX);
    note_nvs_event(context->dedicated ? TEST_NVS_EVENT_SET_DEDICATED
                                      : TEST_NVS_EVENT_SET_LEGACY);
    context->pending = TEST_NVS_PENDING_SET;
    copy_name(context->pending_key, key);
    memcpy(context->pending_blob, value, length);
    context->pending_blob_len = length;
    return ESP_OK;
}

esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key)
{
    test_nvs_handle_t *context = get_nvs_handle(handle);
    assert(context);
    assert(key);
    note_nvs_event(context->dedicated ? TEST_NVS_EVENT_ERASE_DEDICATED
                                      : TEST_NVS_EVENT_ERASE_LEGACY);
    if (!find_nvs_entry(context->dedicated, context->namespace_name, key)) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    context->pending = TEST_NVS_PENDING_ERASE;
    copy_name(context->pending_key, key);
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    test_nvs_handle_t *context = get_nvs_handle(handle);
    assert(context);
    note_nvs_event(context->dedicated ? TEST_NVS_EVENT_COMMIT_DEDICATED
                                      : TEST_NVS_EVENT_COMMIT_LEGACY);
    const esp_err_t configured_result = context->dedicated
                                            ? s_dedicated_commit_result
                                            : s_legacy_commit_result;
    if (configured_result != ESP_OK) {
        return configured_result;
    }
    if (context->pending == TEST_NVS_PENDING_SET) {
        test_nvs_entry_t *entry = allocate_nvs_entry(
            context->dedicated, context->namespace_name, context->pending_key);
        memcpy(entry->blob, context->pending_blob, context->pending_blob_len);
        entry->blob_len = context->pending_blob_len;
    } else if (context->pending == TEST_NVS_PENDING_ERASE) {
        test_nvs_entry_t *entry = find_nvs_entry(
            context->dedicated, context->namespace_name, context->pending_key);
        assert(entry);
        memset(entry, 0, sizeof(*entry));
    }
    context->pending = TEST_NVS_PENDING_NONE;
    return ESP_OK;
}

esp_err_t d1l_rp2040_bridge_file_stat(const char *path,
                                      d1l_rp2040_file_result_t *out_result,
                                      uint32_t timeout_ms)
{
    (void)path;
    (void)out_result;
    (void)timeout_ms;
    return ESP_FAIL;
}

esp_err_t d1l_rp2040_bridge_file_read(const char *path, uint32_t offset,
                                      uint8_t *out_data, size_t max_len,
                                      d1l_rp2040_file_result_t *out_result,
                                      uint32_t timeout_ms)
{
    (void)path;
    (void)offset;
    (void)out_data;
    (void)max_len;
    (void)out_result;
    (void)timeout_ms;
    return ESP_FAIL;
}

esp_err_t d1l_rp2040_bridge_file_write(const char *path, uint32_t offset,
                                       const uint8_t *data, size_t len,
                                       bool truncate,
                                       d1l_rp2040_file_result_t *out_result,
                                       uint32_t timeout_ms)
{
    (void)path;
    (void)offset;
    (void)data;
    (void)len;
    (void)truncate;
    assert(out_result);
    (void)timeout_ms;
    out_result->length = (uint32_t)len;
    if (s_toggle_backend_during_write) {
        s_toggle_backend_during_write = false;
        d1l_retained_blob_store_note_sd_backend(false, false, false,
                                                0U, 0U, 0U);
        d1l_retained_blob_store_note_sd_backend(
            true, true, true, D1L_RP2040_FILE_LINE_MAX,
            D1L_RP2040_FILE_CHUNK_MAX, D1L_RP2040_FILE_PATH_MAX);
    }
    return ESP_OK;
}

esp_err_t d1l_rp2040_bridge_file_delete(const char *path,
                                        d1l_rp2040_file_result_t *out_result,
                                        uint32_t timeout_ms)
{
    (void)path;
    (void)out_result;
    (void)timeout_ms;
    s_delete_count++;
    if (s_toggle_backend_during_delete) {
        s_toggle_backend_during_delete = false;
        d1l_retained_blob_store_note_sd_backend(false, false, false,
                                                0U, 0U, 0U);
        d1l_retained_blob_store_note_sd_backend(
            true, true, true, D1L_RP2040_FILE_LINE_MAX,
            D1L_RP2040_FILE_CHUNK_MAX, D1L_RP2040_FILE_PATH_MAX);
    }
    return ESP_OK;
}

esp_err_t d1l_rp2040_bridge_file_rename(const char *from_path, const char *to_path,
                                        bool replace,
                                        d1l_rp2040_file_result_t *out_result,
                                        uint32_t timeout_ms)
{
    (void)from_path;
    (void)to_path;
    (void)replace;
    (void)out_result;
    (void)timeout_ms;
    s_rename_count++;
    return ESP_OK;
}

static d1l_retained_blob_store_backend_state_t state_for(
    d1l_retained_blob_store_id_t store_id)
{
    d1l_retained_blob_store_backend_state_t state = {0};
    assert(d1l_retained_blob_store_backend_state(store_id, &state));
    return state;
}

static void assert_all_states(bool enabled, uint32_t generation)
{
    for (int id = 0; id < D1L_RETAINED_BLOB_STORE_COUNT; ++id) {
        const d1l_retained_blob_store_backend_state_t state =
            state_for((d1l_retained_blob_store_id_t)id);
        assert(state.enabled == enabled);
        assert(state.generation == generation);
        assert(d1l_retained_blob_store_uses_sd(
                   (d1l_retained_blob_store_id_t)id) == enabled);
    }
}

static void test_retained_partition_init(void)
{
    s_nvs_enabled = true;
    clear_nvs_case();
    memset(s_init_partition, 0, sizeof(s_init_partition));

    assert(d1l_retained_blob_store_init() == ESP_OK);
    assert(strcmp(s_init_partition, TEST_RETAINED_PARTITION) == 0);
    assert(count_nvs_event(TEST_NVS_EVENT_INIT_PARTITION) == 1U);
    assert(d1l_retained_blob_store_nvs_marker_ready());
    assert(!d1l_retained_blob_store_nvs_initialized_this_boot());
}

static void test_first_use_initializes_only_new_retained_region(void)
{
    static const char legacy_blob[] = "legacy survives first use";

    clear_nvs_case();
    memset(s_retained_meta, 0xff, sizeof(s_retained_meta));
    s_retained_partition_erased = true;
    seed_nvs_blob(false, "d1l_messages", "public",
                  legacy_blob, sizeof(legacy_blob));

    assert(d1l_retained_blob_store_init() == ESP_OK);
    assert(d1l_retained_blob_store_nvs_marker_ready());
    assert(d1l_retained_blob_store_nvs_initialized_this_boot());
    assert(count_nvs_event(TEST_NVS_EVENT_ERASE_RETAINED) == 0U);
    assert(count_nvs_event(TEST_NVS_EVENT_ERASE_META) == 0U);
    assert(count_nvs_event(TEST_NVS_EVENT_READ_RETAINED) > 0U);
    assert(count_nvs_event(TEST_NVS_EVENT_WRITE_META) == 2U);
    assert(first_nvs_event(TEST_NVS_EVENT_READ_RETAINED) <
           first_nvs_event(TEST_NVS_EVENT_WRITE_META));
    assert(first_nvs_event(TEST_NVS_EVENT_WRITE_META) <
           first_nvs_event(TEST_NVS_EVENT_INIT_PARTITION));
    assert(first_nvs_event(TEST_NVS_EVENT_INIT_PARTITION) <
           first_nvs_event(TEST_NVS_EVENT_SET_DEDICATED));
    assert_nvs_blob(true, "d1l_messages", "public",
                    legacy_blob, sizeof(legacy_blob));
    assert(find_nvs_entry(false, "d1l_messages", "public") == NULL);
}

static void test_first_marker_recovers_init_power_loss_window(void)
{
    const test_retained_marker_t marker = valid_retained_marker();

    clear_nvs_case();
    memset(s_retained_meta, 0xff, sizeof(s_retained_meta));
    memcpy(s_retained_meta, &marker, sizeof(marker));
    s_retained_partition_erased = true;

    assert(d1l_retained_blob_store_init() == ESP_OK);
    assert(d1l_retained_blob_store_nvs_marker_ready());
    assert(d1l_retained_blob_store_nvs_ready());
    assert(count_nvs_event(TEST_NVS_EVENT_INIT_PARTITION) == 1U);
    assert(count_nvs_event(TEST_NVS_EVENT_ERASE_RETAINED) == 0U);
    assert(count_nvs_event(TEST_NVS_EVENT_WRITE_META) == 1U);
    test_retained_marker_t repaired = {0};
    memcpy(&repaired,
           s_retained_meta + sizeof(s_retained_meta) - sizeof(repaired),
           sizeof(repaired));
    assert(repaired.magic == TEST_RETAINED_META_MAGIC);
    assert(repaired.version == TEST_RETAINED_META_VERSION);
}

static void test_ambiguous_or_future_marker_never_erases_retained_data(void)
{
    static const char dedicated_blob[] = "must survive marker failure";
    const test_retained_marker_t valid = valid_retained_marker();
    const size_t last_offset = sizeof(s_retained_meta) - sizeof(valid);

    clear_nvs_case();
    seed_nvs_blob(true, "d1l_messages", "public",
                  dedicated_blob, sizeof(dedicated_blob));
    memset(s_retained_meta, 0xff, sizeof(s_retained_meta));
    assert(d1l_retained_blob_store_init() == ESP_ERR_INVALID_STATE);
    assert(count_nvs_event(TEST_NVS_EVENT_INIT_PARTITION) == 0U);
    assert(count_nvs_event(TEST_NVS_EVENT_ERASE_RETAINED) == 0U);
    assert_nvs_blob(true, "d1l_messages", "public",
                    dedicated_blob, sizeof(dedicated_blob));

    clear_nvs_case();
    seed_nvs_blob(true, "d1l_messages", "public",
                  dedicated_blob, sizeof(dedicated_blob));
    test_retained_marker_t corrupt = valid;
    corrupt.magic ^= 1U;
    memcpy(s_retained_meta, &corrupt, sizeof(corrupt));
    memcpy(s_retained_meta + last_offset, &corrupt, sizeof(corrupt));
    assert(d1l_retained_blob_store_init() == ESP_ERR_INVALID_STATE);
    assert(count_nvs_event(TEST_NVS_EVENT_INIT_PARTITION) == 0U);
    assert(count_nvs_event(TEST_NVS_EVENT_ERASE_RETAINED) == 0U);
    assert_nvs_blob(true, "d1l_messages", "public",
                    dedicated_blob, sizeof(dedicated_blob));

    clear_nvs_case();
    seed_nvs_blob(true, "d1l_messages", "public",
                  dedicated_blob, sizeof(dedicated_blob));
    test_retained_marker_t future = valid;
    future.version = 2U;
    future.version_inverse = ~future.version;
    memcpy(s_retained_meta, &future, sizeof(future));
    memcpy(s_retained_meta + last_offset, &future, sizeof(future));
    assert(d1l_retained_blob_store_init() == ESP_ERR_INVALID_STATE);
    assert(count_nvs_event(TEST_NVS_EVENT_INIT_PARTITION) == 0U);
    assert(count_nvs_event(TEST_NVS_EVENT_ERASE_RETAINED) == 0U);
    assert_nvs_blob(true, "d1l_messages", "public",
                    dedicated_blob, sizeof(dedicated_blob));

    clear_nvs_case();
    seed_nvs_blob(true, "d1l_messages", "public",
                  dedicated_blob, sizeof(dedicated_blob));
    memcpy(s_retained_meta, &valid, sizeof(valid));
    test_retained_marker_t mixed_future = valid;
    mixed_future.version = 2U;
    mixed_future.version_inverse = ~mixed_future.version;
    memcpy(s_retained_meta + last_offset,
           &mixed_future, sizeof(mixed_future));
    assert(d1l_retained_blob_store_init() == ESP_ERR_INVALID_STATE);
    assert(count_nvs_event(TEST_NVS_EVENT_INIT_PARTITION) == 0U);
    assert(count_nvs_event(TEST_NVS_EVENT_ERASE_RETAINED) == 0U);
    assert_nvs_blob(true, "d1l_messages", "public",
                    dedicated_blob, sizeof(dedicated_blob));
}

static void test_partition_init_proactively_migrates_known_legacy_key(void)
{
    static const char legacy_blob[] = "boot-time public history";

    clear_nvs_case();
    seed_nvs_blob(false, "d1l_messages", "public",
                  legacy_blob, sizeof(legacy_blob));
    assert(d1l_retained_blob_store_init() == ESP_OK);
    assert(d1l_retained_blob_store_nvs_ready());
    assert(d1l_retained_blob_store_nvs_error() == ESP_OK);
    assert(d1l_retained_blob_store_nvs_migration_error() == ESP_OK);
    assert(d1l_retained_blob_store_nvs_migrated_keys() == 1U);
    assert_nvs_blob(true, "d1l_messages", "public",
                    legacy_blob, sizeof(legacy_blob));
    assert(find_nvs_entry(false, "d1l_messages", "public") == NULL);
    assert(first_nvs_event(TEST_NVS_EVENT_COMMIT_DEDICATED) <
           first_nvs_event(TEST_NVS_EVENT_ERASE_LEGACY));
}

static void test_divergent_upgrade_copies_fail_closed(void)
{
    static const char dedicated_blob[] = "candidate retained value";
    static const char legacy_blob[] = "newer downgrade value";
    char readback[64] = {0};
    size_t readback_len = sizeof(readback);

    clear_nvs_case();
    d1l_retained_blob_store_note_sd_backend(false, false, false,
                                             0U, 0U, 0U);
    seed_nvs_blob(true, "d1l_messages", "public",
                  dedicated_blob, sizeof(dedicated_blob));
    seed_nvs_blob(false, "d1l_messages", "public",
                  legacy_blob, sizeof(legacy_blob));

    assert(d1l_retained_blob_store_init() == ESP_ERR_INVALID_STATE);
    assert(!d1l_retained_blob_store_nvs_ready());
    assert(d1l_retained_blob_store_nvs_error() == ESP_OK);
    assert(d1l_retained_blob_store_nvs_migration_error() ==
           ESP_ERR_INVALID_STATE);
    assert(strcmp(d1l_retained_blob_store_backend_name(
                      D1L_RETAINED_BLOB_STORE_PUBLIC_MESSAGES),
                  "unavailable") == 0);
    assert(!d1l_retained_blob_store_is_available(
        D1L_RETAINED_BLOB_STORE_PUBLIC_MESSAGES));
    assert_nvs_blob(true, "d1l_messages", "public",
                    dedicated_blob, sizeof(dedicated_blob));
    assert_nvs_blob(false, "d1l_messages", "public",
                    legacy_blob, sizeof(legacy_blob));
    assert(d1l_retained_blob_store_read_nvs_fallback(
               D1L_RETAINED_BLOB_STORE_PUBLIC_MESSAGES, "public",
               readback, &readback_len) == ESP_ERR_INVALID_STATE);
    assert(count_nvs_event(TEST_NVS_EVENT_ERASE_LEGACY) == 0U);

    clear_nvs_case();
    seed_nvs_blob(true, "d1l_messages", "public",
                  dedicated_blob, sizeof(dedicated_blob));
    seed_nvs_blob(false, "d1l_messages", "public",
                  dedicated_blob, sizeof(dedicated_blob));
    assert(d1l_retained_blob_store_init() == ESP_OK);
    assert(d1l_retained_blob_store_nvs_ready());
    assert(strcmp(d1l_retained_blob_store_backend_name(
                      D1L_RETAINED_BLOB_STORE_PUBLIC_MESSAGES),
                  "nvs") == 0);
    assert(d1l_retained_blob_store_is_available(
        D1L_RETAINED_BLOB_STORE_PUBLIC_MESSAGES));
    assert(find_nvs_entry(false, "d1l_messages", "public") == NULL);
}

static void test_dedicated_read_precedes_legacy(void)
{
    static const char dedicated_blob[] = "dedicated routes";
    static const char legacy_blob[] = "stale legacy routes";
    char readback[64] = {0};
    size_t readback_len = sizeof(readback);

    clear_nvs_case();
    seed_nvs_blob(true, "d1l_routes", "routes_v2",
                  dedicated_blob, sizeof(dedicated_blob));
    seed_nvs_blob(false, "d1l_routes", "routes_v2",
                  legacy_blob, sizeof(legacy_blob));

    assert(d1l_retained_blob_store_read_nvs_fallback(
               D1L_RETAINED_BLOB_STORE_ROUTES, "routes_v2",
               readback, &readback_len) == ESP_OK);
    assert(readback_len == sizeof(dedicated_blob));
    assert(memcmp(readback, dedicated_blob, sizeof(dedicated_blob)) == 0);
    assert(first_nvs_event(TEST_NVS_EVENT_OPEN_DEDICATED) == 0U);
    assert(count_nvs_event(TEST_NVS_EVENT_GET_DEDICATED) == 1U);
    assert(count_nvs_event(TEST_NVS_EVENT_OPEN_LEGACY) == 0U);
    assert_nvs_blob(false, "d1l_routes", "routes_v2",
                    legacy_blob, sizeof(legacy_blob));
}

static void test_legacy_read_migrates_after_dedicated_miss(void)
{
    static const char legacy_blob[] = "legacy packet ring";
    char readback[64] = {0};
    size_t readback_len = sizeof(readback);

    clear_nvs_case();
    seed_nvs_blob(false, "d1l_packets", "ring",
                  legacy_blob, sizeof(legacy_blob));

    assert(d1l_retained_blob_store_read_nvs_fallback(
               D1L_RETAINED_BLOB_STORE_PACKET_LOG, "ring",
               readback, &readback_len) == ESP_OK);
    assert(readback_len == sizeof(legacy_blob));
    assert(memcmp(readback, legacy_blob, sizeof(legacy_blob)) == 0);
    assert_nvs_blob(true, "d1l_packets", "ring",
                    legacy_blob, sizeof(legacy_blob));
    assert(find_nvs_entry(false, "d1l_packets", "ring") == NULL);

    const size_t dedicated_open = first_nvs_event(TEST_NVS_EVENT_OPEN_DEDICATED);
    const size_t legacy_open = first_nvs_event(TEST_NVS_EVENT_OPEN_LEGACY);
    const size_t legacy_get = first_nvs_event(TEST_NVS_EVENT_GET_LEGACY);
    const size_t dedicated_set = first_nvs_event(TEST_NVS_EVENT_SET_DEDICATED);
    const size_t dedicated_commit =
        first_nvs_event(TEST_NVS_EVENT_COMMIT_DEDICATED);
    const size_t legacy_erase = first_nvs_event(TEST_NVS_EVENT_ERASE_LEGACY);
    assert(dedicated_open < legacy_open);
    assert(legacy_open <= legacy_get);
    assert(legacy_get < dedicated_set);
    assert(dedicated_set < dedicated_commit);
    assert(dedicated_commit < legacy_erase);
    assert(count_nvs_event(TEST_NVS_EVENT_SET_LEGACY) == 0U);
}

static void test_dedicated_write_reclaims_legacy_only_after_commit(void)
{
    static const char legacy_blob[] = "old dms";
    static const char new_blob[] = "new dms";

    clear_nvs_case();
    seed_nvs_blob(false, "d1l_dms", "messages_v2",
                  legacy_blob, sizeof(legacy_blob));

    assert(d1l_retained_blob_store_write_nvs_fallback(
               D1L_RETAINED_BLOB_STORE_DM_MESSAGES, "messages_v2",
               new_blob, sizeof(new_blob)) == ESP_OK);
    assert_nvs_blob(true, "d1l_dms", "messages_v2",
                    new_blob, sizeof(new_blob));
    assert(find_nvs_entry(false, "d1l_dms", "messages_v2") == NULL);
    assert(first_nvs_event(TEST_NVS_EVENT_OPEN_DEDICATED) == 0U);
    assert(first_nvs_event(TEST_NVS_EVENT_COMMIT_DEDICATED) <
           first_nvs_event(TEST_NVS_EVENT_ERASE_LEGACY));
    assert(count_nvs_event(TEST_NVS_EVENT_SET_LEGACY) == 0U);

    /* A failed dedicated commit must leave the only committed legacy copy
     * intact and must not even attempt the destructive legacy erase. */
    clear_nvs_case();
    seed_nvs_blob(false, "d1l_dms", "messages_v2",
                  legacy_blob, sizeof(legacy_blob));
    s_dedicated_commit_result = ESP_FAIL;
    assert(d1l_retained_blob_store_write_nvs_fallback(
               D1L_RETAINED_BLOB_STORE_DM_MESSAGES, "messages_v2",
               new_blob, sizeof(new_blob)) == ESP_FAIL);
    assert_nvs_blob(false, "d1l_dms", "messages_v2",
                    legacy_blob, sizeof(legacy_blob));
    assert(find_nvs_entry(true, "d1l_dms", "messages_v2") == NULL);
    assert(count_nvs_event(TEST_NVS_EVENT_ERASE_LEGACY) == 0U);
}

static void test_erase_clears_dedicated_and_legacy_copies(void)
{
    static const char dedicated_blob[] = "new public";
    static const char legacy_blob[] = "old public";

    clear_nvs_case();
    seed_nvs_blob(true, "d1l_messages", "messages_v2",
                  dedicated_blob, sizeof(dedicated_blob));
    seed_nvs_blob(false, "d1l_messages", "messages_v2",
                  legacy_blob, sizeof(legacy_blob));
    assert(d1l_retained_blob_store_erase_nvs_fallback(
               D1L_RETAINED_BLOB_STORE_PUBLIC_MESSAGES,
               "messages_v2") == ESP_OK);
    assert(find_nvs_entry(true, "d1l_messages", "messages_v2") == NULL);
    assert(find_nvs_entry(false, "d1l_messages", "messages_v2") == NULL);
    assert(count_nvs_event(TEST_NVS_EVENT_ERASE_DEDICATED) == 1U);
    assert(count_nvs_event(TEST_NVS_EVENT_ERASE_LEGACY) == 1U);
    assert(first_nvs_event(TEST_NVS_EVENT_ERASE_LEGACY) <
           first_nvs_event(TEST_NVS_EVENT_ERASE_DEDICATED));

    /* A missing dedicated copy is not a reason to strand a legacy blob. */
    clear_nvs_case();
    seed_nvs_blob(false, "d1l_messages", "messages_v2",
                  legacy_blob, sizeof(legacy_blob));
    assert(d1l_retained_blob_store_erase_nvs_fallback(
               D1L_RETAINED_BLOB_STORE_PUBLIC_MESSAGES,
               "messages_v2") == ESP_OK);
    assert(find_nvs_entry(false, "d1l_messages", "messages_v2") == NULL);
    assert(count_nvs_event(TEST_NVS_EVENT_ERASE_DEDICATED) == 0U);
    assert(count_nvs_event(TEST_NVS_EVENT_ERASE_LEGACY) == 1U);
}

static void test_partition_init_failure_never_falls_back_or_resurrects(void)
{
    static const char dedicated_blob[] = "dedicated current";
    static const char legacy_blob[] = "legacy stale";
    static const char replacement[] = "must not write";
    char readback[64] = {0};
    size_t readback_len = sizeof(readback);

    clear_nvs_case();
    seed_nvs_blob(true, "d1l_routes", "routes_v2",
                  dedicated_blob, sizeof(dedicated_blob));
    seed_nvs_blob(false, "d1l_routes", "routes_v2",
                  legacy_blob, sizeof(legacy_blob));
    s_partition_init_result = ESP_FAIL;
    assert(d1l_retained_blob_store_init() == ESP_FAIL);
    assert(!d1l_retained_blob_store_nvs_ready());
    assert(d1l_retained_blob_store_nvs_error() == ESP_FAIL);
    assert(d1l_retained_blob_store_read_nvs_fallback(
               D1L_RETAINED_BLOB_STORE_ROUTES, "routes_v2",
               readback, &readback_len) == ESP_FAIL);
    assert(d1l_retained_blob_store_write_nvs_fallback(
               D1L_RETAINED_BLOB_STORE_ROUTES, "routes_v2",
               replacement, sizeof(replacement)) == ESP_FAIL);
    assert(d1l_retained_blob_store_erase_nvs_fallback(
               D1L_RETAINED_BLOB_STORE_ROUTES, "routes_v2") == ESP_FAIL);
    assert_nvs_blob(true, "d1l_routes", "routes_v2",
                    dedicated_blob, sizeof(dedicated_blob));
    assert_nvs_blob(false, "d1l_routes", "routes_v2",
                    legacy_blob, sizeof(legacy_blob));

    s_partition_init_result = ESP_OK;
    seed_nvs_blob(false, "d1l_routes", "routes_v2",
                  dedicated_blob, sizeof(dedicated_blob));
    assert(d1l_retained_blob_store_init() == ESP_OK);
    readback_len = sizeof(readback);
    assert(d1l_retained_blob_store_read_nvs_fallback(
               D1L_RETAINED_BLOB_STORE_ROUTES, "routes_v2",
               readback, &readback_len) == ESP_OK);
    assert(readback_len == sizeof(dedicated_blob));
    assert(memcmp(readback, dedicated_blob, sizeof(dedicated_blob)) == 0);
}

int main(void)
{
    assert_all_states(false, 0U);

    d1l_retained_blob_store_backend_state_t invalid = {
        .enabled = true,
        .generation = 99U,
    };
    assert(!d1l_retained_blob_store_backend_state(
        D1L_RETAINED_BLOB_STORE_COUNT, &invalid));
    assert(invalid.enabled && invalid.generation == 99U);
    assert(!d1l_retained_blob_store_backend_state(
        D1L_RETAINED_BLOB_STORE_ROUTES, NULL));

    d1l_retained_blob_store_note_sd_backend(false, false, false, 0U, 0U, 0U);
    assert_all_states(false, 0U);

    d1l_retained_blob_store_note_sd_backend(
        true, true, true, D1L_RP2040_FILE_LINE_MAX,
        D1L_RP2040_FILE_CHUNK_MAX, D1L_RP2040_FILE_PATH_MAX);
    assert_all_states(true, 1U);

    d1l_retained_blob_store_note_sd_backend(
        true, true, true, D1L_RP2040_FILE_LINE_MAX,
        D1L_RP2040_FILE_CHUNK_MAX, D1L_RP2040_FILE_PATH_MAX);
    assert_all_states(true, 1U);

    /* A complete false -> true cycle remains visible to a later consumer even
     * when it never sampled the intermediate disabled state. */
    d1l_retained_blob_store_note_sd_backend(false, false, false, 0U, 0U, 0U);
    d1l_retained_blob_store_note_sd_backend(
        true, true, true, D1L_RP2040_FILE_LINE_MAX,
        D1L_RP2040_FILE_CHUNK_MAX, D1L_RP2040_FILE_PATH_MAX);
    assert_all_states(true, 3U);

    static const uint8_t payload[] = "guarded retained blob";
    s_toggle_backend_during_write = true;
    assert(d1l_retained_blob_store_write_sd_primary_guarded(
               D1L_RETAINED_BLOB_STORE_ROUTES, "routes_v2",
               payload, sizeof(payload), 3U) == ESP_ERR_INVALID_STATE);
    assert_all_states(true, 5U);
    assert(s_rename_count == 0U);
    assert(s_delete_count == 0U);

    assert(d1l_retained_blob_store_write_sd_primary_guarded(
               D1L_RETAINED_BLOB_STORE_ROUTES, "routes_v2",
               payload, sizeof(payload), 5U) == ESP_OK);
    assert(s_rename_count == 1U);

    /* Generic Public/DM/packet split writers share the same guarded internal
     * commit point even though they do not supply an external generation. */
    s_toggle_backend_during_write = true;
    assert(d1l_retained_blob_store_write_split(
               D1L_RETAINED_BLOB_STORE_PACKET_LOG, "ring",
               payload, sizeof(payload), payload, sizeof(payload)) ==
           ESP_ERR_INVALID_STATE);
    assert_all_states(true, 7U);
    assert(s_rename_count == 1U);
    assert(s_delete_count == 0U);

    s_toggle_backend_during_delete = true;
    assert(d1l_retained_blob_store_erase_sd_primary_guarded(
               D1L_RETAINED_BLOB_STORE_ROUTES, "routes_v2", 7U) ==
           ESP_ERR_INVALID_STATE);
    assert_all_states(true, 9U);
    assert(s_delete_count == 1U);

    test_retained_partition_init();
    test_first_use_initializes_only_new_retained_region();
    test_first_marker_recovers_init_power_loss_window();
    test_ambiguous_or_future_marker_never_erases_retained_data();
    test_partition_init_proactively_migrates_known_legacy_key();
    test_divergent_upgrade_copies_fail_closed();
    test_dedicated_read_precedes_legacy();
    test_legacy_read_migrates_after_dedicated_miss();
    test_dedicated_write_reclaims_legacy_only_after_commit();
    test_erase_clears_dedicated_and_legacy_copies();
    test_partition_init_failure_never_falls_back_or_resurrects();

    puts("native retained backend generation and NVS partition: ok");
    return 0;
}
