#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "meshcore_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

#define D1L_MESHCORE_TRACE_FIXED_BYTES 9U
#define D1L_MESHCORE_TRACE_MAX_HOPS 63U
#define D1L_MESHCORE_TRACE_PENDING_TIMEOUT_MS 30000U
#define D1L_MESHCORE_TRACE_DUPLICATE_WINDOW_MS 60000U
#define D1L_MESHCORE_TRACE_RETAINED_TARGET "trace_last"

typedef struct {
    uint32_t tag;
    uint32_t auth_code;
    uint8_t path_hops;
    uint8_t path_hashes[D1L_MESHCORE_TRACE_MAX_HOPS];
    uint8_t raw[D1L_MESHCORE_MAX_RAW_PACKET];
    uint8_t raw_len;
} d1l_meshcore_trace_source_t;

typedef struct {
    uint32_t tag;
    uint32_t auth_code;
    uint8_t path_hops;
    uint8_t path_hashes[D1L_MESHCORE_TRACE_MAX_HOPS];
    int8_t path_snrs_quarter_db[D1L_MESHCORE_TRACE_MAX_HOPS];
} d1l_meshcore_trace_terminal_t;

typedef struct {
    bool pending;
    uint32_t pending_tag;
    uint32_t pending_auth_code;
    uint32_t pending_started_ms;
    uint8_t pending_path_hops;
    uint8_t pending_path_hashes[D1L_MESHCORE_TRACE_MAX_HOPS];
    bool completed;
    uint32_t completed_at_ms;
    d1l_meshcore_trace_terminal_t last_result;
} d1l_meshcore_trace_tracker_t;

typedef enum {
    D1L_MESHCORE_CONTACT_TRACE_PLAN_OK = 0,
    D1L_MESHCORE_CONTACT_TRACE_PLAN_INVALID,
    D1L_MESHCORE_CONTACT_TRACE_PLAN_UNSUPPORTED_WIDTH,
    D1L_MESHCORE_CONTACT_TRACE_PLAN_EMPTY,
    D1L_MESHCORE_CONTACT_TRACE_PLAN_TOO_LONG,
} d1l_meshcore_contact_trace_plan_result_t;

typedef struct {
    bool includes_contact;
    uint8_t path_hops;
    uint8_t path_hashes[D1L_MESHCORE_TRACE_MAX_HOPS];
} d1l_meshcore_contact_trace_plan_t;

typedef enum {
    D1L_MESHCORE_TRACE_CORRELATION_MATCHED = 0,
    D1L_MESHCORE_TRACE_CORRELATION_DUPLICATE,
    D1L_MESHCORE_TRACE_CORRELATION_UNMATCHED,
    D1L_MESHCORE_TRACE_CORRELATION_EXPIRED,
    D1L_MESHCORE_TRACE_CORRELATION_AUTH_MISMATCH,
    D1L_MESHCORE_TRACE_CORRELATION_PATH_MISMATCH,
} d1l_meshcore_trace_correlation_t;

typedef enum {
    D1L_MESHCORE_TRACE_FRAME_MALFORMED = 0,
    D1L_MESHCORE_TRACE_FRAME_UNSUPPORTED,
    D1L_MESHCORE_TRACE_FRAME_SOURCE,
    D1L_MESHCORE_TRACE_FRAME_IN_FLIGHT,
    D1L_MESHCORE_TRACE_FRAME_TERMINAL,
} d1l_meshcore_trace_frame_kind_t;

static inline void d1l_meshcore_trace_write_le32(uint8_t *dest,
                                                 uint32_t value)
{
    dest[0] = (uint8_t)(value & 0xffU);
    dest[1] = (uint8_t)((value >> 8U) & 0xffU);
    dest[2] = (uint8_t)((value >> 16U) & 0xffU);
    dest[3] = (uint8_t)(value >> 24U);
}

static inline uint32_t d1l_meshcore_trace_read_le32(const uint8_t *src)
{
    return (uint32_t)src[0] |
           ((uint32_t)src[1] << 8U) |
           ((uint32_t)src[2] << 16U) |
           ((uint32_t)src[3] << 24U);
}

/*
 * Derive the only contact TRACE loop accepted by DeskOS. The caller must
 * first authorize one immutable, current-boot direct route. One-byte route
 * hashes are copied in transmit order. Repeater and room contacts can forward
 * TRACE, so their exact public-key hash is the pivot; chat and sensor contacts
 * cannot, so the farthest proven repeater is the pivot. The return leg omits
 * that pivot and reverses only the already-authorized route. No caller-supplied
 * arbitrary loop can enter this helper.
 */
static inline d1l_meshcore_contact_trace_plan_result_t
d1l_meshcore_trace_plan_contact(
    const uint8_t *out_path,
    uint8_t out_path_len,
    bool contact_forwards_trace,
    uint8_t contact_hash,
    d1l_meshcore_contact_trace_plan_t *out_plan)
{
    if (!out_plan || !d1l_meshcore_wire_path_len_valid(out_path_len)) {
        return D1L_MESHCORE_CONTACT_TRACE_PLAN_INVALID;
    }
    const uint8_t hash_bytes =
        d1l_meshcore_wire_path_hash_size(out_path_len);
    const uint8_t path_hops =
        d1l_meshcore_wire_path_hash_count(out_path_len);
    const uint8_t path_bytes =
        d1l_meshcore_wire_path_byte_len(out_path_len);
    if (hash_bytes != 1U) {
        return D1L_MESHCORE_CONTACT_TRACE_PLAN_UNSUPPORTED_WIDTH;
    }
    if (path_bytes > 0U && !out_path) {
        return D1L_MESHCORE_CONTACT_TRACE_PLAN_INVALID;
    }
    if (path_hops == 0U && !contact_forwards_trace) {
        return D1L_MESHCORE_CONTACT_TRACE_PLAN_EMPTY;
    }

    const size_t loop_hops = contact_forwards_trace ?
        (size_t)path_hops * 2U + 1U :
        (size_t)path_hops * 2U - 1U;
    if (loop_hops > D1L_MESHCORE_TRACE_MAX_HOPS) {
        return D1L_MESHCORE_CONTACT_TRACE_PLAN_TOO_LONG;
    }

    d1l_meshcore_contact_trace_plan_t plan = {
        .includes_contact = contact_forwards_trace,
        .path_hops = (uint8_t)loop_hops,
    };
    if (path_hops > 0U) {
        memcpy(plan.path_hashes, out_path, path_hops);
    }
    size_t write_index = path_hops;
    if (contact_forwards_trace) {
        plan.path_hashes[write_index++] = contact_hash;
        for (size_t i = path_hops; i > 0U; --i) {
            plan.path_hashes[write_index++] = out_path[i - 1U];
        }
    } else {
        for (size_t i = path_hops - 1U; i > 0U; --i) {
            plan.path_hashes[write_index++] = out_path[i - 1U];
        }
    }
    if (write_index != loop_hops) {
        return D1L_MESHCORE_CONTACT_TRACE_PLAN_INVALID;
    }
    *out_plan = plan;
    return D1L_MESHCORE_CONTACT_TRACE_PLAN_OK;
}

/* Every completed tag replaces the same bounded route-store summary row. */
static inline const char *d1l_meshcore_trace_retained_target_for_tag(
    uint32_t tag)
{
    (void)tag;
    return D1L_MESHCORE_TRACE_RETAINED_TARGET;
}

/*
 * Build the exact public flags-zero source frame from pinned Mesh::createTrace
 * followed by Mesh::sendDirect. TRACE is never flood-routed: its explicit
 * one-byte hashes are appended to the payload and the outer path starts empty
 * so repeaters can append SNR samples there.
 */
static inline bool d1l_meshcore_trace_build_source(
    uint32_t tag,
    uint32_t auth_code,
    const uint8_t *path_hashes,
    size_t path_hops,
    d1l_meshcore_trace_source_t *out_source)
{
    if (!out_source || path_hops > D1L_MESHCORE_TRACE_MAX_HOPS ||
        (path_hops > 0U && !path_hashes)) {
        return false;
    }

    d1l_meshcore_trace_source_t source = {0};
    size_t raw_len = 0U;
    const uint8_t header = (uint8_t)(
        (D1L_MESHCORE_PAYLOAD_TRACE << 2U) | D1L_MESHCORE_ROUTE_DIRECT);
    if (!d1l_meshcore_wire_write_prefix(
            header, 0U, 0U, 0U, NULL, source.raw, sizeof(source.raw),
            &raw_len) ||
        raw_len + D1L_MESHCORE_TRACE_FIXED_BYTES + path_hops >
            D1L_MESHCORE_MAX_RAW_PACKET) {
        return false;
    }

    source.tag = tag;
    source.auth_code = auth_code;
    source.path_hops = (uint8_t)path_hops;
    if (path_hops > 0U) {
        memcpy(source.path_hashes, path_hashes, path_hops);
    }
    d1l_meshcore_trace_write_le32(&source.raw[raw_len], tag);
    raw_len += 4U;
    d1l_meshcore_trace_write_le32(&source.raw[raw_len], auth_code);
    raw_len += 4U;
    source.raw[raw_len++] = 0U;
    if (path_hops > 0U) {
        memcpy(&source.raw[raw_len], path_hashes, path_hops);
        raw_len += path_hops;
    }
    source.raw_len = (uint8_t)raw_len;
    *out_source = source;
    return true;
}

/* Classify direct TRACE without treating valid source/in-flight copies or
 * valid multi-byte-path flags outside this bounded slice as malformed. */
static inline d1l_meshcore_trace_frame_kind_t d1l_meshcore_trace_classify(
    const uint8_t *raw,
    size_t raw_len,
    d1l_meshcore_trace_terminal_t *out_terminal)
{
    if (!raw) {
        return D1L_MESHCORE_TRACE_FRAME_MALFORMED;
    }

    d1l_meshcore_wire_packet_t packet = {0};
    if (!d1l_meshcore_wire_decode_v1(raw, raw_len, &packet) ||
        packet.route != D1L_MESHCORE_ROUTE_DIRECT ||
        packet.type != D1L_MESHCORE_PAYLOAD_TRACE ||
        packet.path_hash_bytes != 1U ||
        packet.payload_len < D1L_MESHCORE_TRACE_FIXED_BYTES) {
        return D1L_MESHCORE_TRACE_FRAME_MALFORMED;
    }

    const uint8_t flags = packet.payload[8];
    const size_t explicit_path_bytes =
        packet.payload_len - D1L_MESHCORE_TRACE_FIXED_BYTES;
    const size_t explicit_hash_bytes = 1U << (flags & 0x03U);
    if (explicit_path_bytes == 0U) {
        return D1L_MESHCORE_TRACE_FRAME_UNSUPPORTED;
    }
    if ((explicit_path_bytes % explicit_hash_bytes) != 0U) {
        return D1L_MESHCORE_TRACE_FRAME_MALFORMED;
    }
    const size_t explicit_hops = explicit_path_bytes / explicit_hash_bytes;
    if (explicit_hops == 0U ||
        explicit_hops > D1L_MESHCORE_TRACE_MAX_HOPS ||
        packet.path_byte_len != packet.path_hops ||
        packet.path_hops > explicit_hops) {
        return D1L_MESHCORE_TRACE_FRAME_MALFORMED;
    }
    if (flags != 0U) {
        return D1L_MESHCORE_TRACE_FRAME_UNSUPPORTED;
    }
    if (packet.path_hops == 0U) {
        return D1L_MESHCORE_TRACE_FRAME_SOURCE;
    }
    if (packet.path_hops < explicit_hops) {
        return D1L_MESHCORE_TRACE_FRAME_IN_FLIGHT;
    }

    d1l_meshcore_trace_terminal_t terminal = {0};
    terminal.tag = d1l_meshcore_trace_read_le32(packet.payload);
    terminal.auth_code = d1l_meshcore_trace_read_le32(&packet.payload[4]);
    terminal.path_hops = (uint8_t)explicit_hops;
    memcpy(terminal.path_hashes,
           &packet.payload[D1L_MESHCORE_TRACE_FIXED_BYTES], explicit_hops);
    memcpy(terminal.path_snrs_quarter_db, packet.path, explicit_hops);
    if (out_terminal) {
        *out_terminal = terminal;
    }
    return D1L_MESHCORE_TRACE_FRAME_TERMINAL;
}

/* Parse only a canonical terminal flags-zero TRACE frame. */
static inline bool d1l_meshcore_trace_parse_terminal(
    const uint8_t *raw,
    size_t raw_len,
    d1l_meshcore_trace_terminal_t *out_terminal)
{
    return out_terminal &&
           d1l_meshcore_trace_classify(raw, raw_len, out_terminal) ==
               D1L_MESHCORE_TRACE_FRAME_TERMINAL;
}

static inline bool d1l_meshcore_trace_tracker_expire_pending(
    d1l_meshcore_trace_tracker_t *tracker,
    uint32_t now_ms)
{
    if (!tracker || !tracker->pending ||
        (uint32_t)(now_ms - tracker->pending_started_ms) <
            D1L_MESHCORE_TRACE_PENDING_TIMEOUT_MS) {
        return false;
    }
    tracker->pending = false;
    return true;
}

static inline bool d1l_meshcore_trace_tracker_begin(
    d1l_meshcore_trace_tracker_t *tracker,
    uint32_t tag,
    uint32_t auth_code,
    const uint8_t *path_hashes,
    size_t path_hops,
    uint32_t now_ms)
{
    if (!tracker || tracker->pending ||
        path_hops > D1L_MESHCORE_TRACE_MAX_HOPS ||
        (path_hops > 0U && !path_hashes)) {
        return false;
    }
    tracker->pending = true;
    tracker->pending_tag = tag;
    tracker->pending_auth_code = auth_code;
    tracker->pending_started_ms = now_ms;
    tracker->pending_path_hops = (uint8_t)path_hops;
    memset(tracker->pending_path_hashes, 0,
           sizeof(tracker->pending_path_hashes));
    if (path_hops > 0U) {
        memcpy(tracker->pending_path_hashes, path_hashes, path_hops);
    }
    return true;
}

static inline bool d1l_meshcore_trace_tracker_cancel(
    d1l_meshcore_trace_tracker_t *tracker,
    uint32_t tag,
    uint32_t auth_code)
{
    if (!tracker || !tracker->pending || tracker->pending_tag != tag ||
        tracker->pending_auth_code != auth_code) {
        return false;
    }
    tracker->pending = false;
    return true;
}

static inline bool d1l_meshcore_trace_path_matches(
    const d1l_meshcore_trace_terminal_t *terminal,
    uint8_t expected_hops,
    const uint8_t *expected_hashes)
{
    return terminal && terminal->path_hops == expected_hops &&
           (expected_hops == 0U ||
            (expected_hashes &&
             memcmp(terminal->path_hashes, expected_hashes,
                    expected_hops) == 0));
}

static inline d1l_meshcore_trace_correlation_t
d1l_meshcore_trace_tracker_consume(
    d1l_meshcore_trace_tracker_t *tracker,
    const d1l_meshcore_trace_terminal_t *terminal,
    uint32_t now_ms)
{
    if (!tracker || !terminal) {
        return D1L_MESHCORE_TRACE_CORRELATION_UNMATCHED;
    }

    if (tracker->pending) {
        const bool expired =
            (uint32_t)(now_ms - tracker->pending_started_ms) >=
            D1L_MESHCORE_TRACE_PENDING_TIMEOUT_MS;
        const bool tag_matches = terminal->tag == tracker->pending_tag;
        const bool auth_matches =
            terminal->auth_code == tracker->pending_auth_code;
        const bool path_matches = d1l_meshcore_trace_path_matches(
            terminal, tracker->pending_path_hops,
            tracker->pending_path_hashes);
        if (expired) {
            tracker->pending = false;
            if (tag_matches && auth_matches && path_matches) {
                return D1L_MESHCORE_TRACE_CORRELATION_EXPIRED;
            }
        } else if (tag_matches) {
            if (!auth_matches) {
                return D1L_MESHCORE_TRACE_CORRELATION_AUTH_MISMATCH;
            }
            if (!path_matches) {
                return D1L_MESHCORE_TRACE_CORRELATION_PATH_MISMATCH;
            }
            tracker->pending = false;
            tracker->completed = true;
            tracker->completed_at_ms = now_ms;
            tracker->last_result = *terminal;
            return D1L_MESHCORE_TRACE_CORRELATION_MATCHED;
        }
    }

    if (tracker->completed &&
        (uint32_t)(now_ms - tracker->completed_at_ms) <
            D1L_MESHCORE_TRACE_DUPLICATE_WINDOW_MS &&
        terminal->tag == tracker->last_result.tag &&
        terminal->auth_code == tracker->last_result.auth_code &&
        d1l_meshcore_trace_path_matches(
            terminal, tracker->last_result.path_hops,
            tracker->last_result.path_hashes)) {
        return D1L_MESHCORE_TRACE_CORRELATION_DUPLICATE;
    }

    return D1L_MESHCORE_TRACE_CORRELATION_UNMATCHED;
}

#ifdef __cplusplus
}
#endif
