#include "mock_esp_nvs.h"

#include <stdio.h>
#include <string.h>

#include "esp_timer.h"
#include "freertos/semphr.h"
#include "nvs.h"

#define MOCK_NVS_SLOT_COUNT 4U
#define MOCK_NVS_NAMESPACE_LEN 24U
#define MOCK_NVS_KEY_LEN 24U
#define MOCK_NVS_BLOB_MAX 8192U

typedef enum {
    MOCK_PENDING_NONE = 0,
    MOCK_PENDING_SET,
    MOCK_PENDING_ERASE,
} mock_pending_kind_t;

typedef struct {
    bool used;
    char namespace_name[MOCK_NVS_NAMESPACE_LEN];
    char key[MOCK_NVS_KEY_LEN];
    uint8_t committed[MOCK_NVS_BLOB_MAX];
    size_t committed_len;
    bool committed_valid;
    char pending_key[MOCK_NVS_KEY_LEN];
    uint8_t pending[MOCK_NVS_BLOB_MAX];
    size_t pending_len;
    mock_pending_kind_t pending_kind;
} mock_nvs_slot_t;

static mock_nvs_slot_t s_slots[MOCK_NVS_SLOT_COUNT];
static esp_err_t s_fail_next_set;
static esp_err_t s_fail_next_open;
static size_t s_open_count;
static size_t s_fail_open_call;
static esp_err_t s_fail_scheduled_open;
static int64_t s_now_us;
static void (*s_next_set_hook)(void);
static void (*s_semaphore_take_hook)(void);
static size_t s_semaphore_takes_before_hook;

static void run_hook_once(void (**hook_slot)(void))
{
    void (*hook)(void) = *hook_slot;
    *hook_slot = NULL;
    if (hook) {
        hook();
    }
}

static mock_nvs_slot_t *slot_for_namespace(const char *namespace_name, bool create)
{
    if (!namespace_name || namespace_name[0] == '\0') {
        return NULL;
    }
    for (size_t i = 0; i < MOCK_NVS_SLOT_COUNT; ++i) {
        if (s_slots[i].used &&
            strcmp(s_slots[i].namespace_name, namespace_name) == 0) {
            return &s_slots[i];
        }
    }
    if (!create) {
        return NULL;
    }
    for (size_t i = 0; i < MOCK_NVS_SLOT_COUNT; ++i) {
        if (!s_slots[i].used) {
            s_slots[i].used = true;
            (void)snprintf(s_slots[i].namespace_name,
                           sizeof(s_slots[i].namespace_name), "%s", namespace_name);
            return &s_slots[i];
        }
    }
    return NULL;
}

static mock_nvs_slot_t *slot_for_handle(nvs_handle_t handle)
{
    if (handle == 0U || handle > MOCK_NVS_SLOT_COUNT) {
        return NULL;
    }
    mock_nvs_slot_t *slot = &s_slots[handle - 1U];
    return slot->used ? slot : NULL;
}

void mock_nvs_reset(void)
{
    memset(s_slots, 0, sizeof(s_slots));
    s_fail_next_set = ESP_OK;
    s_fail_next_open = ESP_OK;
    s_open_count = 0U;
    s_fail_open_call = 0U;
    s_fail_scheduled_open = ESP_OK;
    s_now_us = 0;
    s_next_set_hook = NULL;
    s_semaphore_take_hook = NULL;
    s_semaphore_takes_before_hook = 0U;
}

bool mock_nvs_seed_blob(const char *namespace_name, const char *key,
                        const void *value, size_t length)
{
    if (!key || !value || length > MOCK_NVS_BLOB_MAX) {
        return false;
    }
    mock_nvs_slot_t *slot = slot_for_namespace(namespace_name, true);
    if (!slot) {
        return false;
    }
    (void)snprintf(slot->key, sizeof(slot->key), "%s", key);
    memcpy(slot->committed, value, length);
    slot->committed_len = length;
    slot->committed_valid = true;
    slot->pending_kind = MOCK_PENDING_NONE;
    return true;
}

size_t mock_nvs_copy_blob(const char *namespace_name, const char *key,
                          void *dest, size_t dest_size)
{
    mock_nvs_slot_t *slot = slot_for_namespace(namespace_name, false);
    if (!slot || !slot->committed_valid || !key ||
        strcmp(slot->key, key) != 0) {
        return 0U;
    }
    if (dest && dest_size >= slot->committed_len) {
        memcpy(dest, slot->committed, slot->committed_len);
    }
    return slot->committed_len;
}

void mock_nvs_fail_next_set(esp_err_t error)
{
    s_fail_next_set = error == ESP_OK ? ESP_FAIL : error;
}

void mock_nvs_fail_next_open(esp_err_t error)
{
    s_fail_next_open = error == ESP_OK ? ESP_FAIL : error;
}

void mock_nvs_fail_open_after(size_t successful_opens, esp_err_t error)
{
    s_fail_open_call = s_open_count + successful_opens + 1U;
    s_fail_scheduled_open = error == ESP_OK ? ESP_FAIL : error;
}

void mock_nvs_run_during_next_set(void (*hook)(void))
{
    s_next_set_hook = hook;
}

void mock_semaphore_run_after_takes(size_t take_count, void (*hook)(void))
{
    s_semaphore_take_hook = hook;
    s_semaphore_takes_before_hook = hook ? take_count : 0U;
}

void mock_timer_set_us(int64_t now_us)
{
    s_now_us = now_us;
}

int64_t esp_timer_get_time(void)
{
    return s_now_us;
}

SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *buffer)
{
    return buffer;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t handle, TickType_t ticks_to_wait)
{
    (void)handle;
    (void)ticks_to_wait;
    if (s_semaphore_take_hook && s_semaphore_takes_before_hook > 0U) {
        s_semaphore_takes_before_hook--;
        if (s_semaphore_takes_before_hook == 0U) {
            run_hook_once(&s_semaphore_take_hook);
        }
    }
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t handle)
{
    (void)handle;
    return pdTRUE;
}

esp_err_t nvs_open(const char *namespace_name, nvs_open_mode_t open_mode,
                   nvs_handle_t *out_handle)
{
    (void)open_mode;
    if (!out_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    s_open_count++;
    if (s_fail_open_call != 0U && s_open_count == s_fail_open_call) {
        const esp_err_t error = s_fail_scheduled_open;
        s_fail_open_call = 0U;
        s_fail_scheduled_open = ESP_OK;
        return error;
    }
    if (s_fail_next_open != ESP_OK) {
        const esp_err_t error = s_fail_next_open;
        s_fail_next_open = ESP_OK;
        return error;
    }
    mock_nvs_slot_t *slot = slot_for_namespace(namespace_name, true);
    if (!slot) {
        return ESP_ERR_NO_MEM;
    }
    *out_handle = (nvs_handle_t)((slot - s_slots) + 1U);
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle)
{
    mock_nvs_slot_t *slot = slot_for_handle(handle);
    if (slot) {
        slot->pending_kind = MOCK_PENDING_NONE;
        slot->pending_len = 0U;
        slot->pending_key[0] = '\0';
    }
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out_value,
                       size_t *length)
{
    mock_nvs_slot_t *slot = slot_for_handle(handle);
    if (!slot || !key || !length) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!slot->committed_valid || strcmp(slot->key, key) != 0) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    const size_t available = *length;
    *length = slot->committed_len;
    if (!out_value) {
        return ESP_OK;
    }
    if (available < slot->committed_len) {
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    memcpy(out_value, slot->committed, slot->committed_len);
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value,
                       size_t length)
{
    mock_nvs_slot_t *slot = slot_for_handle(handle);
    if (!slot || !key || !value) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_fail_next_set != ESP_OK) {
        const esp_err_t error = s_fail_next_set;
        s_fail_next_set = ESP_OK;
        return error;
    }
    if (length > MOCK_NVS_BLOB_MAX) {
        return ESP_ERR_NO_MEM;
    }
    (void)snprintf(slot->pending_key, sizeof(slot->pending_key), "%s", key);
    memcpy(slot->pending, value, length);
    slot->pending_len = length;
    slot->pending_kind = MOCK_PENDING_SET;
    run_hook_once(&s_next_set_hook);
    return ESP_OK;
}

esp_err_t nvs_get_u32(nvs_handle_t handle, const char *key, uint32_t *out_value)
{
    size_t length = sizeof(*out_value);
    return out_value ? nvs_get_blob(handle, key, out_value, &length)
                     : ESP_ERR_INVALID_ARG;
}

esp_err_t nvs_set_u32(nvs_handle_t handle, const char *key, uint32_t value)
{
    return nvs_set_blob(handle, key, &value, sizeof(value));
}

esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key)
{
    mock_nvs_slot_t *slot = slot_for_handle(handle);
    if (!slot || !key) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!slot->committed_valid || strcmp(slot->key, key) != 0) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    (void)snprintf(slot->pending_key, sizeof(slot->pending_key), "%s", key);
    slot->pending_kind = MOCK_PENDING_ERASE;
    slot->pending_len = 0U;
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    mock_nvs_slot_t *slot = slot_for_handle(handle);
    if (!slot) {
        return ESP_ERR_INVALID_ARG;
    }
    if (slot->pending_kind == MOCK_PENDING_SET) {
        (void)snprintf(slot->key, sizeof(slot->key), "%s", slot->pending_key);
        memcpy(slot->committed, slot->pending, slot->pending_len);
        slot->committed_len = slot->pending_len;
        slot->committed_valid = true;
    } else if (slot->pending_kind == MOCK_PENDING_ERASE) {
        slot->key[0] = '\0';
        slot->committed_len = 0U;
        slot->committed_valid = false;
    }
    slot->pending_kind = MOCK_PENDING_NONE;
    slot->pending_len = 0U;
    slot->pending_key[0] = '\0';
    return ESP_OK;
}
