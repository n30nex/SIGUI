#include "meshcore_oracle.h"

#include "Packet.h"
#include "Utils.h"
#include "ed_25519.h"
#include "helpers/AdvertDataHelpers.h"
#include "helpers/BaseChatMesh.h"
#include "mesh/advert_data.h"
#include "mesh/ed25519_canonical.h"

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
static_assert(D1L_ADVERT_DATA_MAX_LEN ==
                  D1L_MESHCORE_ORACLE_MAX_ADVERT_DATA_BYTES,
              "D1L production advert-data limit changed");
static_assert(PUB_KEY_SIZE == D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES,
              "Pinned MeshCore public-key width changed");
static_assert(SEED_SIZE == D1L_MESHCORE_ORACLE_IDENTITY_SEED_BYTES,
              "Pinned MeshCore identity-seed width changed");
static_assert(PRV_KEY_SIZE == 64U,
              "Pinned MeshCore expanded-private-key width changed");
static_assert(SIGNATURE_SIZE == D1L_MESHCORE_ORACLE_SIGNATURE_BYTES,
              "Pinned MeshCore signature width changed");
static_assert(D1L_MESHCORE_ORACLE_MIN_ACK_BYTES == sizeof(uint32_t),
              "Pinned MeshCore ACK CRC width changed");
static_assert(D1L_MESHCORE_ORACLE_TRACE_FIXED_BYTES == 9U,
              "Pinned MeshCore TRACE prefix changed");
static_assert(D1L_MESHCORE_ORACLE_MAX_TRACE_PATH_BYTES + 1U == MAX_PATH_SIZE,
              "Pinned MeshCore TRACE hop capacity changed");
static_assert(D1L_MESHCORE_ORACLE_GROUP_HASH_BYTES == PATH_HASH_SIZE,
              "Pinned MeshCore group hash width changed");
static_assert(D1L_MESHCORE_ORACLE_GROUP_SECRET_BYTES == PUB_KEY_SIZE,
              "Pinned MeshCore group secret width changed");
static_assert(D1L_MESHCORE_ORACLE_SHARED_SECRET_BYTES == PUB_KEY_SIZE,
              "Pinned MeshCore shared-secret width changed");
static_assert(D1L_MESHCORE_ORACLE_GROUP_MAC_BYTES == CIPHER_MAC_SIZE,
              "Pinned MeshCore group MAC width changed");
static_assert(D1L_MESHCORE_ORACLE_GROUP_BLOCK_BYTES == CIPHER_BLOCK_SIZE,
              "Pinned MeshCore group cipher block width changed");
static_assert(D1L_MESHCORE_ORACLE_MAX_GROUP_PLAINTEXT_BYTES ==
                  MAX_GROUP_DATA_LENGTH + 3U,
              "Pinned MeshCore group-data plaintext limit changed");
static_assert(D1L_MESHCORE_ORACLE_DM_HASH_BYTES == PATH_HASH_SIZE,
              "Pinned MeshCore DM hash width changed");
static_assert(D1L_MESHCORE_ORACLE_MAX_DM_TEXT_BYTES == MAX_TEXT_LEN,
              "Pinned BaseChatMesh DM text limit changed");
static_assert(D1L_MESHCORE_ORACLE_MAX_LOGIN_PASSWORD_BYTES == 15U,
              "Pinned BaseChatMesh login-password limit changed");
static_assert(D1L_MESHCORE_ORACLE_MAX_REQUEST_RESPONSE_PLAINTEXT_BYTES +
                      CIPHER_MAC_SIZE + CIPHER_BLOCK_SIZE - 1U ==
                  MAX_PACKET_PAYLOAD,
              "Pinned MeshCore request/response plaintext limit changed");
static_assert(D1L_MESHCORE_ORACLE_MAX_DM_EXTENDED_TEXT_BYTES + 2U ==
                  D1L_MESHCORE_ORACLE_MAX_DM_TEXT_BYTES,
              "Pinned BaseChatMesh extended-attempt text limit changed");
static_assert(D1L_MESHCORE_ORACLE_EXPECTED_ACK_BYTES == sizeof(uint32_t),
              "Pinned BaseChatMesh expected-ACK width changed");
static_assert(D1L_MESHCORE_ORACLE_DM_ACK_BYTES == 6U,
              "Pinned BaseChatMesh plain-DM ACK width changed");
static_assert(D1L_MESHCORE_ORACLE_MAX_ACK_PATH_BYTES == MAX_PATH_SIZE,
              "Pinned MeshCore maximum return-path width changed");
static_assert(D1L_MESHCORE_ORACLE_MAX_PATH_RETURN_EXTRA_BYTES + 5U ==
                  MAX_PACKET_PAYLOAD - 2U - CIPHER_BLOCK_SIZE,
              "Pinned MeshCore path-return extra limit changed");
static_assert(D1L_MESHCORE_ORACLE_PATH_RETURN_UNIQUENESS_BYTES == 4U,
              "Pinned MeshCore path-return uniqueness width changed");
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

constexpr size_t kMaxCombinedPath =
    MAX_PACKET_PAYLOAD - 2U - CIPHER_BLOCK_SIZE;

void secure_zero(void *data, size_t len)
{
    auto *bytes = reinterpret_cast<volatile uint8_t *>(data);
    while (len > 0U) {
        *bytes++ = 0U;
        --len;
    }
}

bool packet_to_upstream(const d1l_meshcore_oracle_packet_t *packet,
                        mesh::Packet *upstream);

bool create_path_return_packet(
    uint8_t destination_hash,
    uint8_t source_hash,
    const uint8_t *secret,
    uint8_t encoded_return_path_len,
    const uint8_t *return_path,
    uint8_t raw_extra_type,
    const uint8_t *extra,
    size_t extra_len,
    d1l_meshcore_oracle_packet_t *out_packet)
{
    if (secret == nullptr || extra == nullptr || extra_len == 0U ||
        out_packet == nullptr ||
        !mesh::Packet::isValidPathLen(encoded_return_path_len)) {
        return false;
    }
    const size_t hash_size = (encoded_return_path_len >> 6U) + 1U;
    const size_t hash_count = encoded_return_path_len & 63U;
    const size_t path_bytes = hash_size * hash_count;
    if ((path_bytes > 0U && return_path == nullptr) ||
        path_bytes > D1L_MESHCORE_ORACLE_MAX_PATH_BYTES ||
        extra_len > kMaxCombinedPath - 5U - path_bytes) {
        return false;
    }

    std::array<uint8_t, MAX_PACKET_PAYLOAD> plaintext{};
    size_t plaintext_len = 0U;
    plaintext[plaintext_len++] = encoded_return_path_len;
    if (path_bytes > 0U) {
        std::memcpy(&plaintext[plaintext_len], return_path, path_bytes);
        plaintext_len += path_bytes;
    }
    plaintext[plaintext_len++] = raw_extra_type;
    std::memcpy(&plaintext[plaintext_len], extra, extra_len);
    plaintext_len += extra_len;

    d1l_meshcore_oracle_packet_t result{};
    result.header = static_cast<uint8_t>(PAYLOAD_TYPE_PATH << PH_TYPE_SHIFT);
    result.payload[0] = destination_hash;
    result.payload[1] = source_hash;
    const int encrypted_len = mesh::Utils::encryptThenMAC(
        secret, &result.payload[2], plaintext.data(),
        static_cast<int>(plaintext_len));
    if (encrypted_len <= 0 ||
        static_cast<size_t>(encrypted_len) + 2U >
            D1L_MESHCORE_ORACLE_MAX_PAYLOAD_BYTES) {
        return false;
    }
    result.payload_len = static_cast<uint16_t>(encrypted_len + 2);
    *out_packet = result;
    return true;
}

bool parse_path_return_packet(
    const d1l_meshcore_oracle_packet_t *packet,
    uint8_t destination_hash,
    uint8_t source_hash,
    const uint8_t *secret,
    uint8_t expected_raw_extra_type,
    bool require_raw_extra_type,
    size_t expected_extra_len,
    uint8_t *out_encoded_return_path_len,
    uint8_t *out_return_path,
    size_t return_path_capacity,
    size_t *out_return_path_bytes,
    uint8_t *out_extra_type,
    uint8_t *out_extra,
    size_t extra_capacity,
    size_t *out_extra_len)
{
    if (packet == nullptr || secret == nullptr || expected_extra_len == 0U ||
        expected_extra_len >
            D1L_MESHCORE_ORACLE_MAX_PATH_RETURN_EXTRA_BYTES ||
        out_encoded_return_path_len == nullptr || out_return_path == nullptr ||
        out_return_path_bytes == nullptr || out_extra_type == nullptr ||
        out_extra == nullptr || out_extra_len == nullptr) {
        return false;
    }
    mesh::Packet upstream;
    if (!packet_to_upstream(packet, &upstream) ||
        upstream.getPayloadVer() != PAYLOAD_VER_1 ||
        upstream.getPayloadType() != PAYLOAD_TYPE_PATH ||
        upstream.payload_len < 2U + CIPHER_MAC_SIZE + CIPHER_BLOCK_SIZE ||
        upstream.payload[0] != destination_hash ||
        upstream.payload[1] != source_hash) {
        return false;
    }
    const size_t encrypted_len =
        upstream.payload_len - 2U - CIPHER_MAC_SIZE;
    if ((encrypted_len % CIPHER_BLOCK_SIZE) != 0U) {
        return false;
    }

    std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_PAYLOAD_BYTES> plaintext{};
    const int decoded_len = mesh::Utils::MACThenDecrypt(
        secret, plaintext.data(), &upstream.payload[2],
        upstream.payload_len - 2U);
    if (decoded_len <= 0 || static_cast<size_t>(decoded_len) != encrypted_len) {
        return false;
    }

    const uint8_t encoded_path_len = plaintext[0];
    if (!mesh::Packet::isValidPathLen(encoded_path_len)) {
        return false;
    }
    const size_t hash_size = (encoded_path_len >> 6U) + 1U;
    const size_t hash_count = encoded_path_len & 63U;
    const size_t path_bytes = hash_size * hash_count;
    const size_t extra_type_index = 1U + path_bytes;
    const size_t logical_len = extra_type_index + 1U + expected_extra_len;
    const size_t plaintext_size = static_cast<size_t>(decoded_len);
    if (logical_len > plaintext_size || path_bytes > return_path_capacity ||
        expected_extra_len > extra_capacity ||
        (require_raw_extra_type &&
         plaintext[extra_type_index] != expected_raw_extra_type)) {
        return false;
    }
    for (size_t index = logical_len; index < plaintext_size; ++index) {
        if (plaintext[index] != 0U) {
            return false;
        }
    }

    if (path_bytes > 0U) {
        std::memcpy(out_return_path, &plaintext[1], path_bytes);
    }
    std::memcpy(out_extra, &plaintext[extra_type_index + 1U],
                expected_extra_len);
    *out_encoded_return_path_len = encoded_path_len;
    *out_return_path_bytes = path_bytes;
    *out_extra_type = plaintext[extra_type_index] & 0x0FU;
    *out_extra_len = expected_extra_len;
    return true;
}

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

extern "C" bool d1l_meshcore_oracle_verify_signed_advert(
    const uint8_t public_key[D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES],
    const uint8_t timestamp[D1L_MESHCORE_ORACLE_ADVERT_TIMESTAMP_BYTES],
    const uint8_t signature[D1L_MESHCORE_ORACLE_SIGNATURE_BYTES],
    const uint8_t *app_data,
    size_t app_data_len)
{
    if (public_key == nullptr || timestamp == nullptr || signature == nullptr ||
        app_data_len > D1L_MESHCORE_ORACLE_MAX_ADVERT_DATA_BYTES ||
        (app_data_len > 0U && app_data == nullptr) ||
        !d1l_ed25519_encoded_point_is_strict(public_key) ||
        !d1l_ed25519_encoded_point_is_strict(signature) ||
        !d1l_ed25519_signature_s_is_canonical(signature)) {
        return false;
    }
    std::array<uint8_t,
               D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES +
                   D1L_MESHCORE_ORACLE_ADVERT_TIMESTAMP_BYTES +
                   D1L_MESHCORE_ORACLE_MAX_ADVERT_DATA_BYTES>
        message{};
    size_t message_len = 0U;
    std::memcpy(message.data(), public_key,
                D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES);
    message_len += D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES;
    std::memcpy(&message[message_len], timestamp,
                D1L_MESHCORE_ORACLE_ADVERT_TIMESTAMP_BYTES);
    message_len += D1L_MESHCORE_ORACLE_ADVERT_TIMESTAMP_BYTES;
    if (app_data_len > 0U) {
        std::memcpy(&message[message_len], app_data, app_data_len);
        message_len += app_data_len;
    }
    return ed25519_verify(signature, message.data(), message_len, public_key) ==
           1;
}

extern "C" bool d1l_meshcore_oracle_create_signed_advert_packet(
    const uint8_t seed[D1L_MESHCORE_ORACLE_IDENTITY_SEED_BYTES],
    uint32_t timestamp,
    const uint8_t *app_data,
    size_t app_data_len,
    d1l_meshcore_oracle_packet_t *out_packet)
{
    if (seed == nullptr || out_packet == nullptr ||
        app_data_len > D1L_MESHCORE_ORACLE_MAX_ADVERT_DATA_BYTES ||
        (app_data_len > 0U && app_data == nullptr)) {
        return false;
    }

    std::array<uint8_t, D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES> public_key{};
    std::array<uint8_t, PRV_KEY_SIZE> private_key{};
    ed25519_create_keypair(public_key.data(), private_key.data(), seed);
    if (!d1l_ed25519_encoded_point_is_strict(public_key.data())) {
        return false;
    }

    std::array<uint8_t, D1L_MESHCORE_ORACLE_ADVERT_TIMESTAMP_BYTES>
        timestamp_bytes = {
            static_cast<uint8_t>(timestamp),
            static_cast<uint8_t>(timestamp >> 8U),
            static_cast<uint8_t>(timestamp >> 16U),
            static_cast<uint8_t>(timestamp >> 24U),
        };
    std::array<uint8_t,
               D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES +
                   D1L_MESHCORE_ORACLE_ADVERT_TIMESTAMP_BYTES +
                   D1L_MESHCORE_ORACLE_MAX_ADVERT_DATA_BYTES>
        message{};
    size_t message_len = 0U;
    std::memcpy(message.data(), public_key.data(), public_key.size());
    message_len += public_key.size();
    std::memcpy(&message[message_len], timestamp_bytes.data(),
                timestamp_bytes.size());
    message_len += timestamp_bytes.size();
    if (app_data_len > 0U) {
        std::memcpy(&message[message_len], app_data, app_data_len);
        message_len += app_data_len;
    }

    d1l_meshcore_oracle_packet_t result{};
    result.header = static_cast<uint8_t>(PAYLOAD_TYPE_ADVERT << PH_TYPE_SHIFT);
    size_t payload_len = 0U;
    std::memcpy(&result.payload[payload_len], public_key.data(),
                public_key.size());
    payload_len += public_key.size();
    std::memcpy(&result.payload[payload_len], timestamp_bytes.data(),
                timestamp_bytes.size());
    payload_len += timestamp_bytes.size();
    uint8_t *signature = &result.payload[payload_len];
    payload_len += D1L_MESHCORE_ORACLE_SIGNATURE_BYTES;
    if (app_data_len > 0U) {
        std::memcpy(&result.payload[payload_len], app_data, app_data_len);
        payload_len += app_data_len;
    }
    ed25519_sign(signature, message.data(), message_len, public_key.data(),
                 private_key.data());
    if (!d1l_meshcore_oracle_verify_signed_advert(
            public_key.data(), timestamp_bytes.data(), signature, app_data,
            app_data_len)) {
        return false;
    }
    result.payload_len = static_cast<uint16_t>(payload_len);
    *out_packet = result;
    return true;
}

extern "C" bool d1l_meshcore_oracle_parse_signed_advert_packet(
    const d1l_meshcore_oracle_packet_t *packet,
    uint8_t out_public_key[D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES],
    uint32_t *out_timestamp,
    uint8_t *out_app_data,
    size_t app_data_capacity,
    size_t *out_app_data_len)
{
    if (packet == nullptr || out_public_key == nullptr ||
        out_timestamp == nullptr || out_app_data == nullptr ||
        out_app_data_len == nullptr) {
        return false;
    }
    mesh::Packet upstream;
    constexpr size_t fixed_payload_len =
        D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES +
        D1L_MESHCORE_ORACLE_ADVERT_TIMESTAMP_BYTES +
        D1L_MESHCORE_ORACLE_SIGNATURE_BYTES;
    if (!packet_to_upstream(packet, &upstream) ||
        upstream.getPayloadVer() != PAYLOAD_VER_1 ||
        upstream.getPayloadType() != PAYLOAD_TYPE_ADVERT ||
        upstream.payload_len < fixed_payload_len ||
        upstream.payload_len >
            fixed_payload_len + D1L_MESHCORE_ORACLE_MAX_ADVERT_DATA_BYTES) {
        return false;
    }

    const uint8_t *public_key = upstream.payload;
    const uint8_t *timestamp =
        &upstream.payload[D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES];
    const uint8_t *signature =
        &timestamp[D1L_MESHCORE_ORACLE_ADVERT_TIMESTAMP_BYTES];
    const uint8_t *app_data =
        &signature[D1L_MESHCORE_ORACLE_SIGNATURE_BYTES];
    const size_t app_data_len = upstream.payload_len - fixed_payload_len;
    if (app_data_len > app_data_capacity ||
        !d1l_meshcore_oracle_verify_signed_advert(
            public_key, timestamp, signature, app_data, app_data_len)) {
        return false;
    }

    std::array<uint8_t, D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES>
        parsed_public_key{};
    std::memcpy(parsed_public_key.data(), public_key, parsed_public_key.size());
    const uint32_t parsed_timestamp =
        static_cast<uint32_t>(timestamp[0]) |
        (static_cast<uint32_t>(timestamp[1]) << 8U) |
        (static_cast<uint32_t>(timestamp[2]) << 16U) |
        (static_cast<uint32_t>(timestamp[3]) << 24U);
    std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_ADVERT_DATA_BYTES>
        parsed_app_data{};
    if (app_data_len > 0U) {
        std::memcpy(parsed_app_data.data(), app_data, app_data_len);
    }
    std::memcpy(out_public_key, parsed_public_key.data(),
                parsed_public_key.size());
    *out_timestamp = parsed_timestamp;
    if (app_data_len > 0U) {
        std::memcpy(out_app_data, parsed_app_data.data(), app_data_len);
    }
    *out_app_data_len = app_data_len;
    return true;
}

extern "C" bool d1l_meshcore_oracle_identity_shared_secret_from_seed(
    const uint8_t seed[D1L_MESHCORE_ORACLE_IDENTITY_SEED_BYTES],
    const uint8_t peer_public_key[D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES],
    d1l_meshcore_oracle_identity_exchange_t *out_exchange)
{
    if (seed == nullptr || peer_public_key == nullptr ||
        out_exchange == nullptr ||
        !d1l_ed25519_encoded_point_is_strict(peer_public_key)) {
        return false;
    }

    std::array<uint8_t, D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES> public_key{};
    std::array<uint8_t, PRV_KEY_SIZE> private_key{};
    std::array<uint8_t, D1L_MESHCORE_ORACLE_SHARED_SECRET_BYTES>
        shared_secret{};
    ed25519_create_keypair(public_key.data(), private_key.data(), seed);
    if (!d1l_ed25519_encoded_point_is_strict(public_key.data())) {
        secure_zero(private_key.data(), private_key.size());
        return false;
    }
    ed25519_key_exchange(shared_secret.data(), peer_public_key,
                         private_key.data());
    uint8_t secret_or = 0U;
    for (const uint8_t value : shared_secret) {
        secret_or |= value;
    }
    if (secret_or == 0U) {
        secure_zero(private_key.data(), private_key.size());
        secure_zero(shared_secret.data(), shared_secret.size());
        return false;
    }

    d1l_meshcore_oracle_identity_exchange_t result{};
    std::memcpy(result.public_key, public_key.data(), public_key.size());
    std::memcpy(result.shared_secret, shared_secret.data(),
                shared_secret.size());
    *out_exchange = result;
    secure_zero(result.shared_secret, sizeof(result.shared_secret));
    secure_zero(private_key.data(), private_key.size());
    secure_zero(shared_secret.data(), shared_secret.size());
    return true;
}

extern "C" bool d1l_meshcore_oracle_create_login_request_packet(
    uint8_t destination_hash,
    const uint8_t sender_public_key[D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES],
    const uint8_t secret[D1L_MESHCORE_ORACLE_SHARED_SECRET_BYTES],
    uint32_t timestamp,
    uint8_t is_room,
    uint32_t sync_since,
    const uint8_t *password,
    size_t password_len,
    d1l_meshcore_oracle_packet_t *out_packet)
{
    if (sender_public_key == nullptr || secret == nullptr ||
        password == nullptr ||
        password_len > D1L_MESHCORE_ORACLE_MAX_LOGIN_PASSWORD_BYTES ||
        is_room > 1U || (is_room == 0U && sync_since != 0U) ||
        out_packet == nullptr) {
        return false;
    }
    for (size_t index = 0U; index < password_len; ++index) {
        if (password[index] == 0U) {
            return false;
        }
    }

    std::array<uint8_t,
               8U + D1L_MESHCORE_ORACLE_MAX_LOGIN_PASSWORD_BYTES>
        plaintext{};
    size_t plaintext_len = 0U;
    plaintext[plaintext_len++] = static_cast<uint8_t>(timestamp);
    plaintext[plaintext_len++] = static_cast<uint8_t>(timestamp >> 8U);
    plaintext[plaintext_len++] = static_cast<uint8_t>(timestamp >> 16U);
    plaintext[plaintext_len++] = static_cast<uint8_t>(timestamp >> 24U);
    if (is_room != 0U) {
        plaintext[plaintext_len++] = static_cast<uint8_t>(sync_since);
        plaintext[plaintext_len++] = static_cast<uint8_t>(sync_since >> 8U);
        plaintext[plaintext_len++] = static_cast<uint8_t>(sync_since >> 16U);
        plaintext[plaintext_len++] = static_cast<uint8_t>(sync_since >> 24U);
    }
    if (password_len > 0U) {
        std::memcpy(&plaintext[plaintext_len], password, password_len);
        plaintext_len += password_len;
    }

    d1l_meshcore_oracle_packet_t result{};
    result.header =
        static_cast<uint8_t>(PAYLOAD_TYPE_ANON_REQ << PH_TYPE_SHIFT);
    result.payload[0] = destination_hash;
    std::memcpy(&result.payload[1], sender_public_key,
                D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES);
    constexpr size_t outer_len =
        1U + D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES;
    const int encrypted_len = mesh::Utils::encryptThenMAC(
        secret, &result.payload[outer_len], plaintext.data(),
        static_cast<int>(plaintext_len));
    if (encrypted_len <= 0 ||
        outer_len + static_cast<size_t>(encrypted_len) >
            D1L_MESHCORE_ORACLE_MAX_PAYLOAD_BYTES) {
        return false;
    }
    result.payload_len =
        static_cast<uint16_t>(outer_len + static_cast<size_t>(encrypted_len));
    *out_packet = result;
    return true;
}

extern "C" bool d1l_meshcore_oracle_parse_login_request_packet(
    const d1l_meshcore_oracle_packet_t *packet,
    uint8_t destination_hash,
    const uint8_t sender_public_key[D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES],
    const uint8_t secret[D1L_MESHCORE_ORACLE_SHARED_SECRET_BYTES],
    uint8_t is_room,
    uint32_t *out_timestamp,
    uint32_t *out_sync_since,
    uint8_t *out_password,
    size_t password_capacity,
    size_t *out_password_len)
{
    if (packet == nullptr || sender_public_key == nullptr || secret == nullptr ||
        is_room > 1U || out_timestamp == nullptr ||
        out_sync_since == nullptr || out_password == nullptr ||
        out_password_len == nullptr) {
        return false;
    }
    mesh::Packet upstream;
    constexpr size_t outer_len =
        1U + D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES;
    if (!packet_to_upstream(packet, &upstream) ||
        upstream.getPayloadVer() != PAYLOAD_VER_1 ||
        upstream.getPayloadType() != PAYLOAD_TYPE_ANON_REQ ||
        upstream.payload_len < outer_len + CIPHER_MAC_SIZE + CIPHER_BLOCK_SIZE ||
        upstream.payload[0] != destination_hash ||
        std::memcmp(&upstream.payload[1], sender_public_key,
                    D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES) != 0) {
        return false;
    }
    const size_t encrypted_len =
        upstream.payload_len - outer_len - CIPHER_MAC_SIZE;
    if ((encrypted_len != CIPHER_BLOCK_SIZE &&
         encrypted_len != 2U * CIPHER_BLOCK_SIZE) ||
        (encrypted_len % CIPHER_BLOCK_SIZE) != 0U) {
        return false;
    }

    std::array<uint8_t, 2U * CIPHER_BLOCK_SIZE> plaintext{};
    const int decoded_len = mesh::Utils::MACThenDecrypt(
        secret, plaintext.data(), &upstream.payload[outer_len],
        upstream.payload_len - outer_len);
    if (decoded_len <= 0 || static_cast<size_t>(decoded_len) != encrypted_len) {
        return false;
    }
    const size_t prefix_len = is_room != 0U ? 8U : 4U;
    const size_t plaintext_size = static_cast<size_t>(decoded_len);
    size_t password_end = prefix_len;
    while (password_end < plaintext_size && plaintext[password_end] != 0U) {
        ++password_end;
    }
    const size_t password_len = password_end - prefix_len;
    const size_t expected_encrypted_len =
        ((prefix_len + password_len + CIPHER_BLOCK_SIZE - 1U) /
         CIPHER_BLOCK_SIZE) *
        CIPHER_BLOCK_SIZE;
    if (password_len > D1L_MESHCORE_ORACLE_MAX_LOGIN_PASSWORD_BYTES ||
        password_len > password_capacity ||
        plaintext_size != expected_encrypted_len) {
        return false;
    }
    for (size_t index = password_end; index < plaintext_size; ++index) {
        if (plaintext[index] != 0U) {
            return false;
        }
    }

    const uint32_t timestamp =
        static_cast<uint32_t>(plaintext[0]) |
        (static_cast<uint32_t>(plaintext[1]) << 8U) |
        (static_cast<uint32_t>(plaintext[2]) << 16U) |
        (static_cast<uint32_t>(plaintext[3]) << 24U);
    uint32_t sync_since = 0U;
    if (is_room != 0U) {
        sync_since = static_cast<uint32_t>(plaintext[4]) |
                     (static_cast<uint32_t>(plaintext[5]) << 8U) |
                     (static_cast<uint32_t>(plaintext[6]) << 16U) |
                     (static_cast<uint32_t>(plaintext[7]) << 24U);
    }
    std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_LOGIN_PASSWORD_BYTES>
        password{};
    if (password_len > 0U) {
        std::memcpy(password.data(), &plaintext[prefix_len], password_len);
    }
    *out_timestamp = timestamp;
    *out_sync_since = sync_since;
    if (password_len > 0U) {
        std::memcpy(out_password, password.data(), password_len);
    }
    *out_password_len = password_len;
    return true;
}

extern "C" bool d1l_meshcore_oracle_create_request_response_packet(
    uint8_t payload_type,
    uint8_t destination_hash,
    uint8_t source_hash,
    const uint8_t secret[D1L_MESHCORE_ORACLE_SHARED_SECRET_BYTES],
    const uint8_t *plaintext,
    size_t plaintext_len,
    d1l_meshcore_oracle_packet_t *out_packet)
{
    if ((payload_type != PAYLOAD_TYPE_REQ &&
         payload_type != PAYLOAD_TYPE_RESPONSE) ||
        secret == nullptr || plaintext == nullptr || plaintext_len == 0U ||
        plaintext_len >
            D1L_MESHCORE_ORACLE_MAX_REQUEST_RESPONSE_PLAINTEXT_BYTES ||
        out_packet == nullptr) {
        return false;
    }

    d1l_meshcore_oracle_packet_t result{};
    result.header = static_cast<uint8_t>(payload_type << PH_TYPE_SHIFT);
    result.payload[0] = destination_hash;
    result.payload[1] = source_hash;
    const int encrypted_len = mesh::Utils::encryptThenMAC(
        secret, &result.payload[2], plaintext, static_cast<int>(plaintext_len));
    if (encrypted_len <= 0 ||
        static_cast<size_t>(encrypted_len) + 2U >
            D1L_MESHCORE_ORACLE_MAX_PAYLOAD_BYTES) {
        return false;
    }
    result.payload_len = static_cast<uint16_t>(encrypted_len + 2);
    *out_packet = result;
    return true;
}

extern "C" bool d1l_meshcore_oracle_parse_request_response_packet(
    const d1l_meshcore_oracle_packet_t *packet,
    uint8_t payload_type,
    uint8_t destination_hash,
    uint8_t source_hash,
    const uint8_t secret[D1L_MESHCORE_ORACLE_SHARED_SECRET_BYTES],
    size_t expected_plaintext_len,
    uint8_t *out_plaintext,
    size_t plaintext_capacity)
{
    if (packet == nullptr ||
        (payload_type != PAYLOAD_TYPE_REQ &&
         payload_type != PAYLOAD_TYPE_RESPONSE) ||
        secret == nullptr || expected_plaintext_len == 0U ||
        expected_plaintext_len >
            D1L_MESHCORE_ORACLE_MAX_REQUEST_RESPONSE_PLAINTEXT_BYTES ||
        out_plaintext == nullptr || expected_plaintext_len > plaintext_capacity) {
        return false;
    }
    mesh::Packet upstream;
    if (!packet_to_upstream(packet, &upstream) ||
        upstream.getPayloadVer() != PAYLOAD_VER_1 ||
        upstream.getPayloadType() != payload_type ||
        upstream.payload[0] != destination_hash ||
        upstream.payload[1] != source_hash) {
        return false;
    }
    const size_t expected_encrypted_len =
        ((expected_plaintext_len + CIPHER_BLOCK_SIZE - 1U) /
         CIPHER_BLOCK_SIZE) *
        CIPHER_BLOCK_SIZE;
    constexpr size_t outer_len = 2U;
    if (upstream.payload_len !=
        outer_len + CIPHER_MAC_SIZE + expected_encrypted_len) {
        return false;
    }

    std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_PAYLOAD_BYTES> plaintext{};
    const int decoded_len = mesh::Utils::MACThenDecrypt(
        secret, plaintext.data(), &upstream.payload[outer_len],
        upstream.payload_len - outer_len);
    if (decoded_len <= 0 ||
        static_cast<size_t>(decoded_len) != expected_encrypted_len) {
        return false;
    }
    for (size_t index = expected_plaintext_len;
         index < expected_encrypted_len; ++index) {
        if (plaintext[index] != 0U) {
            return false;
        }
    }

    std::memcpy(out_plaintext, plaintext.data(), expected_plaintext_len);
    return true;
}

extern "C" bool d1l_meshcore_oracle_create_login_response_packet(
    uint8_t server_advert_type,
    uint8_t destination_hash,
    uint8_t source_hash,
    const uint8_t secret[D1L_MESHCORE_ORACLE_SHARED_SECRET_BYTES],
    uint32_t server_timestamp,
    uint8_t permissions,
    const uint8_t uniqueness[D1L_MESHCORE_ORACLE_LOGIN_RANDOM_BYTES],
    d1l_meshcore_oracle_packet_t *out_packet)
{
    if ((server_advert_type != D1L_MESHCORE_ADVERT_TYPE_REPEATER &&
         server_advert_type != D1L_MESHCORE_ADVERT_TYPE_ROOM) ||
        secret == nullptr || uniqueness == nullptr || out_packet == nullptr) {
        return false;
    }

    const bool is_admin = (permissions & 0x03U) == 0x03U;
    const uint8_t role = server_advert_type == D1L_MESHCORE_ADVERT_TYPE_REPEATER
        ? static_cast<uint8_t>(is_admin ? 1U : 0U)
        : static_cast<uint8_t>(is_admin ? 1U : (permissions == 0U ? 2U : 0U));
    const uint8_t firmware_version =
        server_advert_type == D1L_MESHCORE_ADVERT_TYPE_REPEATER ? 2U : 1U;
    std::array<uint8_t, D1L_MESHCORE_ORACLE_LOGIN_RESPONSE_BYTES>
        plaintext{};
    plaintext[0] = static_cast<uint8_t>(server_timestamp);
    plaintext[1] = static_cast<uint8_t>(server_timestamp >> 8U);
    plaintext[2] = static_cast<uint8_t>(server_timestamp >> 16U);
    plaintext[3] = static_cast<uint8_t>(server_timestamp >> 24U);
    plaintext[4] = 0U;
    plaintext[5] = 0U;
    plaintext[6] = role;
    plaintext[7] = permissions;
    std::memcpy(&plaintext[8], uniqueness,
                D1L_MESHCORE_ORACLE_LOGIN_RANDOM_BYTES);
    plaintext[12] = firmware_version;
    return d1l_meshcore_oracle_create_request_response_packet(
        PAYLOAD_TYPE_RESPONSE, destination_hash, source_hash, secret,
        plaintext.data(), plaintext.size(), out_packet);
}

extern "C" bool d1l_meshcore_oracle_parse_login_response_packet(
    const d1l_meshcore_oracle_packet_t *packet,
    uint8_t server_advert_type,
    uint8_t destination_hash,
    uint8_t source_hash,
    const uint8_t secret[D1L_MESHCORE_ORACLE_SHARED_SECRET_BYTES],
    uint32_t *out_server_timestamp,
    uint8_t *out_permissions,
    uint8_t out_uniqueness[D1L_MESHCORE_ORACLE_LOGIN_RANDOM_BYTES])
{
    if (packet == nullptr ||
        (server_advert_type != D1L_MESHCORE_ADVERT_TYPE_REPEATER &&
         server_advert_type != D1L_MESHCORE_ADVERT_TYPE_ROOM) ||
        secret == nullptr || out_server_timestamp == nullptr ||
        out_permissions == nullptr || out_uniqueness == nullptr) {
        return false;
    }
    std::array<uint8_t, D1L_MESHCORE_ORACLE_LOGIN_RESPONSE_BYTES>
        plaintext{};
    if (!d1l_meshcore_oracle_parse_request_response_packet(
            packet, PAYLOAD_TYPE_RESPONSE, destination_hash, source_hash,
            secret, plaintext.size(), plaintext.data(), plaintext.size()) ||
        plaintext[4] != 0U || plaintext[5] != 0U) {
        return false;
    }

    const uint8_t permissions = plaintext[7];
    const bool is_admin = (permissions & 0x03U) == 0x03U;
    const uint8_t expected_role =
        server_advert_type == D1L_MESHCORE_ADVERT_TYPE_REPEATER
        ? static_cast<uint8_t>(is_admin ? 1U : 0U)
        : static_cast<uint8_t>(is_admin ? 1U : (permissions == 0U ? 2U : 0U));
    const uint8_t expected_firmware =
        server_advert_type == D1L_MESHCORE_ADVERT_TYPE_REPEATER ? 2U : 1U;
    if (plaintext[6] != expected_role ||
        plaintext[12] != expected_firmware) {
        return false;
    }

    const uint32_t server_timestamp =
        static_cast<uint32_t>(plaintext[0]) |
        (static_cast<uint32_t>(plaintext[1]) << 8U) |
        (static_cast<uint32_t>(plaintext[2]) << 16U) |
        (static_cast<uint32_t>(plaintext[3]) << 24U);
    std::array<uint8_t, D1L_MESHCORE_ORACLE_LOGIN_RANDOM_BYTES> uniqueness{};
    std::memcpy(uniqueness.data(), &plaintext[8], uniqueness.size());
    *out_server_timestamp = server_timestamp;
    *out_permissions = permissions;
    std::memcpy(out_uniqueness, uniqueness.data(), uniqueness.size());
    return true;
}

extern "C" bool d1l_meshcore_oracle_classify_unmatched_login_password(
    uint8_t server_advert_type,
    uint8_t allow_read_only,
    const uint8_t *password,
    size_t password_len,
    const uint8_t *admin_password,
    size_t admin_password_len,
    const uint8_t *guest_password,
    size_t guest_password_len,
    uint8_t *out_authorized,
    uint8_t *out_permissions)
{
    if ((server_advert_type != D1L_MESHCORE_ADVERT_TYPE_REPEATER &&
         server_advert_type != D1L_MESHCORE_ADVERT_TYPE_ROOM) ||
        allow_read_only > 1U ||
        (server_advert_type == D1L_MESHCORE_ADVERT_TYPE_REPEATER &&
         allow_read_only != 0U) ||
        password == nullptr || admin_password == nullptr ||
        guest_password == nullptr || out_authorized == nullptr ||
        out_permissions == nullptr ||
        password_len > D1L_MESHCORE_ORACLE_MAX_LOGIN_PASSWORD_BYTES ||
        admin_password_len > D1L_MESHCORE_ORACLE_MAX_LOGIN_PASSWORD_BYTES ||
        guest_password_len > D1L_MESHCORE_ORACLE_MAX_LOGIN_PASSWORD_BYTES ||
        (password_len > 0U && password[0] < static_cast<uint8_t>(' ')) ||
        std::memchr(password, 0, password_len) != nullptr ||
        std::memchr(admin_password, 0, admin_password_len) != nullptr ||
        std::memchr(guest_password, 0, guest_password_len) != nullptr) {
        return false;
    }

    const bool admin_matches = password_len == admin_password_len &&
        (password_len == 0U ||
         std::memcmp(password, admin_password, password_len) == 0);
    const bool guest_matches = password_len == guest_password_len &&
        (password_len == 0U ||
         std::memcmp(password, guest_password, password_len) == 0);
    uint8_t authorized = 1U;
    uint8_t permissions = 0U;
    if (admin_matches) {
        permissions = 0x03U;
    } else if (guest_matches) {
        permissions = server_advert_type == D1L_MESHCORE_ADVERT_TYPE_REPEATER
            ? 0x00U
            : 0x02U;
    } else if (server_advert_type == D1L_MESHCORE_ADVERT_TYPE_ROOM &&
               allow_read_only == 1U) {
        permissions = 0x00U;
    } else {
        authorized = 0U;
        permissions = 0x00U;
    }

    *out_authorized = authorized;
    *out_permissions = permissions;
    return true;
}

extern "C" bool d1l_meshcore_oracle_resolve_existing_acl_blank_login(
    uint8_t server_advert_type,
    uint8_t is_route_flood,
    const uint8_t *blank_password,
    size_t password_len,
    const uint8_t sender_public_key[D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES],
    const uint8_t *acl_public_keys,
    size_t acl_entry_count,
    uint8_t *out_found,
    uint8_t *out_client_index,
    uint8_t *out_response_secret_source,
    uint8_t *out_client_mutation_mask)
{
    if ((server_advert_type != D1L_MESHCORE_ADVERT_TYPE_REPEATER &&
         server_advert_type != D1L_MESHCORE_ADVERT_TYPE_ROOM) ||
        is_route_flood > 1U || blank_password == nullptr ||
        password_len != 0U || blank_password[0] != 0U ||
        sender_public_key == nullptr || acl_public_keys == nullptr ||
        acl_entry_count > D1L_MESHCORE_ORACLE_MAX_LOGIN_ACL_ENTRIES ||
        out_found == nullptr || out_client_index == nullptr ||
        out_response_secret_source == nullptr ||
        out_client_mutation_mask == nullptr) {
        return false;
    }

    uint8_t client_index = D1L_MESHCORE_ORACLE_LOGIN_ACL_INDEX_NOT_FOUND;
    for (size_t index = 0U; index < acl_entry_count; ++index) {
        const uint8_t *candidate =
            acl_public_keys + index * D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES;
        if (std::memcmp(sender_public_key, candidate,
                        D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES) == 0) {
            client_index = static_cast<uint8_t>(index);
            break;
        }
    }

    const bool found =
        client_index != D1L_MESHCORE_ORACLE_LOGIN_ACL_INDEX_NOT_FOUND;
    *out_found = found ? 1U : 0U;
    *out_client_index = client_index;
    *out_response_secret_source = !found
        ? D1L_MESHCORE_ORACLE_LOGIN_SECRET_SOURCE_NONE
        : (server_advert_type == D1L_MESHCORE_ADVERT_TYPE_REPEATER
               ? D1L_MESHCORE_ORACLE_LOGIN_SECRET_SOURCE_CALLER
               : D1L_MESHCORE_ORACLE_LOGIN_SECRET_SOURCE_STORED_ACL);
    *out_client_mutation_mask = found && is_route_flood == 1U
        ? D1L_MESHCORE_ORACLE_LOGIN_CLIENT_MUTATE_OUT_PATH
        : 0U;
    return true;
}

extern "C" bool d1l_meshcore_oracle_group_channel_hash(
    const uint8_t secret[D1L_MESHCORE_ORACLE_GROUP_SECRET_BYTES],
    uint8_t *out_hash)
{
    if (secret == nullptr || out_hash == nullptr) {
        return false;
    }
    bool upper_half_zero = true;
    for (size_t index = CIPHER_KEY_SIZE; index < PUB_KEY_SIZE; ++index) {
        if (secret[index] != 0U) {
            upper_half_zero = false;
            break;
        }
    }
    mesh::Utils::sha256(out_hash, PATH_HASH_SIZE, secret,
                        upper_half_zero ? CIPHER_KEY_SIZE : PUB_KEY_SIZE);
    return true;
}

extern "C" bool d1l_meshcore_oracle_create_group_packet(
    uint8_t payload_type,
    uint8_t channel_hash,
    const uint8_t secret[D1L_MESHCORE_ORACLE_GROUP_SECRET_BYTES],
    const uint8_t *plaintext,
    size_t plaintext_len,
    d1l_meshcore_oracle_packet_t *out_packet)
{
    if ((payload_type != PAYLOAD_TYPE_GRP_TXT &&
         payload_type != PAYLOAD_TYPE_GRP_DATA) ||
        secret == nullptr || plaintext == nullptr || plaintext_len == 0U ||
        plaintext_len > D1L_MESHCORE_ORACLE_MAX_GROUP_PLAINTEXT_BYTES ||
        out_packet == nullptr) {
        return false;
    }
    d1l_meshcore_oracle_packet_t result{};
    result.header = static_cast<uint8_t>(payload_type << PH_TYPE_SHIFT);
    result.payload[0] = channel_hash;
    const int encrypted_len = mesh::Utils::encryptThenMAC(
        secret, &result.payload[1], plaintext, static_cast<int>(plaintext_len));
    if (encrypted_len <= 0 ||
        static_cast<size_t>(encrypted_len) + 1U >
            D1L_MESHCORE_ORACLE_MAX_PAYLOAD_BYTES) {
        return false;
    }
    result.payload_len = static_cast<uint16_t>(encrypted_len + 1);
    *out_packet = result;
    return true;
}

extern "C" bool d1l_meshcore_oracle_parse_group_packet(
    const d1l_meshcore_oracle_packet_t *packet,
    uint8_t channel_hash,
    const uint8_t secret[D1L_MESHCORE_ORACLE_GROUP_SECRET_BYTES],
    uint8_t *out_plaintext,
    size_t plaintext_capacity,
    size_t *out_plaintext_len)
{
    if (packet == nullptr || secret == nullptr || out_plaintext == nullptr ||
        out_plaintext_len == nullptr) {
        return false;
    }
    mesh::Packet upstream;
    if (!packet_to_upstream(packet, &upstream) ||
        upstream.getPayloadVer() != PAYLOAD_VER_1 ||
        (upstream.getPayloadType() != PAYLOAD_TYPE_GRP_TXT &&
         upstream.getPayloadType() != PAYLOAD_TYPE_GRP_DATA) ||
        upstream.payload_len <
            1U + CIPHER_MAC_SIZE + CIPHER_BLOCK_SIZE ||
        upstream.payload[0] != channel_hash) {
        return false;
    }
    const size_t encrypted_len = upstream.payload_len - 1U - CIPHER_MAC_SIZE;
    if ((encrypted_len % CIPHER_BLOCK_SIZE) != 0U ||
        encrypted_len > plaintext_capacity) {
        return false;
    }
    std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_PAYLOAD_BYTES> plaintext{};
    const int decoded_len = mesh::Utils::MACThenDecrypt(
        secret, plaintext.data(), &upstream.payload[1],
        upstream.payload_len - 1U);
    if (decoded_len <= 0 || static_cast<size_t>(decoded_len) != encrypted_len) {
        return false;
    }
    std::memcpy(out_plaintext, plaintext.data(), encrypted_len);
    *out_plaintext_len = encrypted_len;
    return true;
}

extern "C" bool d1l_meshcore_oracle_create_dm_packet(
    uint8_t destination_hash,
    uint8_t source_hash,
    const uint8_t secret[D1L_MESHCORE_ORACLE_SHARED_SECRET_BYTES],
    uint32_t timestamp,
    uint8_t attempt,
    const uint8_t *text,
    size_t text_len,
    d1l_meshcore_oracle_packet_t *out_packet)
{
    const size_t maximum_text_len =
        attempt > 3U ? D1L_MESHCORE_ORACLE_MAX_DM_EXTENDED_TEXT_BYTES
                     : D1L_MESHCORE_ORACLE_MAX_DM_TEXT_BYTES;
    if (secret == nullptr || text == nullptr || text_len > maximum_text_len ||
        out_packet == nullptr) {
        return false;
    }
    for (size_t index = 0U; index < text_len; ++index) {
        if (text[index] == 0U) {
            return false;
        }
    }

    std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_DM_TEXT_BYTES + 7U>
        plaintext{};
    size_t plaintext_len = 0U;
    plaintext[plaintext_len++] = static_cast<uint8_t>(timestamp);
    plaintext[plaintext_len++] = static_cast<uint8_t>(timestamp >> 8U);
    plaintext[plaintext_len++] = static_cast<uint8_t>(timestamp >> 16U);
    plaintext[plaintext_len++] = static_cast<uint8_t>(timestamp >> 24U);
    plaintext[plaintext_len++] = static_cast<uint8_t>(attempt & 3U);
    if (text_len > 0U) {
        std::memcpy(&plaintext[plaintext_len], text, text_len);
        plaintext_len += text_len;
    }
    if (attempt > 3U) {
        plaintext[plaintext_len++] = 0U;
        plaintext[plaintext_len++] = attempt;
    }

    d1l_meshcore_oracle_packet_t result{};
    result.header = static_cast<uint8_t>(PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT);
    result.payload[0] = destination_hash;
    result.payload[1] = source_hash;
    const int encrypted_len = mesh::Utils::encryptThenMAC(
        secret, &result.payload[2], plaintext.data(),
        static_cast<int>(plaintext_len));
    if (encrypted_len <= 0 ||
        static_cast<size_t>(encrypted_len) + 2U >
            D1L_MESHCORE_ORACLE_MAX_PAYLOAD_BYTES) {
        return false;
    }
    result.payload_len = static_cast<uint16_t>(encrypted_len + 2);
    *out_packet = result;
    return true;
}

extern "C" bool d1l_meshcore_oracle_parse_dm_packet(
    const d1l_meshcore_oracle_packet_t *packet,
    uint8_t destination_hash,
    uint8_t source_hash,
    const uint8_t secret[D1L_MESHCORE_ORACLE_SHARED_SECRET_BYTES],
    uint32_t *out_timestamp,
    uint8_t *out_attempt,
    uint8_t *out_text,
    size_t text_capacity,
    size_t *out_text_len)
{
    if (packet == nullptr || secret == nullptr || out_timestamp == nullptr ||
        out_attempt == nullptr || out_text == nullptr ||
        out_text_len == nullptr) {
        return false;
    }
    mesh::Packet upstream;
    if (!packet_to_upstream(packet, &upstream) ||
        upstream.getPayloadVer() != PAYLOAD_VER_1 ||
        upstream.getPayloadType() != PAYLOAD_TYPE_TXT_MSG ||
        upstream.payload_len <
            2U + CIPHER_MAC_SIZE + CIPHER_BLOCK_SIZE ||
        upstream.payload[0] != destination_hash ||
        upstream.payload[1] != source_hash) {
        return false;
    }
    const size_t encrypted_len =
        upstream.payload_len - 2U - CIPHER_MAC_SIZE;
    if ((encrypted_len % CIPHER_BLOCK_SIZE) != 0U) {
        return false;
    }

    std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_PAYLOAD_BYTES> plaintext{};
    const int decoded_len = mesh::Utils::MACThenDecrypt(
        secret, plaintext.data(), &upstream.payload[2],
        upstream.payload_len - 2U);
    if (decoded_len <= 5 || static_cast<size_t>(decoded_len) != encrypted_len ||
        (plaintext[4] >> 2U) != TXT_TYPE_PLAIN) {
        return false;
    }

    const size_t plaintext_size = static_cast<size_t>(decoded_len);
    size_t text_end = 5U;
    while (text_end < plaintext_size && plaintext[text_end] != 0U) {
        ++text_end;
    }
    const uint8_t low_attempt = static_cast<uint8_t>(plaintext[4] & 3U);
    if (text_end == plaintext_size) {
        const size_t text_len = plaintext_size - 5U;
        if (text_len > D1L_MESHCORE_ORACLE_MAX_DM_TEXT_BYTES ||
            text_len > text_capacity) {
            return false;
        }
        if (text_len > 0U) {
            std::memcpy(out_text, &plaintext[5], text_len);
        }
        *out_timestamp = static_cast<uint32_t>(plaintext[0]) |
                         (static_cast<uint32_t>(plaintext[1]) << 8U) |
                         (static_cast<uint32_t>(plaintext[2]) << 16U) |
                         (static_cast<uint32_t>(plaintext[3]) << 24U);
        *out_attempt = low_attempt;
        *out_text_len = text_len;
        return true;
    }
    const size_t text_len = text_end - 5U;
    const uint8_t extended_attempt =
        text_end + 1U < plaintext_size ? plaintext[text_end + 1U] : 0U;
    const bool has_extended_attempt = extended_attempt > 3U;
    uint8_t attempt = low_attempt;
    size_t padding_start = text_end;
    if (has_extended_attempt) {
        if ((extended_attempt & 3U) != low_attempt ||
            text_len > D1L_MESHCORE_ORACLE_MAX_DM_EXTENDED_TEXT_BYTES) {
            return false;
        }
        attempt = extended_attempt;
        padding_start = text_end + 2U;
    } else if (text_len > D1L_MESHCORE_ORACLE_MAX_DM_TEXT_BYTES) {
        return false;
    }
    for (size_t index = padding_start; index < plaintext_size; ++index) {
        if (plaintext[index] != 0U) {
            return false;
        }
    }
    if (text_len > text_capacity) {
        return false;
    }

    if (text_len > 0U) {
        std::memcpy(out_text, &plaintext[5], text_len);
    }
    *out_timestamp = static_cast<uint32_t>(plaintext[0]) |
                     (static_cast<uint32_t>(plaintext[1]) << 8U) |
                     (static_cast<uint32_t>(plaintext[2]) << 16U) |
                     (static_cast<uint32_t>(plaintext[3]) << 24U);
    *out_attempt = attempt;
    *out_text_len = text_len;
    return true;
}

extern "C" bool d1l_meshcore_oracle_dm_expected_ack_hash(
    const uint8_t sender_public_key[D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES],
    uint32_t timestamp,
    uint8_t attempt,
    const uint8_t *text,
    size_t text_len,
    uint8_t out_expected_ack[D1L_MESHCORE_ORACLE_EXPECTED_ACK_BYTES])
{
    const size_t maximum_text_len =
        attempt > 3U ? D1L_MESHCORE_ORACLE_MAX_DM_EXTENDED_TEXT_BYTES
                     : D1L_MESHCORE_ORACLE_MAX_DM_TEXT_BYTES;
    if (sender_public_key == nullptr || text == nullptr ||
        text_len > maximum_text_len || out_expected_ack == nullptr) {
        return false;
    }
    for (size_t index = 0U; index < text_len; ++index) {
        if (text[index] == 0U) {
            return false;
        }
    }

    std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_DM_TEXT_BYTES + 5U>
        hash_input{};
    hash_input[0] = static_cast<uint8_t>(timestamp);
    hash_input[1] = static_cast<uint8_t>(timestamp >> 8U);
    hash_input[2] = static_cast<uint8_t>(timestamp >> 16U);
    hash_input[3] = static_cast<uint8_t>(timestamp >> 24U);
    hash_input[4] = static_cast<uint8_t>(attempt & 3U);
    if (text_len > 0U) {
        std::memcpy(&hash_input[5], text, text_len);
    }

    mesh::Utils::sha256(
        out_expected_ack, D1L_MESHCORE_ORACLE_EXPECTED_ACK_BYTES,
        hash_input.data(), static_cast<int>(5U + text_len),
        sender_public_key, D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES);
    return true;
}

extern "C" bool d1l_meshcore_oracle_dm_expected_ack(
    const uint8_t sender_public_key[D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES],
    uint32_t timestamp,
    uint8_t attempt,
    const uint8_t *text,
    size_t text_len,
    uint8_t uniqueness_byte,
    uint8_t out_ack[D1L_MESHCORE_ORACLE_DM_ACK_BYTES])
{
    if (out_ack == nullptr ||
        (attempt <= 3U && ((5U + text_len) % CIPHER_BLOCK_SIZE) == 0U)) {
        return false;
    }
    std::array<uint8_t, D1L_MESHCORE_ORACLE_DM_ACK_BYTES> result{};
    if (!d1l_meshcore_oracle_dm_expected_ack_hash(
            sender_public_key, timestamp, attempt, text, text_len,
            result.data())) {
        return false;
    }
    result[4] = attempt > 3U ? attempt : 0U;
    result[5] = uniqueness_byte;
    std::memcpy(out_ack, result.data(), result.size());
    return true;
}

extern "C" bool d1l_meshcore_oracle_create_dm_ack_path_packet(
    uint8_t destination_hash,
    uint8_t source_hash,
    const uint8_t secret[D1L_MESHCORE_ORACLE_SHARED_SECRET_BYTES],
    uint8_t encoded_return_path_len,
    const uint8_t *return_path,
    const uint8_t ack[D1L_MESHCORE_ORACLE_DM_ACK_BYTES],
    d1l_meshcore_oracle_packet_t *out_packet)
{
    return d1l_meshcore_oracle_create_path_return_extra_packet(
        destination_hash, source_hash, secret, encoded_return_path_len,
        return_path, PAYLOAD_TYPE_ACK, ack,
        D1L_MESHCORE_ORACLE_DM_ACK_BYTES, out_packet);
}

extern "C" bool d1l_meshcore_oracle_parse_dm_ack_path_packet(
    const d1l_meshcore_oracle_packet_t *packet,
    uint8_t destination_hash,
    uint8_t source_hash,
    const uint8_t secret[D1L_MESHCORE_ORACLE_SHARED_SECRET_BYTES],
    uint8_t *out_encoded_return_path_len,
    uint8_t *out_return_path,
    size_t return_path_capacity,
    size_t *out_return_path_bytes,
    uint8_t out_ack[D1L_MESHCORE_ORACLE_DM_ACK_BYTES])
{
    if (out_ack == nullptr) {
        return false;
    }
    uint8_t extra_type = 0U;
    size_t extra_len = 0U;
    std::array<uint8_t, D1L_MESHCORE_ORACLE_DM_ACK_BYTES> ack{};
    std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_ACK_PATH_BYTES> path{};
    uint8_t encoded_path_len = 0U;
    size_t path_bytes = 0U;
    if (!d1l_meshcore_oracle_parse_path_return_extra_packet(
            packet, destination_hash, source_hash, secret,
            D1L_MESHCORE_ORACLE_DM_ACK_BYTES, &encoded_path_len, path.data(),
            return_path_capacity, &path_bytes, &extra_type, ack.data(),
            ack.size(), &extra_len) ||
        extra_type != PAYLOAD_TYPE_ACK ||
        extra_len != D1L_MESHCORE_ORACLE_DM_ACK_BYTES ||
        out_encoded_return_path_len == nullptr || out_return_path == nullptr ||
        out_return_path_bytes == nullptr) {
        return false;
    }
    if (path_bytes > 0U) {
        std::memcpy(out_return_path, path.data(), path_bytes);
    }
    std::memcpy(out_ack, ack.data(), ack.size());
    *out_encoded_return_path_len = encoded_path_len;
    *out_return_path_bytes = path_bytes;
    return true;
}

extern "C" bool d1l_meshcore_oracle_create_path_return_extra_packet(
    uint8_t destination_hash,
    uint8_t source_hash,
    const uint8_t secret[D1L_MESHCORE_ORACLE_SHARED_SECRET_BYTES],
    uint8_t encoded_return_path_len,
    const uint8_t *return_path,
    uint8_t extra_type,
    const uint8_t *extra,
    size_t extra_len,
    d1l_meshcore_oracle_packet_t *out_packet)
{
    return create_path_return_packet(
        destination_hash, source_hash, secret, encoded_return_path_len,
        return_path, extra_type, extra, extra_len, out_packet);
}

extern "C" bool d1l_meshcore_oracle_create_path_return_unique_packet(
    uint8_t destination_hash,
    uint8_t source_hash,
    const uint8_t secret[D1L_MESHCORE_ORACLE_SHARED_SECRET_BYTES],
    uint8_t encoded_return_path_len,
    const uint8_t *return_path,
    const uint8_t uniqueness
        [D1L_MESHCORE_ORACLE_PATH_RETURN_UNIQUENESS_BYTES],
    d1l_meshcore_oracle_packet_t *out_packet)
{
    return create_path_return_packet(
        destination_hash, source_hash, secret, encoded_return_path_len,
        return_path, 0xFFU, uniqueness,
        D1L_MESHCORE_ORACLE_PATH_RETURN_UNIQUENESS_BYTES, out_packet);
}

extern "C" bool d1l_meshcore_oracle_parse_path_return_extra_packet(
    const d1l_meshcore_oracle_packet_t *packet,
    uint8_t destination_hash,
    uint8_t source_hash,
    const uint8_t secret[D1L_MESHCORE_ORACLE_SHARED_SECRET_BYTES],
    size_t expected_extra_len,
    uint8_t *out_encoded_return_path_len,
    uint8_t *out_return_path,
    size_t return_path_capacity,
    size_t *out_return_path_bytes,
    uint8_t *out_extra_type,
    uint8_t *out_extra,
    size_t extra_capacity,
    size_t *out_extra_len)
{
    return parse_path_return_packet(
        packet, destination_hash, source_hash, secret, 0U, false,
        expected_extra_len, out_encoded_return_path_len, out_return_path,
        return_path_capacity, out_return_path_bytes, out_extra_type, out_extra,
        extra_capacity, out_extra_len);
}

extern "C" bool d1l_meshcore_oracle_parse_path_return_unique_packet(
    const d1l_meshcore_oracle_packet_t *packet,
    uint8_t destination_hash,
    uint8_t source_hash,
    const uint8_t secret[D1L_MESHCORE_ORACLE_SHARED_SECRET_BYTES],
    uint8_t *out_encoded_return_path_len,
    uint8_t *out_return_path,
    size_t return_path_capacity,
    size_t *out_return_path_bytes,
    uint8_t out_uniqueness
        [D1L_MESHCORE_ORACLE_PATH_RETURN_UNIQUENESS_BYTES])
{
    if (out_uniqueness == nullptr) {
        return false;
    }
    uint8_t extra_type = 0U;
    size_t extra_len = 0U;
    return parse_path_return_packet(
               packet, destination_hash, source_hash, secret, 0xFFU, true,
               D1L_MESHCORE_ORACLE_PATH_RETURN_UNIQUENESS_BYTES,
               out_encoded_return_path_len, out_return_path,
               return_path_capacity, out_return_path_bytes, &extra_type,
               out_uniqueness,
               D1L_MESHCORE_ORACLE_PATH_RETURN_UNIQUENESS_BYTES, &extra_len) &&
           extra_type == 0x0FU &&
           extra_len == D1L_MESHCORE_ORACLE_PATH_RETURN_UNIQUENESS_BYTES;
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

extern "C" bool d1l_meshcore_oracle_create_ack(
    const uint8_t *ack,
    size_t ack_len,
    d1l_meshcore_oracle_packet_t *out_packet)
{
    if (ack == nullptr || out_packet == nullptr ||
        ack_len < D1L_MESHCORE_ORACLE_MIN_ACK_BYTES ||
        ack_len > MAX_PACKET_PAYLOAD) {
        return false;
    }

    d1l_meshcore_oracle_packet_t result{};
    result.header = static_cast<uint8_t>(PAYLOAD_TYPE_ACK << PH_TYPE_SHIFT);
    result.payload_len = static_cast<uint16_t>(ack_len);
    std::memcpy(result.payload, ack, ack_len);
    *out_packet = result;
    return true;
}

extern "C" bool d1l_meshcore_oracle_create_multi_ack(
    const uint8_t *ack,
    size_t ack_len,
    uint8_t remaining,
    d1l_meshcore_oracle_packet_t *out_packet)
{
    if (ack == nullptr || out_packet == nullptr ||
        ack_len < D1L_MESHCORE_ORACLE_MIN_ACK_BYTES ||
        ack_len >= MAX_PACKET_PAYLOAD || remaining > 0x0FU) {
        return false;
    }

    d1l_meshcore_oracle_packet_t result{};
    result.header =
        static_cast<uint8_t>(PAYLOAD_TYPE_MULTIPART << PH_TYPE_SHIFT);
    result.payload[0] =
        static_cast<uint8_t>((remaining << 4U) | PAYLOAD_TYPE_ACK);
    std::memcpy(&result.payload[1], ack, ack_len);
    result.payload_len = static_cast<uint16_t>(ack_len + 1U);
    *out_packet = result;
    return true;
}

extern "C" bool d1l_meshcore_oracle_parse_ack(
    const d1l_meshcore_oracle_packet_t *packet,
    uint8_t *out_ack,
    size_t ack_capacity,
    size_t *out_ack_len,
    uint8_t *out_remaining,
    uint8_t *out_is_multipart)
{
    if (out_ack == nullptr || out_ack_len == nullptr ||
        out_remaining == nullptr || out_is_multipart == nullptr) {
        return false;
    }

    mesh::Packet upstream;
    if (!packet_to_upstream(packet, &upstream) ||
        upstream.getPayloadVer() != PAYLOAD_VER_1) {
        return false;
    }

    const uint8_t payload_type = upstream.getPayloadType();
    const uint8_t *ack = upstream.payload;
    size_t ack_len = upstream.payload_len;
    uint8_t remaining = 0U;
    uint8_t is_multipart = 0U;
    if (payload_type == PAYLOAD_TYPE_MULTIPART) {
        if (ack_len < 2U ||
            (upstream.payload[0] & 0x0FU) != PAYLOAD_TYPE_ACK) {
            return false;
        }
        remaining = static_cast<uint8_t>(upstream.payload[0] >> 4U);
        is_multipart = 1U;
        ++ack;
        --ack_len;
    } else if (payload_type != PAYLOAD_TYPE_ACK) {
        return false;
    }

    if (ack_len < D1L_MESHCORE_ORACLE_MIN_ACK_BYTES ||
        ack_len > ack_capacity) {
        return false;
    }
    std::memcpy(out_ack, ack, ack_len);
    *out_ack_len = ack_len;
    *out_remaining = remaining;
    *out_is_multipart = is_multipart;
    return true;
}

extern "C" bool d1l_meshcore_oracle_create_trace(
    uint32_t tag,
    uint32_t auth_code,
    uint8_t flags,
    d1l_meshcore_oracle_packet_t *out_packet)
{
    if (out_packet == nullptr || flags != 0U) {
        return false;
    }

    d1l_meshcore_oracle_packet_t result{};
    result.header = static_cast<uint8_t>(PAYLOAD_TYPE_TRACE << PH_TYPE_SHIFT);
    std::memcpy(&result.payload[0], &tag, sizeof(tag));
    std::memcpy(&result.payload[4], &auth_code, sizeof(auth_code));
    result.payload[8] = flags;
    result.payload_len = D1L_MESHCORE_ORACLE_TRACE_FIXED_BYTES;
    *out_packet = result;
    return true;
}

extern "C" bool d1l_meshcore_oracle_prepare_trace_direct(
    d1l_meshcore_oracle_packet_t *in_out_packet,
    const uint8_t *path_hashes,
    size_t path_hashes_len,
    uint8_t *out_priority)
{
    if (in_out_packet == nullptr || out_priority == nullptr ||
        (path_hashes_len > 0U && path_hashes == nullptr) ||
        path_hashes_len > D1L_MESHCORE_ORACLE_MAX_TRACE_PATH_BYTES) {
        return false;
    }

    mesh::Packet upstream;
    if (!packet_to_upstream(in_out_packet, &upstream) ||
        upstream.getPayloadVer() != PAYLOAD_VER_1 ||
        upstream.getPayloadType() != PAYLOAD_TYPE_TRACE ||
        upstream.payload_len != D1L_MESHCORE_ORACLE_TRACE_FIXED_BYTES ||
        upstream.path_len != 0U || upstream.payload[8] != 0U) {
        return false;
    }

    upstream.header &= static_cast<uint8_t>(~PH_ROUTE_MASK);
    upstream.header |= ROUTE_TYPE_DIRECT;
    upstream.transport_codes[0] = 0U;
    upstream.transport_codes[1] = 0U;
    if (path_hashes_len > 0U) {
        std::memcpy(&upstream.payload[D1L_MESHCORE_ORACLE_TRACE_FIXED_BYTES],
                    path_hashes, path_hashes_len);
    }
    upstream.payload_len = static_cast<uint16_t>(
        D1L_MESHCORE_ORACLE_TRACE_FIXED_BYTES + path_hashes_len);
    upstream.path_len = 0U;
    *in_out_packet = packet_from_upstream(upstream);
    *out_priority = 5U;
    return true;
}

extern "C" bool d1l_meshcore_oracle_parse_trace_source(
    const d1l_meshcore_oracle_packet_t *packet,
    uint32_t *out_tag,
    uint32_t *out_auth_code,
    uint8_t *out_flags,
    uint8_t *out_path_hashes,
    size_t path_hashes_capacity,
    size_t *out_path_hashes_len)
{
    if (out_tag == nullptr || out_auth_code == nullptr ||
        out_flags == nullptr || out_path_hashes == nullptr ||
        out_path_hashes_len == nullptr) {
        return false;
    }

    mesh::Packet upstream;
    if (!packet_to_upstream(packet, &upstream) ||
        upstream.getPayloadVer() != PAYLOAD_VER_1 ||
        upstream.getPayloadType() != PAYLOAD_TYPE_TRACE ||
        upstream.getRouteType() != ROUTE_TYPE_DIRECT ||
        upstream.path_len != 0U ||
        upstream.payload_len < D1L_MESHCORE_ORACLE_TRACE_FIXED_BYTES ||
        upstream.payload[8] != 0U) {
        return false;
    }
    const size_t path_hashes_len =
        upstream.payload_len - D1L_MESHCORE_ORACLE_TRACE_FIXED_BYTES;
    const uint8_t flags = upstream.payload[8];
    if (path_hashes_len > D1L_MESHCORE_ORACLE_MAX_TRACE_PATH_BYTES ||
        path_hashes_len > path_hashes_capacity) {
        return false;
    }

    uint32_t tag = 0U;
    uint32_t auth_code = 0U;
    std::memcpy(&tag, &upstream.payload[0], sizeof(tag));
    std::memcpy(&auth_code, &upstream.payload[4], sizeof(auth_code));
    if (path_hashes_len > 0U) {
        std::memcpy(out_path_hashes,
                    &upstream.payload[D1L_MESHCORE_ORACLE_TRACE_FIXED_BYTES],
                    path_hashes_len);
    }
    *out_tag = tag;
    *out_auth_code = auth_code;
    *out_flags = flags;
    *out_path_hashes_len = path_hashes_len;
    return true;
}
