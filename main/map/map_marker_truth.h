#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mesh/node_store.h"

typedef enum {
    D1L_MAP_CENTER_SOURCE_UNKNOWN = 0,
    D1L_MAP_CENTER_SOURCE_MANUAL,
    D1L_MAP_CENTER_SOURCE_AUTHENTICATED_COMPANION,
} d1l_map_center_source_t;

typedef enum {
    D1L_MAP_MARKER_ROLE_UNKNOWN = 0,
    D1L_MAP_MARKER_ROLE_COMPANION,
    D1L_MAP_MARKER_ROLE_REPEATER,
    D1L_MAP_MARKER_ROLE_ROOM_SERVER,
    D1L_MAP_MARKER_ROLE_SENSOR,
} d1l_map_marker_role_t;

typedef enum {
    D1L_MAP_MARKER_REJECT_NONE = 0,
    D1L_MAP_MARKER_REJECT_MALFORMED,
    D1L_MAP_MARKER_REJECT_PROVENANCE_UNKNOWN,
    D1L_MAP_MARKER_REJECT_AGE_UNVERIFIED,
    D1L_MAP_MARKER_REJECT_TIMESTAMP_MISSING,
    D1L_MAP_MARKER_REJECT_TIMESTAMP_IN_FUTURE,
} d1l_map_marker_reject_reason_t;

typedef struct {
    bool renderable;
    d1l_map_marker_reject_reason_t reject_reason;
    d1l_map_marker_role_t role;
    uint32_t age_sec;
} d1l_map_marker_truth_t;

/* A saved center is usable only when its producer is explicit. Unknown source
 * is deliberately not treated as manual or companion provenance. */
bool d1l_map_center_source_is_trusted(d1l_map_center_source_t source);
const char *d1l_map_center_source_label(d1l_map_center_source_t source);

/* Evaluate the location fields that may become a visible pin. No freshness
 * expiry is invented here: a valid age is reported for every non-future signed
 * advert, while product policy may present old ages truthfully. */
bool d1l_map_marker_truth_evaluate(const d1l_node_marker_t *marker,
                                   bool age_reference_valid,
                                   uint32_t age_reference_timestamp,
                                   d1l_map_marker_truth_t *out_truth);
const char *d1l_map_marker_role_label(d1l_map_marker_role_t role);
const char *d1l_map_marker_reject_reason_code(
    d1l_map_marker_reject_reason_t reason);
void d1l_map_marker_format_age(uint32_t age_sec, char *dest, size_t dest_len);
