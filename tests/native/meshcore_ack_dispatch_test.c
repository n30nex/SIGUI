#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mesh/meshcore_ack_dispatch.h"

static d1l_meshcore_wire_packet_t inbound_packet(uint8_t route,
                                                  uint8_t path_len,
                                                  const uint8_t *path)
{
    d1l_meshcore_wire_packet_t packet = {0};
    packet.header = (uint8_t)((D1L_MESHCORE_PAYLOAD_TEXT << 2U) | route);
    packet.route = route;
    packet.type = D1L_MESHCORE_PAYLOAD_TEXT;
    packet.version = D1L_MESHCORE_PAYLOAD_VER_1;
    packet.path_len = path_len;
    packet.path_hash_bytes = d1l_meshcore_wire_path_hash_size(path_len);
    packet.path_hops = d1l_meshcore_wire_path_hash_count(path_len);
    packet.path = path;
    packet.path_byte_len = d1l_meshcore_wire_path_byte_len(path_len);
    return packet;
}

static void test_flood_ack_path_plan(void)
{
    const uint8_t inbound_path[] = {0x10U, 0x11U, 0x20U, 0x21U};
    const uint8_t ack_hash[] = {0x44U, 0x33U, 0x22U, 0x11U};
    d1l_meshcore_wire_packet_t packet =
        inbound_packet(D1L_MESHCORE_ROUTE_FLOOD, 0x42U, inbound_path);
    d1l_meshcore_ack_dispatch_plan_t plan = {0};

    assert(d1l_meshcore_ack_dispatch_plan(
        &packet, 3U, false, NULL, 0U, ack_hash, 7U, 0xA5U, &plan));
    assert(plan.kind == D1L_MESHCORE_ACK_DISPATCH_FLOOD_ACK_PATH);
    assert(plan.route == D1L_MESHCORE_ROUTE_FLOOD);
    assert(plan.path_len == 0x80U);
    assert(plan.path_byte_len == 0U);
    assert(plan.return_path_len == 0x42U);
    assert(plan.return_path_byte_len == sizeof(inbound_path));
    assert(memcmp(plan.return_path, inbound_path, sizeof(inbound_path)) == 0);
    assert(memcmp(plan.ack, ack_hash, sizeof(ack_hash)) == 0);
    assert(plan.ack[4] == 7U);
    assert(plan.ack[5] == 0xA5U);
    assert(plan.delay_ms == 200U);

    packet.route = D1L_MESHCORE_ROUTE_TRANSPORT_FLOOD;
    assert(d1l_meshcore_ack_dispatch_plan(
        &packet, 1U, false, NULL, 0U, ack_hash, 0U, 1U, &plan));
    assert(plan.kind == D1L_MESHCORE_ACK_DISPATCH_FLOOD_ACK_PATH);
    assert(plan.route == D1L_MESHCORE_ROUTE_FLOOD);
}

static void test_direct_and_fallback_ack_plans(void)
{
    const uint8_t inbound_path[] = {0x31U};
    const uint8_t outbound_path[] = {0x41U, 0x42U, 0x43U};
    const uint8_t ack_hash[] = {1U, 2U, 3U, 4U};
    d1l_meshcore_wire_packet_t packet =
        inbound_packet(D1L_MESHCORE_ROUTE_DIRECT, 0x01U, inbound_path);
    d1l_meshcore_ack_dispatch_plan_t plan = {0};

    assert(d1l_meshcore_ack_dispatch_plan(
        &packet, 2U, true, outbound_path, 0x03U,
        ack_hash, 0U, 9U, &plan));
    assert(plan.kind == D1L_MESHCORE_ACK_DISPATCH_DIRECT_ACK);
    assert(plan.route == D1L_MESHCORE_ROUTE_DIRECT);
    assert(plan.path_len == 0x03U);
    assert(plan.path_byte_len == sizeof(outbound_path));
    assert(memcmp(plan.path, outbound_path, sizeof(outbound_path)) == 0);

    packet.route = D1L_MESHCORE_ROUTE_TRANSPORT_DIRECT;
    assert(d1l_meshcore_ack_dispatch_plan(
        &packet, 2U, false, NULL, 0U, ack_hash, 0U, 9U, &plan));
    assert(plan.kind == D1L_MESHCORE_ACK_DISPATCH_FLOOD_ACK);
    assert(plan.route == D1L_MESHCORE_ROUTE_FLOOD);
    assert(plan.path_len == 0x40U);
    assert(plan.path_byte_len == 0U);
}

static void expect_rejected_without_output_change(
    const d1l_meshcore_wire_packet_t *packet,
    uint8_t flood_hash_bytes,
    bool outbound_valid,
    const uint8_t *outbound_path,
    uint8_t outbound_path_len)
{
    const uint8_t ack_hash[] = {1U, 2U, 3U, 4U};
    d1l_meshcore_ack_dispatch_plan_t plan;
    memset(&plan, 0xA5, sizeof(plan));
    const d1l_meshcore_ack_dispatch_plan_t before = plan;
    assert(!d1l_meshcore_ack_dispatch_plan(
        packet, flood_hash_bytes, outbound_valid, outbound_path,
        outbound_path_len, ack_hash, 0U, 0U, &plan));
    assert(memcmp(&plan, &before, sizeof(plan)) == 0);
}

static void test_fail_closed_inputs(void)
{
    const uint8_t path[] = {0x01U};
    d1l_meshcore_wire_packet_t packet =
        inbound_packet(D1L_MESHCORE_ROUTE_FLOOD, 0x01U, path);

    expect_rejected_without_output_change(NULL, 1U, false, NULL, 0U);
    expect_rejected_without_output_change(&packet, 0U, false, NULL, 0U);
    expect_rejected_without_output_change(&packet, 4U, false, NULL, 0U);

    packet.type = D1L_MESHCORE_PAYLOAD_ACK;
    expect_rejected_without_output_change(&packet, 1U, false, NULL, 0U);
    packet.type = D1L_MESHCORE_PAYLOAD_TEXT;
    packet.version = 1U;
    expect_rejected_without_output_change(&packet, 1U, false, NULL, 0U);
    packet.version = D1L_MESHCORE_PAYLOAD_VER_1;
    packet.path_byte_len = 0U;
    expect_rejected_without_output_change(&packet, 1U, false, NULL, 0U);

    packet = inbound_packet(D1L_MESHCORE_ROUTE_DIRECT, 0U, NULL);
    expect_rejected_without_output_change(&packet, 1U, true, NULL, 0x01U);
    expect_rejected_without_output_change(&packet, 1U, true, path, 0xC0U);
}

static void fill_digest(uint8_t digest[D1L_MESHCORE_DM_ACK_DEDUPE_DIGEST_BYTES],
                        uint8_t value)
{
    memset(digest, value, D1L_MESHCORE_DM_ACK_DEDUPE_DIGEST_BYTES);
}

static void test_retry_attempts_share_message_identity(void)
{
    uint8_t sender[D1L_MESHCORE_DM_IDENTITY_SENDER_BYTES];
    memset(sender, 0x42, sizeof(sender));
    const uint8_t attempt_0[] = {1U, 2U, 3U, 4U, 0U, 'h', 'i'};
    const uint8_t attempt_1[] = {1U, 2U, 3U, 4U, 1U, 'h', 'i'};
    const uint8_t attempt_5[] = {1U, 2U, 3U, 4U, 1U, 'h', 'i', 0U, 5U};
    uint8_t identity_0[64] = {0};
    uint8_t identity_1[64] = {0};
    uint8_t identity_5[64] = {0};
    size_t identity_0_len = 0U;
    size_t identity_1_len = 0U;
    size_t identity_5_len = 0U;

    assert(d1l_meshcore_dm_identity_material(
        sender, attempt_0, sizeof(attempt_0), 2U,
        identity_0, sizeof(identity_0), &identity_0_len));
    assert(d1l_meshcore_dm_identity_material(
        sender, attempt_1, sizeof(attempt_1), 2U,
        identity_1, sizeof(identity_1), &identity_1_len));
    assert(d1l_meshcore_dm_identity_material(
        sender, attempt_5, sizeof(attempt_5), 2U,
        identity_5, sizeof(identity_5), &identity_5_len));
    assert(identity_0_len == D1L_MESHCORE_DM_IDENTITY_PREFIX_BYTES + 2U);
    assert(identity_1_len == identity_0_len);
    assert(identity_5_len == identity_0_len);
    assert(memcmp(identity_0, identity_1, identity_0_len) == 0);
    assert(memcmp(identity_0, identity_5, identity_0_len) == 0);
    assert(identity_0[D1L_MESHCORE_DM_IDENTITY_SENDER_BYTES + 4U] == 0U);

    uint8_t other_sender[D1L_MESHCORE_DM_IDENTITY_SENDER_BYTES];
    memcpy(other_sender, sender, sizeof(other_sender));
    other_sender[0] ^= 0x01U;
    assert(d1l_meshcore_dm_identity_material(
        other_sender, attempt_0, sizeof(attempt_0), 2U,
        identity_1, sizeof(identity_1), &identity_1_len));
    assert(memcmp(identity_0, identity_1, identity_0_len) != 0);

    uint8_t too_small[8];
    memset(too_small, 0xA5, sizeof(too_small));
    const uint8_t before[8] = {
        0xA5U, 0xA5U, 0xA5U, 0xA5U, 0xA5U, 0xA5U, 0xA5U, 0xA5U,
    };
    size_t rejected_len = 123U;
    assert(!d1l_meshcore_dm_identity_material(
        sender, attempt_0, sizeof(attempt_0), 2U,
        too_small, sizeof(too_small), &rejected_len));
    assert(memcmp(too_small, before, sizeof(too_small)) == 0);
    assert(rejected_len == 123U);
}

static void test_bounded_duplicate_reack(void)
{
    d1l_meshcore_ack_dedupe_t cache = {0};
    uint8_t digest[D1L_MESHCORE_DM_ACK_DEDUPE_DIGEST_BYTES];
    fill_digest(digest, 0x11U);

    assert(!d1l_meshcore_ack_dedupe_contains(&cache, digest));
    assert(d1l_meshcore_ack_dedupe_remember(&cache, digest));
    assert(d1l_meshcore_ack_dedupe_contains(&cache, digest));
    assert(!d1l_meshcore_ack_dedupe_is_durable(&cache, digest));
    assert(d1l_meshcore_ack_dedupe_mark_durable(&cache, digest, true));
    assert(d1l_meshcore_ack_dedupe_is_durable(&cache, digest));
    assert(!d1l_meshcore_ack_dedupe_remember(&cache, digest));
    assert(d1l_meshcore_ack_dedupe_dispatch_allowed(
        &cache, digest, D1L_MESHCORE_DM_ACK_MAX_DISPATCHES));
    assert(d1l_meshcore_ack_dedupe_mark_dispatched(
        &cache, digest, D1L_MESHCORE_DM_ACK_MAX_DISPATCHES));
    assert(d1l_meshcore_ack_dedupe_dispatch_allowed(
        &cache, digest, D1L_MESHCORE_DM_ACK_MAX_DISPATCHES));
    assert(d1l_meshcore_ack_dedupe_mark_dispatched(
        &cache, digest, D1L_MESHCORE_DM_ACK_MAX_DISPATCHES));
    assert(!d1l_meshcore_ack_dedupe_dispatch_allowed(
        &cache, digest, D1L_MESHCORE_DM_ACK_MAX_DISPATCHES));
    assert(!d1l_meshcore_ack_dedupe_mark_dispatched(
        &cache, digest, D1L_MESHCORE_DM_ACK_MAX_DISPATCHES));
}

static void test_retained_reboot_hydration_keeps_dispatch_budget(void)
{
    d1l_meshcore_ack_dedupe_t cache = {0};
    uint8_t digest[D1L_MESHCORE_DM_ACK_DEDUPE_DIGEST_BYTES];
    fill_digest(digest, 0x73U);

    assert(d1l_meshcore_ack_dedupe_remember_state(
        &cache, digest, true, 1U));
    assert(d1l_meshcore_ack_dedupe_is_durable(&cache, digest));
    assert(d1l_meshcore_ack_dedupe_dispatch_allowed(
        &cache, digest, D1L_MESHCORE_DM_ACK_MAX_DISPATCHES));
    assert(d1l_meshcore_ack_dedupe_set_dispatch_count(
        &cache, digest, 2U, D1L_MESHCORE_DM_ACK_MAX_DISPATCHES));
    assert(!d1l_meshcore_ack_dedupe_dispatch_allowed(
        &cache, digest, D1L_MESHCORE_DM_ACK_MAX_DISPATCHES));
    assert(!d1l_meshcore_ack_dedupe_set_dispatch_count(
        &cache, digest, 3U, D1L_MESHCORE_DM_ACK_MAX_DISPATCHES));
    assert(d1l_meshcore_ack_dedupe_mark_durable(&cache, digest, false));
    assert(!d1l_meshcore_ack_dedupe_is_durable(&cache, digest));

    fill_digest(digest, 0x74U);
    assert(!d1l_meshcore_ack_dedupe_remember_state(
        &cache, digest, true, 3U));
}

static void test_dedupe_capacity_and_fail_closed_inputs(void)
{
    d1l_meshcore_ack_dedupe_t cache = {0};
    uint8_t first[D1L_MESHCORE_DM_ACK_DEDUPE_DIGEST_BYTES];
    uint8_t digest[D1L_MESHCORE_DM_ACK_DEDUPE_DIGEST_BYTES];
    fill_digest(first, 1U);

    for (uint8_t i = 1U; i <= D1L_MESHCORE_DM_ACK_DEDUPE_CAPACITY; ++i) {
        fill_digest(digest, i);
        assert(d1l_meshcore_ack_dedupe_remember(&cache, digest));
    }
    assert(cache.next == 0U);
    assert(d1l_meshcore_ack_dedupe_contains(&cache, first));

    fill_digest(digest, 0xFEU);
    assert(d1l_meshcore_ack_dedupe_remember(&cache, digest));
    assert(!d1l_meshcore_ack_dedupe_contains(&cache, first));
    assert(d1l_meshcore_ack_dedupe_contains(&cache, digest));
    assert(cache.next == 1U);

    assert(d1l_meshcore_ack_dedupe_find(NULL, digest) == -1);
    assert(d1l_meshcore_ack_dedupe_find(&cache, NULL) == -1);
    assert(!d1l_meshcore_ack_dedupe_remember(NULL, digest));
    assert(!d1l_meshcore_ack_dedupe_remember(&cache, NULL));
    assert(!d1l_meshcore_ack_dedupe_dispatch_allowed(NULL, digest, 2U));
    assert(!d1l_meshcore_ack_dedupe_mark_dispatched(NULL, digest, 2U));

    cache.next = D1L_MESHCORE_DM_ACK_DEDUPE_CAPACITY;
    fill_digest(digest, 0xFDU);
    assert(!d1l_meshcore_ack_dedupe_remember(&cache, digest));
}

int main(void)
{
    test_flood_ack_path_plan();
    test_direct_and_fallback_ack_plans();
    test_fail_closed_inputs();
    test_retry_attempts_share_message_identity();
    test_bounded_duplicate_reack();
    test_retained_reboot_hydration_keeps_dispatch_budget();
    test_dedupe_capacity_and_fail_closed_inputs();
    assert(strcmp(d1l_meshcore_ack_dispatch_kind_name(
                      D1L_MESHCORE_ACK_DISPATCH_FLOOD_ACK_PATH),
                  "flood_ack_path") == 0);
    assert(strcmp(d1l_meshcore_ack_dispatch_kind_name(
                      D1L_MESHCORE_ACK_DISPATCH_NONE),
                  "none") == 0);
    puts("meshcore ACK dispatch vectors passed");
    return 0;
}
