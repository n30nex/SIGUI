#pragma once

#include <stddef.h>

typedef struct {
    size_t total;
    size_t chat_companion;
    size_t repeater;
    size_t room_server;
    size_t sensor;
    size_t unknown;
} d1l_ui_node_role_counts_t;

void d1l_ui_nodes_summarize_roles(const char *const *roles,
                                  size_t row_count,
                                  size_t role_capacity,
                                  d1l_ui_node_role_counts_t *out_counts);
