#pragma once

#include <stdbool.h>
#include <stdint.h>

#define D1L_STORAGE_STATUS_STALE_FAILURE_LIMIT 3U

uint32_t d1l_storage_status_policy_note_failure(uint32_t current_failures);
bool d1l_storage_status_policy_is_stale(uint32_t consecutive_failures);
bool d1l_storage_status_policy_allows_cached_io(uint32_t consecutive_failures);
bool d1l_storage_status_policy_effective_present(bool last_confirmed_present,
                                                 const char *reported_state,
                                                 bool reported_present);
