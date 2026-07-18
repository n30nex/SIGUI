#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "mesh/dm_delivery_state.h"

static uint8_t byte_at(const uint8_t *data, size_t size, size_t index)
{
    return size > 0U ? data[index % size] : (uint8_t)(index * 37U + 11U);
}

static uint32_t u32_at(const uint8_t *data, size_t size, size_t index)
{
    uint32_t value = 0U;
    for (size_t offset = 0U; offset < 4U; ++offset) {
        value |= (uint32_t)byte_at(data, size, index + offset) << (offset * 8U);
    }
    return value;
}

static uint64_t u64_at(const uint8_t *data, size_t size, size_t index)
{
    uint64_t value = 0U;
    for (size_t offset = 0U; offset < 8U; ++offset) {
        value |= (uint64_t)byte_at(data, size, index + offset) << (offset * 8U);
    }
    return value;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    d1l_dm_delivery_owner_t owner = {0};
    const uint64_t initial_session = u64_at(data, size, 0U) | 1U;
    const uint32_t initial_revision = u32_at(data, size, 8U) | 1U;
    const uint32_t initial_ack = u32_at(data, size, 12U);
    assert(d1l_dm_delivery_owner_begin(
        &owner, initial_session, initial_revision, initial_ack));

    const size_t operations = size > 0U ? size : 1U;
    for (size_t operation = 0U; operation < operations; ++operation) {
        const size_t cursor = operation * 19U;
        const uint8_t action = byte_at(data, size, cursor) % 6U;
        d1l_dm_delivery_owner_t before = owner;
        if (action == 0U) {
            const uint64_t session = u64_at(data, size, cursor + 1U);
            const uint32_t revision = u32_at(data, size, cursor + 9U);
            const uint32_t ack = u32_at(data, size, cursor + 13U);
            const bool expected = !before.active && session != 0U &&
                                  revision != 0U;
            const bool accepted = d1l_dm_delivery_owner_begin(
                &owner, session, revision, ack);
            assert(accepted == expected);
            if (accepted) {
                d1l_dm_delivery_owner_t expected_owner = before;
                expected_owner.active = true;
                expected_owner.session_id = session;
                expected_owner.revision = revision;
                expected_owner.ack_hash = ack;
                expected_owner.state = D1L_DM_DELIVERY_QUEUED;
                assert(memcmp(&owner, &expected_owner, sizeof(owner)) == 0);
            } else {
                assert(memcmp(&owner, &before, sizeof(owner)) == 0);
            }
        } else if (action == 1U) {
            uint64_t session = u64_at(data, size, cursor + 1U);
            uint32_t expected_revision = u32_at(data, size, cursor + 9U);
            if ((byte_at(data, size, cursor + 17U) & 1U) != 0U) {
                session = before.session_id;
            }
            if ((byte_at(data, size, cursor + 17U) & 2U) != 0U) {
                expected_revision = before.revision;
            }
            const d1l_dm_delivery_state_t next_state =
                (d1l_dm_delivery_state_t)byte_at(data, size, cursor + 13U);
            uint32_t next_revision = u32_at(data, size, cursor + 14U);
            if ((byte_at(data, size, cursor + 18U) & 1U) != 0U) {
                next_revision = expected_revision + 1U;
            }
            const bool expected = before.active &&
                before.session_id == session &&
                before.revision == expected_revision &&
                expected_revision != UINT32_MAX &&
                next_revision == expected_revision + 1U &&
                d1l_dm_delivery_transition_allowed(before.state, next_state) &&
                (next_state != D1L_DM_DELIVERY_ACKNOWLEDGED ||
                 before.state == D1L_DM_DELIVERY_AWAITING_ACK);
            const bool accepted = d1l_dm_delivery_owner_apply(
                &owner, session, expected_revision, next_state, next_revision);
            assert(accepted == expected);
            if (accepted) {
                d1l_dm_delivery_owner_t expected_owner = before;
                expected_owner.state = next_state;
                expected_owner.revision = before.revision + 1U;
                assert(memcmp(&owner, &expected_owner, sizeof(owner)) == 0);
            } else {
                assert(memcmp(&owner, &before, sizeof(owner)) == 0);
            }
        } else if (action == 2U) {
            const uint32_t ack = u32_at(data, size, cursor + 1U);
            const bool expected = before.active &&
                before.state == D1L_DM_DELIVERY_AWAITING_ACK &&
                before.ack_hash == ack;
            assert(d1l_dm_delivery_owner_ack_matches(&owner, ack) == expected);
            assert(memcmp(&owner, &before, sizeof(owner)) == 0);
        } else if (action == 3U) {
            d1l_dm_delivery_owner_clear(&owner);
            const d1l_dm_delivery_owner_t cleared = {0};
            assert(memcmp(&owner, &cleared, sizeof(owner)) == 0);
        } else if (action == 4U) {
            owner.active = (byte_at(data, size, cursor + 1U) & 1U) != 0U;
            owner.session_id = u64_at(data, size, cursor + 2U);
            owner.revision = u32_at(data, size, cursor + 10U);
            owner.ack_hash = u32_at(data, size, cursor + 14U);
            owner.state = (d1l_dm_delivery_state_t)
                byte_at(data, size, cursor + 18U);
        } else {
            const d1l_dm_delivery_state_t from =
                (d1l_dm_delivery_state_t)byte_at(data, size, cursor + 1U);
            const d1l_dm_delivery_state_t to =
                (d1l_dm_delivery_state_t)byte_at(data, size, cursor + 2U);
            const bool first = d1l_dm_delivery_transition_allowed(from, to);
            const bool second = d1l_dm_delivery_transition_allowed(from, to);
            assert(first == second);
            assert(memcmp(&owner, &before, sizeof(owner)) == 0);
        }
    }
    return 0;
}
