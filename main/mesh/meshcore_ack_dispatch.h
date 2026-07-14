#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "meshcore_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

#define D1L_MESHCORE_DM_ACK_WIRE_BYTES 6U
#define D1L_MESHCORE_DM_ACK_DELAY_MS 200U
#define D1L_MESHCORE_DM_ACK_DEDUPE_CAPACITY 16U
#define D1L_MESHCORE_DM_ACK_DEDUPE_DIGEST_BYTES 32U
#define D1L_MESHCORE_DM_ACK_MAX_DISPATCHES 2U
#define D1L_MESHCORE_DM_IDENTITY_SENDER_BYTES 32U
#define D1L_MESHCORE_DM_IDENTITY_PREFIX_BYTES \
    (D1L_MESHCORE_DM_IDENTITY_SENDER_BYTES + 5U)

/*
 * Canonical identity for one visible plain-DM row. Retry attempt bits are
 * transport/session metadata, not message identity, so both the low two flag
 * bits and the optional extended-attempt byte are intentionally excluded.
 */
static inline bool d1l_meshcore_dm_identity_material(
    const uint8_t sender[D1L_MESHCORE_DM_IDENTITY_SENDER_BYTES],
    const uint8_t *plain,
    size_t plain_len,
    size_t message_len,
    uint8_t *out,
    size_t out_size,
    size_t *out_len)
{
    if (!sender || !plain || plain_len < 5U || message_len > plain_len - 5U ||
        !out || !out_len) {
        return false;
    }
    const size_t required = D1L_MESHCORE_DM_IDENTITY_PREFIX_BYTES + message_len;
    if (required > out_size) {
        return false;
    }

    size_t index = 0U;
    memcpy(&out[index], sender, D1L_MESHCORE_DM_IDENTITY_SENDER_BYTES);
    index += D1L_MESHCORE_DM_IDENTITY_SENDER_BYTES;
    memcpy(&out[index], plain, 4U);
    index += 4U;
    out[index++] = (uint8_t)(plain[4] >> 2U);
    if (message_len > 0U) {
        memcpy(&out[index], &plain[5], message_len);
        index += message_len;
    }
    *out_len = index;
    return true;
}

typedef enum {
    D1L_MESHCORE_ACK_DISPATCH_NONE = 0,
    D1L_MESHCORE_ACK_DISPATCH_FLOOD_ACK,
    D1L_MESHCORE_ACK_DISPATCH_DIRECT_ACK,
    D1L_MESHCORE_ACK_DISPATCH_FLOOD_ACK_PATH,
} d1l_meshcore_ack_dispatch_kind_t;

typedef struct {
    d1l_meshcore_ack_dispatch_kind_t kind;
    uint8_t route;
    uint8_t path_len;
    uint8_t path[D1L_MESHCORE_MAX_PATH_BYTES];
    uint8_t path_byte_len;
    uint8_t return_path_len;
    uint8_t return_path[D1L_MESHCORE_MAX_PATH_BYTES];
    uint8_t return_path_byte_len;
    uint8_t ack[D1L_MESHCORE_DM_ACK_WIRE_BYTES];
    uint32_t delay_ms;
} d1l_meshcore_ack_dispatch_plan_t;

typedef struct {
    bool occupied[D1L_MESHCORE_DM_ACK_DEDUPE_CAPACITY];
    bool durable[D1L_MESHCORE_DM_ACK_DEDUPE_CAPACITY];
    uint8_t digests[D1L_MESHCORE_DM_ACK_DEDUPE_CAPACITY]
                   [D1L_MESHCORE_DM_ACK_DEDUPE_DIGEST_BYTES];
    uint8_t dispatch_count[D1L_MESHCORE_DM_ACK_DEDUPE_CAPACITY];
    uint8_t next;
} d1l_meshcore_ack_dedupe_t;

static inline int d1l_meshcore_ack_dedupe_find(
    const d1l_meshcore_ack_dedupe_t *cache,
    const uint8_t digest[D1L_MESHCORE_DM_ACK_DEDUPE_DIGEST_BYTES])
{
    if (!cache || !digest) {
        return -1;
    }
    for (size_t i = 0U; i < D1L_MESHCORE_DM_ACK_DEDUPE_CAPACITY; ++i) {
        if (cache->occupied[i] &&
            memcmp(cache->digests[i], digest,
                   D1L_MESHCORE_DM_ACK_DEDUPE_DIGEST_BYTES) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static inline bool d1l_meshcore_ack_dedupe_contains(
    const d1l_meshcore_ack_dedupe_t *cache,
    const uint8_t digest[D1L_MESHCORE_DM_ACK_DEDUPE_DIGEST_BYTES])
{
    return d1l_meshcore_ack_dedupe_find(cache, digest) >= 0;
}

static inline bool d1l_meshcore_ack_dedupe_remember_state(
    d1l_meshcore_ack_dedupe_t *cache,
    const uint8_t digest[D1L_MESHCORE_DM_ACK_DEDUPE_DIGEST_BYTES],
    bool durable,
    uint8_t dispatch_count)
{
    if (!cache || !digest ||
        cache->next >= D1L_MESHCORE_DM_ACK_DEDUPE_CAPACITY ||
        dispatch_count > D1L_MESHCORE_DM_ACK_MAX_DISPATCHES ||
        d1l_meshcore_ack_dedupe_contains(cache, digest)) {
        return false;
    }
    const uint8_t slot = cache->next;
    memcpy(cache->digests[slot], digest,
           D1L_MESHCORE_DM_ACK_DEDUPE_DIGEST_BYTES);
    cache->occupied[slot] = true;
    cache->durable[slot] = durable;
    cache->dispatch_count[slot] = dispatch_count;
    cache->next = (uint8_t)((slot + 1U) % D1L_MESHCORE_DM_ACK_DEDUPE_CAPACITY);
    return true;
}

static inline bool d1l_meshcore_ack_dedupe_remember(
    d1l_meshcore_ack_dedupe_t *cache,
    const uint8_t digest[D1L_MESHCORE_DM_ACK_DEDUPE_DIGEST_BYTES])
{
    return d1l_meshcore_ack_dedupe_remember_state(
        cache, digest, false, 0U);
}

static inline bool d1l_meshcore_ack_dedupe_is_durable(
    const d1l_meshcore_ack_dedupe_t *cache,
    const uint8_t digest[D1L_MESHCORE_DM_ACK_DEDUPE_DIGEST_BYTES])
{
    const int slot = d1l_meshcore_ack_dedupe_find(cache, digest);
    return slot >= 0 && cache->durable[slot];
}

static inline bool d1l_meshcore_ack_dedupe_mark_durable(
    d1l_meshcore_ack_dedupe_t *cache,
    const uint8_t digest[D1L_MESHCORE_DM_ACK_DEDUPE_DIGEST_BYTES],
    bool durable)
{
    const int slot = d1l_meshcore_ack_dedupe_find(cache, digest);
    if (slot < 0) {
        return false;
    }
    cache->durable[slot] = durable;
    return true;
}

static inline bool d1l_meshcore_ack_dedupe_set_dispatch_count(
    d1l_meshcore_ack_dedupe_t *cache,
    const uint8_t digest[D1L_MESHCORE_DM_ACK_DEDUPE_DIGEST_BYTES],
    uint8_t dispatch_count,
    uint8_t maximum_dispatches)
{
    const int slot = d1l_meshcore_ack_dedupe_find(cache, digest);
    if (slot < 0 || maximum_dispatches == 0U ||
        dispatch_count > maximum_dispatches) {
        return false;
    }
    cache->dispatch_count[slot] = dispatch_count;
    return true;
}

static inline bool d1l_meshcore_ack_dedupe_dispatch_allowed(
    const d1l_meshcore_ack_dedupe_t *cache,
    const uint8_t digest[D1L_MESHCORE_DM_ACK_DEDUPE_DIGEST_BYTES],
    uint8_t maximum_dispatches)
{
    const int slot = d1l_meshcore_ack_dedupe_find(cache, digest);
    return slot >= 0 && maximum_dispatches > 0U &&
           cache->dispatch_count[slot] < maximum_dispatches;
}

static inline bool d1l_meshcore_ack_dedupe_mark_dispatched(
    d1l_meshcore_ack_dedupe_t *cache,
    const uint8_t digest[D1L_MESHCORE_DM_ACK_DEDUPE_DIGEST_BYTES],
    uint8_t maximum_dispatches)
{
    const int slot = d1l_meshcore_ack_dedupe_find(cache, digest);
    if (slot < 0 || maximum_dispatches == 0U ||
        cache->dispatch_count[slot] >= maximum_dispatches) {
        return false;
    }
    cache->dispatch_count[slot]++;
    return true;
}

/*
 * Plan the pinned BaseChatMesh plain-DM response without performing crypto or
 * touching the radio. Flood DMs receive a flood ACK+PATH response. Direct DMs
 * use the contact's proven outbound path when one exists; otherwise they fail
 * safely to the upstream-compatible flood ACK fallback.
 *
 * The caller supplies the four protocol-correlated ACK bytes plus the optional
 * extended-attempt byte and a nonce. Output is assigned only after every route
 * and path invariant validates, so malformed input cannot leak a partial plan.
 */
static inline bool d1l_meshcore_ack_dispatch_plan(
    const d1l_meshcore_wire_packet_t *inbound,
    uint8_t flood_path_hash_bytes,
    bool outbound_path_valid,
    const uint8_t *outbound_path,
    uint8_t outbound_path_len,
    const uint8_t ack_hash[4],
    uint8_t extended_attempt,
    uint8_t nonce,
    d1l_meshcore_ack_dispatch_plan_t *out_plan)
{
    if (!inbound || !ack_hash || !out_plan ||
        inbound->version != D1L_MESHCORE_PAYLOAD_VER_1 ||
        inbound->type != D1L_MESHCORE_PAYLOAD_TEXT ||
        inbound->route > D1L_MESHCORE_ROUTE_TRANSPORT_DIRECT ||
        flood_path_hash_bytes < 1U || flood_path_hash_bytes > 3U ||
        !d1l_meshcore_wire_path_len_valid(inbound->path_len)) {
        return false;
    }

    const uint8_t inbound_path_bytes =
        d1l_meshcore_wire_path_byte_len(inbound->path_len);
    if (inbound->path_byte_len != inbound_path_bytes ||
        (inbound_path_bytes > 0U && inbound->path == NULL)) {
        return false;
    }

    if (outbound_path_valid &&
        (!d1l_meshcore_wire_path_len_valid(outbound_path_len) ||
         (d1l_meshcore_wire_path_byte_len(outbound_path_len) > 0U &&
          outbound_path == NULL))) {
        return false;
    }

    d1l_meshcore_ack_dispatch_plan_t plan = {0};
    memcpy(plan.ack, ack_hash, 4U);
    plan.ack[4] = extended_attempt;
    plan.ack[5] = nonce;
    plan.delay_ms = D1L_MESHCORE_DM_ACK_DELAY_MS;

    const bool inbound_flood =
        inbound->route == D1L_MESHCORE_ROUTE_FLOOD ||
        inbound->route == D1L_MESHCORE_ROUTE_TRANSPORT_FLOOD;
    if (inbound_flood) {
        plan.kind = D1L_MESHCORE_ACK_DISPATCH_FLOOD_ACK_PATH;
        plan.route = D1L_MESHCORE_ROUTE_FLOOD;
        plan.path_len = (uint8_t)((flood_path_hash_bytes - 1U) << 6U);
        plan.return_path_len = inbound->path_len;
        plan.return_path_byte_len = inbound_path_bytes;
        if (inbound_path_bytes > 0U) {
            memcpy(plan.return_path, inbound->path, inbound_path_bytes);
        }
    } else if (outbound_path_valid) {
        plan.kind = D1L_MESHCORE_ACK_DISPATCH_DIRECT_ACK;
        plan.route = D1L_MESHCORE_ROUTE_DIRECT;
        plan.path_len = outbound_path_len;
        plan.path_byte_len = d1l_meshcore_wire_path_byte_len(outbound_path_len);
        if (plan.path_byte_len > 0U) {
            memcpy(plan.path, outbound_path, plan.path_byte_len);
        }
    } else {
        plan.kind = D1L_MESHCORE_ACK_DISPATCH_FLOOD_ACK;
        plan.route = D1L_MESHCORE_ROUTE_FLOOD;
        plan.path_len = (uint8_t)((flood_path_hash_bytes - 1U) << 6U);
    }

    *out_plan = plan;
    return true;
}

static inline const char *d1l_meshcore_ack_dispatch_kind_name(
    d1l_meshcore_ack_dispatch_kind_t kind)
{
    switch (kind) {
    case D1L_MESHCORE_ACK_DISPATCH_FLOOD_ACK:
        return "flood_ack";
    case D1L_MESHCORE_ACK_DISPATCH_DIRECT_ACK:
        return "direct_ack";
    case D1L_MESHCORE_ACK_DISPATCH_FLOOD_ACK_PATH:
        return "flood_ack_path";
    default:
        return "none";
    }
}

#ifdef __cplusplus
}
#endif
