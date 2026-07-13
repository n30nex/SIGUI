#include "meshcore_oracle.h"

#include "Packet.h"
#include "helpers/AdvertDataHelpers.h"

#include <array>
#include <cstring>

static_assert(MAX_PATH_SIZE == D1L_MESHCORE_ORACLE_MAX_PATH_BYTES,
              "Pinned MeshCore path limit changed");
static_assert(MAX_PACKET_PAYLOAD == D1L_MESHCORE_ORACLE_MAX_PAYLOAD_BYTES,
              "Pinned MeshCore payload limit changed");
static_assert(MAX_TRANS_UNIT == D1L_MESHCORE_ORACLE_MAX_RAW_BYTES,
              "Pinned MeshCore MTU changed");
static_assert(MAX_ADVERT_DATA_SIZE ==
                  D1L_MESHCORE_ORACLE_MAX_ADVERT_DATA_BYTES,
              "Pinned MeshCore advert-data limit changed");
static_assert(ADV_TYPE_NONE == D1L_MESHCORE_ADVERT_TYPE_NONE,
              "Pinned MeshCore NONE advert type changed");
static_assert(ADV_TYPE_CHAT == D1L_MESHCORE_ADVERT_TYPE_CHAT,
              "Pinned MeshCore CHAT advert type changed");
static_assert(ADV_TYPE_REPEATER == D1L_MESHCORE_ADVERT_TYPE_REPEATER,
              "Pinned MeshCore REPEATER advert type changed");
static_assert(ADV_TYPE_ROOM == D1L_MESHCORE_ADVERT_TYPE_ROOM,
              "Pinned MeshCore ROOM advert type changed");
static_assert(ADV_TYPE_SENSOR == D1L_MESHCORE_ADVERT_TYPE_SENSOR,
              "Pinned MeshCore SENSOR advert type changed");
static_assert(ADV_LATLON_MASK == D1L_MESHCORE_ADVERT_LATLON_MASK,
              "Pinned MeshCore advert location flag changed");
static_assert(ADV_FEAT1_MASK == D1L_MESHCORE_ADVERT_FEAT1_MASK,
              "Pinned MeshCore advert feature-1 flag changed");
static_assert(ADV_FEAT2_MASK == D1L_MESHCORE_ADVERT_FEAT2_MASK,
              "Pinned MeshCore advert feature-2 flag changed");
static_assert(ADV_NAME_MASK == D1L_MESHCORE_ADVERT_NAME_MASK,
              "Pinned MeshCore advert name flag changed");

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

bool advert_layout_is_canonical(const uint8_t *raw, size_t raw_len)
{
    if (raw == nullptr || raw_len == 0U ||
        raw_len > MAX_ADVERT_DATA_SIZE) {
        return false;
    }
    const uint8_t flags = raw[0];
    size_t offset = 1U;
    if ((flags & ADV_LATLON_MASK) != 0U) {
        if (offset + 8U > raw_len) {
            return false;
        }
        offset += 8U;
    }
    if ((flags & ADV_FEAT1_MASK) != 0U) {
        if (offset + 2U > raw_len ||
            (raw[offset] == 0U && raw[offset + 1U] == 0U)) {
            return false;
        }
        offset += 2U;
    }
    if ((flags & ADV_FEAT2_MASK) != 0U) {
        if (offset + 2U > raw_len ||
            (raw[offset] == 0U && raw[offset + 1U] == 0U)) {
            return false;
        }
        offset += 2U;
    }
    if ((flags & ADV_NAME_MASK) == 0U) {
        return offset == raw_len;
    }
    if (offset >= raw_len) {
        return false;
    }
    for (size_t index = offset; index < raw_len; ++index) {
        if (raw[index] == 0U) {
            return false;
        }
    }
    return true;
}

bool advert_is_canonical(const d1l_meshcore_oracle_advert_data_t *advert,
                         size_t dest_capacity,
                         size_t *required_len)
{
    if (advert == nullptr || required_len == nullptr || advert->type > 0x0FU ||
        advert->has_lat_lon > 1U || advert->has_feat1 > 1U ||
        advert->has_feat2 > 1U || advert->has_name > 1U ||
        (advert->has_lat_lon == 0U &&
         (advert->latitude_e6 != 0 || advert->longitude_e6 != 0)) ||
        (advert->has_feat1 == 0U) != (advert->feat1 == 0U) ||
        (advert->has_feat2 == 0U) != (advert->feat2 == 0U) ||
        (advert->has_name == 0U) != (advert->name_len == 0U) ||
        advert->name_len > D1L_MESHCORE_ORACLE_MAX_ADVERT_NAME_BYTES) {
        return false;
    }
    for (size_t index = 0U; index < advert->name_len; ++index) {
        if (advert->name[index] == 0U) {
            return false;
        }
    }
    const size_t encoded_len =
        1U + (advert->has_lat_lon != 0U ? 8U : 0U) +
        (advert->has_feat1 != 0U ? 2U : 0U) +
        (advert->has_feat2 != 0U ? 2U : 0U) + advert->name_len;
    if (encoded_len > MAX_ADVERT_DATA_SIZE || encoded_len > dest_capacity) {
        return false;
    }
    *required_len = encoded_len;
    return true;
}

double exact_microdegrees(int32_t coordinate)
{
    const double adjustment = coordinate < 0 ? -0.25 : 0.25;
    return (static_cast<double>(coordinate) + adjustment) / 1000000.0;
}

bool packet_to_upstream(const d1l_meshcore_oracle_packet_t *packet,
                        mesh::Packet *upstream)
{
    size_t encoded_len = 0U;
    if (upstream == nullptr ||
        !structurally_safe_to_write(packet, MAX_TRANS_UNIT, &encoded_len)) {
        return false;
    }
    (void)encoded_len;
    upstream->header = packet->header;
    upstream->transport_codes[0] = packet->transport_codes[0];
    upstream->transport_codes[1] = packet->transport_codes[1];
    upstream->path_len = packet->path_len;
    const size_t path_bytes = upstream->getPathByteLen();
    if (path_bytes > 0U) {
        std::memcpy(upstream->path, packet->path, path_bytes);
    }
    upstream->payload_len = packet->payload_len;
    std::memcpy(upstream->payload, packet->payload, packet->payload_len);
    return true;
}

d1l_meshcore_oracle_packet_t packet_from_upstream(const mesh::Packet &upstream)
{
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
    return result;
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

    *out_packet = packet_from_upstream(upstream);
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
    if (!packet_to_upstream(packet, &upstream)) {
        return false;
    }

    std::array<uint8_t, MAX_TRANS_UNIT> encoded{};
    const size_t encoded_len = upstream.writeTo(encoded.data());
    if (encoded_len != required_len) {
        return false;
    }
    std::memcpy(dest, encoded.data(), encoded_len);
    *out_len = encoded_len;
    return true;
}

extern "C" bool d1l_meshcore_oracle_advert_data_decode(
    const uint8_t *raw,
    size_t raw_len,
    d1l_meshcore_oracle_advert_data_t *out_advert)
{
    if (out_advert == nullptr || !advert_layout_is_canonical(raw, raw_len)) {
        return false;
    }

    AdvertDataParser upstream(raw, static_cast<uint8_t>(raw_len));
    if (!upstream.isValid()) {
        return false;
    }
    d1l_meshcore_oracle_advert_data_t result{};
    const uint8_t flags = raw[0];
    result.type = upstream.getType();
    result.has_lat_lon = (flags & ADV_LATLON_MASK) != 0U ? 1U : 0U;
    result.latitude_e6 = upstream.getIntLat();
    result.longitude_e6 = upstream.getIntLon();
    result.has_feat1 = (flags & ADV_FEAT1_MASK) != 0U ? 1U : 0U;
    result.feat1 = upstream.getFeat1();
    result.has_feat2 = (flags & ADV_FEAT2_MASK) != 0U ? 1U : 0U;
    result.feat2 = upstream.getFeat2();
    result.has_name = (flags & ADV_NAME_MASK) != 0U ? 1U : 0U;
    if (result.has_name != 0U) {
        result.name_len = static_cast<uint8_t>(std::strlen(upstream.getName()));
        std::memcpy(result.name, upstream.getName(), result.name_len);
    }
    *out_advert = result;
    return true;
}

extern "C" bool d1l_meshcore_oracle_advert_data_encode(
    const d1l_meshcore_oracle_advert_data_t *advert,
    uint8_t *dest,
    size_t dest_capacity,
    size_t *out_len)
{
    if (dest == nullptr || out_len == nullptr) {
        return false;
    }
    size_t required_len = 0U;
    if (!advert_is_canonical(advert, dest_capacity, &required_len)) {
        return false;
    }

    std::array<char, D1L_MESHCORE_ORACLE_MAX_ADVERT_NAME_BYTES + 1U> name{};
    if (advert->name_len > 0U) {
        std::memcpy(name.data(), advert->name, advert->name_len);
    }
    const double latitude = exact_microdegrees(advert->latitude_e6);
    const double longitude = exact_microdegrees(advert->longitude_e6);
    AdvertDataBuilder upstream = advert->has_lat_lon != 0U
        ? AdvertDataBuilder(advert->type, name.data(), latitude, longitude)
        : AdvertDataBuilder(advert->type, name.data());
    if (advert->has_feat1 != 0U) {
        upstream.setFeat1(advert->feat1);
    }
    if (advert->has_feat2 != 0U) {
        upstream.setFeat2(advert->feat2);
    }
    std::array<uint8_t, MAX_ADVERT_DATA_SIZE> encoded{};
    const size_t encoded_len = upstream.encodeTo(encoded.data());
    if (encoded_len != required_len) {
        return false;
    }
    d1l_meshcore_oracle_advert_data_t roundtrip{};
    if (!d1l_meshcore_oracle_advert_data_decode(encoded.data(), encoded_len,
                                                 &roundtrip) ||
        roundtrip.type != advert->type ||
        roundtrip.has_lat_lon != advert->has_lat_lon ||
        roundtrip.latitude_e6 != advert->latitude_e6 ||
        roundtrip.longitude_e6 != advert->longitude_e6 ||
        roundtrip.has_feat1 != advert->has_feat1 ||
        roundtrip.feat1 != advert->feat1 ||
        roundtrip.has_feat2 != advert->has_feat2 ||
        roundtrip.feat2 != advert->feat2 ||
        roundtrip.has_name != advert->has_name ||
        roundtrip.name_len != advert->name_len ||
        (advert->name_len > 0U &&
         std::memcmp(roundtrip.name, advert->name, advert->name_len) != 0)) {
        return false;
    }
    std::memcpy(dest, encoded.data(), encoded_len);
    *out_len = encoded_len;
    return true;
}

extern "C" bool d1l_meshcore_oracle_prepare_flood(
    d1l_meshcore_oracle_packet_t *in_out_packet,
    uint8_t path_hash_size,
    uint8_t use_transport,
    const uint16_t transport_codes[2],
    uint8_t *out_priority)
{
    if (in_out_packet == nullptr || out_priority == nullptr ||
        path_hash_size == 0U || path_hash_size > 3U || use_transport > 1U ||
        (use_transport != 0U && transport_codes == nullptr)) {
        return false;
    }
    mesh::Packet upstream;
    if (!packet_to_upstream(in_out_packet, &upstream) ||
        upstream.getPayloadType() == PAYLOAD_TYPE_TRACE) {
        return false;
    }
    upstream.header &= static_cast<uint8_t>(~PH_ROUTE_MASK);
    if (use_transport != 0U) {
        upstream.header |= ROUTE_TYPE_TRANSPORT_FLOOD;
        upstream.transport_codes[0] = transport_codes[0];
        upstream.transport_codes[1] = transport_codes[1];
    } else {
        upstream.header |= ROUTE_TYPE_FLOOD;
        upstream.transport_codes[0] = 0U;
        upstream.transport_codes[1] = 0U;
    }
    upstream.setPathHashSizeAndCount(path_hash_size, 0U);
    uint8_t priority = 1U;
    if (upstream.getPayloadType() == PAYLOAD_TYPE_PATH) {
        priority = 2U;
    } else if (upstream.getPayloadType() == PAYLOAD_TYPE_ADVERT) {
        priority = 3U;
    }
    *in_out_packet = packet_from_upstream(upstream);
    *out_priority = priority;
    return true;
}

extern "C" bool d1l_meshcore_oracle_prepare_direct(
    d1l_meshcore_oracle_packet_t *in_out_packet,
    const uint8_t *path,
    uint8_t path_len,
    uint8_t *out_priority)
{
    if (in_out_packet == nullptr || out_priority == nullptr ||
        !mesh::Packet::isValidPathLen(path_len)) {
        return false;
    }
    const size_t path_bytes =
        static_cast<size_t>((path_len >> 6U) + 1U) * (path_len & 63U);
    if (path_bytes > 0U && path == nullptr) {
        return false;
    }
    mesh::Packet upstream;
    if (!packet_to_upstream(in_out_packet, &upstream) ||
        upstream.getPayloadType() == PAYLOAD_TYPE_TRACE) {
        return false;
    }
    upstream.header &= static_cast<uint8_t>(~PH_ROUTE_MASK);
    upstream.header |= ROUTE_TYPE_DIRECT;
    upstream.transport_codes[0] = 0U;
    upstream.transport_codes[1] = 0U;
    const uint8_t empty_path = 0U;
    const uint8_t *safe_path = path_bytes > 0U ? path : &empty_path;
    upstream.path_len =
        mesh::Packet::copyPath(upstream.path, safe_path, path_len);
    const uint8_t priority =
        upstream.getPayloadType() == PAYLOAD_TYPE_PATH ? 1U : 0U;
    *in_out_packet = packet_from_upstream(upstream);
    *out_priority = priority;
    return true;
}

extern "C" bool d1l_meshcore_oracle_prepare_zero_hop(
    d1l_meshcore_oracle_packet_t *in_out_packet,
    uint8_t use_transport,
    const uint16_t transport_codes[2],
    uint8_t *out_priority)
{
    if (in_out_packet == nullptr || out_priority == nullptr ||
        use_transport > 1U ||
        (use_transport != 0U && transport_codes == nullptr)) {
        return false;
    }
    mesh::Packet upstream;
    if (!packet_to_upstream(in_out_packet, &upstream) ||
        upstream.getPayloadType() == PAYLOAD_TYPE_TRACE) {
        return false;
    }
    upstream.header &= static_cast<uint8_t>(~PH_ROUTE_MASK);
    if (use_transport != 0U) {
        upstream.header |= ROUTE_TYPE_TRANSPORT_DIRECT;
        upstream.transport_codes[0] = transport_codes[0];
        upstream.transport_codes[1] = transport_codes[1];
    } else {
        upstream.header |= ROUTE_TYPE_DIRECT;
        upstream.transport_codes[0] = 0U;
        upstream.transport_codes[1] = 0U;
    }
    upstream.path_len = 0U;
    *in_out_packet = packet_from_upstream(upstream);
    *out_priority = 0U;
    return true;
}
