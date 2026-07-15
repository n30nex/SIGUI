#include <assert.h>
#include <stdio.h>

#include "ui/ui_messages.h"

int main(void)
{
    assert(D1L_UI_MESSAGES_THREAD_MAX_ROWS == D1L_DM_STORE_CAPACITY + 1U);
    assert(sizeof(d1l_ui_messages_controller_t) <=
           D1L_UI_MESSAGES_CONTROLLER_MAX_BYTES);

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

    puts("native UI messages volatile boundary: ok");
    return 0;
}
