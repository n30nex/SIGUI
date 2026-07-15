#include <assert.h>
#include <stdio.h>

#include "ui/ui_messages.h"

int main(void)
{
    assert(D1L_UI_MESSAGES_THREAD_MAX_ROWS == D1L_DM_STORE_CAPACITY + 1U);
    assert(sizeof(d1l_ui_messages_controller_t) <=
           D1L_UI_MESSAGES_CONTROLLER_MAX_BYTES);
    assert(D1L_UI_MESSAGES_CHANNEL_CONTROL_BINDING_COUNT == 3U);
    assert(D1L_CHANNEL_STORE_CAPACITY == 8U);

    d1l_ui_messages_controller_t controller = {0};
    controller.thread_row_count = D1L_UI_MESSAGES_THREAD_MAX_ROWS;
    assert(d1l_ui_messages_thread_row_index_valid(&controller, 0U));
    assert(d1l_ui_messages_thread_row_index_valid(
        &controller, D1L_DM_STORE_CAPACITY));
    assert(!d1l_ui_messages_thread_row_index_valid(
        &controller, D1L_UI_MESSAGES_THREAD_MAX_ROWS));
    assert(!d1l_ui_messages_thread_row_index_valid(NULL, 0U));

    controller.thread_row_count = 0U;
    assert(!d1l_ui_messages_thread_row_index_valid(&controller, 0U));

    assert(d1l_ui_messages_store_state(false, true, false) ==
           D1L_UI_MESSAGES_STORE_LOADING);
    assert(d1l_ui_messages_store_state(true, true, false) ==
           D1L_UI_MESSAGES_STORE_READY);
    assert(d1l_ui_messages_store_state(true, true, true) ==
           D1L_UI_MESSAGES_STORE_DEGRADED);
    assert(d1l_ui_messages_store_state(false, false, false) ==
           D1L_UI_MESSAGES_STORE_UNAVAILABLE);
    assert(d1l_ui_messages_store_state(true, false, false) ==
           D1L_UI_MESSAGES_STORE_UNAVAILABLE);

    assert(d1l_ui_messages_delivery_retry_active(
        D1L_DM_DELIVERY_RETRY_WAIT));
    assert(d1l_ui_messages_delivery_retry_active(
        D1L_DM_DELIVERY_RETRY_TX));
    assert(!d1l_ui_messages_delivery_retry_active(
        D1L_DM_DELIVERY_FAILED_TIMEOUT));
    assert(d1l_ui_messages_delivery_failure_latched(
        D1L_DM_DELIVERY_FAILED_RADIO));
    assert(d1l_ui_messages_delivery_failure_latched(
        D1L_DM_DELIVERY_FAILED_TIMEOUT));
    assert(d1l_ui_messages_delivery_failure_latched(
        D1L_DM_DELIVERY_FAILED_QUEUE));
    assert(d1l_ui_messages_delivery_failure_latched(
        D1L_DM_DELIVERY_INTERRUPTED_BY_REBOOT));
    assert(!d1l_ui_messages_delivery_failure_latched(
        D1L_DM_DELIVERY_RETRY_WAIT));

    puts("native UI messages volatile boundary: ok");
    return 0;
}
