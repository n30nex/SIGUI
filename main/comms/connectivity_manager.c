#include "connectivity_manager.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "comms/wifi_retry_policy.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#ifdef CONFIG_ESP_WIFI_ENABLED
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#endif

#define D1L_WIFI_RETRY_TASK_STACK_BYTES 3072U
#define D1L_WIFI_RETRY_TASK_PRIORITY 5U
#define D1L_WIFI_CONTROL_LOCK_TIMEOUT_MS 10000U

static bool s_wifi_started;
static bool s_wifi_connected;
static bool s_wifi_connecting;
static char s_wifi_ip[16] = "";
static char s_wifi_last_error[32] = "none";
static uint16_t s_wifi_last_disconnect_reason;
static d1l_wifi_retry_policy_t s_wifi_policy;
static d1l_wifi_local_leave_token_t s_wifi_local_leave_token;
static portMUX_TYPE s_wifi_policy_lock = portMUX_INITIALIZER_UNLOCKED;

#ifdef CONFIG_ESP_WIFI_ENABLED
static bool s_wifi_initialized;
static esp_netif_t *s_wifi_sta_netif;
static esp_event_handler_instance_t s_wifi_event_instance;
static esp_event_handler_instance_t s_ip_event_instance;
static TaskHandle_t s_wifi_retry_task;
static SemaphoreHandle_t s_wifi_control_lock;
#endif

static bool build_wifi_enabled(void)
{
#ifdef CONFIG_ESP_WIFI_ENABLED
    return true;
#else
    return false;
#endif
}

static bool build_ble_enabled(void)
{
#ifdef CONFIG_BT_ENABLED
    return true;
#else
    return false;
#endif
}

static d1l_wifi_retry_policy_t wifi_policy_snapshot(void)
{
    d1l_wifi_retry_policy_t snapshot;
    portENTER_CRITICAL(&s_wifi_policy_lock);
    snapshot = s_wifi_policy;
    portEXIT_CRITICAL(&s_wifi_policy_lock);
    return snapshot;
}

static const char *ble_runtime_state(bool desired, bool build_enabled)
{
    if (!desired) {
        return "off";
    }
    if (!build_enabled) {
        return "build_disabled";
    }
    return "pairing_pending_stack";
}

static void set_wifi_last_error(const char *reason)
{
    snprintf(s_wifi_last_error, sizeof(s_wifi_last_error), "%s",
             reason ? reason : "unknown");
}

#ifdef CONFIG_ESP_WIFI_ENABLED
static uint64_t wifi_now_ms(void)
{
    const int64_t now_us = esp_timer_get_time();
    return now_us <= 0 ? 0U : (uint64_t)now_us / 1000U;
}

static void notify_wifi_retry_worker(void)
{
    if (s_wifi_retry_task) {
        xTaskNotifyGive(s_wifi_retry_task);
    }
}

static void arm_local_leave_token(void)
{
    portENTER_CRITICAL(&s_wifi_policy_lock);
    d1l_wifi_local_leave_token_arm(&s_wifi_local_leave_token);
    portEXIT_CRITICAL(&s_wifi_policy_lock);
}

static void clear_local_leave_token(void)
{
    portENTER_CRITICAL(&s_wifi_policy_lock);
    d1l_wifi_local_leave_token_clear(&s_wifi_local_leave_token);
    portEXIT_CRITICAL(&s_wifi_policy_lock);
}

static bool consume_expected_local_leave(uint8_t reason)
{
    portENTER_CRITICAL(&s_wifi_policy_lock);
    const bool expected = d1l_wifi_local_leave_token_consume(
        &s_wifi_local_leave_token, reason == WIFI_REASON_ASSOC_LEAVE);
    portEXIT_CRITICAL(&s_wifi_policy_lock);
    return expected;
}

static bool take_wifi_control(void)
{
    TickType_t timeout_ticks =
        pdMS_TO_TICKS(D1L_WIFI_CONTROL_LOCK_TIMEOUT_MS);
    if (timeout_ticks == 0U) {
        timeout_ticks = 1U;
    }
    return s_wifi_control_lock &&
           xSemaphoreTake(s_wifi_control_lock, timeout_ticks) == pdTRUE;
}

static void give_wifi_control(void)
{
    if (s_wifi_control_lock) {
        xSemaphoreGive(s_wifi_control_lock);
    }
}

static d1l_wifi_failure_class_t classify_disconnect_reason(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
        return D1L_WIFI_FAILURE_AUTH;
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        return D1L_WIFI_FAILURE_AP_UNAVAILABLE;
    default:
        return D1L_WIFI_FAILURE_TRANSIENT;
    }
}

static TickType_t retry_wait_ticks(uint64_t due_ms, uint64_t now_ms)
{
    if (due_ms <= now_ms) {
        return 0U;
    }
    const uint64_t delta_ms = due_ms - now_ms;
    const uint32_t bounded_ms = delta_ms > UINT32_MAX ? UINT32_MAX :
        (uint32_t)delta_ms;
    TickType_t ticks = pdMS_TO_TICKS(bounded_ms);
    if (ticks == 0U) {
        ticks = 1U;
    }
    return ticks;
}

static void wifi_retry_worker(void *context)
{
    (void)context;
    while (true) {
        const d1l_wifi_retry_policy_t snapshot = wifi_policy_snapshot();
        if (!snapshot.retry_scheduled ||
            snapshot.state == D1L_WIFI_RUNTIME_SCANNING) {
            (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

        const uint64_t now_ms = wifi_now_ms();
        const TickType_t wait_ticks = retry_wait_ticks(snapshot.retry_due_ms,
                                                       now_ms);
        if (wait_ticks > 0U) {
            (void)ulTaskNotifyTake(pdTRUE, wait_ticks);
            continue;
        }

        if (!take_wifi_control()) {
            set_wifi_last_error("retry_control_failed");
            continue;
        }

        bool retry_due;
        const uint64_t retry_now_ms = wifi_now_ms();
        portENTER_CRITICAL(&s_wifi_policy_lock);
        retry_due = d1l_wifi_retry_policy_begin_due_retry(&s_wifi_policy,
                                                          retry_now_ms);
        portEXIT_CRITICAL(&s_wifi_policy_lock);
        if (!retry_due) {
            give_wifi_control();
            continue;
        }

        s_wifi_connecting = true;
        const esp_err_t ret = esp_wifi_connect();
        give_wifi_control();
        if (ret == ESP_OK) {
            set_wifi_last_error("none");
            continue;
        }

        s_wifi_connecting = false;
        set_wifi_last_error(esp_err_to_name(ret));
        const uint64_t failure_now_ms = wifi_now_ms();
        const uint32_t jitter_seed = esp_random();
        uint32_t delay_ms = 0U;
        portENTER_CRITICAL(&s_wifi_policy_lock);
        const bool scheduled = d1l_wifi_retry_policy_on_disconnect(
            &s_wifi_policy, D1L_WIFI_FAILURE_TRANSIENT, failure_now_ms,
            jitter_seed, &delay_ms);
        portEXIT_CRITICAL(&s_wifi_policy_lock);
        if (scheduled) {
            notify_wifi_retry_worker();
        }
    }
}

static esp_err_t ensure_wifi_retry_worker(void)
{
    if (s_wifi_retry_task) {
        return ESP_OK;
    }
    if (!s_wifi_control_lock) {
        s_wifi_control_lock = xSemaphoreCreateMutex();
        if (!s_wifi_control_lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (xTaskCreate(wifi_retry_worker, "wifi_retry",
                    D1L_WIFI_RETRY_TASK_STACK_BYTES, NULL,
                    D1L_WIFI_RETRY_TASK_PRIORITY,
                    &s_wifi_retry_task) != pdPASS) {
        s_wifi_retry_task = NULL;
        vSemaphoreDelete(s_wifi_control_lock);
        s_wifi_control_lock = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t ok_if_invalid_state(esp_err_t ret)
{
    return ret == ESP_ERR_INVALID_STATE ? ESP_OK : ret;
}

static const char *wifi_auth_name(wifi_auth_mode_t auth)
{
    switch (auth) {
    case WIFI_AUTH_OPEN:
        return "open";
    case WIFI_AUTH_WEP:
        return "wep";
    case WIFI_AUTH_WPA_PSK:
        return "wpa_psk";
    case WIFI_AUTH_WPA2_PSK:
        return "wpa2_psk";
    case WIFI_AUTH_WPA_WPA2_PSK:
        return "wpa_wpa2_psk";
    case WIFI_AUTH_WPA2_ENTERPRISE:
        return "wpa2_enterprise";
    case WIFI_AUTH_WPA3_PSK:
        return "wpa3_psk";
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return "wpa2_wpa3_psk";
    case WIFI_AUTH_WAPI_PSK:
        return "wapi_psk";
    default:
        return "unknown";
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event =
            (const wifi_event_sta_disconnected_t *)event_data;
        const uint8_t reason = event ? event->reason : WIFI_REASON_UNSPECIFIED;
        const d1l_wifi_failure_class_t failure =
            classify_disconnect_reason(reason);
        const uint64_t now_ms = wifi_now_ms();
        const uint32_t jitter_seed = esp_random();
        s_wifi_connected = false;
        s_wifi_connecting = false;
        s_wifi_ip[0] = '\0';
        s_wifi_last_disconnect_reason = reason;
        const bool suppress_retry = consume_expected_local_leave(reason);
        if (suppress_retry) {
            set_wifi_last_error("none");
            return;
        }
        uint32_t delay_ms = 0U;
        portENTER_CRITICAL(&s_wifi_policy_lock);
        const bool scheduled = d1l_wifi_retry_policy_on_disconnect(
            &s_wifi_policy, failure, now_ms, jitter_seed, &delay_ms);
        const d1l_wifi_retry_policy_t snapshot = s_wifi_policy;
        portEXIT_CRITICAL(&s_wifi_policy_lock);
        if (snapshot.user_cancelled || !snapshot.enabled) {
            set_wifi_last_error(snapshot.user_cancelled ? "cancelled" : "none");
        } else if (snapshot.safe_mode) {
            set_wifi_last_error("safe_mode_disabled");
        } else {
            set_wifi_last_error(d1l_wifi_failure_class_name(failure));
        }
        if (scheduled) {
            notify_wifi_retry_worker();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        portENTER_CRITICAL(&s_wifi_policy_lock);
        d1l_wifi_retry_policy_mark_starting(&s_wifi_policy);
        portEXIT_CRITICAL(&s_wifi_policy_lock);
    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_CONNECTED) {
        portENTER_CRITICAL(&s_wifi_policy_lock);
        d1l_wifi_retry_policy_mark_connecting(&s_wifi_policy);
        const bool connection_accepted =
            s_wifi_policy.state == D1L_WIFI_RUNTIME_CONNECTING;
        portEXIT_CRITICAL(&s_wifi_policy_lock);
        s_wifi_connecting = connection_accepted;
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
        clear_local_leave_token();
        portENTER_CRITICAL(&s_wifi_policy_lock);
        d1l_wifi_retry_policy_connected(&s_wifi_policy);
        const bool connection_accepted =
            s_wifi_policy.state == D1L_WIFI_RUNTIME_CONNECTED;
        portEXIT_CRITICAL(&s_wifi_policy_lock);
        if (!connection_accepted) {
            s_wifi_connected = false;
            s_wifi_connecting = false;
            s_wifi_ip[0] = '\0';
            return;
        }
        s_wifi_connected = true;
        s_wifi_connecting = false;
        s_wifi_last_disconnect_reason = 0U;
        if (event) {
            snprintf(s_wifi_ip, sizeof(s_wifi_ip), IPSTR, IP2STR(&event->ip_info.ip));
        }
        set_wifi_last_error("none");
    }
}

static void stop_wifi_runtime(void)
{
    clear_local_leave_token();
    if (s_wifi_event_instance) {
        (void)esp_event_handler_instance_unregister(
            WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_event_instance);
        s_wifi_event_instance = NULL;
    }
    if (s_ip_event_instance) {
        (void)esp_event_handler_instance_unregister(
            IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_event_instance);
        s_ip_event_instance = NULL;
    }
    if (s_wifi_started) {
        (void)esp_wifi_disconnect();
    }
    if (s_wifi_initialized) {
        (void)esp_wifi_stop();
        (void)esp_wifi_deinit();
    }
    if (s_wifi_sta_netif) {
        esp_netif_destroy_default_wifi(s_wifi_sta_netif);
        s_wifi_sta_netif = NULL;
    }
    s_wifi_started = false;
    s_wifi_initialized = false;
    s_wifi_connected = false;
    s_wifi_connecting = false;
    s_wifi_ip[0] = '\0';
}

static esp_err_t fail_closed_wifi_runtime(esp_err_t failure, const char *reason)
{
    char original_reason[sizeof(s_wifi_last_error)];
    snprintf(original_reason, sizeof(original_reason), "%s",
             reason ? reason : esp_err_to_name(failure));

    portENTER_CRITICAL(&s_wifi_policy_lock);
    d1l_wifi_retry_policy_disable(&s_wifi_policy);
    portEXIT_CRITICAL(&s_wifi_policy_lock);
    notify_wifi_retry_worker();
    stop_wifi_runtime();

    const d1l_settings_t *current = d1l_settings_current();
    if (current->wifi_enabled) {
        d1l_settings_t safe_settings = *current;
        safe_settings.wifi_enabled = false;
        esp_err_t rollback_ret = d1l_settings_save(&safe_settings);
        if (rollback_ret != ESP_OK) {
            set_wifi_last_error("rollback_save_failed");
            return rollback_ret;
        }
    }

    set_wifi_last_error(original_reason);
    return failure;
}

static const char *wifi_boot_recovery_reason(void)
{
    switch (esp_reset_reason()) {
    case ESP_RST_PANIC:
        return "boot_recovery_panic";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
        return "boot_recovery_wdt";
    case ESP_RST_BROWNOUT:
        return "boot_recovery_brownout";
    default:
        return NULL;
    }
}

static esp_err_t ensure_wifi_started(void)
{
    if (!build_wifi_enabled()) {
        set_wifi_last_error("build_disabled");
        return ESP_ERR_NOT_SUPPORTED;
    }
    esp_err_t ret = ensure_wifi_retry_worker();
    if (ret != ESP_OK) {
        return fail_closed_wifi_runtime(ret, "retry_task_failed");
    }
    if (!s_wifi_initialized) {
        ret = ok_if_invalid_state(esp_netif_init());
        if (ret != ESP_OK) {
            return fail_closed_wifi_runtime(ret, esp_err_to_name(ret));
        }
        ret = ok_if_invalid_state(esp_event_loop_create_default());
        if (ret != ESP_OK) {
            return fail_closed_wifi_runtime(ret, esp_err_to_name(ret));
        }
        if (!s_wifi_sta_netif) {
            esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_WIFI_STA();
            s_wifi_sta_netif = esp_netif_new(&netif_config);
            if (!s_wifi_sta_netif) {
                return fail_closed_wifi_runtime(ESP_FAIL, "sta_netif_failed");
            }
            ret = esp_netif_attach_wifi_station(s_wifi_sta_netif);
            if (ret != ESP_OK) {
                return fail_closed_wifi_runtime(ret, esp_err_to_name(ret));
            }
            ret = esp_wifi_set_default_wifi_sta_handlers();
            if (ret != ESP_OK) {
                return fail_closed_wifi_runtime(ret, esp_err_to_name(ret));
            }
        }
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ret = esp_wifi_init(&cfg);
        if (ret != ESP_OK) {
            return fail_closed_wifi_runtime(ret, esp_err_to_name(ret));
        }
        s_wifi_initialized = true;
        ret = esp_wifi_set_storage(WIFI_STORAGE_RAM);
        if (ret != ESP_OK) {
            return fail_closed_wifi_runtime(ret, esp_err_to_name(ret));
        }
        ret = esp_wifi_set_mode(WIFI_MODE_STA);
        if (ret != ESP_OK) {
            return fail_closed_wifi_runtime(ret, esp_err_to_name(ret));
        }
    }
    if (!s_wifi_event_instance) {
        esp_err_t ret = esp_event_handler_instance_register(
            WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL,
            &s_wifi_event_instance);
        if (ret != ESP_OK) {
            return fail_closed_wifi_runtime(ret, esp_err_to_name(ret));
        }
    }
    if (!s_ip_event_instance) {
        esp_err_t ret = esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL,
            &s_ip_event_instance);
        if (ret != ESP_OK) {
            return fail_closed_wifi_runtime(ret, esp_err_to_name(ret));
        }
    }
    if (!s_wifi_started) {
        esp_err_t ret = esp_wifi_start();
        if (ret != ESP_OK) {
            return fail_closed_wifi_runtime(ret, esp_err_to_name(ret));
        }
        s_wifi_started = true;
        set_wifi_last_error("none");
    }
    return ESP_OK;
}

static void copy_scan_record(d1l_wifi_scan_ap_t *dest, const wifi_ap_record_t *src)
{
    if (!dest || !src) {
        return;
    }
    memset(dest, 0, sizeof(*dest));
    size_t len = 0;
    while (len < sizeof(src->ssid) && src->ssid[len] != '\0') {
        len++;
    }
    if (len >= sizeof(dest->ssid)) {
        len = sizeof(dest->ssid) - 1U;
    }
    memcpy(dest->ssid, src->ssid, len);
    dest->ssid[len] = '\0';
    dest->rssi_dbm = src->rssi;
    dest->channel = src->primary;
    dest->auth = wifi_auth_name(src->authmode);
}

static void copy_wifi_config_field(uint8_t *dest, size_t dest_size, const char *src)
{
    if (!dest || dest_size == 0U) {
        return;
    }
    memset(dest, 0, dest_size);
    if (!src) {
        return;
    }
    size_t len = 0;
    while (len < dest_size && src[len] != '\0') {
        dest[len] = (uint8_t)src[len];
        len++;
    }
}

static void finish_wifi_scan_policy(void)
{
    portENTER_CRITICAL(&s_wifi_policy_lock);
    d1l_wifi_retry_policy_finish_scan(&s_wifi_policy, s_wifi_connected,
                                      s_wifi_connecting);
    portEXIT_CRITICAL(&s_wifi_policy_lock);
    notify_wifi_retry_worker();
}
#endif

static void fill_status(d1l_connectivity_status_t *out_status)
{
    if (!out_status) {
        return;
    }
    memset(out_status, 0, sizeof(*out_status));
    const d1l_settings_t *settings = d1l_settings_current();
    const d1l_wifi_retry_policy_t policy = wifi_policy_snapshot();
    out_status->usb_console_ready = true;
    out_status->wifi_enabled_setting = settings->wifi_enabled;
    out_status->ble_companion_enabled_setting = settings->ble_companion_enabled;
    out_status->observer_enabled_setting = settings->observer_enabled;
    out_status->wifi_build_enabled = build_wifi_enabled();
    out_status->ble_build_enabled = build_ble_enabled();
    out_status->wifi_stack_active = s_wifi_started;
    out_status->wifi_connected = s_wifi_connected;
    out_status->wifi_connecting = s_wifi_connecting;
    out_status->wifi_retry_scheduled = policy.retry_scheduled;
    out_status->wifi_user_cancelled = policy.user_cancelled;
    out_status->wifi_safe_mode = policy.safe_mode;
    out_status->ble_stack_active = false;
    out_status->wifi_profile_saved = settings->wifi_profile_saved;
    out_status->wifi_password_saved = settings->wifi_password[0] != '\0';
    out_status->wifi_scan_supported = out_status->wifi_build_enabled;
    out_status->wifi_state = out_status->wifi_build_enabled ?
        d1l_wifi_runtime_state_name(policy.state) : "off";
    out_status->ble_state =
        ble_runtime_state(settings->ble_companion_enabled, out_status->ble_build_enabled);
    out_status->wifi_ssid = settings->wifi_ssid;
    out_status->wifi_ip = s_wifi_ip;
    out_status->wifi_retry_attempt = policy.retry_attempt;
    out_status->wifi_last_disconnect_reason = s_wifi_last_disconnect_reason;
    out_status->wifi_retry_delay_ms = policy.retry_delay_ms;
    out_status->wifi_last_error = s_wifi_last_error;
    out_status->wifi_last_failure_class =
        d1l_wifi_failure_class_name(policy.last_failure);
#ifdef CONFIG_ESP_WIFI_ENABLED
    if (s_wifi_retry_task) {
        out_status->wifi_retry_task_stack_high_water_bytes =
            (uint32_t)uxTaskGetStackHighWaterMark(s_wifi_retry_task);
    }
    if (s_wifi_started && s_wifi_connected) {
        wifi_ap_record_t ap = {0};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            out_status->wifi_rssi_dbm = ap.rssi;
            out_status->wifi_channel = ap.primary;
        }
    }
#endif
    out_status->coexistence_policy = "offline_first_one_companion_radio";
}

esp_err_t d1l_connectivity_prepare_reboot(void)
{
    portENTER_CRITICAL(&s_wifi_policy_lock);
    d1l_wifi_retry_policy_cancel(&s_wifi_policy);
    portEXIT_CRITICAL(&s_wifi_policy_lock);
#ifdef CONFIG_ESP_WIFI_ENABLED
    notify_wifi_retry_worker();
    /* esp_restart() normally replays the default Wi-Fi shutdown handler. The
     * controlled ROM system-reset path bypasses shutdown handlers, so mirror
     * that handler without changing the persisted Wi-Fi setting or profile. */
    if (s_wifi_initialized) {
        if (s_wifi_control_lock && !take_wifi_control()) {
            return ESP_FAIL;
        }
        esp_err_t ret = esp_wifi_stop();
        if (s_wifi_control_lock) {
            give_wifi_control();
        }
        if (ret != ESP_OK) {
            return ret;
        }
    }
#endif
    s_wifi_started = false;
    s_wifi_connected = false;
    s_wifi_connecting = false;
    s_wifi_ip[0] = '\0';
    return ESP_OK;
}

esp_err_t d1l_connectivity_init(void)
{
    d1l_settings_t settings = *d1l_settings_current();
    if (settings.wifi_enabled && settings.ble_companion_enabled) {
        settings.ble_companion_enabled = false;
        esp_err_t ret = d1l_settings_save(&settings);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    const d1l_settings_t *current = d1l_settings_current();
    portENTER_CRITICAL(&s_wifi_policy_lock);
    d1l_wifi_retry_policy_init(
        &s_wifi_policy,
        current->wifi_enabled && build_wifi_enabled(),
        current->wifi_profile_saved && current->wifi_ssid[0] != '\0');
    portEXIT_CRITICAL(&s_wifi_policy_lock);
    if (current->wifi_enabled && !build_wifi_enabled()) {
        set_wifi_last_error("build_disabled");
        return ESP_OK;
    }
#ifdef CONFIG_ESP_WIFI_ENABLED
    if (current->wifi_enabled && current->wifi_profile_saved) {
        const char *recovery_reason = wifi_boot_recovery_reason();
        if (recovery_reason) {
            /* Boot-local containment only. Persisted repeated-crash counting
             * and last-enabled-subsystem attribution remain a later WP-13
             * slice; never claim this one-reset guard as that closure. */
            portENTER_CRITICAL(&s_wifi_policy_lock);
            d1l_wifi_retry_policy_enter_safe_mode(&s_wifi_policy);
            portEXIT_CRITICAL(&s_wifi_policy_lock);
            set_wifi_last_error(recovery_reason);
            return ESP_OK;
        }
        return d1l_connectivity_wifi_connect();
    }
#endif
    return ESP_OK;
}

void d1l_connectivity_status(d1l_connectivity_status_t *out_status)
{
    fill_status(out_status);
}

esp_err_t d1l_connectivity_wifi_scan(d1l_wifi_scan_result_t *out_result)
{
    if (!out_result) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_result, 0, sizeof(*out_result));
    out_result->scan_supported = build_wifi_enabled();
    out_result->reason = "not_started";
    out_result->last_error = ESP_OK;
    const d1l_settings_t *settings = d1l_settings_current();
    if (!settings->wifi_enabled) {
        out_result->reason = "disabled_by_setting";
        out_result->last_error = ESP_ERR_INVALID_STATE;
        return ESP_ERR_INVALID_STATE;
    }
    if (!build_wifi_enabled()) {
        out_result->reason = "build_disabled";
        out_result->last_error = ESP_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }
#ifndef CONFIG_ESP_WIFI_ENABLED
    out_result->reason = "build_disabled";
    out_result->last_error = ESP_ERR_NOT_SUPPORTED;
    return ESP_ERR_NOT_SUPPORTED;
#else
    esp_err_t ret = ensure_wifi_started();
    if (ret != ESP_OK) {
        out_result->reason = s_wifi_last_error;
        out_result->last_error = ret;
        return ret;
    }
    portENTER_CRITICAL(&s_wifi_policy_lock);
    const bool scan_allowed = d1l_wifi_retry_policy_begin_scan(&s_wifi_policy);
    portEXIT_CRITICAL(&s_wifi_policy_lock);
    if (!scan_allowed) {
        out_result->reason = "scan_cancelled";
        out_result->last_error = ESP_ERR_INVALID_STATE;
        return ESP_ERR_INVALID_STATE;
    }
    if (!take_wifi_control()) {
        finish_wifi_scan_policy();
        out_result->reason = "scan_control_failed";
        out_result->last_error = ESP_FAIL;
        return ESP_FAIL;
    }
    ret = esp_wifi_scan_start(NULL, true);
    if (ret != ESP_OK) {
        give_wifi_control();
        finish_wifi_scan_policy();
        out_result->reason = esp_err_to_name(ret);
        out_result->last_error = ret;
        set_wifi_last_error(out_result->reason);
        return ret;
    }
    out_result->scan_started = true;

    uint16_t ap_count = 0;
    ret = esp_wifi_scan_get_ap_num(&ap_count);
    if (ret != ESP_OK) {
        give_wifi_control();
        finish_wifi_scan_policy();
        out_result->reason = esp_err_to_name(ret);
        out_result->last_error = ret;
        set_wifi_last_error(out_result->reason);
        return ret;
    }
    out_result->total_count = ap_count;
    uint16_t record_count = ap_count > D1L_WIFI_SCAN_MAX ? D1L_WIFI_SCAN_MAX : ap_count;
    out_result->truncated = ap_count > D1L_WIFI_SCAN_MAX;
    if (record_count > 0U) {
        wifi_ap_record_t records[D1L_WIFI_SCAN_MAX] = {0};
        uint16_t records_to_fetch = record_count;
        ret = esp_wifi_scan_get_ap_records(&records_to_fetch, records);
        if (ret != ESP_OK) {
            give_wifi_control();
            finish_wifi_scan_policy();
            out_result->reason = esp_err_to_name(ret);
            out_result->last_error = ret;
            set_wifi_last_error(out_result->reason);
            return ret;
        }
        out_result->returned_count = records_to_fetch;
        for (uint16_t i = 0; i < records_to_fetch; ++i) {
            copy_scan_record(&out_result->aps[i], &records[i]);
        }
    } else {
        out_result->returned_count = 0;
    }
    out_result->reason = "ok";
    out_result->last_error = ESP_OK;
    give_wifi_control();
    finish_wifi_scan_policy();
    set_wifi_last_error("none");
    return ESP_OK;
#endif
}

esp_err_t d1l_connectivity_wifi_connect(void)
{
    const d1l_settings_t *settings = d1l_settings_current();
    if (!settings->wifi_enabled) {
        set_wifi_last_error("disabled_by_setting");
        return ESP_ERR_INVALID_STATE;
    }
    if (!settings->wifi_profile_saved || settings->wifi_ssid[0] == '\0') {
        set_wifi_last_error("profile_required");
        return ESP_ERR_INVALID_STATE;
    }
    if (!build_wifi_enabled()) {
        set_wifi_last_error("build_disabled");
        return ESP_ERR_NOT_SUPPORTED;
    }
    portENTER_CRITICAL(&s_wifi_policy_lock);
    const bool connect_allowed =
        d1l_wifi_retry_policy_begin_manual_connect(&s_wifi_policy);
    portEXIT_CRITICAL(&s_wifi_policy_lock);
    if (!connect_allowed) {
        set_wifi_last_error("profile_required");
        return ESP_ERR_INVALID_STATE;
    }
#ifndef CONFIG_ESP_WIFI_ENABLED
    return ESP_ERR_NOT_SUPPORTED;
#else
    notify_wifi_retry_worker();
    esp_err_t ret = ensure_wifi_started();
    if (ret != ESP_OK) {
        return ret;
    }
    if (!take_wifi_control()) {
        set_wifi_last_error("connect_control_failed");
        return ESP_FAIL;
    }
    wifi_config_t config = {0};
    copy_wifi_config_field(config.sta.ssid, sizeof(config.sta.ssid),
                           settings->wifi_ssid);
    copy_wifi_config_field(config.sta.password, sizeof(config.sta.password),
                           settings->wifi_password);
    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;
    if (s_wifi_connected || s_wifi_connecting) {
        arm_local_leave_token();
        ret = esp_wifi_disconnect();
        if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_CONNECT) {
            clear_local_leave_token();
            memset(&config, 0, sizeof(config));
            give_wifi_control();
            return fail_closed_wifi_runtime(ret, esp_err_to_name(ret));
        }
        if (ret == ESP_ERR_WIFI_NOT_CONNECT) {
            clear_local_leave_token();
        }
        s_wifi_connected = false;
        s_wifi_connecting = false;
        s_wifi_ip[0] = '\0';
    }
    ret = esp_wifi_set_config(WIFI_IF_STA, &config);
    memset(&config, 0, sizeof(config));
    if (ret != ESP_OK) {
        clear_local_leave_token();
        give_wifi_control();
        return fail_closed_wifi_runtime(ret, esp_err_to_name(ret));
    }
    ret = esp_wifi_connect();
    /* Retain one pending local-leave token across a successful asynchronous
     * connect.  Only an ASSOC_LEAVE event may consume it as intentional;
     * GOT_IP clears it, and every other disconnect clears then takes the
     * genuine-failure path below. */
    if (ret != ESP_OK) {
        clear_local_leave_token();
        s_wifi_connecting = false;
        set_wifi_last_error(esp_err_to_name(ret));
        const uint64_t now_ms = wifi_now_ms();
        const uint32_t jitter_seed = esp_random();
        uint32_t delay_ms = 0U;
        portENTER_CRITICAL(&s_wifi_policy_lock);
        const bool scheduled = d1l_wifi_retry_policy_on_disconnect(
            &s_wifi_policy, D1L_WIFI_FAILURE_TRANSIENT, now_ms,
            jitter_seed, &delay_ms);
        portEXIT_CRITICAL(&s_wifi_policy_lock);
        give_wifi_control();
        if (scheduled) {
            notify_wifi_retry_worker();
        }
        return ret;
    }
    portENTER_CRITICAL(&s_wifi_policy_lock);
    d1l_wifi_retry_policy_mark_connecting(&s_wifi_policy);
    const bool still_connecting =
        s_wifi_policy.state == D1L_WIFI_RUNTIME_CONNECTING;
    portEXIT_CRITICAL(&s_wifi_policy_lock);
    if (!still_connecting) {
        (void)esp_wifi_disconnect();
        s_wifi_connected = false;
        s_wifi_connecting = false;
        s_wifi_ip[0] = '\0';
        give_wifi_control();
        set_wifi_last_error("cancelled");
        return ESP_ERR_INVALID_STATE;
    }
    s_wifi_connecting = true;
    give_wifi_control();
    set_wifi_last_error("none");
    return ESP_OK;
#endif
}

esp_err_t d1l_connectivity_wifi_disconnect(void)
{
    portENTER_CRITICAL(&s_wifi_policy_lock);
    d1l_wifi_retry_policy_cancel(&s_wifi_policy);
    portEXIT_CRITICAL(&s_wifi_policy_lock);
#ifdef CONFIG_ESP_WIFI_ENABLED
    notify_wifi_retry_worker();
    if (s_wifi_started) {
        if (!take_wifi_control()) {
            set_wifi_last_error("disconnect_control_failed");
            return ESP_FAIL;
        }
        esp_err_t ret = esp_wifi_disconnect();
        give_wifi_control();
        if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_CONNECT) {
            set_wifi_last_error(esp_err_to_name(ret));
            return ret;
        }
    }
#endif
    s_wifi_connected = false;
    s_wifi_connecting = false;
    s_wifi_ip[0] = '\0';
    set_wifi_last_error("cancelled");
    return ESP_OK;
}

esp_err_t d1l_connectivity_set_wifi_enabled(bool enabled)
{
    if (enabled && !build_wifi_enabled()) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    d1l_settings_t settings = *d1l_settings_current();
    esp_err_t ret;
    if (!enabled) {
        settings.wifi_enabled = false;
        ret = d1l_settings_save(&settings);
        if (ret != ESP_OK) {
            return ret;
        }
        portENTER_CRITICAL(&s_wifi_policy_lock);
        d1l_wifi_retry_policy_disable(&s_wifi_policy);
        portEXIT_CRITICAL(&s_wifi_policy_lock);
#ifdef CONFIG_ESP_WIFI_ENABLED
        notify_wifi_retry_worker();
        if (s_wifi_control_lock && !take_wifi_control()) {
            set_wifi_last_error("disable_control_failed");
            return ESP_FAIL;
        }
        stop_wifi_runtime();
        if (s_wifi_control_lock) {
            give_wifi_control();
        }
#endif
        set_wifi_last_error("none");
        return ESP_OK;
    }

    settings.ble_companion_enabled = false;
#ifdef CONFIG_ESP_WIFI_ENABLED
    ret = ensure_wifi_started();
    if (ret != ESP_OK) {
        return ret;
    }
#endif

    settings.wifi_enabled = true;
    ret = d1l_settings_save(&settings);
    if (ret != ESP_OK) {
#ifdef CONFIG_ESP_WIFI_ENABLED
        stop_wifi_runtime();
#endif
        set_wifi_last_error(esp_err_to_name(ret));
        return ret;
    }

    portENTER_CRITICAL(&s_wifi_policy_lock);
    d1l_wifi_retry_policy_configure(
        &s_wifi_policy, true,
        settings.wifi_profile_saved && settings.wifi_ssid[0] != '\0');
    portEXIT_CRITICAL(&s_wifi_policy_lock);

#ifdef CONFIG_ESP_WIFI_ENABLED
    if (settings.wifi_profile_saved) {
        ret = d1l_connectivity_wifi_connect();
        if (ret != ESP_OK) {
            return ret;
        }
    }
#endif
    return ESP_OK;
}

esp_err_t d1l_connectivity_set_ble_enabled(bool enabled)
{
    if (enabled && !build_ble_enabled()) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    d1l_settings_t settings = *d1l_settings_current();
    settings.ble_companion_enabled = enabled;
    if (enabled) {
        settings.wifi_enabled = false;
    }
    const esp_err_t ret = d1l_settings_save(&settings);
    if (ret != ESP_OK) {
        return ret;
    }
    if (enabled) {
        portENTER_CRITICAL(&s_wifi_policy_lock);
        d1l_wifi_retry_policy_disable(&s_wifi_policy);
        portEXIT_CRITICAL(&s_wifi_policy_lock);
#ifdef CONFIG_ESP_WIFI_ENABLED
        notify_wifi_retry_worker();
        if (s_wifi_control_lock && !take_wifi_control()) {
            return ESP_FAIL;
        }
        stop_wifi_runtime();
        if (s_wifi_control_lock) {
            give_wifi_control();
        }
#endif
    }
    return ESP_OK;
}

esp_err_t d1l_connectivity_save_wifi_profile(const char *ssid, const char *password)
{
    esp_err_t ret = d1l_settings_save_wifi_profile(ssid, password);
    if (ret != ESP_OK) {
        return ret;
    }
    const d1l_settings_t *settings = d1l_settings_current();
    portENTER_CRITICAL(&s_wifi_policy_lock);
    d1l_wifi_retry_policy_configure(
        &s_wifi_policy, settings->wifi_enabled && build_wifi_enabled(),
        settings->wifi_profile_saved && settings->wifi_ssid[0] != '\0');
    portEXIT_CRITICAL(&s_wifi_policy_lock);
    if (settings->wifi_enabled && build_wifi_enabled()) {
        return d1l_connectivity_wifi_connect();
    }
    return ESP_OK;
}

esp_err_t d1l_connectivity_clear_wifi_profile(void)
{
    esp_err_t ret = d1l_settings_clear_wifi_profile();
    if (ret == ESP_OK) {
        (void)d1l_connectivity_wifi_disconnect();
        const d1l_settings_t *settings = d1l_settings_current();
        portENTER_CRITICAL(&s_wifi_policy_lock);
        d1l_wifi_retry_policy_configure(
            &s_wifi_policy, settings->wifi_enabled && build_wifi_enabled(),
            false);
        portEXIT_CRITICAL(&s_wifi_policy_lock);
        set_wifi_last_error(settings->wifi_enabled ? "profile_required" : "none");
    }
    return ret;
}
