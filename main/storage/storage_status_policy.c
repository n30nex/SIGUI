#include "storage_status_policy.h"

#include <stdint.h>
#include <string.h>

uint32_t d1l_storage_status_policy_note_failure(uint32_t current_failures)
{
    return current_failures < UINT32_MAX ? current_failures + 1U : UINT32_MAX;
}

bool d1l_storage_status_policy_is_stale(uint32_t consecutive_failures)
{
    return consecutive_failures > 0U;
}

bool d1l_storage_status_policy_allows_cached_io(uint32_t consecutive_failures)
{
    return consecutive_failures < D1L_STORAGE_STATUS_STALE_FAILURE_LIMIT;
}

bool d1l_storage_status_policy_effective_present(bool last_confirmed_present,
                                                 const char *reported_state,
                                                 bool reported_present)
{
    if (reported_present) {
        return true;
    }
    if (reported_state && strcmp(reported_state, "no_card") == 0) {
        return false;
    }
    return last_confirmed_present;
}
