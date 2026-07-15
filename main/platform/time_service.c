#include "time_service.h"

#include <stddef.h>
#include <sys/time.h>
#include <time.h>

#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

#include "app/settings_model.h"

#ifndef D1L_BUILD_EPOCH_SEC
#error "D1L_BUILD_EPOCH_SEC must be derived from the exact source commit"
#endif

_Static_assert(D1L_BUILD_EPOCH_SEC >= D1L_TIME_PROTOCOL_TIMESTAMP_BASE,
               "build epoch predates the protocol epoch");
_Static_assert((int64_t)D1L_BUILD_EPOCH_SEC <=
                   D1L_TIME_SNTP_MAX_PROTOCOL_EPOCH,
               "build epoch leaves insufficient uint32 protocol headroom");

#define D1L_TIME_NVS_NAMESPACE "d1l_settings"
#define D1L_TIME_PROTOCOL_LEGACY_KEY "mesh_ts"
#define D1L_TIME_PROTOCOL_HIGH_WATER_KEY "mesh_hi_v2"

static StaticSemaphore_t s_time_mutex_storage;
static SemaphoreHandle_t s_time_mutex;
static d1l_time_service_core_t s_time_core;
static bool s_initialized;
static bool s_protocol_seed_ready;
static bool s_sntp_initialized;
static d1l_time_protocol_persistence_state_t s_protocol_persistence_state =
    D1L_TIME_PROTOCOL_PERSISTENCE_UNINITIALIZED;
static esp_err_t s_protocol_persistence_error = ESP_ERR_INVALID_STATE;
static esp_err_t s_sntp_init_error = ESP_ERR_INVALID_STATE;

static uint64_t monotonic_now_us(void)
{
    const int64_t now_us = esp_timer_get_time();
    return now_us > 0 ? (uint64_t)now_us : 0U;
}

static bool time_lock(void)
{
    return s_time_mutex &&
           xSemaphoreTake(s_time_mutex, portMAX_DELAY) == pdTRUE;
}

static void time_unlock(void)
{
    (void)xSemaphoreGive(s_time_mutex);
}

static esp_err_t get_optional_u32(nvs_handle_t handle,
                                  const char *key,
                                  bool *found,
                                  uint32_t *value)
{
    if (!found || !value) {
        return ESP_ERR_INVALID_ARG;
    }
    *found = false;
    const esp_err_t ret = nvs_get_u32(handle, key, value);
    if (ret == ESP_OK) {
        *found = true;
        return ESP_OK;
    }
    return ret == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : ret;
}

static esp_err_t load_protocol_seed_locked(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(D1L_TIME_NVS_NAMESPACE, NVS_READONLY, &handle);
    bool legacy_present = false;
    bool high_water_present = false;
    uint32_t legacy_value = 0U;
    uint32_t reserved_through = D1L_TIME_PROTOCOL_TIMESTAMP_BASE;
    if (ret == ESP_OK) {
        ret = get_optional_u32(handle, D1L_TIME_PROTOCOL_LEGACY_KEY,
                               &legacy_present, &legacy_value);
        if (ret == ESP_OK) {
            ret = get_optional_u32(handle, D1L_TIME_PROTOCOL_HIGH_WATER_KEY,
                                   &high_water_present, &reserved_through);
        }
        nvs_close(handle);
        (void)legacy_value;
    } else if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ret = ESP_OK;
    }
    if (ret == ESP_OK) {
        s_protocol_persistence_state =
            d1l_time_core_classify_protocol_seed(
                legacy_present, high_water_present, reserved_through);
        if (s_protocol_persistence_state ==
            D1L_TIME_PROTOCOL_PERSISTENCE_FRESH) {
            ret = d1l_time_core_seed_protocol(
                &s_time_core, false, D1L_TIME_PROTOCOL_TIMESTAMP_BASE);
        } else if (s_protocol_persistence_state ==
                   D1L_TIME_PROTOCOL_PERSISTENCE_READY) {
            ret = d1l_time_core_seed_protocol(&s_time_core, true,
                                              reserved_through);
        } else {
            /* The predecessor key could lag RAM-only timestamps after an NVS
             * exhaustion fallback.  It is a lower bound, not proof that the
             * next value was never transmitted, so preserve it and fail
             * closed for an explicit migration procedure. */
            ret = ESP_ERR_INVALID_STATE;
        }
    } else {
        s_protocol_persistence_state =
            D1L_TIME_PROTOCOL_PERSISTENCE_STORAGE_ERROR;
    }
    if (ret != ESP_OK &&
        s_protocol_persistence_state !=
            D1L_TIME_PROTOCOL_PERSISTENCE_MIGRATION_REQUIRED &&
        s_protocol_persistence_state !=
            D1L_TIME_PROTOCOL_PERSISTENCE_CORRUPT) {
        s_protocol_persistence_state =
            D1L_TIME_PROTOCOL_PERSISTENCE_STORAGE_ERROR;
    }
    s_protocol_seed_ready = ret == ESP_OK;
    s_protocol_persistence_error = ret;
    return ret;
}

static esp_err_t reserve_protocol_range(void *context,
                                        uint32_t reserved_through)
{
    (void)context;
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(D1L_TIME_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        s_protocol_persistence_state =
            D1L_TIME_PROTOCOL_PERSISTENCE_STORAGE_ERROR;
        s_protocol_persistence_error = ret;
        return ret;
    }
    ret = nvs_set_u32(handle, D1L_TIME_PROTOCOL_HIGH_WATER_KEY,
                      reserved_through);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    s_protocol_persistence_state = ret == ESP_OK ?
        D1L_TIME_PROTOCOL_PERSISTENCE_READY :
        D1L_TIME_PROTOCOL_PERSISTENCE_STORAGE_ERROR;
    s_protocol_persistence_error = ret;
    return ret;
}

static bool continue_allowed(d1l_time_continue_cb_t should_continue,
                             void *context)
{
    return !should_continue || should_continue(context);
}

static bool snapshot_certificate_valid_locked(void)
{
    d1l_time_core_snapshot_t snapshot;
    d1l_time_core_snapshot(&s_time_core, monotonic_now_us(), &snapshot);
    return snapshot.certificate_time_valid &&
           snapshot.wall_epoch_sec >= D1L_TIME_WALL_MIN_EPOCH;
}

static esp_err_t accept_sntp_system_time(uint32_t expected_generation)
{
    if (!time_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    /* A companion or retained-source update that starts after this SNTP wait
     * was admitted supersedes the in-flight network adoption.  Sampling the
     * system clock and committing the source under this same lock gives the
     * transition one explicit linearization point. */
    if (s_time_core.wall_generation != expected_generation) {
        const bool ready = snapshot_certificate_valid_locked();
        time_unlock();
        /* A validated companion source wins immediately.  A weaker
         * generation change only invalidates this admission token; let the
         * bounded caller refresh its token and retry instead of permanently
         * wedging certificate-time acquisition. */
        return ready ? ESP_OK : ESP_ERR_NOT_FINISHED;
    }
    const time_t now = time(NULL);
    if (now == (time_t)-1 || (int64_t)now < D1L_TIME_WALL_MIN_EPOCH) {
        time_unlock();
        return ESP_ERR_NOT_FINISHED;
    }
    const esp_err_t ret = d1l_time_core_set_wall_if_generation(
        &s_time_core, expected_generation, (int64_t)now,
        monotonic_now_us(), D1L_TIME_VALIDITY_NETWORK_VALIDATED,
        D1L_TIME_SOURCE_SNTP);
    const bool ready = ret == ESP_OK && snapshot_certificate_valid_locked();
    time_unlock();
    return ready ? ESP_OK : (ret == ESP_OK ? ESP_ERR_INVALID_STATE : ret);
}

static esp_err_t certificate_state(uint32_t *generation, bool *ready)
{
    if (!generation || !ready) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized && d1l_time_service_init() != ESP_OK && !s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!time_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    d1l_time_core_snapshot_t snapshot;
    d1l_time_core_snapshot(&s_time_core, monotonic_now_us(), &snapshot);
    *generation = snapshot.wall_generation;
    *ready = snapshot.certificate_time_valid &&
             snapshot.wall_epoch_sec >= D1L_TIME_WALL_MIN_EPOCH;
    time_unlock();
    return ESP_OK;
}

static esp_err_t ensure_sntp_initialized(void)
{
    if (!time_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_sntp_initialized) {
        time_unlock();
        return ESP_OK;
    }
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    config.wait_for_sync = true;
    const esp_err_t ret = esp_netif_sntp_init(&config);
    s_sntp_initialized = ret == ESP_OK;
    s_sntp_init_error = ret;
    time_unlock();
    return ret;
}

esp_err_t d1l_time_service_init(void)
{
    if (!s_time_mutex) {
        s_time_mutex = xSemaphoreCreateMutexStatic(&s_time_mutex_storage);
        if (!s_time_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (!time_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_initialized) {
        const esp_err_t core_ret = d1l_time_core_init(
            &s_time_core, monotonic_now_us(),
            (uint32_t)D1L_BUILD_EPOCH_SEC);
        if (core_ret != ESP_OK) {
            time_unlock();
            return core_ret;
        }
        s_initialized = true;
    }
    esp_err_t ret = ESP_OK;
    if (!s_protocol_seed_ready) {
        ret = load_protocol_seed_locked();
    }
    time_unlock();
    return ret;
}

uint64_t d1l_time_service_boot_monotonic_us(void)
{
    if (!s_initialized && d1l_time_service_init() != ESP_OK) {
        return 0U;
    }
    if (!time_lock()) {
        return 0U;
    }
    const uint64_t value =
        d1l_time_core_observe_monotonic(&s_time_core, monotonic_now_us());
    time_unlock();
    return value;
}

esp_err_t d1l_time_service_preflight_protocol_timestamp(void)
{
    esp_err_t ret = d1l_time_service_init();
    if (ret != ESP_OK && !s_protocol_seed_ready) {
        return ret;
    }
    if (!time_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_protocol_seed_ready) {
        ret = load_protocol_seed_locked();
    }
    if (ret == ESP_OK) {
        ret = d1l_time_core_preflight_protocol_timestamp(
            &s_time_core, monotonic_now_us());
    }
    time_unlock();
    return ret;
}

esp_err_t d1l_time_service_next_protocol_timestamp(uint32_t *out_timestamp)
{
    if (!out_timestamp) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = d1l_time_service_init();
    if (ret != ESP_OK && !s_protocol_seed_ready) {
        return ret;
    }
    if (!time_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_protocol_seed_ready) {
        ret = load_protocol_seed_locked();
    }
    if (ret == ESP_OK) {
        ret = d1l_time_core_next_protocol_timestamp(
            &s_time_core, monotonic_now_us(), reserve_protocol_range, NULL,
            out_timestamp);
    }
    time_unlock();
    return ret;
}

void d1l_time_service_status(d1l_time_service_status_t *out_status)
{
    if (!out_status) {
        return;
    }
    *out_status = (d1l_time_service_status_t) {0};
    out_status->protocol_persistence_state = s_protocol_persistence_state;
    out_status->protocol_persistence_error = s_protocol_persistence_error;
    if (!s_initialized && d1l_time_service_init() != ESP_OK && !s_initialized) {
        out_status->protocol_persistence_state =
            s_protocol_persistence_state;
        out_status->protocol_persistence_error = s_protocol_persistence_error;
        out_status->protocol_tx_error = ESP_ERR_INVALID_STATE;
        out_status->sntp_init_error = s_sntp_init_error;
        return;
    }
    if (!time_lock()) {
        out_status->protocol_persistence_error = ESP_ERR_INVALID_STATE;
        out_status->protocol_tx_error = ESP_ERR_INVALID_STATE;
        out_status->sntp_init_error = s_sntp_init_error;
        return;
    }
    d1l_time_core_snapshot(&s_time_core, monotonic_now_us(),
                           &out_status->clock);
    out_status->initialized = s_initialized;
    out_status->protocol_persistence_state = s_protocol_persistence_state;
    out_status->protocol_persistence_ready =
        s_protocol_seed_ready && s_protocol_persistence_error == ESP_OK;
    out_status->protocol_persistence_error = s_protocol_persistence_error;
    out_status->protocol_tx_ready =
        out_status->protocol_persistence_ready &&
        out_status->clock.protocol_tx_ready;
    out_status->protocol_tx_error =
        out_status->protocol_persistence_ready ?
            out_status->clock.protocol_tx_error :
            s_protocol_persistence_error;
    out_status->sntp_initialized = s_sntp_initialized;
    out_status->sntp_init_error = s_sntp_init_error;
    time_unlock();
}

bool d1l_time_service_certificate_time_valid(void)
{
    uint32_t generation = 0U;
    bool ready = false;
    return certificate_state(&generation, &ready) == ESP_OK && ready;
}

esp_err_t d1l_time_service_wait_for_certificate_time(
    uint32_t timeout_ms,
    uint32_t slice_ms,
    d1l_time_continue_cb_t should_continue,
    void *continue_context)
{
    if (slice_ms == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!continue_allowed(should_continue, continue_context)) {
        return ESP_ERR_INVALID_STATE;
    }
    uint32_t expected_generation = 0U;
    bool certificate_ready = false;
    esp_err_t ret = certificate_state(&expected_generation,
                                      &certificate_ready);
    if (ret != ESP_OK) {
        return ret;
    }
    if (certificate_ready) {
        return ESP_OK;
    }
    if (timeout_ms == 0U) {
        return ESP_ERR_TIMEOUT;
    }
    ret = ensure_sntp_initialized();
    if (ret != ESP_OK) {
        return ret;
    }
    ret = certificate_state(&expected_generation, &certificate_ready);
    if (ret != ESP_OK) {
        return ret;
    }
    if (certificate_ready) {
        return ESP_OK;
    }

    uint32_t elapsed_ms = 0U;
    while (elapsed_ms < timeout_ms) {
        if (!continue_allowed(should_continue, continue_context)) {
            return ESP_ERR_INVALID_STATE;
        }
        uint32_t wait_ms = timeout_ms - elapsed_ms;
        if (wait_ms > slice_ms) {
            wait_ms = slice_ms;
        }
        TickType_t wait_ticks = pdMS_TO_TICKS(wait_ms);
        if (wait_ticks == 0U) {
            wait_ticks = 1U;
        }
        ret = esp_netif_sntp_sync_wait(wait_ticks);
        if (!continue_allowed(should_continue, continue_context)) {
            return ESP_ERR_INVALID_STATE;
        }
        if (ret == ESP_OK) {
            const esp_err_t accept_ret =
                accept_sntp_system_time(expected_generation);
            if (accept_ret == ESP_OK) {
                return ESP_OK;
            }
            if (accept_ret == ESP_ERR_NOT_FINISHED) {
                const esp_err_t state_ret = certificate_state(
                    &expected_generation, &certificate_ready);
                if (state_ret != ESP_OK) {
                    return state_ret;
                }
                if (certificate_ready) {
                    return ESP_OK;
                }
            } else {
                return accept_ret;
            }
        } else {
            uint32_t observed_generation = 0U;
            bool observed_ready = false;
            const esp_err_t state_ret =
                certificate_state(&observed_generation, &observed_ready);
            if (state_ret != ESP_OK) {
                return state_ret;
            }
            if (observed_ready) {
                return ESP_OK;
            }
        }
        if (ret != ESP_ERR_TIMEOUT && ret != ESP_ERR_NOT_FINISHED &&
            ret != ESP_OK) {
            return ret;
        }
        elapsed_ms += wait_ms;
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t d1l_time_service_set_companion_time(int64_t epoch_sec,
                                              bool authenticated)
{
    if (!authenticated || epoch_sec < D1L_TIME_WALL_MIN_EPOCH) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized && d1l_time_service_init() != ESP_OK && !s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!time_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    struct timeval value = {
        .tv_sec = (time_t)epoch_sec,
        .tv_usec = 0,
    };
    const esp_err_t preflight = d1l_time_core_preflight_wall(
        &s_time_core, epoch_sec,
        D1L_TIME_VALIDITY_COMPANION_VALIDATED,
        D1L_TIME_SOURCE_COMPANION_AUTHENTICATED);
    if ((int64_t)value.tv_sec != epoch_sec || preflight != ESP_OK) {
        time_unlock();
        return preflight == ESP_OK ? ESP_ERR_INVALID_ARG : preflight;
    }
    if (settimeofday(&value, NULL) != 0) {
        time_unlock();
        return ESP_FAIL;
    }
    /* The preflight and commit share this mutex, so no generation or source
     * invariant can change after the system-clock side effect. */
    const esp_err_t ret = d1l_time_core_set_wall(
        &s_time_core, epoch_sec, monotonic_now_us(),
        D1L_TIME_VALIDITY_COMPANION_VALIDATED,
        D1L_TIME_SOURCE_COMPANION_AUTHENTICATED);
    time_unlock();
    return ret;
}

esp_err_t d1l_time_service_note_authenticated_lower_bound(
    int64_t epoch_sec,
    bool authenticated)
{
    if (!authenticated || epoch_sec < D1L_TIME_WALL_MIN_EPOCH) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized && d1l_time_service_init() != ESP_OK && !s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!time_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t ret = d1l_time_core_note_authenticated_lower_bound(
        &s_time_core, epoch_sec, monotonic_now_us());
    time_unlock();
    return ret;
}

/* Compatibility boundary for the current MeshCore command owner.  The
 * allocator itself is solely owned by this service; callers migrate without
 * editing the active protocol hotspot in this slice. */
esp_err_t d1l_settings_next_mesh_timestamp(uint32_t *timestamp)
{
    return d1l_time_service_next_protocol_timestamp(timestamp);
}
