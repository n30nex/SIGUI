#include "meshcore_path_state.h"

#include <limits.h>
#include <string.h>

static bool next_generation(uint32_t generation, uint32_t *out_generation)
{
    if (!out_generation || generation == UINT32_MAX) {
        return false;
    }
    *out_generation = generation + 1U;
    return true;
}

bool d1l_meshcore_path_source_valid(uint8_t source)
{
    return source > D1L_MESHCORE_PATH_SOURCE_NONE &&
           source < D1L_MESHCORE_PATH_SOURCE_COUNT;
}

bool d1l_meshcore_path_lifecycle_valid(uint8_t lifecycle)
{
    return lifecycle < D1L_MESHCORE_PATH_STATE_COUNT;
}

void d1l_meshcore_path_state_reset(d1l_meshcore_path_state_t *state)
{
    if (state) {
        memset(state, 0, sizeof(*state));
    }
}

bool d1l_meshcore_path_state_learn(d1l_meshcore_path_state_t *state,
                                   d1l_meshcore_path_source_t source,
                                   uint32_t now_ms)
{
    if (!state || !d1l_meshcore_path_source_valid((uint8_t)source)) {
        return false;
    }

    uint32_t generation = 0U;
    if (!next_generation(state->generation, &generation)) {
        return false;
    }
    memset(state, 0, sizeof(*state));
    state->learned_at_ms = now_ms;
    state->expires_at_ms = now_ms + D1L_MESHCORE_DIRECT_PATH_MAX_AGE_MS;
    state->generation = generation;
    state->source = (uint8_t)source;
    state->lifecycle = D1L_MESHCORE_PATH_STATE_VALID;
    return true;
}

uint32_t d1l_meshcore_path_state_validated_at_ms(
    const d1l_meshcore_path_state_t *state)
{
    if (!state) {
        return 0U;
    }
    return (state->flags & D1L_MESHCORE_PATH_FLAG_HAS_SUCCESS) != 0U ?
        state->last_success_ms : state->learned_at_ms;
}

bool d1l_meshcore_path_state_expire_if_due(d1l_meshcore_path_state_t *state,
                                           uint32_t now_ms)
{
    if (!state || state->lifecycle != D1L_MESHCORE_PATH_STATE_VALID) {
        return false;
    }
    const uint32_t validated_at =
        d1l_meshcore_path_state_validated_at_ms(state);
    if ((uint32_t)(now_ms - validated_at) <=
        D1L_MESHCORE_DIRECT_PATH_MAX_AGE_MS) {
        return false;
    }
    state->lifecycle = D1L_MESHCORE_PATH_STATE_EXPIRED;
    uint32_t next = 0U;
    if (next_generation(state->generation, &next)) {
        state->generation = next;
    }
    return true;
}

d1l_meshcore_path_result_t d1l_meshcore_path_state_note_direct_result(
    d1l_meshcore_path_state_t *state, uint32_t expected_generation,
    bool success, uint32_t now_ms)
{
    if (!state || expected_generation == 0U ||
        state->generation != expected_generation ||
        state->lifecycle != D1L_MESHCORE_PATH_STATE_VALID) {
        return D1L_MESHCORE_PATH_RESULT_STALE;
    }

    if (success) {
        state->last_success_ms = now_ms;
        state->flags |= D1L_MESHCORE_PATH_FLAG_HAS_SUCCESS;
        state->expires_at_ms =
            now_ms + D1L_MESHCORE_DIRECT_PATH_MAX_AGE_MS;
        state->consecutive_failures = 0U;
        return D1L_MESHCORE_PATH_RESULT_UPDATED;
    }

    state->last_failure_ms = now_ms;
    if (state->consecutive_failures < UINT8_MAX) {
        state->consecutive_failures++;
    }
    if (state->consecutive_failures <
        D1L_MESHCORE_DIRECT_PATH_FAILURE_THRESHOLD) {
        return D1L_MESHCORE_PATH_RESULT_UPDATED;
    }

    state->lifecycle = D1L_MESHCORE_PATH_STATE_FAILED;
    uint32_t next = 0U;
    if (next_generation(state->generation, &next)) {
        state->generation = next;
    }
    return D1L_MESHCORE_PATH_RESULT_FLOOD_FALLBACK;
}

const char *d1l_meshcore_path_source_name(uint8_t source)
{
    switch ((d1l_meshcore_path_source_t)source) {
    case D1L_MESHCORE_PATH_SOURCE_NONE:
        return "none";
    case D1L_MESHCORE_PATH_SOURCE_ADVERT:
        return "advert";
    case D1L_MESHCORE_PATH_SOURCE_ACK_PATH:
        return "ack_path";
    case D1L_MESHCORE_PATH_SOURCE_PATH_RESPONSE:
        return "path_response";
    case D1L_MESHCORE_PATH_SOURCE_OBSERVED:
        return "observed";
    case D1L_MESHCORE_PATH_SOURCE_MIGRATED:
        return "migrated";
    default:
        return "invalid";
    }
}

const char *d1l_meshcore_path_lifecycle_name(uint8_t lifecycle)
{
    switch ((d1l_meshcore_path_lifecycle_t)lifecycle) {
    case D1L_MESHCORE_PATH_STATE_NONE:
        return "none";
    case D1L_MESHCORE_PATH_STATE_VALID:
        return "valid";
    case D1L_MESHCORE_PATH_STATE_EXPIRED:
        return "expired";
    case D1L_MESHCORE_PATH_STATE_FAILED:
        return "failed";
    default:
        return "invalid";
    }
}
