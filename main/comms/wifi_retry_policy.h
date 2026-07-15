#pragma once

#include <stdbool.h>
#include <stdint.h>

#define D1L_WIFI_RETRY_BASE_DELAY_MS 1000U
#define D1L_WIFI_RETRY_MAX_DELAY_MS 30000U
#define D1L_WIFI_RETRY_MAX_JITTER_MS 1000U
#define D1L_WIFI_RETRY_MAX_WINDOW_MS 300000U
#define D1L_WIFI_RETRY_MAX_ATTEMPTS 8U

typedef enum {
    D1L_WIFI_RUNTIME_OFF = 0,
    D1L_WIFI_RUNTIME_PROFILE_REQUIRED,
    D1L_WIFI_RUNTIME_STARTING,
    D1L_WIFI_RUNTIME_SCANNING,
    D1L_WIFI_RUNTIME_CONNECTING,
    D1L_WIFI_RUNTIME_CONNECTED,
    D1L_WIFI_RUNTIME_AUTH_FAILED,
    D1L_WIFI_RUNTIME_AP_UNAVAILABLE,
    D1L_WIFI_RUNTIME_RETRY_BACKOFF,
    D1L_WIFI_RUNTIME_SAFE_MODE_DISABLED,
} d1l_wifi_runtime_state_t;

typedef enum {
    D1L_WIFI_FAILURE_NONE = 0,
    D1L_WIFI_FAILURE_TRANSIENT,
    D1L_WIFI_FAILURE_AUTH,
    D1L_WIFI_FAILURE_AP_UNAVAILABLE,
} d1l_wifi_failure_class_t;

typedef struct {
    bool pending;
} d1l_wifi_local_leave_token_t;

typedef struct {
    d1l_wifi_runtime_state_t state;
    d1l_wifi_failure_class_t last_failure;
    uint64_t retry_window_started_ms;
    uint64_t retry_due_ms;
    uint32_t retry_delay_ms;
    uint8_t retry_attempt;
    bool enabled;
    bool profile_available;
    bool user_cancelled;
    bool safe_mode;
    bool retry_window_active;
    bool retry_scheduled;
} d1l_wifi_retry_policy_t;

void d1l_wifi_retry_policy_init(d1l_wifi_retry_policy_t *policy,
                                bool enabled,
                                bool profile_available);
void d1l_wifi_retry_policy_configure(d1l_wifi_retry_policy_t *policy,
                                     bool enabled,
                                     bool profile_available);
bool d1l_wifi_retry_policy_begin_manual_connect(
    d1l_wifi_retry_policy_t *policy);
void d1l_wifi_retry_policy_mark_starting(d1l_wifi_retry_policy_t *policy);
void d1l_wifi_retry_policy_mark_connecting(d1l_wifi_retry_policy_t *policy);
bool d1l_wifi_retry_policy_begin_scan(d1l_wifi_retry_policy_t *policy);
void d1l_wifi_retry_policy_finish_scan(d1l_wifi_retry_policy_t *policy,
                                       bool connected,
                                       bool connecting);
bool d1l_wifi_retry_policy_on_disconnect(
    d1l_wifi_retry_policy_t *policy,
    d1l_wifi_failure_class_t failure,
    uint64_t now_ms,
    uint32_t jitter_seed,
    uint32_t *out_delay_ms);
bool d1l_wifi_retry_policy_begin_due_retry(d1l_wifi_retry_policy_t *policy,
                                           uint64_t now_ms);
void d1l_wifi_retry_policy_connected(d1l_wifi_retry_policy_t *policy);
void d1l_wifi_retry_policy_cancel(d1l_wifi_retry_policy_t *policy);
void d1l_wifi_retry_policy_disable(d1l_wifi_retry_policy_t *policy);
void d1l_wifi_retry_policy_enter_safe_mode(d1l_wifi_retry_policy_t *policy);
void d1l_wifi_retry_policy_clear_safe_mode(d1l_wifi_retry_policy_t *policy);
void d1l_wifi_local_leave_token_arm(d1l_wifi_local_leave_token_t *token);
void d1l_wifi_local_leave_token_clear(d1l_wifi_local_leave_token_t *token);
bool d1l_wifi_local_leave_token_consume(
    d1l_wifi_local_leave_token_t *token,
    bool association_leave);
const char *d1l_wifi_runtime_state_name(d1l_wifi_runtime_state_t state);
const char *d1l_wifi_failure_class_name(d1l_wifi_failure_class_t failure);
