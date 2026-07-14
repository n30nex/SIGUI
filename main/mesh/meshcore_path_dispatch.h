#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "meshcore_path_state.h"
#include "meshcore_wire.h"

#define D1L_MESHCORE_PATH_EXTRA_TYPE_RESPONSE 0x01U
#define D1L_MESHCORE_PATH_REPLAY_IDENTITY_BYTES 32U
#define D1L_MESHCORE_PATH_REPLAY_CAPACITY 8U

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    D1L_MESHCORE_PATH_EXTRA_NONE = 0,
    D1L_MESHCORE_PATH_EXTRA_ACK,
    D1L_MESHCORE_PATH_EXTRA_RESPONSE,
} d1l_meshcore_path_extra_kind_t;

typedef struct {
    uint8_t path_len;
    const uint8_t *path;
    uint8_t path_byte_len;
    uint8_t raw_extra_type;
    uint8_t extra_type;
    const uint8_t *extra;
    size_t extra_len;
    d1l_meshcore_path_source_t source;
    d1l_meshcore_path_extra_kind_t kind;
} d1l_meshcore_path_plain_t;

/* Decode authenticated PATH plaintext using the pinned MeshCore low-nibble
 * subtype rule.  Unknown/dummy subtypes still carry an observed route. */
static inline bool d1l_meshcore_path_plain_decode(
    const uint8_t *plain, size_t plain_len,
    d1l_meshcore_path_plain_t *out_path)
{
    if (!plain || !out_path || plain_len < 2U ||
        !d1l_meshcore_wire_path_len_valid(plain[0])) {
        return false;
    }
    const uint8_t path_bytes =
        d1l_meshcore_wire_path_byte_len(plain[0]);
    if ((size_t)path_bytes + 2U > plain_len) {
        return false;
    }

    d1l_meshcore_path_plain_t decoded = {
        .path_len = plain[0],
        .path = path_bytes > 0U ? &plain[1] : NULL,
        .path_byte_len = path_bytes,
        .raw_extra_type = plain[1U + path_bytes],
        .extra = &plain[2U + path_bytes],
        .extra_len = plain_len - (2U + path_bytes),
        .source = D1L_MESHCORE_PATH_SOURCE_OBSERVED,
        .kind = D1L_MESHCORE_PATH_EXTRA_NONE,
    };
    decoded.extra_type = decoded.raw_extra_type & 0x0fU;
    if (decoded.extra_type == D1L_MESHCORE_PAYLOAD_ACK) {
        decoded.source = D1L_MESHCORE_PATH_SOURCE_ACK_PATH;
        if (decoded.extra_len >= 4U) {
            decoded.kind = D1L_MESHCORE_PATH_EXTRA_ACK;
        }
    } else if (decoded.extra_type == D1L_MESHCORE_PATH_EXTRA_TYPE_RESPONSE) {
        decoded.source = D1L_MESHCORE_PATH_SOURCE_PATH_RESPONSE;
        if (decoded.extra_len >= 4U) {
            decoded.kind = D1L_MESHCORE_PATH_EXTRA_RESPONSE;
        }
    }
    *out_path = decoded;
    return true;
}

typedef struct {
    uint32_t tag;
    uint64_t deadline_us;
    bool active;
} d1l_meshcore_path_response_expectation_t;

typedef enum {
    D1L_MESHCORE_PATH_RESPONSE_UNMATCHED = 0,
    D1L_MESHCORE_PATH_RESPONSE_MATCHED,
    D1L_MESHCORE_PATH_RESPONSE_EXPIRED,
    D1L_MESHCORE_PATH_RESPONSE_MALFORMED,
} d1l_meshcore_path_response_result_t;

typedef struct {
    bool queued;
} d1l_meshcore_reciprocal_path_plan_t;

typedef struct {
    uint8_t identities[D1L_MESHCORE_PATH_REPLAY_CAPACITY]
                      [D1L_MESHCORE_PATH_REPLAY_IDENTITY_BYTES];
    bool valid[D1L_MESHCORE_PATH_REPLAY_CAPACITY];
    uint8_t next_slot;
} d1l_meshcore_path_replay_cache_t;

/* The runtime owner calls this only after authenticated decryption.  Taking
 * the bounded identity before contact mutation makes an exact replay unable
 * to advance path generation or queue another reciprocal RF response. */
static inline bool d1l_meshcore_path_replay_take(
    d1l_meshcore_path_replay_cache_t *cache,
    const uint8_t identity[D1L_MESHCORE_PATH_REPLAY_IDENTITY_BYTES])
{
    if (!cache || !identity) {
        return false;
    }
    for (size_t i = 0U; i < D1L_MESHCORE_PATH_REPLAY_CAPACITY; ++i) {
        if (cache->valid[i] &&
            memcmp(cache->identities[i], identity,
                   D1L_MESHCORE_PATH_REPLAY_IDENTITY_BYTES) == 0) {
            return false;
        }
    }
    const uint8_t slot = cache->next_slot;
    memcpy(cache->identities[slot], identity,
           D1L_MESHCORE_PATH_REPLAY_IDENTITY_BYTES);
    cache->valid[slot] = true;
    cache->next_slot = (uint8_t)(
        (slot + 1U) % D1L_MESHCORE_PATH_REPLAY_CAPACITY);
    return true;
}

static inline bool d1l_meshcore_path_replay_forget(
    d1l_meshcore_path_replay_cache_t *cache,
    const uint8_t identity[D1L_MESHCORE_PATH_REPLAY_IDENTITY_BYTES])
{
    if (!cache || !identity) {
        return false;
    }
    for (size_t i = 0U; i < D1L_MESHCORE_PATH_REPLAY_CAPACITY; ++i) {
        if (cache->valid[i] &&
            memcmp(cache->identities[i], identity,
                   D1L_MESHCORE_PATH_REPLAY_IDENTITY_BYTES) == 0) {
            memset(cache->identities[i], 0,
                   D1L_MESHCORE_PATH_REPLAY_IDENTITY_BYTES);
            cache->valid[i] = false;
            return true;
        }
    }
    return false;
}

static inline bool d1l_meshcore_reciprocal_path_take(
    d1l_meshcore_reciprocal_path_plan_t *plan, bool accepted,
    uint8_t inbound_route)
{
    if (!plan || plan->queued || !accepted ||
        (inbound_route != D1L_MESHCORE_ROUTE_FLOOD &&
         inbound_route != D1L_MESHCORE_ROUTE_TRANSPORT_FLOOD)) {
        return false;
    }
    plan->queued = true;
    return true;
}

static inline uint32_t d1l_meshcore_path_response_read_tag(
    const uint8_t data[4])
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

/* Correlate and consume one bounded response expectation.  Contact identity
 * matching is an explicit caller input because the service owns that string. */
static inline d1l_meshcore_path_response_result_t
d1l_meshcore_path_response_take(
    d1l_meshcore_path_response_expectation_t *expectation,
    bool contact_matches, const uint8_t *data, size_t data_len,
    uint64_t now_us)
{
    if (!data || data_len < 4U ||
        data_len > D1L_MESHCORE_MAX_PACKET_PAYLOAD) {
        return D1L_MESHCORE_PATH_RESPONSE_MALFORMED;
    }
    if (!expectation || !expectation->active || !contact_matches ||
        d1l_meshcore_path_response_read_tag(data) != expectation->tag) {
        return D1L_MESHCORE_PATH_RESPONSE_UNMATCHED;
    }
    expectation->active = false;
    return now_us > expectation->deadline_us ?
        D1L_MESHCORE_PATH_RESPONSE_EXPIRED :
        D1L_MESHCORE_PATH_RESPONSE_MATCHED;
}

#ifdef __cplusplus
}
#endif
