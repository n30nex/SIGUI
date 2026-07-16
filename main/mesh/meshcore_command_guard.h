#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "mesh/meshcore_runtime_guard.h"

/* MeshCore Admin currently permits at most fifteen credential bytes. Keep the
 * terminating byte in the guarded slot so no credential is copied into the
 * FreeRTOS command queue. meshcore_service.c asserts this capacity against the
 * protocol constant at compile time. */
#define D1L_MESH_COMMAND_ADMIN_PASSWORD_CAPACITY 16U

typedef struct {
    d1l_mesh_request_guard_t lifecycle;
    char admin_password[D1L_MESH_COMMAND_ADMIN_PASSWORD_CAPACITY];
    uint8_t admin_password_len;
    bool admin_password_present;
} d1l_mesh_command_request_t;

typedef bool (*d1l_mesh_command_enqueue_fn_t)(
    void *context, const void *command);

typedef enum {
    D1L_MESH_COMMAND_ENQUEUE_INVALID = 0,
    D1L_MESH_COMMAND_ENQUEUE_QUEUED,
    D1L_MESH_COMMAND_ENQUEUE_SATURATED,
} d1l_mesh_command_enqueue_result_t;

static inline void d1l_mesh_command_secure_zero(void *value, size_t size)
{
    volatile uint8_t *bytes = (volatile uint8_t *)value;
    size_t remaining = bytes ? size : 0U;
    while (remaining > 0U) {
        *bytes++ = 0U;
        remaining--;
    }
}

static inline void d1l_mesh_command_request_wipe_admin_password(
    d1l_mesh_command_request_t *request)
{
    if (!request) {
        return;
    }
    d1l_mesh_command_secure_zero(request->admin_password,
                                 sizeof(request->admin_password));
    request->admin_password_len = 0U;
    request->admin_password_present = false;
}

static inline bool d1l_mesh_command_request_matches(
    const d1l_mesh_command_request_t *request, uint32_t request_id)
{
    return request &&
           d1l_mesh_request_guard_matches(&request->lifecycle, request_id);
}

static inline d1l_mesh_request_state_t d1l_mesh_command_request_state(
    const d1l_mesh_command_request_t *request)
{
    return request ? d1l_mesh_request_guard_state(&request->lifecycle) :
                     D1L_MESH_REQUEST_FREE;
}

static inline bool d1l_mesh_command_request_claim(
    d1l_mesh_command_request_t *request, uint32_t request_id)
{
    if (!request ||
        !d1l_mesh_request_guard_claim(&request->lifecycle, request_id)) {
        return false;
    }
    d1l_mesh_command_request_wipe_admin_password(request);
    return true;
}

static inline bool d1l_mesh_command_request_publish(
    d1l_mesh_command_request_t *request, uint32_t request_id)
{
    return request && d1l_mesh_request_guard_publish(
                          &request->lifecycle, request_id);
}

static inline bool d1l_mesh_command_request_release(
    d1l_mesh_command_request_t *request, uint32_t request_id,
    d1l_mesh_request_state_t expected_state)
{
    if (!d1l_mesh_command_request_matches(request, request_id) ||
        d1l_mesh_command_request_state(request) != expected_state) {
        return false;
    }
    d1l_mesh_command_request_wipe_admin_password(request);
    return d1l_mesh_request_guard_release(
        &request->lifecycle, request_id, expected_state);
}

static inline bool d1l_mesh_command_request_store_admin_password(
    d1l_mesh_command_request_t *request, uint32_t request_id,
    const char *password, size_t password_len)
{
    if (!password ||
        password_len >= D1L_MESH_COMMAND_ADMIN_PASSWORD_CAPACITY ||
        !d1l_mesh_command_request_matches(request, request_id) ||
        d1l_mesh_command_request_state(request) !=
            D1L_MESH_REQUEST_PENDING) {
        return false;
    }
    d1l_mesh_command_request_wipe_admin_password(request);
    if (password_len > 0U) {
        memcpy(request->admin_password, password, password_len);
    }
    request->admin_password[password_len] = '\0';
    request->admin_password_len = (uint8_t)password_len;
    request->admin_password_present = true;
    return true;
}

static inline bool d1l_mesh_command_request_take_admin_password(
    d1l_mesh_command_request_t *request, uint32_t request_id,
    char *out_password, size_t out_size, size_t *out_len)
{
    if (!out_password || !out_len ||
        !d1l_mesh_command_request_matches(request, request_id) ||
        d1l_mesh_command_request_state(request) !=
            D1L_MESH_REQUEST_ADMITTED) {
        return false;
    }
    const size_t password_len = request->admin_password_len;
    const bool valid = request->admin_password_present &&
        password_len < D1L_MESH_COMMAND_ADMIN_PASSWORD_CAPACITY &&
        out_size > password_len &&
        request->admin_password[password_len] == '\0';
    if (valid) {
        memcpy(out_password, request->admin_password, password_len + 1U);
        *out_len = password_len;
    }
    d1l_mesh_command_request_wipe_admin_password(request);
    return valid;
}

static inline bool d1l_mesh_command_request_cancel_and_release(
    d1l_mesh_command_request_t *request, uint32_t request_id)
{
    if (!request || !d1l_mesh_request_guard_transition(
            &request->lifecycle, request_id, D1L_MESH_REQUEST_PENDING,
            D1L_MESH_REQUEST_CALLER_CANCELLED)) {
        return false;
    }
    d1l_mesh_command_request_wipe_admin_password(request);
    return d1l_mesh_request_guard_release(
        &request->lifecycle, request_id,
        D1L_MESH_REQUEST_CALLER_CANCELLED);
}

static inline bool d1l_mesh_command_request_expire(
    d1l_mesh_command_request_t *request, uint32_t request_id)
{
    if (!request || !d1l_mesh_request_guard_transition(
            &request->lifecycle, request_id, D1L_MESH_REQUEST_PENDING,
            D1L_MESH_REQUEST_OWNER_EXPIRED)) {
        return false;
    }
    d1l_mesh_command_request_wipe_admin_password(request);
    return true;
}

static inline bool d1l_mesh_command_request_admit(
    d1l_mesh_command_request_t *request, uint32_t request_id)
{
    return request && d1l_mesh_request_guard_transition(
                          &request->lifecycle, request_id,
                          D1L_MESH_REQUEST_PENDING,
                          D1L_MESH_REQUEST_ADMITTED);
}

static inline bool d1l_mesh_command_request_complete(
    d1l_mesh_command_request_t *request, uint32_t request_id)
{
    if (!d1l_mesh_command_request_matches(request, request_id) ||
        d1l_mesh_command_request_state(request) !=
            D1L_MESH_REQUEST_ADMITTED) {
        return false;
    }
    /* An admitted request is exclusively owner-held until completion is
     * signalled, so wiping before the terminal CAS cannot race slot reuse. */
    d1l_mesh_command_request_wipe_admin_password(request);
    return d1l_mesh_request_guard_transition(
        &request->lifecycle, request_id, D1L_MESH_REQUEST_ADMITTED,
        D1L_MESH_REQUEST_COMPLETED);
}

static inline d1l_mesh_command_enqueue_result_t
d1l_mesh_command_request_enqueue(
    d1l_mesh_command_request_t *request, uint32_t request_id,
    d1l_mesh_command_enqueue_fn_t enqueue, void *enqueue_context,
    const void *command)
{
    if (!enqueue || !command ||
        !d1l_mesh_command_request_matches(request, request_id) ||
        d1l_mesh_command_request_state(request) !=
            D1L_MESH_REQUEST_PENDING) {
        return D1L_MESH_COMMAND_ENQUEUE_INVALID;
    }
    if (enqueue(enqueue_context, command)) {
        return D1L_MESH_COMMAND_ENQUEUE_QUEUED;
    }
    return d1l_mesh_command_request_cancel_and_release(request, request_id) ?
        D1L_MESH_COMMAND_ENQUEUE_SATURATED :
        D1L_MESH_COMMAND_ENQUEUE_INVALID;
}

static inline bool d1l_mesh_command_sync_wait_allowed(
    const void *owner, const void *caller)
{
    return !owner || owner != caller;
}
