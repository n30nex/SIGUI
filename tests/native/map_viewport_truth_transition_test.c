#include <assert.h>
#include <stdio.h>

#include "map/map_marker_truth.h"
#include "map/map_viewport_truth_transition.h"

static d1l_node_marker_t signed_marker_at(uint32_t timestamp)
{
    d1l_node_marker_t marker = {0};
    snprintf(marker.fingerprint, sizeof(marker.fingerprint),
             "0123456789abcdef");
    snprintf(marker.name, sizeof(marker.name), "Hilltop");
    snprintf(marker.type, sizeof(marker.type), "repeater");
    marker.lat_e6 = 43675000;
    marker.lon_e6 = -79440000;
    marker.location_advert_timestamp = timestamp;
    marker.location_provenance =
        D1L_NODE_LOCATION_PROVENANCE_SIGNED_ADVERT;
    return marker;
}

static void test_stateful_time_and_center_truth_transitions(void)
{
    d1l_map_viewport_truth_state_t state = {
        .retained_view_valid = true,
        .retained_lease_active = true,
    };
    d1l_map_viewport_truth_input_t input = {
        .age_reference_valid = true,
        .age_reference_timestamp = 2000U,
        .center_source = D1L_MAP_CENTER_SOURCE_MANUAL,
        .center_location_set = true,
        .retained_center_matches_snapshot = true,
    };
    d1l_map_viewport_truth_result_t result = {0};

    assert(d1l_map_viewport_truth_apply(&state, &input, &result));
    assert(result.marker_context_changed);
    assert(!result.backward_reference_correction);
    assert(!result.invalidate_retained_view);
    assert(!result.release_retained_lease);
    assert(result.acquisition_allowed);
    assert(state.center_trusted_for_acquire);
    assert(state.retained_view_valid);
    assert(state.retained_lease_active);
    const uint32_t higher_reference_generation =
        state.marker_context_generation;

    d1l_node_marker_t marker = signed_marker_at(1995U);
    d1l_map_marker_truth_t truth = {0};
    assert(d1l_map_marker_truth_evaluate(
        &marker, state.age_reference_valid,
        state.age_reference_timestamp, &truth));
    assert(truth.renderable);
    assert(truth.age_sec == 5U);

    /* Both timestamps remain in the same minute bucket. The explicit
     * backwards edge must still rebuild and remove the now-future pin. */
    assert(2000U / 60U == 1990U / 60U);
    input.age_reference_timestamp = 1990U;
    assert(d1l_map_viewport_truth_apply(&state, &input, &result));
    assert(result.marker_context_changed);
    assert(result.backward_reference_correction);
    assert(state.marker_context_generation != higher_reference_generation);
    assert(result.acquisition_allowed);
    assert(!result.invalidate_retained_view);
    assert(state.retained_view_valid);
    assert(state.retained_lease_active);
    assert(!d1l_map_marker_truth_evaluate(
        &marker, state.age_reference_valid,
        state.age_reference_timestamp, &truth));
    assert(!truth.renderable);
    assert(truth.reject_reason ==
           D1L_MAP_MARKER_REJECT_TIMESTAMP_IN_FUTURE);

    input.center_source = D1L_MAP_CENTER_SOURCE_UNKNOWN;
    assert(d1l_map_viewport_truth_apply(&state, &input, &result));
    assert(result.marker_context_changed);
    assert(result.invalidate_retained_view);
    assert(result.release_retained_lease);
    assert(!result.acquisition_allowed);
    assert(!state.center_trusted_for_acquire);
    assert(!state.retained_view_valid);
    assert(!state.retained_lease_active);
}

int main(void)
{
    test_stateful_time_and_center_truth_transitions();
    return 0;
}
