#include "meshcore_wire.h"

#include <string.h>

static bool route_has_transport_codes(uint8_t route)
{
    return route == D1L_MESHCORE_ROUTE_TRANSPORT_FLOOD ||
           route == D1L_MESHCORE_ROUTE_TRANSPORT_DIRECT;
}

static uint16_t read_le16(const uint8_t *src)
{
    return (uint16_t)src[0] | ((uint16_t)src[1] << 8);
}

static void write_le16(uint8_t *dest, uint16_t value)
{
    dest[0] = (uint8_t)(value & 0xffU);
    dest[1] = (uint8_t)(value >> 8);
}

bool d1l_meshcore_wire_path_len_valid(uint8_t path_len)
{
    const uint8_t hash_count = path_len & 63U;
    const uint8_t hash_size = (uint8_t)((path_len >> 6) + 1U);
    return hash_size < 4U &&
           (uint16_t)hash_count * hash_size <= D1L_MESHCORE_MAX_PATH_BYTES;
}

uint8_t d1l_meshcore_wire_path_hash_size(uint8_t path_len)
{
    return (uint8_t)((path_len >> 6) + 1U);
}

uint8_t d1l_meshcore_wire_path_hash_count(uint8_t path_len)
{
    return (uint8_t)(path_len & 63U);
}

uint8_t d1l_meshcore_wire_path_byte_len(uint8_t path_len)
{
    return (uint8_t)(d1l_meshcore_wire_path_hash_size(path_len) *
                     d1l_meshcore_wire_path_hash_count(path_len));
}

bool d1l_meshcore_wire_decode(const uint8_t *raw,
                              size_t size,
                              d1l_meshcore_wire_packet_t *out_packet)
{
    if (!raw || !out_packet || size < 3U || size > D1L_MESHCORE_MAX_RAW_PACKET) {
        return false;
    }

    d1l_meshcore_wire_packet_t packet = {0};
    size_t index = 0U;
    packet.header = raw[index++];
    packet.route = packet.header & 0x03U;
    packet.type = (packet.header >> 2) & 0x0fU;
    packet.version = (packet.header >> 6) & 0x03U;
    if (route_has_transport_codes(packet.route)) {
        if (size - index < 5U) {
            return false;
        }
        packet.transport_codes[0] = read_le16(&raw[index]);
        index += 2U;
        packet.transport_codes[1] = read_le16(&raw[index]);
        index += 2U;
    }

    packet.path_len = raw[index++];
    if (!d1l_meshcore_wire_path_len_valid(packet.path_len)) {
        return false;
    }
    packet.path_byte_len = d1l_meshcore_wire_path_byte_len(packet.path_len);
    if ((size - index) <= packet.path_byte_len) {
        return false;
    }
    packet.path = packet.path_byte_len > 0U ? &raw[index] : NULL;
    index += packet.path_byte_len;
    packet.path_hash_bytes = d1l_meshcore_wire_path_hash_size(packet.path_len);
    packet.path_hops = d1l_meshcore_wire_path_hash_count(packet.path_len);
    packet.payload_len = (uint16_t)(size - index);
    if (packet.payload_len == 0U ||
        packet.payload_len > D1L_MESHCORE_MAX_PACKET_PAYLOAD) {
        return false;
    }
    packet.payload = &raw[index];
    *out_packet = packet;
    return true;
}

bool d1l_meshcore_wire_write_prefix(uint8_t header,
                                    uint16_t transport_code_0,
                                    uint16_t transport_code_1,
                                    uint8_t path_len,
                                    const uint8_t *path,
                                    uint8_t *dest,
                                    size_t dest_size,
                                    size_t *out_len)
{
    if (!dest || !out_len || !d1l_meshcore_wire_path_len_valid(path_len)) {
        return false;
    }
    const uint8_t path_bytes = d1l_meshcore_wire_path_byte_len(path_len);
    if (path_bytes > 0U && !path) {
        return false;
    }
    const bool transport = route_has_transport_codes(header & 0x03U);
    const size_t prefix_len = 2U + path_bytes + (transport ? 4U : 0U);
    if (prefix_len > dest_size || prefix_len >= D1L_MESHCORE_MAX_RAW_PACKET) {
        return false;
    }

    size_t index = 0U;
    dest[index++] = header;
    if (transport) {
        write_le16(&dest[index], transport_code_0);
        index += 2U;
        write_le16(&dest[index], transport_code_1);
        index += 2U;
    }
    dest[index++] = path_len;
    if (path_bytes > 0U) {
        memcpy(&dest[index], path, path_bytes);
        index += path_bytes;
    }
    *out_len = index;
    return true;
}

bool d1l_meshcore_wire_encode(const d1l_meshcore_wire_packet_t *packet,
                              uint8_t *dest,
                              size_t dest_size,
                              size_t *out_len)
{
    if (!packet || !dest || !out_len || !packet->payload ||
        packet->payload_len == 0U ||
        packet->payload_len > D1L_MESHCORE_MAX_PACKET_PAYLOAD) {
        return false;
    }

    if (!d1l_meshcore_wire_path_len_valid(packet->path_len)) {
        return false;
    }
    const uint8_t path_bytes = d1l_meshcore_wire_path_byte_len(packet->path_len);
    if (path_bytes > 0U && !packet->path) {
        return false;
    }
    const bool transport = route_has_transport_codes(packet->header & 0x03U);
    const size_t required_len = 2U + path_bytes + (transport ? 4U : 0U) +
                                packet->payload_len;
    if (required_len > dest_size || required_len > D1L_MESHCORE_MAX_RAW_PACKET) {
        return false;
    }

    size_t prefix_len = 0U;
    if (!d1l_meshcore_wire_write_prefix(packet->header,
                                        packet->transport_codes[0],
                                        packet->transport_codes[1],
                                        packet->path_len,
                                        packet->path,
                                        dest,
                                        dest_size,
                                        &prefix_len)) {
        return false;
    }
    memcpy(&dest[prefix_len], packet->payload, packet->payload_len);
    *out_len = prefix_len + packet->payload_len;
    return true;
}
