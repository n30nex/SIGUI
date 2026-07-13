#include "route_store_worker.h"

#include <stdbool.h>

#include "esp_timer.h"
#include "diagnostics/health_monitor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mesh/dm_store.h"
#include "mesh/message_store.h"
#include "mesh/packet_log.h"
#include "mesh/route_store.h"

#define D1L_ROUTE_STORE_WORKER_INTERVAL_MS 1000U
/* This worker now owns the deepest Public/DM/packet/route persistence call
 * chain.  The previous route-only 4 KiB allocation overflowed on hardware as
 * soon as late-SD packet reconciliation ran.  ESP-IDF specifies task stack
 * depth and high-water marks in bytes, so keep a production-sized margin and
 * expose the measured floor through health diagnostics. */
#define D1L_ROUTE_STORE_WORKER_STACK_BYTES 12288U
#define D1L_ROUTE_STORE_WORKER_PRIORITY (tskIDLE_PRIORITY + 1U)
#define D1L_ROUTE_STORE_WORKER_QUEUE_LENGTH 2U
#define D1L_ROUTE_STORE_WORKER_POLL_MS 10U

typedef struct {
    uint32_t request_id;
} d1l_route_store_worker_request_t;

static QueueHandle_t s_request_queue;
static SemaphoreHandle_t s_request_mutex;
static SemaphoreHandle_t s_flush_mutex;
static TaskHandle_t s_worker_task;
static portMUX_TYPE s_state_mux = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_quiesce_owner_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_worker_starting;
static TaskHandle_t s_quiesce_owner;
static TaskHandle_t s_quiesce_requester;
static uint32_t s_next_request_id;
static uint32_t s_result_request_id;
static esp_err_t s_result;

static bool worker_quiesce_requested(void)
{
    bool requested;
    portENTER_CRITICAL(&s_quiesce_owner_mux);
    requested = s_quiesce_requester != NULL;
    portEXIT_CRITICAL(&s_quiesce_owner_mux);
    return requested;
}

bool d1l_route_store_persistence_should_yield(void)
{
    const TaskHandle_t current = xTaskGetCurrentTaskHandle();
    TaskHandle_t requester;
    TaskHandle_t owner;
    portENTER_CRITICAL(&s_quiesce_owner_mux);
    requester = s_quiesce_requester;
    owner = s_quiesce_owner;
    portEXIT_CRITICAL(&s_quiesce_owner_mux);
    return requester != NULL && current != requester && current != owner;
}

static esp_err_t quiesce_yield_result(bool force, esp_err_t first_error)
{
    if (first_error != ESP_OK) {
        return first_error;
    }
    /* A forced flush must never report success after yielding partway through
     * its durability sequence. Background due-work may yield cleanly because
     * the foreground quiesce owner will perform the required operation. */
    return force ? ESP_ERR_TIMEOUT : ESP_OK;
}

static esp_err_t flush_retained_stores(bool force)
{
    esp_err_t first_error = ESP_OK;
    if (worker_quiesce_requested()) {
        return quiesce_yield_result(force, first_error);
    }
    const esp_err_t message_ret = force ? d1l_message_store_flush() :
                                          d1l_message_store_flush_if_due();
    if (message_ret != ESP_OK) {
        first_error = message_ret;
    }
    if (worker_quiesce_requested()) {
        return quiesce_yield_result(force, first_error);
    }
    const esp_err_t dm_ret = force ? d1l_dm_store_flush() :
                                     d1l_dm_store_flush_if_due();
    if (dm_ret != ESP_OK && first_error == ESP_OK) {
        first_error = dm_ret;
    }
    if (worker_quiesce_requested()) {
        return quiesce_yield_result(force, first_error);
    }
    const esp_err_t packet_ret = force ? d1l_packet_log_flush() :
                                         d1l_packet_log_flush_if_due();
    if (packet_ret != ESP_OK && first_error == ESP_OK) {
        first_error = packet_ret;
    }
    if (worker_quiesce_requested()) {
        return quiesce_yield_result(force, first_error);
    }
    const esp_err_t route_ret = force ? d1l_route_store_flush() :
                                        d1l_route_store_flush_if_due();
    if (route_ret != ESP_OK && first_error == ESP_OK) {
        first_error = route_ret;
    }
    return first_error;
}

static esp_err_t flush_retained_stores_locked(bool force)
{
    if (!s_flush_mutex) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_flush_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    const esp_err_t ret = flush_retained_stores(force);
    (void)xSemaphoreGive(s_flush_mutex);
    return ret;
}

static TickType_t quiesce_remaining_ticks(int64_t started_us,
                                          uint32_t timeout_ms)
{
    const int64_t elapsed_us = esp_timer_get_time() - started_us;
    const int64_t timeout_us = (int64_t)timeout_ms * 1000LL;
    if (elapsed_us >= timeout_us) {
        return 0;
    }
    const uint32_t remaining_ms =
        (uint32_t)((timeout_us - elapsed_us + 999LL) / 1000LL);
    TickType_t ticks = pdMS_TO_TICKS(remaining_ms);
    return ticks == 0U ? 1U : ticks;
}

static void publish_result(uint32_t request_id, esp_err_t result)
{
    portENTER_CRITICAL(&s_state_mux);
    s_result_request_id = request_id;
    s_result = result;
    portEXIT_CRITICAL(&s_state_mux);
}

static void route_store_worker_task(void *arg)
{
    (void)arg;
    d1l_route_store_worker_request_t request = {0};
    for (;;) {
        if (xQueueReceive(s_request_queue, &request,
                          pdMS_TO_TICKS(D1L_ROUTE_STORE_WORKER_INTERVAL_MS)) == pdTRUE) {
            publish_result(request.request_id, flush_retained_stores_locked(true));
        } else {
            (void)flush_retained_stores_locked(false);
        }
    }
}

esp_err_t d1l_route_store_worker_start(void)
{
    portENTER_CRITICAL(&s_state_mux);
    const bool already_started = s_worker_task != NULL;
    const bool already_starting = s_worker_starting;
    if (!already_started && !already_starting) {
        s_worker_starting = true;
    }
    portEXIT_CRITICAL(&s_state_mux);
    if (already_started) {
        return ESP_OK;
    }
    if (already_starting) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_request_mutex) {
        s_request_mutex = xSemaphoreCreateMutex();
        if (!s_request_mutex) {
            portENTER_CRITICAL(&s_state_mux);
            s_worker_starting = false;
            portEXIT_CRITICAL(&s_state_mux);
            return ESP_ERR_NO_MEM;
        }
    }
    if (!s_flush_mutex) {
        s_flush_mutex = xSemaphoreCreateMutex();
        if (!s_flush_mutex) {
            portENTER_CRITICAL(&s_state_mux);
            s_worker_starting = false;
            portEXIT_CRITICAL(&s_state_mux);
            return ESP_ERR_NO_MEM;
        }
    }
    if (!s_request_queue) {
        s_request_queue = xQueueCreate(D1L_ROUTE_STORE_WORKER_QUEUE_LENGTH,
                                       sizeof(d1l_route_store_worker_request_t));
        if (!s_request_queue) {
            portENTER_CRITICAL(&s_state_mux);
            s_worker_starting = false;
            portEXIT_CRITICAL(&s_state_mux);
            return ESP_ERR_NO_MEM;
        }
    }
    TaskHandle_t created_task = NULL;
    if (xTaskCreate(route_store_worker_task, "route_persist",
                    D1L_ROUTE_STORE_WORKER_STACK_BYTES, NULL,
                    D1L_ROUTE_STORE_WORKER_PRIORITY,
                    &created_task) != pdPASS) {
        portENTER_CRITICAL(&s_state_mux);
        s_worker_starting = false;
        portEXIT_CRITICAL(&s_state_mux);
        return ESP_ERR_NO_MEM;
    }
    d1l_health_monitor_register_retained_task(created_task);
    portENTER_CRITICAL(&s_state_mux);
    s_worker_task = created_task;
    s_worker_starting = false;
    portEXIT_CRITICAL(&s_state_mux);
    return ESP_OK;
}

esp_err_t d1l_route_store_worker_force_flush(uint32_t timeout_ms)
{
    if (timeout_ms == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    const int64_t started_us = esp_timer_get_time();
    esp_err_t ret = d1l_route_store_worker_start();
    if (ret != ESP_OK) {
        return ret;
    }
    const TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    if (xSemaphoreTake(s_request_mutex, timeout_ticks) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (esp_timer_get_time() - started_us >= (int64_t)timeout_ms * 1000LL) {
        (void)xSemaphoreGive(s_request_mutex);
        return ESP_ERR_TIMEOUT;
    }

    portENTER_CRITICAL(&s_state_mux);
    uint32_t request_id = ++s_next_request_id;
    if (request_id == 0U) {
        request_id = ++s_next_request_id;
    }
    portEXIT_CRITICAL(&s_state_mux);
    const d1l_route_store_worker_request_t request = {
        .request_id = request_id,
    };
    if (xQueueSend(s_request_queue, &request, 0) != pdTRUE) {
        (void)xSemaphoreGive(s_request_mutex);
        return ESP_ERR_TIMEOUT;
    }

    for (;;) {
        uint32_t result_request_id;
        esp_err_t result;
        portENTER_CRITICAL(&s_state_mux);
        result_request_id = s_result_request_id;
        result = s_result;
        portEXIT_CRITICAL(&s_state_mux);
        if (result_request_id == request_id) {
            (void)xSemaphoreGive(s_request_mutex);
            return result;
        }
        const int64_t elapsed_us = esp_timer_get_time() - started_us;
        if (elapsed_us >= (int64_t)timeout_ms * 1000LL) {
            (void)xSemaphoreGive(s_request_mutex);
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(D1L_ROUTE_STORE_WORKER_POLL_MS));
    }
}

esp_err_t d1l_route_store_worker_quiesce_begin(uint32_t timeout_ms)
{
    if (timeout_ms == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    const int64_t started_us = esp_timer_get_time();
    esp_err_t ret = d1l_route_store_worker_start();
    if (ret != ESP_OK) {
        return ret;
    }

    const TaskHandle_t current = xTaskGetCurrentTaskHandle();
    bool request_registered = false;
    portENTER_CRITICAL(&s_quiesce_owner_mux);
    if (s_quiesce_owner == NULL && s_quiesce_requester == NULL) {
        s_quiesce_requester = current;
        request_registered = true;
    }
    portEXIT_CRITICAL(&s_quiesce_owner_mux);
    if (!request_registered) {
        return ESP_ERR_INVALID_STATE;
    }

    TickType_t ticks = quiesce_remaining_ticks(started_us, timeout_ms);
    if (ticks == 0U || xSemaphoreTake(s_request_mutex, ticks) != pdTRUE) {
        portENTER_CRITICAL(&s_quiesce_owner_mux);
        s_quiesce_requester = NULL;
        portEXIT_CRITICAL(&s_quiesce_owner_mux);
        return ESP_ERR_TIMEOUT;
    }
    ticks = quiesce_remaining_ticks(started_us, timeout_ms);
    if (ticks == 0U || xSemaphoreTake(s_flush_mutex, ticks) != pdTRUE) {
        portENTER_CRITICAL(&s_quiesce_owner_mux);
        s_quiesce_requester = NULL;
        portEXIT_CRITICAL(&s_quiesce_owner_mux);
        (void)xSemaphoreGive(s_request_mutex);
        return ESP_ERR_TIMEOUT;
    }
    portENTER_CRITICAL(&s_quiesce_owner_mux);
    s_quiesce_owner = current;
    portEXIT_CRITICAL(&s_quiesce_owner_mux);
    return ESP_OK;
}

void d1l_route_store_worker_quiesce_end(void)
{
    const TaskHandle_t current = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL(&s_quiesce_owner_mux);
    if (s_quiesce_owner != current) {
        portEXIT_CRITICAL(&s_quiesce_owner_mux);
        return;
    }
    s_quiesce_owner = NULL;
    s_quiesce_requester = NULL;
    portEXIT_CRITICAL(&s_quiesce_owner_mux);
    (void)xSemaphoreGive(s_flush_mutex);
    (void)xSemaphoreGive(s_request_mutex);
}
