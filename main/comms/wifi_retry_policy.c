#include "wifi_retry_policy.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

_Static_assert(D1L_WIFI_RETRY_BASE_DELAY_MS > 0U,
               "Wi-Fi retry base delay must be nonzero");
_Static_assert(D1L_WIFI_RETRY_BASE_DELAY_MS <= D1L_WIFI_RETRY_MAX_DELAY_MS,
               "Wi-Fi retry base delay must fit the maximum delay");
_Static_assert(D1L_WIFI_RETRY_MAX_JITTER_MS < D1L_WIFI_RETRY_MAX_DELAY_MS,
               "Wi-Fi retry jitter must leave room for backoff");
_Static_assert(D1L_WIFI_RETRY_MAX_ATTEMPTS > 0U,
               "Wi-Fi retry policy must permit at least one attempt");

static void clear_retry(d1l_wifi_retry_policy_t *policy)
{
    policy->retry_window_started_ms = 0U;
    policy->retry_due_ms = 0U;
    policy->retry_delay_ms = 0U;
    policy->retry_attempt = 0U;
    policy->retry_window_active = false;
    policy->retry_scheduled = false;
}

static void select_idle_state(d1l_wifi_retry_policy_t *policy)
{
    if (!policy->enabled || policy->user_cancelled) {
        policy->state = D1L_WIFI_RUNTIME_OFF;
    } else if (policy->safe_mode) {
        policy->state = D1L_WIFI_RUNTIME_SAFE_MODE_DISABLED;
    } else if (!policy->profile_available) {
        policy->state = D1L_WIFI_RUNTIME_PROFILE_REQUIRED;
    } else {
        policy->state = D1L_WIFI_RUNTIME_STARTING;
    }
}

static uint32_t retry_delay_ms(uint8_t attempt, uint32_t jitter_seed)
{
    uint64_t base = D1L_WIFI_RETRY_BASE_DELAY_MS;
    for (uint8_t shift = 0U; shift < attempt; ++shift) {
        base *= 2U;
        if (base >= D1L_WIFI_RETRY_MAX_DELAY_MS) {
            base = D1L_WIFI_RETRY_MAX_DELAY_MS;
            break;
        }
    }
    if (base > D1L_WIFI_RETRY_MAX_DELAY_MS) {
        base = D1L_WIFI_RETRY_MAX_DELAY_MS;
    }

    uint32_t jitter_cap = (uint32_t)(base / 4U);
    if (jitter_cap > D1L_WIFI_RETRY_MAX_JITTER_MS) {
        jitter_cap = D1L_WIFI_RETRY_MAX_JITTER_MS;
    }
    const uint32_t room = D1L_WIFI_RETRY_MAX_DELAY_MS - (uint32_t)base;
    if (jitter_cap > room) {
        jitter_cap = room;
    }
    const uint32_t jitter = jitter_cap == 0U ? 0U :
        jitter_seed % (jitter_cap + 1U);
    return (uint32_t)base + jitter;
}

void d1l_wifi_retry_policy_init(d1l_wifi_retry_policy_t *policy,
                                bool enabled,
                                bool profile_available)
{
    if (!policy) {
        return;
    }
    memset(policy, 0, sizeof(*policy));
    policy->enabled = enabled;
    policy->profile_available = profile_available;
    select_idle_state(policy);
}

void d1l_wifi_retry_policy_configure(d1l_wifi_retry_policy_t *policy,
                                     bool enabled,
                                     bool profile_available)
{
    if (!policy) {
        return;
    }
    policy->enabled = enabled;
    policy->profile_available = profile_available;
    policy->user_cancelled = false;
    policy->safe_mode = false;
    policy->last_failure = D1L_WIFI_FAILURE_NONE;
    clear_retry(policy);
    select_idle_state(policy);
}

bool d1l_wifi_retry_policy_begin_manual_connect(
    d1l_wifi_retry_policy_t *policy)
{
    if (!policy || !policy->enabled || !policy->profile_available) {
        if (policy) {
            policy->user_cancelled = false;
            policy->safe_mode = false;
            clear_retry(policy);
            select_idle_state(policy);
        }
        return false;
    }
    policy->user_cancelled = false;
    policy->safe_mode = false;
    policy->last_failure = D1L_WIFI_FAILURE_NONE;
    clear_retry(policy);
    policy->state = D1L_WIFI_RUNTIME_STARTING;
    return true;
}

void d1l_wifi_retry_policy_mark_starting(d1l_wifi_retry_policy_t *policy)
{
    if (!policy) {
        return;
    }
    if (policy->enabled && policy->profile_available &&
        !policy->user_cancelled && !policy->safe_mode) {
        policy->state = D1L_WIFI_RUNTIME_STARTING;
    } else {
        select_idle_state(policy);
    }
}

void d1l_wifi_retry_policy_mark_connecting(d1l_wifi_retry_policy_t *policy)
{
    if (!policy) {
        return;
    }
    if (policy->enabled && policy->profile_available &&
        !policy->user_cancelled && !policy->safe_mode) {
        policy->state = D1L_WIFI_RUNTIME_CONNECTING;
    } else {
        select_idle_state(policy);
    }
}

bool d1l_wifi_retry_policy_begin_scan(d1l_wifi_retry_policy_t *policy)
{
    if (!policy || !policy->enabled || policy->user_cancelled ||
        policy->safe_mode) {
        return false;
    }
    policy->state = D1L_WIFI_RUNTIME_SCANNING;
    return true;
}

void d1l_wifi_retry_policy_finish_scan(d1l_wifi_retry_policy_t *policy,
                                       bool connected,
                                       bool connecting)
{
    if (!policy) {
        return;
    }
    if (!policy->enabled || policy->user_cancelled || policy->safe_mode ||
        !policy->profile_available) {
        select_idle_state(policy);
    } else if (connected) {
        policy->state = D1L_WIFI_RUNTIME_CONNECTED;
    } else if (policy->retry_scheduled) {
        policy->state = D1L_WIFI_RUNTIME_RETRY_BACKOFF;
    } else if (connecting) {
        policy->state = D1L_WIFI_RUNTIME_CONNECTING;
    } else {
        policy->state = D1L_WIFI_RUNTIME_STARTING;
    }
}

bool d1l_wifi_retry_policy_on_disconnect(
    d1l_wifi_retry_policy_t *policy,
    d1l_wifi_failure_class_t failure,
    uint64_t now_ms,
    uint32_t jitter_seed,
    uint32_t *out_delay_ms)
{
    if (out_delay_ms) {
        *out_delay_ms = 0U;
    }
    if (!policy) {
        return false;
    }
    policy->retry_due_ms = 0U;
    policy->retry_delay_ms = 0U;
    policy->retry_scheduled = false;

    if (!policy->enabled || policy->user_cancelled || policy->safe_mode ||
        !policy->profile_available) {
        select_idle_state(policy);
        return false;
    }

    policy->last_failure = failure;
    if (failure == D1L_WIFI_FAILURE_AUTH) {
        clear_retry(policy);
        policy->last_failure = failure;
        policy->state = D1L_WIFI_RUNTIME_AUTH_FAILED;
        return false;
    }

    if (!policy->retry_window_active) {
        policy->retry_window_active = true;
        policy->retry_window_started_ms = now_ms;
    }
    if (now_ms < policy->retry_window_started_ms ||
        now_ms - policy->retry_window_started_ms >=
            D1L_WIFI_RETRY_MAX_WINDOW_MS ||
        policy->retry_attempt >= D1L_WIFI_RETRY_MAX_ATTEMPTS) {
        policy->state = D1L_WIFI_RUNTIME_AP_UNAVAILABLE;
        return false;
    }

    const uint32_t delay = retry_delay_ms(policy->retry_attempt, jitter_seed);
    const uint64_t elapsed = now_ms - policy->retry_window_started_ms;
    const uint64_t remaining = D1L_WIFI_RETRY_MAX_WINDOW_MS - elapsed;
    if ((uint64_t)delay >= remaining || UINT64_MAX - now_ms < delay) {
        policy->state = D1L_WIFI_RUNTIME_AP_UNAVAILABLE;
        return false;
    }

    policy->retry_attempt++;
    policy->retry_delay_ms = delay;
    policy->retry_due_ms = now_ms + delay;
    policy->retry_scheduled = true;
    policy->state = D1L_WIFI_RUNTIME_RETRY_BACKOFF;
    if (out_delay_ms) {
        *out_delay_ms = delay;
    }
    return true;
}

bool d1l_wifi_retry_policy_begin_due_retry(d1l_wifi_retry_policy_t *policy,
                                           uint64_t now_ms)
{
    if (!policy || !policy->enabled || !policy->profile_available ||
        policy->user_cancelled || policy->safe_mode ||
        !policy->retry_scheduled ||
        policy->state != D1L_WIFI_RUNTIME_RETRY_BACKOFF ||
        now_ms < policy->retry_due_ms) {
        return false;
    }
    if (!policy->retry_window_active ||
        now_ms < policy->retry_window_started_ms ||
        now_ms - policy->retry_window_started_ms >=
            D1L_WIFI_RETRY_MAX_WINDOW_MS) {
        policy->retry_scheduled = false;
        policy->retry_due_ms = 0U;
        policy->retry_delay_ms = 0U;
        policy->state = D1L_WIFI_RUNTIME_AP_UNAVAILABLE;
        return false;
    }
    policy->retry_scheduled = false;
    policy->retry_due_ms = 0U;
    policy->retry_delay_ms = 0U;
    policy->state = D1L_WIFI_RUNTIME_CONNECTING;
    return true;
}

void d1l_wifi_retry_policy_connected(d1l_wifi_retry_policy_t *policy)
{
    if (!policy) {
        return;
    }
    if (!policy->enabled || policy->user_cancelled || policy->safe_mode ||
        !policy->profile_available) {
        clear_retry(policy);
        select_idle_state(policy);
        return;
    }
    policy->user_cancelled = false;
    policy->safe_mode = false;
    policy->last_failure = D1L_WIFI_FAILURE_NONE;
    clear_retry(policy);
    policy->state = D1L_WIFI_RUNTIME_CONNECTED;
}

void d1l_wifi_retry_policy_cancel(d1l_wifi_retry_policy_t *policy)
{
    if (!policy) {
        return;
    }
    policy->user_cancelled = true;
    policy->last_failure = D1L_WIFI_FAILURE_NONE;
    clear_retry(policy);
    policy->state = D1L_WIFI_RUNTIME_OFF;
}

void d1l_wifi_retry_policy_disable(d1l_wifi_retry_policy_t *policy)
{
    if (!policy) {
        return;
    }
    policy->enabled = false;
    policy->user_cancelled = false;
    policy->safe_mode = false;
    policy->last_failure = D1L_WIFI_FAILURE_NONE;
    clear_retry(policy);
    policy->state = D1L_WIFI_RUNTIME_OFF;
}

void d1l_wifi_retry_policy_enter_safe_mode(d1l_wifi_retry_policy_t *policy)
{
    if (!policy) {
        return;
    }
    policy->safe_mode = true;
    policy->user_cancelled = false;
    clear_retry(policy);
    policy->state = D1L_WIFI_RUNTIME_SAFE_MODE_DISABLED;
}

void d1l_wifi_retry_policy_clear_safe_mode(d1l_wifi_retry_policy_t *policy)
{
    if (!policy) {
        return;
    }
    policy->safe_mode = false;
    clear_retry(policy);
    select_idle_state(policy);
}

void d1l_wifi_local_leave_token_arm(d1l_wifi_local_leave_token_t *token)
{
    if (token) {
        token->pending = true;
    }
}

void d1l_wifi_local_leave_token_clear(d1l_wifi_local_leave_token_t *token)
{
    if (token) {
        token->pending = false;
    }
}

bool d1l_wifi_local_leave_token_consume(
    d1l_wifi_local_leave_token_t *token,
    bool association_leave)
{
    if (!token) {
        return false;
    }
    const bool expected = token->pending && association_leave;
    token->pending = false;
    return expected;
}

const char *d1l_wifi_runtime_state_name(d1l_wifi_runtime_state_t state)
{
    switch (state) {
        case D1L_WIFI_RUNTIME_OFF:
            return "off";
        case D1L_WIFI_RUNTIME_PROFILE_REQUIRED:
            return "profile_required";
        case D1L_WIFI_RUNTIME_STARTING:
            return "starting";
        case D1L_WIFI_RUNTIME_SCANNING:
            return "scanning";
        case D1L_WIFI_RUNTIME_CONNECTING:
            return "connecting";
        case D1L_WIFI_RUNTIME_CONNECTED:
            return "connected";
        case D1L_WIFI_RUNTIME_AUTH_FAILED:
            return "auth_failed";
        case D1L_WIFI_RUNTIME_AP_UNAVAILABLE:
            return "ap_unavailable";
        case D1L_WIFI_RUNTIME_RETRY_BACKOFF:
            return "retry_backoff";
        case D1L_WIFI_RUNTIME_SAFE_MODE_DISABLED:
            return "safe_mode_disabled";
        default:
            return "off";
    }
}

const char *d1l_wifi_failure_class_name(d1l_wifi_failure_class_t failure)
{
    switch (failure) {
        case D1L_WIFI_FAILURE_NONE:
            return "none";
        case D1L_WIFI_FAILURE_TRANSIENT:
            return "transient_disconnect";
        case D1L_WIFI_FAILURE_AUTH:
            return "auth_failed";
        case D1L_WIFI_FAILURE_AP_UNAVAILABLE:
            return "ap_unavailable";
        default:
            return "transient_disconnect";
    }
}
