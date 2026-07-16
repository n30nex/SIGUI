#include "ui_nodes_summary.h"

#include <stdbool.h>
#include <string.h>

static bool role_equals(const char *role, const char *expected)
{
    return role && expected && strcmp(role, expected) == 0;
}

void d1l_ui_nodes_summarize_roles(const char *const *roles,
                                  size_t row_count,
                                  size_t role_capacity,
                                  d1l_ui_node_role_counts_t *out_counts)
{
    if (!out_counts) {
        return;
    }
    *out_counts = (d1l_ui_node_role_counts_t){0};
    if (!roles) {
        return;
    }
    if (row_count > role_capacity) {
        row_count = role_capacity;
    }
    for (size_t i = 0; i < row_count; ++i) {
        const char *role = roles[i];
        if (role_equals(role, "companion") || role_equals(role, "chat")) {
            ++out_counts->chat_companion;
        } else if (role_equals(role, "repeater")) {
            ++out_counts->repeater;
        } else if (role_equals(role, "room")) {
            ++out_counts->room_server;
        } else if (role_equals(role, "sensor")) {
            ++out_counts->sensor;
        } else {
            ++out_counts->unknown;
        }
        ++out_counts->total;
    }
}
