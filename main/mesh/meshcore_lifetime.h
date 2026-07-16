#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Canonical retained contacts do not expire.  Only their live reachability
 * claim ages out, on the same conservative horizon as a direct route. */
#define D1L_MESHCORE_CONTACT_REACHABLE_MAX_AGE_MS (30U * 60U * 1000U)

/* Wrap-safe for a monotonic uint32 millisecond clock.  The boundary itself is
 * current; max_age + 1 is expired.  Callers use horizons far below 2^31. */
static inline bool d1l_meshcore_lifetime_age_current_u32(
    uint32_t observed_at_ms, uint32_t now_ms, uint32_t max_age_ms)
{
    return (uint32_t)(now_ms - observed_at_ms) <= max_age_ms;
}

static inline bool d1l_meshcore_lifetime_contact_reachable(
    uint32_t last_heard_ms, uint32_t now_ms)
{
    return last_heard_ms != 0U &&
           d1l_meshcore_lifetime_age_current_u32(
               last_heard_ms, now_ms,
               D1L_MESHCORE_CONTACT_REACHABLE_MAX_AGE_MS);
}

/* Advert replay watermarks are retained protocol timestamps, not a wrapping
 * local clock.  UINT32_MAX is therefore terminal and zero never wraps it. */
static inline bool d1l_meshcore_lifetime_advert_is_strictly_newer(
    bool retained, uint32_t retained_timestamp, uint32_t candidate_timestamp)
{
    return !retained || candidate_timestamp > retained_timestamp;
}

/* Pinned SimpleMeshTables lifetime is a 160-entry cyclic insertion window,
 * not a wall-clock TTL.  Keep the exact boundary explicit and overflow-safe. */
static inline bool d1l_meshcore_lifetime_packet_fifo_next(
    uint16_t current_index, uint16_t capacity, uint16_t *out_next_index)
{
    if (!out_next_index || capacity == 0U || current_index >= capacity) {
        return false;
    }
    *out_next_index = current_index + 1U == capacity ?
        0U : (uint16_t)(current_index + 1U);
    return true;
}
