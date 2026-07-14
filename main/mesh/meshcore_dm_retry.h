#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "meshcore_wire.h"

/* Conservative bounded owner-clock windows.  They begin only after TxDone,
 * so radio airtime is never charged against the peer's response window. */
#define D1L_MESHCORE_DIRECT_ACK_TIMEOUT_MS 15000U
#define D1L_MESHCORE_FLOOD_ACK_TIMEOUT_MS 30000U

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    D1L_MESHCORE_DM_ACK_DEADLINE_NONE = 0,
    D1L_MESHCORE_DM_ACK_DEADLINE_RETRY_FLOOD,
    D1L_MESHCORE_DM_ACK_DEADLINE_FAIL_TIMEOUT,
} d1l_meshcore_dm_ack_deadline_action_t;

typedef struct {
    uint64_t deadline_us;
    bool armed;
} d1l_meshcore_dm_ack_deadline_t;

static inline void d1l_meshcore_dm_ack_deadline_clear(
    d1l_meshcore_dm_ack_deadline_t *deadline)
{
    if (deadline) {
        deadline->deadline_us = 0U;
        deadline->armed = false;
    }
}

static inline bool d1l_meshcore_dm_ack_deadline_arm(
    d1l_meshcore_dm_ack_deadline_t *deadline, uint64_t now_us,
    uint8_t route)
{
    if (!deadline ||
        (route != D1L_MESHCORE_ROUTE_DIRECT &&
         route != D1L_MESHCORE_ROUTE_FLOOD)) {
        return false;
    }
    const uint64_t timeout_ms = route == D1L_MESHCORE_ROUTE_DIRECT ?
        D1L_MESHCORE_DIRECT_ACK_TIMEOUT_MS :
        D1L_MESHCORE_FLOOD_ACK_TIMEOUT_MS;
    const uint64_t timeout_us = timeout_ms * 1000ULL;
    deadline->deadline_us = now_us > UINT64_MAX - timeout_us ?
        UINT64_MAX : now_us + timeout_us;
    deadline->armed = true;
    return true;
}

/* Take-once planning surface used by the sole Mesh runtime owner and native
 * fake-clock tests.  Only the initial direct attempt may schedule a retry;
 * every flood attempt and every later attempt terminates on timeout. */
static inline d1l_meshcore_dm_ack_deadline_action_t
d1l_meshcore_dm_ack_deadline_take_due(
    d1l_meshcore_dm_ack_deadline_t *deadline, uint64_t now_us,
    uint8_t route, uint8_t attempt)
{
    if (!deadline || !deadline->armed || now_us < deadline->deadline_us) {
        return D1L_MESHCORE_DM_ACK_DEADLINE_NONE;
    }
    d1l_meshcore_dm_ack_deadline_clear(deadline);
    return route == D1L_MESHCORE_ROUTE_DIRECT && attempt == 0U ?
        D1L_MESHCORE_DM_ACK_DEADLINE_RETRY_FLOOD :
        D1L_MESHCORE_DM_ACK_DEADLINE_FAIL_TIMEOUT;
}

/* Radio ownership is independent of the peer's ACK window.  A due deadline
 * must remain armed while another operation owns the radio, so contention
 * cannot consume the sole flood retry or attribute a path failure before an
 * RF attempt is possible. */
static inline d1l_meshcore_dm_ack_deadline_action_t
d1l_meshcore_dm_ack_deadline_take_due_when_idle(
    d1l_meshcore_dm_ack_deadline_t *deadline, uint64_t now_us,
    uint8_t route, uint8_t attempt, bool radio_busy)
{
    if (radio_busy) {
        return D1L_MESHCORE_DM_ACK_DEADLINE_NONE;
    }
    return d1l_meshcore_dm_ack_deadline_take_due(
        deadline, now_us, route, attempt);
}

#ifdef __cplusplus
}
#endif
