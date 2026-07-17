#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "mesh/meshcore_packet_semantics.h"

static void assert_zeroed(const d1l_meshcore_packet_semantic_view_t *view)
{
    const d1l_meshcore_packet_semantic_view_t zero = {0};
    assert(memcmp(view, &zero, sizeof(zero)) == 0);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    d1l_meshcore_packet_semantic_view_t first;
    d1l_meshcore_packet_semantic_view_t second;
    memset(&first, 0xa5, sizeof(first));
    memset(&second, 0x5a, sizeof(second));
    const bool accepted = d1l_meshcore_packet_semantic_parse(data, size, &first);
    const bool repeated = d1l_meshcore_packet_semantic_parse(data, size, &second);
    assert(accepted == repeated);
    if (!accepted) {
        assert_zeroed(&first);
        assert_zeroed(&second);
        return 0;
    }

    assert(first.kind == second.kind);
    assert(first.body == second.body);
    assert(first.body_len == second.body_len);
    assert(first.wire.header == second.wire.header);
    assert(first.wire.route == second.wire.route);
    assert(first.wire.type == second.wire.type);
    assert(first.wire.version == D1L_MESHCORE_PAYLOAD_VER_1);
    assert(first.wire.payload == second.wire.payload);
    assert(first.wire.payload_len == second.wire.payload_len);
    assert(first.wire.payload >= data);
    assert(first.wire.payload + first.wire.payload_len <= data + size);
    assert(first.body >= first.wire.payload);
    assert(first.body + first.body_len <=
           first.wire.payload + first.wire.payload_len);
    assert(first.body_len > 0U);
    return 0;
}
