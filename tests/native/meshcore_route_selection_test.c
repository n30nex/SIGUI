#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mesh/meshcore_ack_dispatch.h"
#include "mesh/meshcore_route_selection.h"

static void test_valid_direct_path_selection(void)
{
    uint8_t learned_path[] = {0x10U, 0x11U, 0x20U, 0x21U};
    d1l_meshcore_route_selection_t selection = {0};
    const uint32_t learned_at = 1000U;

    assert(d1l_meshcore_route_select(
        true, true, learned_path, 0x42U, learned_at,
        learned_at + D1L_MESHCORE_DIRECT_PATH_MAX_AGE_MS,
        1U, &selection));
    assert(selection.route == D1L_MESHCORE_ROUTE_DIRECT);
    assert(selection.reason == D1L_MESHCORE_ROUTE_SELECTION_DIRECT_PROVEN);
    assert(selection.path_len == 0x42U);
    assert(selection.path_byte_len == sizeof(learned_path));
    assert(selection.path_hash_bytes == 2U);
    assert(selection.path_hops == 2U);
    assert(selection.path_age_ms == D1L_MESHCORE_DIRECT_PATH_MAX_AGE_MS);
    assert(memcmp(selection.path, learned_path, sizeof(learned_path)) == 0);

    learned_path[0] = 0xFFU;
    assert(selection.path[0] == 0x10U);

    assert(d1l_meshcore_route_select(
        true, true, NULL, 0U, 2000U, 2000U, 2U, &selection));
    assert(selection.route == D1L_MESHCORE_ROUTE_DIRECT);
    assert(selection.path_len == 0U);
    assert(selection.path_byte_len == 0U);
    assert(selection.path_hash_bytes == 1U);
    assert(selection.path_hops == 0U);
}

static void expect_flood(
    const d1l_meshcore_route_selection_t *selection,
    d1l_meshcore_route_selection_reason_t reason,
    uint8_t hash_bytes)
{
    assert(selection->route == D1L_MESHCORE_ROUTE_FLOOD);
    assert(selection->reason == reason);
    assert(selection->path_len == (uint8_t)((hash_bytes - 1U) << 6U));
    assert(selection->path_byte_len == 0U);
    assert(selection->path_hash_bytes == hash_bytes);
    assert(selection->path_hops == 0U);
}

static void test_missing_stale_and_malformed_fallback(void)
{
    const uint8_t one_hop[] = {0x33U};
    d1l_meshcore_route_selection_t selection = {0};

    assert(d1l_meshcore_route_select(
        false, false, one_hop, 0x01U, 0U, 100U, 3U, &selection));
    expect_flood(&selection, D1L_MESHCORE_ROUTE_SELECTION_FLOOD_NO_PATH, 3U);
    assert(selection.path_age_ms == 0U);

    assert(d1l_meshcore_route_select(
        true, true, one_hop, 0x01U, 100U,
        101U + D1L_MESHCORE_DIRECT_PATH_MAX_AGE_MS,
        2U, &selection));
    expect_flood(&selection, D1L_MESHCORE_ROUTE_SELECTION_FLOOD_STALE_PATH, 2U);
    assert(selection.path_age_ms == D1L_MESHCORE_DIRECT_PATH_MAX_AGE_MS + 1U);

    assert(d1l_meshcore_route_select(
        true, true, one_hop, 0xC0U, 100U, 101U, 1U, &selection));
    expect_flood(&selection, D1L_MESHCORE_ROUTE_SELECTION_FLOOD_MALFORMED_PATH, 1U);

    assert(d1l_meshcore_route_select(
        true, true, NULL, 0x01U, 100U, 101U, 1U, &selection));
    expect_flood(&selection, D1L_MESHCORE_ROUTE_SELECTION_FLOOD_MALFORMED_PATH, 1U);

    assert(d1l_meshcore_route_select(
        true, true, one_hop, 0x01U, 500U, 100U, 1U, &selection));
    expect_flood(&selection, D1L_MESHCORE_ROUTE_SELECTION_FLOOD_STALE_PATH, 1U);
    assert(selection.path_age_ms == UINT32_MAX - 399U);
}

static void test_preboot_timestamp_alias_never_becomes_fresh(void)
{
    const uint8_t one_hop[] = {0x55U};
    d1l_meshcore_route_selection_t selection = {0};

    /* The persisted uptime exactly aliases the later boot's current uptime. */
    assert(d1l_meshcore_route_select(
        true, false, one_hop, 0x01U, 500U, 500U, 1U, &selection));
    expect_flood(
        &selection, D1L_MESHCORE_ROUTE_SELECTION_FLOOD_PREBOOT_PATH, 1U);
    assert(selection.path_age_ms == 0U);

    assert(d1l_meshcore_route_select(
        true, true, one_hop, 0x01U, 500U, 500U, 1U, &selection));
    assert(selection.route == D1L_MESHCORE_ROUTE_DIRECT);
    assert(selection.reason == D1L_MESHCORE_ROUTE_SELECTION_DIRECT_PROVEN);
}

static d1l_meshcore_wire_packet_t direct_inbound_packet(void)
{
    d1l_meshcore_wire_packet_t packet = {0};
    packet.header = (uint8_t)((D1L_MESHCORE_PAYLOAD_TEXT << 2U) |
                              D1L_MESHCORE_ROUTE_DIRECT);
    packet.route = D1L_MESHCORE_ROUTE_DIRECT;
    packet.type = D1L_MESHCORE_PAYLOAD_TEXT;
    packet.version = D1L_MESHCORE_PAYLOAD_VER_1;
    packet.path_len = 0U;
    packet.path_hash_bytes = 1U;
    return packet;
}

static void expect_ack_kind_for_selection(
    const d1l_meshcore_route_selection_t *selection,
    d1l_meshcore_ack_dispatch_kind_t expected_kind)
{
    const d1l_meshcore_wire_packet_t inbound = direct_inbound_packet();
    const uint8_t ack_hash[] = {1U, 2U, 3U, 4U};
    const bool direct = selection->route == D1L_MESHCORE_ROUTE_DIRECT;
    d1l_meshcore_ack_dispatch_plan_t plan = {0};
    assert(d1l_meshcore_ack_dispatch_plan(
        &inbound, 2U, direct, direct ? selection->path : NULL,
        direct ? selection->path_len : 0U, ack_hash, 0U, 0x42U, &plan));
    assert(plan.kind == expected_kind);
    assert(plan.route == (direct ? D1L_MESHCORE_ROUTE_DIRECT :
                                   D1L_MESHCORE_ROUTE_FLOOD));
    if (direct) {
        assert(plan.path_len == selection->path_len);
        assert(plan.path_byte_len == selection->path_byte_len);
        assert(memcmp(plan.path, selection->path, selection->path_byte_len) == 0);
    }
}

static void test_ack_plans_consume_immutable_freshness_selection(void)
{
    uint8_t learned_path[] = {0x66U};
    d1l_meshcore_route_selection_t selection = {0};

    assert(d1l_meshcore_route_select(
        true, true, learned_path, 0x01U, 100U,
        101U + D1L_MESHCORE_DIRECT_PATH_MAX_AGE_MS,
        2U, &selection));
    expect_flood(
        &selection, D1L_MESHCORE_ROUTE_SELECTION_FLOOD_STALE_PATH, 2U);
    const d1l_meshcore_route_selection_t stale_snapshot = selection;
    learned_path[0] = 0xFFU;
    assert(memcmp(&selection, &stale_snapshot, sizeof(selection)) == 0);
    expect_ack_kind_for_selection(
        &selection, D1L_MESHCORE_ACK_DISPATCH_FLOOD_ACK);

    assert(d1l_meshcore_route_select(
        true, true, learned_path, 0xC0U, 100U, 101U, 2U, &selection));
    expect_flood(
        &selection, D1L_MESHCORE_ROUTE_SELECTION_FLOOD_MALFORMED_PATH, 2U);
    expect_ack_kind_for_selection(
        &selection, D1L_MESHCORE_ACK_DISPATCH_FLOOD_ACK);

    assert(d1l_meshcore_route_select(
        true, false, learned_path, 0x01U, 101U, 101U, 2U, &selection));
    expect_flood(
        &selection, D1L_MESHCORE_ROUTE_SELECTION_FLOOD_PREBOOT_PATH, 2U);
    expect_ack_kind_for_selection(
        &selection, D1L_MESHCORE_ACK_DISPATCH_FLOOD_ACK);

    learned_path[0] = 0x77U;
    assert(d1l_meshcore_route_select(
        true, true, learned_path, 0x01U, 100U, 101U, 2U, &selection));
    expect_ack_kind_for_selection(
        &selection, D1L_MESHCORE_ACK_DISPATCH_DIRECT_ACK);
    assert(selection.path[0] == 0x77U);

    assert(d1l_meshcore_route_select(
        true, true, NULL, 0U, 100U, 101U, 2U, &selection));
    assert(selection.route == D1L_MESHCORE_ROUTE_DIRECT);
    assert(selection.path_len == 0U);
    expect_ack_kind_for_selection(
        &selection, D1L_MESHCORE_ACK_DISPATCH_DIRECT_ACK);
}

static void test_fail_closed_without_partial_output(void)
{
    const uint8_t path[] = {0x44U};
    d1l_meshcore_route_selection_t selection;
    memset(&selection, 0xA5, sizeof(selection));
    const d1l_meshcore_route_selection_t before = selection;

    assert(!d1l_meshcore_route_select(
        true, true, path, 0x01U, 1U, 2U, 0U, &selection));
    assert(memcmp(&selection, &before, sizeof(selection)) == 0);
    assert(!d1l_meshcore_route_select(
        true, true, path, 0x01U, 1U, 2U, 4U, &selection));
    assert(memcmp(&selection, &before, sizeof(selection)) == 0);
    assert(!d1l_meshcore_route_select(
        true, true, path, 0x01U, 1U, 2U, 1U, NULL));
}

int main(void)
{
    test_valid_direct_path_selection();
    test_missing_stale_and_malformed_fallback();
    test_preboot_timestamp_alias_never_becomes_fresh();
    test_ack_plans_consume_immutable_freshness_selection();
    test_fail_closed_without_partial_output();
    assert(strcmp(d1l_meshcore_route_selection_reason_name(
                      D1L_MESHCORE_ROUTE_SELECTION_NONE),
                  "none") == 0);
    assert(strcmp(d1l_meshcore_route_selection_reason_name(
                      D1L_MESHCORE_ROUTE_SELECTION_FLOOD_STALE_PATH),
                  "flood_stale_path") == 0);
    assert(strcmp(d1l_meshcore_route_selection_reason_name(
                      D1L_MESHCORE_ROUTE_SELECTION_FLOOD_PREBOOT_PATH),
                  "flood_preboot_path") == 0);
    puts("meshcore route selection vectors passed");
    return 0;
}
