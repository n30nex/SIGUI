#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "map/map_marker_truth.h"

static d1l_node_marker_t marker_for(const char *type)
{
    d1l_node_marker_t marker = {0};
    snprintf(marker.fingerprint, sizeof(marker.fingerprint),
             "0123456789abcdef");
    snprintf(marker.name, sizeof(marker.name), "Hilltop");
    snprintf(marker.type, sizeof(marker.type), "%s", type);
    marker.lat_e6 = 43675000;
    marker.lon_e6 = -79440000;
    marker.location_advert_timestamp = 2000U;
    marker.location_seq = 7U;
    marker.location_provenance =
        D1L_NODE_LOCATION_PROVENANCE_SIGNED_ADVERT;
    return marker;
}

static void assert_role(const char *type, d1l_map_marker_role_t expected,
                        const char *label)
{
    d1l_node_marker_t marker = marker_for(type);
    d1l_map_marker_truth_t truth = {0};
    assert(d1l_map_marker_truth_evaluate(&marker, true, 2120U, &truth));
    assert(truth.renderable);
    assert(truth.reject_reason == D1L_MAP_MARKER_REJECT_NONE);
    assert(truth.role == expected);
    assert(truth.age_sec == 120U);
    assert(strcmp(d1l_map_marker_role_label(truth.role), label) == 0);
}

static void test_exact_roles_without_path_inference(void)
{
    assert_role("chat", D1L_MAP_MARKER_ROLE_COMPANION, "Companion");
    assert_role("repeater", D1L_MAP_MARKER_ROLE_REPEATER, "Repeater");
    assert_role("room", D1L_MAP_MARKER_ROLE_ROOM_SERVER, "Room Server");
    assert_role("sensor", D1L_MAP_MARKER_ROLE_SENSOR, "Sensor");
    assert_role("future-type", D1L_MAP_MARKER_ROLE_UNKNOWN, "Unknown role");
}

static void assert_rejected(const d1l_node_marker_t *marker,
                            bool age_reference_valid,
                            uint32_t reference,
                            d1l_map_marker_reject_reason_t expected)
{
    d1l_map_marker_truth_t truth = {
        .renderable = true,
        .age_sec = UINT32_MAX,
    };
    assert(!d1l_map_marker_truth_evaluate(
        marker, age_reference_valid, reference, &truth));
    assert(!truth.renderable);
    assert(truth.reject_reason == expected);
    assert(strcmp(d1l_map_marker_reject_reason_code(expected), "ready") != 0);
}

static void test_fail_closed_provenance_and_age(void)
{
    d1l_node_marker_t marker = marker_for("repeater");
    marker.location_provenance = D1L_NODE_LOCATION_PROVENANCE_UNKNOWN;
    assert_rejected(&marker, true, 2120U,
                    D1L_MAP_MARKER_REJECT_PROVENANCE_UNKNOWN);

    marker = marker_for("repeater");
    assert_rejected(&marker, false, 2120U,
                    D1L_MAP_MARKER_REJECT_AGE_UNVERIFIED);
    marker.location_advert_timestamp = 0U;
    assert_rejected(&marker, true, 2120U,
                    D1L_MAP_MARKER_REJECT_TIMESTAMP_MISSING);
    marker.location_advert_timestamp = 2121U;
    assert_rejected(&marker, true, 2120U,
                    D1L_MAP_MARKER_REJECT_TIMESTAMP_IN_FUTURE);
}

static void test_malformed_markers_never_render(void)
{
    d1l_node_marker_t marker = marker_for("chat");
    marker.lat_e6 = 90000001;
    assert_rejected(&marker, true, 2120U, D1L_MAP_MARKER_REJECT_MALFORMED);

    marker = marker_for("chat");
    marker.lon_e6 = -180000001;
    assert_rejected(&marker, true, 2120U, D1L_MAP_MARKER_REJECT_MALFORMED);

    marker = marker_for("chat");
    memset(marker.fingerprint, 'a', sizeof(marker.fingerprint));
    assert_rejected(&marker, true, 2120U, D1L_MAP_MARKER_REJECT_MALFORMED);

    d1l_map_marker_truth_t truth = {0};
    assert(!d1l_map_marker_truth_evaluate(NULL, true, 2120U, &truth));
    assert(!d1l_map_marker_truth_evaluate(&marker, true, 2120U, NULL));
}

static void test_center_provenance_is_explicit(void)
{
    assert(!d1l_map_center_source_is_trusted(
        D1L_MAP_CENTER_SOURCE_UNKNOWN));
    assert(d1l_map_center_source_is_trusted(
        D1L_MAP_CENTER_SOURCE_MANUAL));
    assert(d1l_map_center_source_is_trusted(
        D1L_MAP_CENTER_SOURCE_AUTHENTICATED_COMPANION));
    assert(strcmp(d1l_map_center_source_label(
                      D1L_MAP_CENTER_SOURCE_MANUAL),
                  "Manual") == 0);
    assert(strcmp(d1l_map_center_source_label(
                      D1L_MAP_CENTER_SOURCE_AUTHENTICATED_COMPANION),
                  "Companion") == 0);
    assert(strcmp(d1l_map_center_source_label(
                      D1L_MAP_CENTER_SOURCE_UNKNOWN),
                  "Unknown") == 0);
}

static void assert_age(uint32_t age_sec, const char *expected)
{
    char text[12] = {0};
    d1l_map_marker_format_age(age_sec, text, sizeof(text));
    assert(strcmp(text, expected) == 0);
}

static void test_age_presentation_boundaries(void)
{
    assert_age(0U, "<1m");
    assert_age(59U, "<1m");
    assert_age(60U, "1m");
    assert_age(3599U, "59m");
    assert_age(3600U, "1h");
    assert_age(86399U, "23h");
    assert_age(86400U, "1d");

    char one_byte[1] = {'x'};
    d1l_map_marker_format_age(60U, one_byte, sizeof(one_byte));
    assert(one_byte[0] == '\0');
    d1l_map_marker_format_age(60U, NULL, 0U);
}

int main(void)
{
    test_exact_roles_without_path_inference();
    test_fail_closed_provenance_and_age();
    test_malformed_markers_never_render();
    test_center_provenance_is_explicit();
    test_age_presentation_boundaries();
    return 0;
}
