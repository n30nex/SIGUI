#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "comms/wifi_retry_policy.h"

static void assert_state(const d1l_wifi_retry_policy_t *policy,
                         d1l_wifi_runtime_state_t expected)
{
    assert(policy != NULL);
    assert(policy->state == expected);
}

static void test_truthful_states_and_manual_controls(void)
{
    d1l_wifi_retry_policy_t policy;
    d1l_wifi_retry_policy_init(&policy, false, false);
    assert_state(&policy, D1L_WIFI_RUNTIME_OFF);

    d1l_wifi_retry_policy_configure(&policy, true, false);
    assert_state(&policy, D1L_WIFI_RUNTIME_PROFILE_REQUIRED);
    assert(d1l_wifi_retry_policy_begin_scan(&policy));
    assert_state(&policy, D1L_WIFI_RUNTIME_SCANNING);
    d1l_wifi_retry_policy_finish_scan(&policy, false, false);
    assert_state(&policy, D1L_WIFI_RUNTIME_PROFILE_REQUIRED);

    d1l_wifi_retry_policy_configure(&policy, true, true);
    assert_state(&policy, D1L_WIFI_RUNTIME_STARTING);
    assert(d1l_wifi_retry_policy_begin_manual_connect(&policy));
    d1l_wifi_retry_policy_mark_connecting(&policy);
    assert_state(&policy, D1L_WIFI_RUNTIME_CONNECTING);

    d1l_wifi_retry_policy_connected(&policy);
    assert_state(&policy, D1L_WIFI_RUNTIME_CONNECTED);
    assert(policy.retry_attempt == 0U);
    assert(!policy.retry_scheduled);

    d1l_wifi_retry_policy_cancel(&policy);
    assert_state(&policy, D1L_WIFI_RUNTIME_OFF);
    assert(policy.user_cancelled);
    uint32_t delay_ms = UINT32_MAX;
    assert(!d1l_wifi_retry_policy_on_disconnect(
        &policy, D1L_WIFI_FAILURE_TRANSIENT, 10U, 0U, &delay_ms));
    assert(delay_ms == 0U);
    assert_state(&policy, D1L_WIFI_RUNTIME_OFF);

    /* A late GOT_IP from an in-flight attempt cannot undo user cancel. */
    d1l_wifi_retry_policy_connected(&policy);
    assert_state(&policy, D1L_WIFI_RUNTIME_OFF);
    assert(policy.user_cancelled);

    assert(d1l_wifi_retry_policy_begin_manual_connect(&policy));
    assert(!policy.user_cancelled);
    d1l_wifi_retry_policy_enter_safe_mode(&policy);
    assert_state(&policy, D1L_WIFI_RUNTIME_SAFE_MODE_DISABLED);
    assert(policy.safe_mode);
    /* Nor may a stale connection event escape boot-local safe mode. */
    d1l_wifi_retry_policy_connected(&policy);
    assert_state(&policy, D1L_WIFI_RUNTIME_SAFE_MODE_DISABLED);
    assert(policy.safe_mode);
    d1l_wifi_retry_policy_clear_safe_mode(&policy);
    assert_state(&policy, D1L_WIFI_RUNTIME_STARTING);

    d1l_wifi_retry_policy_disable(&policy);
    assert_state(&policy, D1L_WIFI_RUNTIME_OFF);
    assert(!policy.enabled);
    d1l_wifi_retry_policy_connected(&policy);
    assert_state(&policy, D1L_WIFI_RUNTIME_OFF);
    assert(!policy.enabled);
}

static void test_bounded_backoff_and_failure_classification(void)
{
    d1l_wifi_retry_policy_t policy;
    d1l_wifi_retry_policy_init(&policy, true, true);
    assert(d1l_wifi_retry_policy_begin_manual_connect(&policy));
    d1l_wifi_retry_policy_mark_connecting(&policy);

    uint32_t delay_ms = 0U;
    assert(d1l_wifi_retry_policy_on_disconnect(
        &policy, D1L_WIFI_FAILURE_TRANSIENT, 0U, 0U, &delay_ms));
    assert(delay_ms == D1L_WIFI_RETRY_BASE_DELAY_MS);
    assert(delay_ms <= D1L_WIFI_RETRY_MAX_DELAY_MS);
    assert_state(&policy, D1L_WIFI_RUNTIME_RETRY_BACKOFF);
    assert(policy.retry_attempt == 1U);
    assert(!d1l_wifi_retry_policy_begin_due_retry(&policy, delay_ms - 1U));
    assert(d1l_wifi_retry_policy_begin_due_retry(&policy, delay_ms));
    assert_state(&policy, D1L_WIFI_RUNTIME_CONNECTING);

    assert(d1l_wifi_retry_policy_on_disconnect(
        &policy, D1L_WIFI_FAILURE_AP_UNAVAILABLE, delay_ms + 1U,
        UINT32_MAX, &delay_ms));
    assert(delay_ms >= D1L_WIFI_RETRY_BASE_DELAY_MS * 2U);
    assert(delay_ms <= D1L_WIFI_RETRY_MAX_DELAY_MS);
    assert(policy.last_failure == D1L_WIFI_FAILURE_AP_UNAVAILABLE);
    assert_state(&policy, D1L_WIFI_RUNTIME_RETRY_BACKOFF);
    assert(d1l_wifi_retry_policy_begin_scan(&policy));
    assert_state(&policy, D1L_WIFI_RUNTIME_SCANNING);
    d1l_wifi_retry_policy_finish_scan(&policy, false, false);
    assert_state(&policy, D1L_WIFI_RUNTIME_RETRY_BACKOFF);
    assert(policy.retry_scheduled);

    assert(!d1l_wifi_retry_policy_on_disconnect(
        &policy, D1L_WIFI_FAILURE_AUTH, 5000U, 0U, &delay_ms));
    assert(delay_ms == 0U);
    assert_state(&policy, D1L_WIFI_RUNTIME_AUTH_FAILED);
    assert(!policy.retry_scheduled);

    assert(d1l_wifi_retry_policy_begin_manual_connect(&policy));
    assert(policy.retry_attempt == 0U);
    d1l_wifi_retry_policy_connected(&policy);
    assert(policy.last_failure == D1L_WIFI_FAILURE_NONE);
    assert(policy.retry_attempt == 0U);
}

static void test_attempt_and_window_limits(void)
{
    d1l_wifi_retry_policy_t policy;
    d1l_wifi_retry_policy_init(&policy, true, true);
    assert(d1l_wifi_retry_policy_begin_manual_connect(&policy));

    uint64_t now_ms = 0U;
    uint32_t delay_ms = 0U;
    for (uint8_t attempt = 0U;
         attempt < D1L_WIFI_RETRY_MAX_ATTEMPTS;
         ++attempt) {
        assert(d1l_wifi_retry_policy_on_disconnect(
            &policy, D1L_WIFI_FAILURE_AP_UNAVAILABLE, now_ms, attempt,
            &delay_ms));
        assert(delay_ms >= D1L_WIFI_RETRY_BASE_DELAY_MS);
        assert(delay_ms <= D1L_WIFI_RETRY_MAX_DELAY_MS);
        now_ms += delay_ms;
        assert(d1l_wifi_retry_policy_begin_due_retry(&policy, now_ms));
    }
    assert(!d1l_wifi_retry_policy_on_disconnect(
        &policy, D1L_WIFI_FAILURE_AP_UNAVAILABLE, now_ms, 0U, &delay_ms));
    assert_state(&policy, D1L_WIFI_RUNTIME_AP_UNAVAILABLE);
    assert(!policy.retry_scheduled);

    d1l_wifi_retry_policy_configure(&policy, true, true);
    assert(d1l_wifi_retry_policy_on_disconnect(
        &policy, D1L_WIFI_FAILURE_TRANSIENT, 1000U, 0U, &delay_ms));
    assert(!d1l_wifi_retry_policy_on_disconnect(
        &policy, D1L_WIFI_FAILURE_TRANSIENT,
        1000U + D1L_WIFI_RETRY_MAX_WINDOW_MS, 0U, &delay_ms));
    assert_state(&policy, D1L_WIFI_RUNTIME_AP_UNAVAILABLE);

    d1l_wifi_retry_policy_configure(&policy, true, true);
    assert(d1l_wifi_retry_policy_on_disconnect(
        &policy, D1L_WIFI_FAILURE_TRANSIENT, 2000U, 0U, &delay_ms));
    assert(!d1l_wifi_retry_policy_on_disconnect(
        &policy, D1L_WIFI_FAILURE_TRANSIENT, 1999U, 0U, &delay_ms));
    assert_state(&policy, D1L_WIFI_RUNTIME_AP_UNAVAILABLE);

    d1l_wifi_retry_policy_configure(&policy, true, true);
    assert(!d1l_wifi_retry_policy_on_disconnect(
        &policy, D1L_WIFI_FAILURE_TRANSIENT, UINT64_MAX - 500U, 0U,
        &delay_ms));
    assert_state(&policy, D1L_WIFI_RUNTIME_AP_UNAVAILABLE);
}

static void test_local_leave_token_does_not_hide_genuine_ap_loss(void)
{
    d1l_wifi_local_leave_token_t token = {0};

    /* A delayed event from our explicit disconnect is suppressed once. */
    d1l_wifi_local_leave_token_arm(&token);
    assert(token.pending);
    assert(d1l_wifi_local_leave_token_consume(&token, true));
    assert(!token.pending);
    assert(!d1l_wifi_local_leave_token_consume(&token, true));

    /* A real AP-loss reason clears the pending token and is not suppressed. */
    d1l_wifi_local_leave_token_arm(&token);
    assert(!d1l_wifi_local_leave_token_consume(&token, false));
    assert(!token.pending);
    assert(!d1l_wifi_local_leave_token_consume(&token, true));

    d1l_wifi_local_leave_token_arm(&token);
    d1l_wifi_local_leave_token_clear(&token);
    assert(!d1l_wifi_local_leave_token_consume(&token, true));
}

static void test_state_and_failure_names(void)
{
    const char *expected_states[] = {
        "off", "profile_required", "starting", "scanning", "connecting",
        "connected", "auth_failed", "ap_unavailable", "retry_backoff",
        "safe_mode_disabled",
    };
    for (uint8_t state = D1L_WIFI_RUNTIME_OFF;
         state <= D1L_WIFI_RUNTIME_SAFE_MODE_DISABLED;
         ++state) {
        assert(strcmp(d1l_wifi_runtime_state_name(
                          (d1l_wifi_runtime_state_t)state),
                      expected_states[state]) == 0);
    }
    assert(strcmp(d1l_wifi_failure_class_name(D1L_WIFI_FAILURE_NONE),
                  "none") == 0);
    assert(strcmp(d1l_wifi_failure_class_name(D1L_WIFI_FAILURE_TRANSIENT),
                  "transient_disconnect") == 0);
    assert(strcmp(d1l_wifi_failure_class_name(D1L_WIFI_FAILURE_AUTH),
                  "auth_failed") == 0);
    assert(strcmp(d1l_wifi_failure_class_name(
                      D1L_WIFI_FAILURE_AP_UNAVAILABLE),
                  "ap_unavailable") == 0);
}

int main(void)
{
    test_truthful_states_and_manual_controls();
    test_bounded_backoff_and_failure_classification();
    test_attempt_and_window_limits();
    test_local_leave_token_does_not_hide_genuine_ap_loss();
    test_state_and_failure_names();
    puts("native Wi-Fi retry policy: ok");
    return 0;
}
