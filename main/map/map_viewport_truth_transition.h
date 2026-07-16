#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "map_marker_truth.h"

typedef struct {
    bool age_reference_valid;
    uint32_t age_reference_timestamp;
    uint32_t marker_context_generation;
    d1l_map_center_source_t center_source;
    bool center_trusted_for_acquire;
    bool retained_view_valid;
    bool retained_lease_active;
} d1l_map_viewport_truth_state_t;

typedef struct {
    bool age_reference_valid;
    uint32_t age_reference_timestamp;
    d1l_map_center_source_t center_source;
    bool center_location_set;
    bool retained_center_matches_snapshot;
} d1l_map_viewport_truth_input_t;

typedef struct {
    bool marker_context_changed;
    bool backward_reference_correction;
    bool invalidate_retained_view;
    bool release_retained_lease;
    bool acquisition_allowed;
} d1l_map_viewport_truth_result_t;

/* Apply one current model snapshot to retained viewport truth. The caller owns
 * the actual tile lease and must perform both requested invalidation actions
 * before another acquire. This helper is pure and preserves transition state. */
bool d1l_map_viewport_truth_apply(
    d1l_map_viewport_truth_state_t *state,
    const d1l_map_viewport_truth_input_t *input,
    d1l_map_viewport_truth_result_t *out_result);
