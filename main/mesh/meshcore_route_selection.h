#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "meshcore_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

#define D1L_MESHCORE_DIRECT_PATH_MAX_AGE_MS (30U * 60U * 1000U)

typedef enum {
    D1L_MESHCORE_ROUTE_SELECTION_NONE = 0,
    D1L_MESHCORE_ROUTE_SELECTION_DIRECT_PROVEN,
    D1L_MESHCORE_ROUTE_SELECTION_FLOOD_NO_PATH,
    D1L_MESHCORE_ROUTE_SELECTION_FLOOD_PREBOOT_PATH,
    D1L_MESHCORE_ROUTE_SELECTION_FLOOD_STALE_PATH,
    D1L_MESHCORE_ROUTE_SELECTION_FLOOD_MALFORMED_PATH,
} d1l_meshcore_route_selection_reason_t;

typedef struct {
    uint8_t route;
    uint8_t path_len;
    uint8_t path[D1L_MESHCORE_MAX_PATH_BYTES];
    uint8_t path_byte_len;
    uint8_t path_hash_bytes;
    uint8_t path_hops;
    uint32_t path_age_ms;
    d1l_meshcore_route_selection_reason_t reason;
} d1l_meshcore_route_selection_t;

/*
 * Select one immutable outbound route. A canonical learned path follows the
 * pinned MeshCore rule (known, including zero-hop, means direct) only after it
 * has been authenticated during this boot, with DeskOS' required age gate
 * layered on top. Missing, preboot, stale, or malformed learned state always
 * produces a complete flood plan; invalid global policy input leaves the
 * caller's output untouched.
 */
static inline bool d1l_meshcore_route_select(
    bool learned_path_known,
    bool learned_this_boot,
    const uint8_t *learned_path,
    uint8_t learned_path_len,
    uint32_t learned_at_ms,
    uint32_t now_ms,
    uint8_t flood_path_hash_bytes,
    d1l_meshcore_route_selection_t *out_selection)
{
    if (!out_selection || flood_path_hash_bytes < 1U ||
        flood_path_hash_bytes > 3U) {
        return false;
    }

    d1l_meshcore_route_selection_t selection = {0};
    selection.route = D1L_MESHCORE_ROUTE_FLOOD;
    selection.path_len = (uint8_t)((flood_path_hash_bytes - 1U) << 6U);
    selection.path_hash_bytes = flood_path_hash_bytes;
    selection.reason = D1L_MESHCORE_ROUTE_SELECTION_FLOOD_NO_PATH;

    if (learned_path_known) {
        const bool encoded_path_valid =
            d1l_meshcore_wire_path_len_valid(learned_path_len);
        const uint8_t learned_path_bytes = encoded_path_valid ?
            d1l_meshcore_wire_path_byte_len(learned_path_len) : 0U;
        if (!encoded_path_valid ||
            (learned_path_bytes > 0U && learned_path == NULL)) {
            selection.reason = D1L_MESHCORE_ROUTE_SELECTION_FLOOD_MALFORMED_PATH;
        } else if (!learned_this_boot) {
            /* Persisted ESP uptime is not comparable across boots. Never let
             * an old learned_at_ms become fresh again when a later boot reaches
             * the same uptime; only a path authenticated in this boot can be
            * direct. */
            selection.reason = D1L_MESHCORE_ROUTE_SELECTION_FLOOD_PREBOOT_PATH;
        } else {
            selection.path_age_ms = now_ms - learned_at_ms;
            if (selection.path_age_ms >
                D1L_MESHCORE_DIRECT_PATH_MAX_AGE_MS) {
                selection.reason =
                    D1L_MESHCORE_ROUTE_SELECTION_FLOOD_STALE_PATH;
            } else {
                selection.route = D1L_MESHCORE_ROUTE_DIRECT;
                selection.path_len = learned_path_len;
                selection.path_byte_len = learned_path_bytes;
                selection.path_hash_bytes =
                    d1l_meshcore_wire_path_hash_size(learned_path_len);
                selection.path_hops =
                    d1l_meshcore_wire_path_hash_count(learned_path_len);
                selection.reason =
                    D1L_MESHCORE_ROUTE_SELECTION_DIRECT_PROVEN;
                if (learned_path_bytes > 0U) {
                    memcpy(selection.path, learned_path, learned_path_bytes);
                }
            }
        }
    }

    *out_selection = selection;
    return true;
}

static inline const char *d1l_meshcore_route_selection_reason_name(
    d1l_meshcore_route_selection_reason_t reason)
{
    switch (reason) {
    case D1L_MESHCORE_ROUTE_SELECTION_NONE:
        return "none";
    case D1L_MESHCORE_ROUTE_SELECTION_DIRECT_PROVEN:
        return "direct_proven";
    case D1L_MESHCORE_ROUTE_SELECTION_FLOOD_NO_PATH:
        return "flood_no_path";
    case D1L_MESHCORE_ROUTE_SELECTION_FLOOD_PREBOOT_PATH:
        return "flood_preboot_path";
    case D1L_MESHCORE_ROUTE_SELECTION_FLOOD_STALE_PATH:
        return "flood_stale_path";
    case D1L_MESHCORE_ROUTE_SELECTION_FLOOD_MALFORMED_PATH:
        return "flood_bad_path";
    default:
        return "unknown";
    }
}

#ifdef __cplusplus
}
#endif
