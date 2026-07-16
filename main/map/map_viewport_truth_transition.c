#include "map_viewport_truth_transition.h"

#include <string.h>

static uint32_t next_generation(uint32_t generation)
{
    generation++;
    return generation == 0U ? 1U : generation;
}

bool d1l_map_viewport_truth_apply(
    d1l_map_viewport_truth_state_t *state,
    const d1l_map_viewport_truth_input_t *input,
    d1l_map_viewport_truth_result_t *out_result)
{
    if (!state || !input || !out_result) {
        return false;
    }

    memset(out_result, 0, sizeof(*out_result));
    const uint32_t reference = input->age_reference_valid ?
        input->age_reference_timestamp : 0U;
    const bool backward_reference_correction =
        state->age_reference_valid && input->age_reference_valid &&
        reference < state->age_reference_timestamp;
    const uint32_t old_age_bucket = state->age_reference_valid ?
        state->age_reference_timestamp / 60U : 0U;
    const uint32_t new_age_bucket = input->age_reference_valid ?
        reference / 60U : 0U;
    const bool marker_context_changed =
        input->age_reference_valid != state->age_reference_valid ||
        backward_reference_correction ||
        old_age_bucket != new_age_bucket ||
        input->center_source != state->center_source;

    const bool center_currently_trusted =
        input->center_location_set &&
        d1l_map_center_source_is_trusted(input->center_source) &&
        state->retained_view_valid &&
        input->retained_center_matches_snapshot;
    const bool invalidate_retained_view =
        !center_currently_trusted &&
        (state->center_trusted_for_acquire || state->retained_view_valid);
    const bool release_retained_lease =
        !center_currently_trusted && state->retained_lease_active;

    if (marker_context_changed) {
        state->marker_context_generation =
            next_generation(state->marker_context_generation);
    }
    state->age_reference_valid = input->age_reference_valid;
    state->age_reference_timestamp = reference;
    state->center_source = input->center_source;
    state->center_trusted_for_acquire = center_currently_trusted;
    if (!center_currently_trusted) {
        state->retained_view_valid = false;
        state->retained_lease_active = false;
    }

    out_result->marker_context_changed = marker_context_changed;
    out_result->backward_reference_correction =
        backward_reference_correction;
    out_result->invalidate_retained_view = invalidate_retained_view;
    out_result->release_retained_lease = release_retained_lease;
    out_result->acquisition_allowed = center_currently_trusted;
    return true;
}
