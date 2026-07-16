#include "map_marker_truth.h"

#include <stdio.h>
#include <string.h>

static bool bounded_text_is_terminated(const char *text, size_t capacity)
{
    return text && capacity > 0U && memchr(text, '\0', capacity) != NULL;
}

static bool marker_is_well_formed(const d1l_node_marker_t *marker)
{
    return marker &&
        bounded_text_is_terminated(marker->fingerprint,
                                   sizeof(marker->fingerprint)) &&
        bounded_text_is_terminated(marker->name, sizeof(marker->name)) &&
        bounded_text_is_terminated(marker->type, sizeof(marker->type)) &&
        marker->fingerprint[0] != '\0' &&
        marker->lat_e6 >= -90000000 && marker->lat_e6 <= 90000000 &&
        marker->lon_e6 >= -180000000 && marker->lon_e6 <= 180000000;
}

static d1l_map_marker_role_t marker_role(const char *type)
{
    if (strcmp(type, "chat") == 0) {
        return D1L_MAP_MARKER_ROLE_COMPANION;
    }
    if (strcmp(type, "repeater") == 0) {
        return D1L_MAP_MARKER_ROLE_REPEATER;
    }
    if (strcmp(type, "room") == 0) {
        return D1L_MAP_MARKER_ROLE_ROOM_SERVER;
    }
    if (strcmp(type, "sensor") == 0) {
        return D1L_MAP_MARKER_ROLE_SENSOR;
    }
    return D1L_MAP_MARKER_ROLE_UNKNOWN;
}

bool d1l_map_center_source_is_trusted(d1l_map_center_source_t source)
{
    return source == D1L_MAP_CENTER_SOURCE_MANUAL ||
        source == D1L_MAP_CENTER_SOURCE_AUTHENTICATED_COMPANION;
}

const char *d1l_map_center_source_label(d1l_map_center_source_t source)
{
    switch (source) {
    case D1L_MAP_CENTER_SOURCE_MANUAL:
        return "Manual";
    case D1L_MAP_CENTER_SOURCE_AUTHENTICATED_COMPANION:
        return "Companion";
    case D1L_MAP_CENTER_SOURCE_UNKNOWN:
    default:
        return "Unknown";
    }
}

bool d1l_map_marker_truth_evaluate(const d1l_node_marker_t *marker,
                                   bool age_reference_valid,
                                   uint32_t age_reference_timestamp,
                                   d1l_map_marker_truth_t *out_truth)
{
    if (!out_truth) {
        return false;
    }
    memset(out_truth, 0, sizeof(*out_truth));
    out_truth->reject_reason = D1L_MAP_MARKER_REJECT_MALFORMED;
    if (!marker_is_well_formed(marker)) {
        return false;
    }
    out_truth->role = marker_role(marker->type);
    if (marker->location_provenance !=
        D1L_NODE_LOCATION_PROVENANCE_SIGNED_ADVERT) {
        out_truth->reject_reason = D1L_MAP_MARKER_REJECT_PROVENANCE_UNKNOWN;
        return false;
    }
    if (!age_reference_valid) {
        out_truth->reject_reason = D1L_MAP_MARKER_REJECT_AGE_UNVERIFIED;
        return false;
    }
    if (marker->location_advert_timestamp == 0U) {
        out_truth->reject_reason = D1L_MAP_MARKER_REJECT_TIMESTAMP_MISSING;
        return false;
    }
    if (marker->location_advert_timestamp > age_reference_timestamp) {
        out_truth->reject_reason = D1L_MAP_MARKER_REJECT_TIMESTAMP_IN_FUTURE;
        return false;
    }
    out_truth->age_sec =
        age_reference_timestamp - marker->location_advert_timestamp;
    out_truth->reject_reason = D1L_MAP_MARKER_REJECT_NONE;
    out_truth->renderable = true;
    return true;
}

const char *d1l_map_marker_role_label(d1l_map_marker_role_t role)
{
    switch (role) {
    case D1L_MAP_MARKER_ROLE_COMPANION:
        return "Companion";
    case D1L_MAP_MARKER_ROLE_REPEATER:
        return "Repeater";
    case D1L_MAP_MARKER_ROLE_ROOM_SERVER:
        return "Room Server";
    case D1L_MAP_MARKER_ROLE_SENSOR:
        return "Sensor";
    case D1L_MAP_MARKER_ROLE_UNKNOWN:
    default:
        return "Unknown role";
    }
}

const char *d1l_map_marker_reject_reason_code(
    d1l_map_marker_reject_reason_t reason)
{
    switch (reason) {
    case D1L_MAP_MARKER_REJECT_NONE:
        return "ready";
    case D1L_MAP_MARKER_REJECT_MALFORMED:
        return "malformed";
    case D1L_MAP_MARKER_REJECT_PROVENANCE_UNKNOWN:
        return "provenance_unknown";
    case D1L_MAP_MARKER_REJECT_AGE_UNVERIFIED:
        return "age_unverified";
    case D1L_MAP_MARKER_REJECT_TIMESTAMP_MISSING:
        return "timestamp_missing";
    case D1L_MAP_MARKER_REJECT_TIMESTAMP_IN_FUTURE:
        return "timestamp_in_future";
    default:
        return "invalid";
    }
}

void d1l_map_marker_format_age(uint32_t age_sec, char *dest, size_t dest_len)
{
    if (!dest || dest_len == 0U) {
        return;
    }
    if (age_sec < 60U) {
        snprintf(dest, dest_len, "<1m");
    } else if (age_sec < 3600U) {
        snprintf(dest, dest_len, "%lum", (unsigned long)(age_sec / 60U));
    } else if (age_sec < 86400U) {
        snprintf(dest, dest_len, "%luh", (unsigned long)(age_sec / 3600U));
    } else {
        snprintf(dest, dest_len, "%lud", (unsigned long)(age_sec / 86400U));
    }
}
