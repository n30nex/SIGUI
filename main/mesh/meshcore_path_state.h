#pragma once

#include <stdbool.h>
#include <stdint.h>

#define D1L_MESHCORE_DIRECT_PATH_MAX_AGE_MS (30U * 60U * 1000U)
#define D1L_MESHCORE_DIRECT_PATH_FAILURE_THRESHOLD 2U
#define D1L_MESHCORE_PATH_FLAG_HAS_SUCCESS 0x01U
#define D1L_MESHCORE_PATH_FLAGS_MASK D1L_MESHCORE_PATH_FLAG_HAS_SUCCESS

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    D1L_MESHCORE_PATH_SOURCE_NONE = 0,
    D1L_MESHCORE_PATH_SOURCE_ADVERT,
    D1L_MESHCORE_PATH_SOURCE_ACK_PATH,
    D1L_MESHCORE_PATH_SOURCE_PATH_RESPONSE,
    D1L_MESHCORE_PATH_SOURCE_OBSERVED,
    D1L_MESHCORE_PATH_SOURCE_MIGRATED,
    D1L_MESHCORE_PATH_SOURCE_COUNT,
} d1l_meshcore_path_source_t;

typedef enum {
    D1L_MESHCORE_PATH_STATE_NONE = 0,
    D1L_MESHCORE_PATH_STATE_VALID,
    D1L_MESHCORE_PATH_STATE_EXPIRED,
    D1L_MESHCORE_PATH_STATE_FAILED,
    D1L_MESHCORE_PATH_STATE_COUNT,
} d1l_meshcore_path_lifecycle_t;

/* Retained metadata for the canonical path bytes stored with a contact. */
typedef struct {
    uint32_t learned_at_ms;
    uint32_t last_success_ms;
    uint32_t last_failure_ms;
    uint32_t expires_at_ms;
    uint32_t generation;
    uint8_t source;
    uint8_t lifecycle;
    uint8_t consecutive_failures;
    uint8_t flags;
} d1l_meshcore_path_state_t;

typedef enum {
    D1L_MESHCORE_PATH_RESULT_STALE = 0,
    D1L_MESHCORE_PATH_RESULT_UPDATED,
    D1L_MESHCORE_PATH_RESULT_FLOOD_FALLBACK,
} d1l_meshcore_path_result_t;

bool d1l_meshcore_path_source_valid(uint8_t source);
bool d1l_meshcore_path_lifecycle_valid(uint8_t lifecycle);
void d1l_meshcore_path_state_reset(d1l_meshcore_path_state_t *state);
bool d1l_meshcore_path_state_learn(d1l_meshcore_path_state_t *state,
                                   d1l_meshcore_path_source_t source,
                                   uint32_t now_ms);
bool d1l_meshcore_path_state_expire_if_due(d1l_meshcore_path_state_t *state,
                                           uint32_t now_ms);
d1l_meshcore_path_result_t d1l_meshcore_path_state_note_direct_result(
    d1l_meshcore_path_state_t *state, uint32_t expected_generation,
    bool success, uint32_t now_ms);
uint32_t d1l_meshcore_path_state_validated_at_ms(
    const d1l_meshcore_path_state_t *state);
const char *d1l_meshcore_path_source_name(uint8_t source);
const char *d1l_meshcore_path_lifecycle_name(uint8_t lifecycle);

#ifdef __cplusplus
}
#endif
