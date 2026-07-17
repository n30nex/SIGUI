#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "mesh/meshcore_path_dispatch.h"
#include "mesh/meshcore_route_selection.h"

static uint8_t byte_at(const uint8_t *data, size_t size, size_t index)
{
    return size > 0U ? data[index % size] : (uint8_t)(index * 29U + 7U);
}

static uint32_t u32_at(const uint8_t *data, size_t size, size_t index)
{
    uint32_t value = 0U;
    for (size_t offset = 0U; offset < 4U; ++offset) {
        value |= (uint32_t)byte_at(data, size, index + offset) << (offset * 8U);
    }
    return value;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    d1l_meshcore_path_plain_t first;
    d1l_meshcore_path_plain_t second;
    memset(&first, 0xa5, sizeof(first));
    memset(&second, 0x5a, sizeof(second));
    const d1l_meshcore_path_plain_t first_before = first;
    const d1l_meshcore_path_plain_t second_before = second;
    const bool accepted = d1l_meshcore_path_plain_decode(data, size, &first);
    const bool repeated = d1l_meshcore_path_plain_decode(data, size, &second);
    assert(accepted == repeated);
    if (!accepted) {
        assert(memcmp(&first, &first_before, sizeof(first)) == 0);
        assert(memcmp(&second, &second_before, sizeof(second)) == 0);
    } else {
        assert(memcmp(&first, &second, sizeof(first)) == 0);
        assert(d1l_meshcore_wire_path_len_valid(first.path_len));
        assert(first.path_byte_len ==
               d1l_meshcore_wire_path_byte_len(first.path_len));
        assert(d1l_meshcore_wire_path_hash_size(first.path_len) <= 3U);
        assert(first.path_byte_len ==
               d1l_meshcore_wire_path_hash_size(first.path_len) *
               d1l_meshcore_wire_path_hash_count(first.path_len));
        if (first.path_byte_len == 0U) {
            assert(first.path == NULL);
        } else {
            assert(first.path >= data);
            assert(first.path + first.path_byte_len <= data + size);
        }
        assert(first.extra >= data);
        assert(first.extra + first.extra_len <= data + size);
        assert(first.extra_type == (first.raw_extra_type & 0x0fU));
        if (first.extra_type == D1L_MESHCORE_PAYLOAD_ACK) {
            assert(first.source == D1L_MESHCORE_PATH_SOURCE_ACK_PATH);
            assert(first.kind == (first.extra_len >= 4U ?
                D1L_MESHCORE_PATH_EXTRA_ACK : D1L_MESHCORE_PATH_EXTRA_NONE));
        } else if (first.extra_type == D1L_MESHCORE_PATH_EXTRA_TYPE_RESPONSE) {
            assert(first.source == D1L_MESHCORE_PATH_SOURCE_PATH_RESPONSE);
            assert(first.kind == (first.extra_len >= 4U ?
                D1L_MESHCORE_PATH_EXTRA_RESPONSE : D1L_MESHCORE_PATH_EXTRA_NONE));
        } else {
            assert(first.source == D1L_MESHCORE_PATH_SOURCE_OBSERVED);
            assert(first.kind == D1L_MESHCORE_PATH_EXTRA_NONE);
        }
    }

    uint8_t path[D1L_MESHCORE_MAX_PATH_BYTES];
    for (size_t index = 0U; index < sizeof(path); ++index) {
        path[index] = byte_at(data, size, index + 1U);
    }
    const bool known = (byte_at(data, size, 65U) & 1U) != 0U;
    const bool learned_this_boot = (byte_at(data, size, 66U) & 1U) != 0U;
    const uint8_t path_len = byte_at(data, size, 67U);
    const uint32_t learned_at = u32_at(data, size, 68U);
    const uint32_t now = u32_at(data, size, 72U);
    const uint8_t flood_hash_bytes =
        (uint8_t)(1U + byte_at(data, size, 76U) % 3U);
    d1l_meshcore_route_selection_t selection_a;
    d1l_meshcore_route_selection_t selection_b;
    memset(&selection_a, 0xa5, sizeof(selection_a));
    memset(&selection_b, 0x5a, sizeof(selection_b));
    assert(d1l_meshcore_route_select(
        known, learned_this_boot, path, path_len, learned_at, now,
        flood_hash_bytes, &selection_a));
    assert(d1l_meshcore_route_select(
        known, learned_this_boot, path, path_len, learned_at, now,
        flood_hash_bytes, &selection_b));
    assert(memcmp(&selection_a, &selection_b, sizeof(selection_a)) == 0);
    if (selection_a.route == D1L_MESHCORE_ROUTE_DIRECT) {
        assert(known && learned_this_boot);
        assert(d1l_meshcore_wire_path_len_valid(path_len));
        assert(selection_a.path_len == path_len);
        assert(selection_a.path_hash_bytes ==
               d1l_meshcore_wire_path_hash_size(path_len));
        assert(selection_a.path_hops ==
               d1l_meshcore_wire_path_hash_count(path_len));
        assert(selection_a.path_byte_len ==
               d1l_meshcore_wire_path_byte_len(path_len));
        assert(memcmp(selection_a.path, path,
                      selection_a.path_byte_len) == 0);
    } else {
        assert(selection_a.route == D1L_MESHCORE_ROUTE_FLOOD);
        assert(selection_a.path_len ==
               (uint8_t)((flood_hash_bytes - 1U) << 6U));
        assert(selection_a.path_byte_len == 0U);
        assert(selection_a.path_hops == 0U);
        assert(selection_a.path_hash_bytes == flood_hash_bytes);
    }

    d1l_meshcore_route_selection_t rejected;
    memset(&rejected, 0x3c, sizeof(rejected));
    const d1l_meshcore_route_selection_t rejected_before = rejected;
    assert(!d1l_meshcore_route_select(
        known, learned_this_boot, path, path_len, learned_at, now,
        0U, &rejected));
    assert(memcmp(&rejected, &rejected_before, sizeof(rejected)) == 0);

    d1l_meshcore_path_state_t path_state = {
        .learned_at_ms = learned_at,
        .last_success_ms = u32_at(data, size, 77U),
        .last_failure_ms = u32_at(data, size, 81U),
        .expires_at_ms = u32_at(data, size, 85U),
        .generation = u32_at(data, size, 89U),
        .source = byte_at(data, size, 93U),
        .lifecycle = byte_at(data, size, 94U),
        .consecutive_failures = byte_at(data, size, 95U),
        .flags = byte_at(data, size, 96U),
    };
    assert(d1l_meshcore_route_select_canonical(
        known, learned_this_boot, path, path_len, &path_state, now,
        flood_hash_bytes, &selection_a));
    assert(d1l_meshcore_route_select_canonical(
        known, learned_this_boot, path, path_len, &path_state, now,
        flood_hash_bytes, &selection_b));
    assert(memcmp(&selection_a, &selection_b, sizeof(selection_a)) == 0);
    assert(selection_a.path_generation == path_state.generation);
    return 0;
}
