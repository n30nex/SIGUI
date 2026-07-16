#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "ui/ui_nodes_summary.h"

static void test_exact_canonical_counts_and_unknown_fail_closed(void)
{
    const char *roles[] = {
        "companion",
        "chat",
        "repeater",
        "room",
        "sensor",
        "",
        "Repeater",
        "mystery",
    };
    d1l_ui_node_role_counts_t counts = {0};
    d1l_ui_nodes_summarize_roles(
        roles, sizeof(roles) / sizeof(roles[0]),
        sizeof(roles) / sizeof(roles[0]), &counts);

    assert(counts.total == 8U);
    assert(counts.chat_companion == 2U);
    assert(counts.repeater == 1U);
    assert(counts.room_server == 1U);
    assert(counts.sensor == 1U);
    assert(counts.unknown == 3U);
    assert(counts.total == counts.chat_companion + counts.repeater +
        counts.room_server + counts.sensor + counts.unknown);
}

static void test_null_and_capacity_bounds(void)
{
    d1l_ui_node_role_counts_t counts = {
        .total = 99U,
        .unknown = 99U,
    };
    d1l_ui_nodes_summarize_roles(NULL, 4U, 4U, &counts);
    assert(counts.total == 0U);
    assert(counts.unknown == 0U);

    const char *roles[] = {"sensor", "sensor", "sensor"};
    d1l_ui_nodes_summarize_roles(roles, SIZE_MAX, 3U, &counts);
    assert(counts.total == 3U);
    assert(counts.sensor == 3U);

    d1l_ui_nodes_summarize_roles(roles, 1U, 3U, NULL);
}

int main(void)
{
    test_exact_canonical_counts_and_unknown_fail_closed();
    test_null_and_capacity_bounds();
    puts("native UI Nodes role summary: ok");
    return 0;
}
