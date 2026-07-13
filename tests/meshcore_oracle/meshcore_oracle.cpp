#include "meshcore_oracle.h"

#include "Packet.h"

#include <array>
#include <cstring>

static_assert(MAX_PATH_SIZE == D1L_MESHCORE_ORACLE_MAX_PATH_BYTES,
              "Pinned MeshCore path limit changed");
static_assert(MAX_PACKET_PAYLOAD == D1L_MESHCORE_ORACLE_MAX_PAYLOAD_BYTES,
              "Pinned MeshCore payload limit changed");
static_assert(MAX_TRANS_UNIT == D1L_MESHCORE_ORACLE_MAX_RAW_BYTES,
              "Pinned MeshCore MTU changed");

namespace {

bool has_transport_codes(uint8_t header)
{
    const uint8_t route = header & PH_ROUTE_MASK;
    return route == ROUTE_TYPE_TRANSPORT_FLOOD ||
           route == ROUTE_TYPE_TRANSPORT_DIRECT;
}

bool structurally_safe_to_read(const uint8_t *raw, size_t raw_len)
{
    if (raw == nullptr || raw_len < 3U || raw_len > MAX_TRANS_UNIT) {
        return false;
    }

    const size_t path_len_index = has_transport_codes(raw[0]) ? 5U : 1U;
    if (path_len_index + 1U >= raw_len) {
        return false;
    }
    const uint8_t encoded_path_len = raw[path_len_index];
    if (!mesh::Packet::isValidPathLen(encoded_path_len)) {
        return false;
    }
    const size_t path_hash_size = (encoded_path_len >> 6U) + 1U;
    const size_t path_hash_count = encoded_path_len & 63U;
    const size_t path_bytes = path_hash_size * path_hash_count;
    return path_len_index + 1U + path_bytes < raw_len;
}

bool structurally_safe_to_write(const d1l_meshcore_oracle_packet_t *packet,
                                size_t dest_capacity,
                                size_t *required_len)
{
    if (packet == nullptr || required_len == nullptr ||
        packet->payload_len == 0U ||
        packet->payload_len > MAX_PACKET_PAYLOAD ||
        !mesh::Packet::isValidPathLen(packet->path_len)) {
        return false;
    }

    const size_t path_hash_size = (packet->path_len >> 6U) + 1U;
    const size_t path_hash_count = packet->path_len & 63U;
    const size_t path_bytes = path_hash_size * path_hash_count;
    const size_t transport_bytes = has_transport_codes(packet->header) ? 4U : 0U;
    const size_t encoded_len =
        2U + transport_bytes + path_bytes + packet->payload_len;
    if (encoded_len > MAX_TRANS_UNIT || encoded_len > dest_capacity) {
        return false;
    }
    *required_len = encoded_len;
    return true;
}

}  // namespace

extern "C" uint32_t d1l_meshcore_oracle_abi_version(void)
{
    return D1L_MESHCORE_ORACLE_ABI_VERSION;
}

extern "C" const char *d1l_meshcore_oracle_upstream_commit(void)
{
    return D1L_MESHCORE_ORACLE_UPSTREAM_COMMIT;
}

extern "C" bool d1l_meshcore_oracle_packet_decode(
    const uint8_t *raw,
    size_t raw_len,
    d1l_meshcore_oracle_packet_t *out_packet)
{
    if (out_packet == nullptr || !structurally_safe_to_read(raw, raw_len)) {
        return false;
    }

    mesh::Packet upstream;
    if (!upstream.readFrom(raw, static_cast<uint8_t>(raw_len))) {
        return false;
    }

    d1l_meshcore_oracle_packet_t result{};
    result.header = upstream.header;
    result.transport_codes[0] = upstream.transport_codes[0];
    result.transport_codes[1] = upstream.transport_codes[1];
    result.path_len = static_cast<uint8_t>(upstream.path_len);
    const size_t path_bytes = upstream.getPathByteLen();
    if (path_bytes > 0U) {
        std::memcpy(result.path, upstream.path, path_bytes);
    }
    result.payload_len = upstream.payload_len;
    std::memcpy(result.payload, upstream.payload, upstream.payload_len);
    *out_packet = result;
    return true;
}

extern "C" bool d1l_meshcore_oracle_packet_encode(
    const d1l_meshcore_oracle_packet_t *packet,
    uint8_t *dest,
    size_t dest_capacity,
    size_t *out_len)
{
    if (dest == nullptr || out_len == nullptr) {
        return false;
    }
    size_t required_len = 0U;
    if (!structurally_safe_to_write(packet, dest_capacity, &required_len)) {
        return false;
    }

    mesh::Packet upstream;
    upstream.header = packet->header;
    upstream.transport_codes[0] = packet->transport_codes[0];
    upstream.transport_codes[1] = packet->transport_codes[1];
    upstream.path_len = packet->path_len;
    const size_t path_bytes = upstream.getPathByteLen();
    if (path_bytes > 0U) {
        std::memcpy(upstream.path, packet->path, path_bytes);
    }
    upstream.payload_len = packet->payload_len;
    std::memcpy(upstream.payload, packet->payload, packet->payload_len);

    std::array<uint8_t, MAX_TRANS_UNIT> encoded{};
    const size_t encoded_len = upstream.writeTo(encoded.data());
    if (encoded_len != required_len) {
        return false;
    }
    std::memcpy(dest, encoded.data(), encoded_len);
    *out_len = encoded_len;
    return true;
}
