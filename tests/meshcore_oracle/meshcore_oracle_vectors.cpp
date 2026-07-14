#include "meshcore_oracle.h"

#include "AES.h"
#include "Packet.h"
#include "SHA256.h"
#include "Utils.h"
#include "ed_25519.h"
#include "mesh/ed25519_canonical.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kPacketRoundtripVectors = 4U;
constexpr std::size_t kPacketInvalidVectors = 5U;
constexpr std::size_t kAdvertRoundtripVectors = 4U;
constexpr std::size_t kAdvertInvalidVectors = 11U;
constexpr std::size_t kSignedAdvertProductionVectors = 3U;
constexpr std::size_t kSignedAdvertPacketRoundtripVectors = 3U;
constexpr std::size_t kSignedAdvertPacketInvalidVectors = 23U;
constexpr std::size_t kSignedAdvertValidVectors = 3U;
constexpr std::size_t kSignedAdvertInvalidVectors = 11U;
constexpr std::size_t kVerifierKatValidVectors = 1U;
constexpr std::size_t kVerifierKatInvalidVectors = 3U;
constexpr std::size_t kPointValidationValidVectors = 4U;
constexpr std::size_t kPointValidationInvalidVectors = 7U;
constexpr std::size_t kCryptoAdapterKatValidVectors = 3U;
constexpr std::size_t kGroupRoundtripVectors = 4U;
constexpr std::size_t kGroupInvalidVectors = 21U;
constexpr std::size_t kLoginRequestRoundtripVectors = 6U;
constexpr std::size_t kLoginRequestInvalidVectors = 35U;
constexpr std::size_t kRequestResponseRoundtripVectors = 6U;
constexpr std::size_t kRequestResponseInvalidVectors = 30U;
constexpr std::size_t kDmRoundtripVectors = 268U;
constexpr std::size_t kDmInvalidVectors = 29U;
constexpr std::size_t kExpectedAckDefinedBodyVectors = 4U;
constexpr std::size_t kExpectedAckValidVectors = 9U;
constexpr std::size_t kExpectedAckPathRoundtripVectors = 4U;
constexpr std::size_t kExpectedAckInvalidVectors = 35U;
constexpr std::size_t kPathReturnRoundtripVectors = 6U;
constexpr std::size_t kPathReturnInvalidVectors = 35U;
constexpr std::size_t kRouteRoundtripVectors = 7U;
constexpr std::size_t kRouteInvalidVectors = 10U;
constexpr std::size_t kAckRoundtripVectors = 5U;
constexpr std::size_t kAckInvalidVectors = 17U;
constexpr std::size_t kTraceRoundtripVectors = 6U;
constexpr std::size_t kTraceInvalidVectors = 19U;

struct Vector {
    uint8_t header;
    uint16_t transport_0;
    uint16_t transport_1;
    uint8_t path_len;
    std::vector<uint8_t> path;
    std::vector<uint8_t> payload;
};

struct AdvertVector {
    uint8_t type;
    uint8_t has_lat_lon;
    int32_t latitude_e6;
    int32_t longitude_e6;
    uint8_t has_feat1;
    uint16_t feat1;
    uint8_t has_feat2;
    uint16_t feat2;
    std::string name;
};

struct AckVector {
    std::vector<uint8_t> ack;
    uint8_t remaining;
    bool multipart;
};

struct SignedAdvertVector {
    std::array<uint8_t, D1L_MESHCORE_ORACLE_ADVERT_TIMESTAMP_BYTES> timestamp;
    std::vector<uint8_t> app_data;
    std::array<uint8_t, D1L_MESHCORE_ORACLE_SIGNATURE_BYTES> signature;
};

struct SignedAdvertMessage {
    std::array<uint8_t,
               D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES +
                   D1L_MESHCORE_ORACLE_ADVERT_TIMESTAMP_BYTES +
                   D1L_MESHCORE_ORACLE_MAX_ADVERT_DATA_BYTES>
        bytes{};
    std::size_t length = 0U;
};

constexpr std::array<uint8_t, 32U> kSignedAdvertSeed = {
    0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U,
    0x08U, 0x09U, 0x0AU, 0x0BU, 0x0CU, 0x0DU, 0x0EU, 0x0FU,
    0x10U, 0x11U, 0x12U, 0x13U, 0x14U, 0x15U, 0x16U, 0x17U,
    0x18U, 0x19U, 0x1AU, 0x1BU, 0x1CU, 0x1DU, 0x1EU, 0x1FU};

constexpr std::array<uint8_t, D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES>
    kSignedAdvertPublicKey = {
        0x03U, 0xA1U, 0x07U, 0xBFU, 0xF3U, 0xCEU, 0x10U, 0xBEU,
        0x1DU, 0x70U, 0xDDU, 0x18U, 0xE7U, 0x4BU, 0xC0U, 0x99U,
        0x67U, 0xE4U, 0xD6U, 0x30U, 0x9BU, 0xA5U, 0x0DU, 0x5FU,
        0x1DU, 0xDCU, 0x86U, 0x64U, 0x12U, 0x55U, 0x31U, 0xB8U};

constexpr std::array<uint8_t, D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES>
    kRfc8032PublicKey = {
        0xD7U, 0x5AU, 0x98U, 0x01U, 0x82U, 0xB1U, 0x0AU, 0xB7U,
        0xD5U, 0x4BU, 0xFEU, 0xD3U, 0xC9U, 0x64U, 0x07U, 0x3AU,
        0x0EU, 0xE1U, 0x72U, 0xF3U, 0xDAU, 0xA6U, 0x23U, 0x25U,
        0xAFU, 0x02U, 0x1AU, 0x68U, 0xF7U, 0x07U, 0x51U, 0x1AU};
constexpr std::array<uint8_t, D1L_MESHCORE_ORACLE_SIGNATURE_BYTES>
    kRfc8032EmptyMessageSignature = {
        0xE5U, 0x56U, 0x43U, 0x00U, 0xC3U, 0x60U, 0xACU, 0x72U,
        0x90U, 0x86U, 0xE2U, 0xCCU, 0x80U, 0x6EU, 0x82U, 0x8AU,
        0x84U, 0x87U, 0x7FU, 0x1EU, 0xB8U, 0xE5U, 0xD9U, 0x74U,
        0xD8U, 0x73U, 0xE0U, 0x65U, 0x22U, 0x49U, 0x01U, 0x55U,
        0x5FU, 0xB8U, 0x82U, 0x15U, 0x90U, 0xA3U, 0x3BU, 0xACU,
        0xC6U, 0x1EU, 0x39U, 0x70U, 0x1CU, 0xF9U, 0xB4U, 0x6BU,
        0xD2U, 0x5BU, 0xF5U, 0xF0U, 0x59U, 0x5BU, 0xBEU, 0x24U,
        0x65U, 0x51U, 0x41U, 0x43U, 0x8EU, 0x7AU, 0x10U, 0x0BU};
constexpr std::array<uint8_t, D1L_MESHCORE_ORACLE_SIGNATURE_BYTES>
    kRfc8032EmptyMessageSignaturePlusOrder = {
        0xE5U, 0x56U, 0x43U, 0x00U, 0xC3U, 0x60U, 0xACU, 0x72U,
        0x90U, 0x86U, 0xE2U, 0xCCU, 0x80U, 0x6EU, 0x82U, 0x8AU,
        0x84U, 0x87U, 0x7FU, 0x1EU, 0xB8U, 0xE5U, 0xD9U, 0x74U,
        0xD8U, 0x73U, 0xE0U, 0x65U, 0x22U, 0x49U, 0x01U, 0x55U,
        0x4CU, 0x8CU, 0x78U, 0x72U, 0xAAU, 0x06U, 0x4EU, 0x04U,
        0x9DU, 0xBBU, 0x30U, 0x13U, 0xFBU, 0xF2U, 0x93U, 0x80U,
        0xD2U, 0x5BU, 0xF5U, 0xF0U, 0x59U, 0x5BU, 0xBEU, 0x24U,
        0x65U, 0x51U, 0x41U, 0x43U, 0x8EU, 0x7AU, 0x10U, 0x1BU};

SignedAdvertMessage make_signed_advert_message(
    const std::array<uint8_t, D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES> &public_key,
    const SignedAdvertVector &vector)
{
    SignedAdvertMessage message{};
    std::memcpy(message.bytes.data(), public_key.data(), public_key.size());
    message.length += public_key.size();
    std::memcpy(&message.bytes[message.length], vector.timestamp.data(),
                vector.timestamp.size());
    message.length += vector.timestamp.size();
    if (!vector.app_data.empty()) {
        std::memcpy(&message.bytes[message.length], vector.app_data.data(),
                    vector.app_data.size());
        message.length += vector.app_data.size();
    }
    return message;
}

std::array<uint8_t, D1L_MESHCORE_ORACLE_SIGNATURE_BYTES>
signature_plus_group_order(
    const std::array<uint8_t, D1L_MESHCORE_ORACLE_SIGNATURE_BYTES> &signature)
{
    constexpr std::array<uint8_t, D1L_ED25519_SCALAR_BYTES> group_order = {
        0xEDU, 0xD3U, 0xF5U, 0x5CU, 0x1AU, 0x63U, 0x12U, 0x58U,
        0xD6U, 0x9CU, 0xF7U, 0xA2U, 0xDEU, 0xF9U, 0xDEU, 0x14U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x10U};
    auto result = signature;
    unsigned int carry = 0U;
    for (std::size_t index = 0; index < group_order.size(); ++index) {
        const unsigned int sum =
            static_cast<unsigned int>(result[32U + index]) +
            static_cast<unsigned int>(group_order[index]) + carry;
        result[32U + index] = static_cast<uint8_t>(sum & 0xFFU);
        carry = sum >> 8U;
    }
    return result;
}

struct TraceVector {
    uint32_t tag;
    uint32_t auth_code;
    uint8_t flags;
    std::vector<uint8_t> path_hashes;
};

bool packet_matches(const d1l_meshcore_oracle_packet_t &packet,
                    const Vector &vector)
{
    const bool transport =
        (vector.header & PH_ROUTE_MASK) == ROUTE_TYPE_TRANSPORT_FLOOD ||
        (vector.header & PH_ROUTE_MASK) == ROUTE_TYPE_TRANSPORT_DIRECT;
    return packet.header == vector.header &&
           (!transport ||
            (packet.transport_codes[0] == vector.transport_0 &&
             packet.transport_codes[1] == vector.transport_1)) &&
           packet.path_len == vector.path_len &&
           packet.payload_len == vector.payload.size() &&
           (vector.path.empty() ||
            std::memcmp(packet.path, vector.path.data(), vector.path.size()) == 0) &&
           std::memcmp(packet.payload, vector.payload.data(), vector.payload.size()) == 0;
}

void append_le16(std::vector<uint8_t> &bytes, uint16_t value)
{
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8U));
}

void append_le32(std::vector<uint8_t> &bytes, int32_t value)
{
    const uint32_t encoded = static_cast<uint32_t>(value);
    bytes.push_back(static_cast<uint8_t>(encoded));
    bytes.push_back(static_cast<uint8_t>(encoded >> 8U));
    bytes.push_back(static_cast<uint8_t>(encoded >> 16U));
    bytes.push_back(static_cast<uint8_t>(encoded >> 24U));
}

std::vector<uint8_t> expected_advert_bytes(const AdvertVector &vector)
{
    uint8_t flags = vector.type;
    flags |= vector.has_lat_lon != 0U ? D1L_MESHCORE_ADVERT_LATLON_MASK : 0U;
    flags |= vector.has_feat1 != 0U ? D1L_MESHCORE_ADVERT_FEAT1_MASK : 0U;
    flags |= vector.has_feat2 != 0U ? D1L_MESHCORE_ADVERT_FEAT2_MASK : 0U;
    flags |= !vector.name.empty() ? D1L_MESHCORE_ADVERT_NAME_MASK : 0U;
    std::vector<uint8_t> expected = {flags};
    if (vector.has_lat_lon != 0U) {
        append_le32(expected, vector.latitude_e6);
        append_le32(expected, vector.longitude_e6);
    }
    if (vector.has_feat1 != 0U) {
        append_le16(expected, vector.feat1);
    }
    if (vector.has_feat2 != 0U) {
        append_le16(expected, vector.feat2);
    }
    expected.insert(expected.end(), vector.name.begin(), vector.name.end());
    return expected;
}

d1l_meshcore_oracle_advert_data_t make_advert(const AdvertVector &vector)
{
    d1l_meshcore_oracle_advert_data_t advert{};
    advert.type = vector.type;
    advert.has_lat_lon = vector.has_lat_lon;
    advert.latitude_e6 = vector.latitude_e6;
    advert.longitude_e6 = vector.longitude_e6;
    advert.has_feat1 = vector.has_feat1;
    advert.feat1 = vector.feat1;
    advert.has_feat2 = vector.has_feat2;
    advert.feat2 = vector.feat2;
    advert.has_name = vector.name.empty() ? 0U : 1U;
    advert.name_len = static_cast<uint8_t>(vector.name.size());
    if (!vector.name.empty()) {
        std::memcpy(advert.name, vector.name.data(), vector.name.size());
    }
    return advert;
}

bool advert_matches(const d1l_meshcore_oracle_advert_data_t &advert,
                    const AdvertVector &vector)
{
    return advert.type == vector.type &&
           advert.has_lat_lon == vector.has_lat_lon &&
           advert.latitude_e6 == vector.latitude_e6 &&
           advert.longitude_e6 == vector.longitude_e6 &&
           advert.has_feat1 == vector.has_feat1 &&
           advert.feat1 == vector.feat1 &&
           advert.has_feat2 == vector.has_feat2 &&
           advert.feat2 == vector.feat2 &&
           advert.has_name == (vector.name.empty() ? 0U : 1U) &&
           advert.name_len == vector.name.size() &&
           (vector.name.empty() ||
            std::memcmp(advert.name, vector.name.data(), vector.name.size()) == 0);
}

size_t encoded_path_bytes(uint8_t path_len)
{
    return static_cast<size_t>((path_len >> 6U) + 1U) * (path_len & 63U);
}

bool packets_equal(const d1l_meshcore_oracle_packet_t &left,
                   const d1l_meshcore_oracle_packet_t &right)
{
    const size_t left_path_bytes = encoded_path_bytes(left.path_len);
    return left.header == right.header &&
           left.transport_codes[0] == right.transport_codes[0] &&
           left.transport_codes[1] == right.transport_codes[1] &&
           left.path_len == right.path_len &&
           left_path_bytes == encoded_path_bytes(right.path_len) &&
           (left_path_bytes == 0U ||
            std::memcmp(left.path, right.path, left_path_bytes) == 0) &&
           left.payload_len == right.payload_len &&
           std::memcmp(left.payload, right.payload, left.payload_len) == 0;
}

d1l_meshcore_oracle_packet_t make_route_packet(uint8_t payload_type)
{
    d1l_meshcore_oracle_packet_t packet{};
    packet.header = static_cast<uint8_t>((payload_type << PH_TYPE_SHIFT) |
                                         ROUTE_TYPE_TRANSPORT_DIRECT);
    packet.transport_codes[0] = 0xAAAAU;
    packet.transport_codes[1] = 0x5555U;
    packet.path_len = 0x01U;
    packet.path[0] = 0xEEU;
    packet.payload_len = 4U;
    packet.payload[0] = 0x10U;
    packet.payload[1] = 0x20U;
    packet.payload[2] = 0x30U;
    packet.payload[3] = 0x40U;
    return packet;
}

}  // namespace

int main()
{
    std::vector<std::string> failures;
    if (d1l_meshcore_oracle_abi_version() != D1L_MESHCORE_ORACLE_ABI_VERSION) {
        failures.push_back("ABI version mismatch");
    }
    if (std::strcmp(d1l_meshcore_oracle_upstream_commit(),
                    D1L_MESHCORE_ORACLE_UPSTREAM_COMMIT) != 0) {
        failures.push_back("upstream commit mismatch");
    }

    const std::array<Vector, kPacketRoundtripVectors> vectors = {{
        {static_cast<uint8_t>((PAYLOAD_TYPE_GRP_TXT << PH_TYPE_SHIFT) |
                              ROUTE_TYPE_FLOOD),
         0U, 0U, 0x00U, {}, {0x11U, 0x22U, 0x33U}},
        {static_cast<uint8_t>((PAYLOAD_TYPE_ACK << PH_TYPE_SHIFT) |
                              ROUTE_TYPE_DIRECT),
         0U, 0U, 0x01U, {0xA1U}, {0xDEU, 0xADU, 0xBEU, 0xEFU}},
        {static_cast<uint8_t>((PAYLOAD_TYPE_PATH << PH_TYPE_SHIFT) |
                              ROUTE_TYPE_TRANSPORT_DIRECT),
         0x1234U, 0xABCDU, 0x42U,
         {0x10U, 0x11U, 0x20U, 0x21U},
         {0x01U, 0x02U, 0x03U, 0x04U, 0x05U}},
        {static_cast<uint8_t>((PAYLOAD_VER_4 << PH_VER_SHIFT) |
                              (PAYLOAD_TYPE_TRACE << PH_TYPE_SHIFT) |
                              ROUTE_TYPE_TRANSPORT_FLOOD),
         0x0102U, 0x0304U, 0x81U,
         {0x31U, 0x32U, 0x33U},
         {0xF0U, 0x0DU}},
    }};

    for (std::size_t index = 0; index < vectors.size(); ++index) {
        const Vector &vector = vectors[index];
        d1l_meshcore_oracle_packet_t input{};
        input.header = vector.header;
        input.transport_codes[0] = vector.transport_0;
        input.transport_codes[1] = vector.transport_1;
        input.path_len = vector.path_len;
        if (!vector.path.empty()) {
            std::memcpy(input.path, vector.path.data(), vector.path.size());
        }
        input.payload_len = static_cast<uint16_t>(vector.payload.size());
        std::memcpy(input.payload, vector.payload.data(), vector.payload.size());

        std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_RAW_BYTES> raw{};
        size_t raw_len = 0U;
        if (!d1l_meshcore_oracle_packet_encode(&input, raw.data(), raw.size(),
                                               &raw_len)) {
            failures.push_back("encode rejected roundtrip vector " +
                               std::to_string(index));
            continue;
        }
        d1l_meshcore_oracle_packet_t decoded{};
        if (!d1l_meshcore_oracle_packet_decode(raw.data(), raw_len, &decoded) ||
            !packet_matches(decoded, vector)) {
            failures.push_back("decode changed roundtrip vector " +
                               std::to_string(index));
        }
    }

    d1l_meshcore_oracle_packet_t sentinel;
    std::memset(&sentinel, 0xA5, sizeof(sentinel));
    d1l_meshcore_oracle_packet_t before = sentinel;
    const std::array<uint8_t, 2> short_raw = {0x09U, 0x00U};
    if (d1l_meshcore_oracle_packet_decode(short_raw.data(), short_raw.size(),
                                          &sentinel) ||
        std::memcmp(&sentinel, &before, sizeof(sentinel)) != 0) {
        failures.push_back("short decode did not fail without output mutation");
    }

    const std::array<uint8_t, 3> reserved_path = {0x09U, 0xC0U, 0x42U};
    if (d1l_meshcore_oracle_packet_decode(reserved_path.data(), reserved_path.size(),
                                          &sentinel) ||
        std::memcmp(&sentinel, &before, sizeof(sentinel)) != 0) {
        failures.push_back("reserved path did not fail without output mutation");
    }

    d1l_meshcore_oracle_packet_t invalid{};
    invalid.header = static_cast<uint8_t>((PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT) |
                                          ROUTE_TYPE_FLOOD);
    std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_RAW_BYTES> output;
    output.fill(0xD7U);
    const auto output_before = output;
    size_t output_len = 0xBEEFU;
    if (d1l_meshcore_oracle_packet_encode(&invalid, output.data(), output.size(),
                                          &output_len) ||
        output_len != 0xBEEFU || output != output_before) {
        failures.push_back("zero-payload encode changed output");
    }

    invalid.payload_len = 1U;
    invalid.payload[0] = 0x42U;
    if (d1l_meshcore_oracle_packet_encode(&invalid, output.data(), 2U,
                                          &output_len) ||
        output_len != 0xBEEFU || output != output_before) {
        failures.push_back("undersized encode changed output");
    }
    if (d1l_meshcore_oracle_packet_encode(&invalid, nullptr, output.size(),
                                          &output_len) ||
        output_len != 0xBEEFU) {
        failures.push_back("null-destination encode changed output length");
    }

    const std::array<AdvertVector, kAdvertRoundtripVectors> advert_vectors = {{
        {D1L_MESHCORE_ADVERT_TYPE_NONE, 0U, 0, 0, 0U, 0U, 0U, 0U, ""},
        {D1L_MESHCORE_ADVERT_TYPE_CHAT, 0U, 0, 0, 0U, 0U, 0U, 0U,
         "Alpha"},
        {D1L_MESHCORE_ADVERT_TYPE_REPEATER, 1U, 43653200, -79383200,
         0U, 0U, 0U, 0U, "Relay"},
        {D1L_MESHCORE_ADVERT_TYPE_SENSOR, 1U, 45678901, -123456789,
         1U, 0x1234U, 1U, 0xBEEFU, "sensor-west-0000001"},
    }};
    for (std::size_t index = 0; index < advert_vectors.size(); ++index) {
        const AdvertVector &vector = advert_vectors[index];
        const d1l_meshcore_oracle_advert_data_t input = make_advert(vector);
        const std::vector<uint8_t> expected = expected_advert_bytes(vector);
        std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_ADVERT_DATA_BYTES> raw{};
        size_t raw_len = 0U;
        if (!d1l_meshcore_oracle_advert_data_encode(
                &input, raw.data(), raw.size(), &raw_len) ||
            raw_len != expected.size() ||
            std::memcmp(raw.data(), expected.data(), expected.size()) != 0) {
            failures.push_back("advert encode changed vector " +
                               std::to_string(index));
            continue;
        }
        d1l_meshcore_oracle_advert_data_t decoded{};
        if (!d1l_meshcore_oracle_advert_data_decode(raw.data(), raw_len,
                                                    &decoded) ||
            !advert_matches(decoded, vector)) {
            failures.push_back("advert decode changed vector " +
                               std::to_string(index));
            continue;
        }
        std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_ADVERT_DATA_BYTES> reraw{};
        size_t reraw_len = 0U;
        if (!d1l_meshcore_oracle_advert_data_encode(
                &decoded, reraw.data(), reraw.size(), &reraw_len) ||
            reraw_len != raw_len ||
            std::memcmp(reraw.data(), raw.data(), raw_len) != 0) {
            failures.push_back("advert re-encode changed vector " +
                               std::to_string(index));
        }
    }

    auto expect_advert_decode_reject =
        [&failures](const char *name, const uint8_t *raw, size_t raw_len) {
            d1l_meshcore_oracle_advert_data_t output;
            std::memset(&output, 0xA6, sizeof(output));
            const d1l_meshcore_oracle_advert_data_t before = output;
            if (d1l_meshcore_oracle_advert_data_decode(raw, raw_len, &output) ||
                std::memcmp(&output, &before, sizeof(output)) != 0) {
                failures.push_back(std::string(name) +
                                   " advert decode changed output");
            }
        };
    const std::array<uint8_t, 1> truncated_lat = {
        D1L_MESHCORE_ADVERT_LATLON_MASK};
    const std::array<uint8_t, 2> truncated_feat1 = {
        D1L_MESHCORE_ADVERT_FEAT1_MASK, 0x01U};
    const std::array<uint8_t, 2> truncated_feat2 = {
        D1L_MESHCORE_ADVERT_FEAT2_MASK, 0x01U};
    const std::array<uint8_t, 2> trailing_without_name = {0x00U, 0x41U};
    const std::array<uint8_t, 1> empty_name = {D1L_MESHCORE_ADVERT_NAME_MASK};
    const std::array<uint8_t, 3> zero_feat1 = {
        D1L_MESHCORE_ADVERT_FEAT1_MASK, 0x00U, 0x00U};
    expect_advert_decode_reject("empty", nullptr, 0U);
    expect_advert_decode_reject("truncated location", truncated_lat.data(),
                                truncated_lat.size());
    expect_advert_decode_reject("truncated feature 1", truncated_feat1.data(),
                                truncated_feat1.size());
    expect_advert_decode_reject("truncated feature 2", truncated_feat2.data(),
                                truncated_feat2.size());
    expect_advert_decode_reject("unnamed trailing bytes",
                                trailing_without_name.data(),
                                trailing_without_name.size());
    expect_advert_decode_reject("empty flagged name", empty_name.data(),
                                empty_name.size());
    expect_advert_decode_reject("zero flagged feature", zero_feat1.data(),
                                zero_feat1.size());

    auto expect_advert_encode_reject =
        [&failures](const char *name,
                    const d1l_meshcore_oracle_advert_data_t &advert,
                    size_t capacity) {
            std::array<uint8_t,
                       D1L_MESHCORE_ORACLE_MAX_ADVERT_DATA_BYTES> output;
            output.fill(0xC7U);
            const auto before = output;
            size_t output_len = 0xCAFEU;
            if (d1l_meshcore_oracle_advert_data_encode(
                    &advert, output.data(), capacity, &output_len) ||
                output != before || output_len != 0xCAFEU) {
                failures.push_back(std::string(name) +
                                   " advert encode changed output");
            }
        };
    d1l_meshcore_oracle_advert_data_t invalid_advert =
        make_advert(advert_vectors[1]);
    invalid_advert.type = 0x10U;
    expect_advert_encode_reject("invalid type", invalid_advert,
                                D1L_MESHCORE_ORACLE_MAX_ADVERT_DATA_BYTES);
    invalid_advert = make_advert(advert_vectors[1]);
    invalid_advert.name_len = 2U;
    invalid_advert.name[1] = 0U;
    expect_advert_encode_reject("embedded null", invalid_advert,
                                D1L_MESHCORE_ORACLE_MAX_ADVERT_DATA_BYTES);
    invalid_advert = make_advert(advert_vectors[3]);
    invalid_advert.name_len = 20U;
    std::memset(invalid_advert.name, 'X', invalid_advert.name_len);
    expect_advert_encode_reject("oversized layout", invalid_advert,
                                D1L_MESHCORE_ORACLE_MAX_ADVERT_DATA_BYTES);
    invalid_advert = make_advert(advert_vectors[1]);
    expect_advert_encode_reject("undersized destination", invalid_advert, 5U);

    const std::array<SignedAdvertVector, kSignedAdvertProductionVectors>
        signed_advert_vectors = {{
            {{0x00U, 0x00U, 0x00U, 0x00U}, {},
              {0x60U, 0xB4U, 0x4AU, 0x2DU, 0xD6U, 0x91U, 0xC6U, 0xEFU,
               0xF0U, 0xE2U, 0x6DU, 0xF8U, 0x30U, 0xA8U, 0x90U, 0xE2U,
               0xB4U, 0x3DU, 0xCDU, 0x9FU, 0x75U, 0xE0U, 0xA0U, 0x7BU,
               0x51U, 0xECU, 0xE1U, 0x69U, 0x8EU, 0xB6U, 0x07U, 0x4DU,
               0xB0U, 0x49U, 0x1EU, 0x57U, 0xB5U, 0x1FU, 0xFEU, 0xD4U,
               0x24U, 0x2DU, 0x77U, 0x03U, 0x0FU, 0x40U, 0xE7U, 0xDFU,
               0x07U, 0x59U, 0x33U, 0xF0U, 0xA8U, 0x98U, 0x38U, 0x13U,
               0x17U, 0xC8U, 0x9CU, 0x56U, 0x99U, 0x0FU, 0xE3U, 0x00U}},
            {{0x78U, 0x56U, 0x34U, 0x12U},
             {0x81U, 'o', 'r', 'a', 'c', 'l', 'e'},
             {0x1FU, 0xB3U, 0x44U, 0x55U, 0x10U, 0xADU, 0x9DU, 0xAEU,
              0x26U, 0x30U, 0x64U, 0x27U, 0x43U, 0x47U, 0x78U, 0xEDU,
              0x55U, 0x45U, 0x99U, 0x06U, 0x83U, 0x46U, 0x15U, 0x52U,
              0x20U, 0x0AU, 0xE5U, 0xBDU, 0x09U, 0x9DU, 0xACU, 0x88U,
              0x6DU, 0x8DU, 0x61U, 0xB3U, 0x2CU, 0x45U, 0x7DU, 0x7CU,
              0xCBU, 0x2FU, 0x56U, 0x24U, 0x35U, 0x0BU, 0x81U, 0x4DU,
              0x47U, 0xEAU, 0xC6U, 0x06U, 0xAFU, 0x77U, 0xAAU, 0x97U,
              0x63U, 0xB9U, 0xEBU, 0x90U, 0x34U, 0x97U, 0x73U, 0x0AU}},
            {{0xEFU, 0xBEU, 0xADU, 0xDEU},
             {0x81U, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j',
              'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u',
              'v', 'w', 'x', 'y', 'z', 'A', 'B', 'C', 'D', 'E'},
             {0x34U, 0xD8U, 0xCDU, 0xE6U, 0x24U, 0x0DU, 0xD7U, 0xFEU,
              0x87U, 0xFBU, 0x71U, 0xBBU, 0x33U, 0x9FU, 0x6BU, 0x11U,
              0x70U, 0x6DU, 0x09U, 0xE0U, 0x83U, 0x5AU, 0x0FU, 0x71U,
              0x1CU, 0xFBU, 0xA2U, 0xBCU, 0x2EU, 0x3CU, 0xCFU, 0x17U,
              0xEFU, 0xA6U, 0x26U, 0xABU, 0x89U, 0x4AU, 0xD0U, 0xF9U,
              0x78U, 0x2FU, 0x19U, 0x71U, 0x28U, 0x07U, 0x3EU, 0xEDU,
              0x3BU, 0x6BU, 0x77U, 0xBBU, 0x75U, 0xADU, 0x1EU, 0xB6U,
              0x5BU, 0xC4U, 0xC0U, 0x67U, 0x59U, 0x1FU, 0xB1U, 0x04U}},
        }};
    std::array<uint8_t, D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES>
        regenerated_public_key{};
    std::array<uint8_t, 64U> regenerated_private_key{};
    ed25519_create_keypair(regenerated_public_key.data(),
                           regenerated_private_key.data(),
                           kSignedAdvertSeed.data());
    if (regenerated_public_key != kSignedAdvertPublicKey) {
        failures.push_back("fixed-seed signed advert public key changed");
    }
    for (std::size_t index = 0; index < signed_advert_vectors.size(); ++index) {
        const SignedAdvertVector &vector = signed_advert_vectors[index];
        const SignedAdvertMessage message =
            make_signed_advert_message(kSignedAdvertPublicKey, vector);
        std::array<uint8_t, D1L_MESHCORE_ORACLE_SIGNATURE_BYTES>
            regenerated_signature{};
        ed25519_sign(regenerated_signature.data(), message.bytes.data(),
                     message.length, regenerated_public_key.data(),
                     regenerated_private_key.data());
        if (regenerated_signature != vector.signature) {
            failures.push_back("fixed-seed signed advert signature changed vector " +
                               std::to_string(index));
        }
        const uint8_t *app_data =
            vector.app_data.empty() ? nullptr : vector.app_data.data();
        for (std::size_t repetition = 0; repetition < 64U; ++repetition) {
            if (!d1l_meshcore_oracle_verify_signed_advert(
                    kSignedAdvertPublicKey.data(), vector.timestamp.data(),
                    vector.signature.data(), app_data, vector.app_data.size())) {
                failures.push_back("signed advert verification changed vector " +
                                   std::to_string(index));
                break;
            }
        }

        const uint32_t timestamp_value =
            static_cast<uint32_t>(vector.timestamp[0]) |
            (static_cast<uint32_t>(vector.timestamp[1]) << 8U) |
            (static_cast<uint32_t>(vector.timestamp[2]) << 16U) |
            (static_cast<uint32_t>(vector.timestamp[3]) << 24U);
        d1l_meshcore_oracle_packet_t advert_packet{};
        if (!d1l_meshcore_oracle_create_signed_advert_packet(
                kSignedAdvertSeed.data(), timestamp_value, app_data,
                vector.app_data.size(), &advert_packet) ||
            advert_packet.header !=
                static_cast<uint8_t>(PAYLOAD_TYPE_ADVERT << PH_TYPE_SHIFT) ||
            advert_packet.path_len != 0U ||
            advert_packet.payload_len !=
                kSignedAdvertPublicKey.size() + vector.timestamp.size() +
                    vector.signature.size() + vector.app_data.size() ||
            std::memcmp(advert_packet.payload, kSignedAdvertPublicKey.data(),
                        kSignedAdvertPublicKey.size()) != 0 ||
            std::memcmp(&advert_packet.payload[kSignedAdvertPublicKey.size()],
                        vector.timestamp.data(), vector.timestamp.size()) != 0 ||
            std::memcmp(&advert_packet.payload[kSignedAdvertPublicKey.size() +
                                               vector.timestamp.size()],
                        vector.signature.data(), vector.signature.size()) != 0 ||
            (!vector.app_data.empty() &&
             std::memcmp(
                 &advert_packet.payload[kSignedAdvertPublicKey.size() +
                                        vector.timestamp.size() +
                                        vector.signature.size()],
                 vector.app_data.data(), vector.app_data.size()) != 0)) {
            failures.push_back("signed advert packet creation changed vector " +
                               std::to_string(index));
            continue;
        }

        constexpr std::array<uint16_t, 2U> transport_codes = {0x1357U,
                                                               0x2468U};
        uint8_t priority = 0U;
        const uint8_t use_transport = index == 2U ? 1U : 0U;
        if (!d1l_meshcore_oracle_prepare_flood(
                &advert_packet, static_cast<uint8_t>(index + 1U),
                use_transport, transport_codes.data(), &priority) ||
            priority != 3U) {
            failures.push_back("signed advert flood preparation changed vector " +
                               std::to_string(index));
            continue;
        }
        std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_RAW_BYTES> wire{};
        size_t wire_len = 0U;
        d1l_meshcore_oracle_packet_t decoded_packet{};
        if (!d1l_meshcore_oracle_packet_encode(
                &advert_packet, wire.data(), wire.size(), &wire_len) ||
            !d1l_meshcore_oracle_packet_decode(wire.data(), wire_len,
                                               &decoded_packet)) {
            failures.push_back("signed advert wire roundtrip failed vector " +
                               std::to_string(index));
            continue;
        }
        std::array<uint8_t, D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES>
            parsed_public_key{};
        uint32_t parsed_timestamp = 0U;
        std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_ADVERT_DATA_BYTES>
            parsed_app_data{};
        size_t parsed_app_data_len = 0U;
        if (!d1l_meshcore_oracle_parse_signed_advert_packet(
                &decoded_packet, parsed_public_key.data(), &parsed_timestamp,
                parsed_app_data.data(), parsed_app_data.size(),
                &parsed_app_data_len) ||
            parsed_public_key != kSignedAdvertPublicKey ||
            parsed_timestamp != timestamp_value ||
            parsed_app_data_len != vector.app_data.size() ||
            (!vector.app_data.empty() &&
             std::memcmp(parsed_app_data.data(), vector.app_data.data(),
                         vector.app_data.size()) != 0)) {
            failures.push_back("signed advert authenticated parse changed vector " +
                               std::to_string(index));
        }
    }

    const SignedAdvertVector &packet_vector = signed_advert_vectors[1];
    const uint32_t packet_timestamp =
        static_cast<uint32_t>(packet_vector.timestamp[0]) |
        (static_cast<uint32_t>(packet_vector.timestamp[1]) << 8U) |
        (static_cast<uint32_t>(packet_vector.timestamp[2]) << 16U) |
        (static_cast<uint32_t>(packet_vector.timestamp[3]) << 24U);
    d1l_meshcore_oracle_packet_t valid_advert_packet{};
    if (!d1l_meshcore_oracle_create_signed_advert_packet(
            kSignedAdvertSeed.data(), packet_timestamp,
            packet_vector.app_data.data(), packet_vector.app_data.size(),
            &valid_advert_packet)) {
        failures.push_back("signed advert invalid-vector fixture creation failed");
    }
    auto expect_signed_advert_create_reject =
        [&failures, &packet_vector, packet_timestamp](
            const char *name, const uint8_t *seed, const uint8_t *app_data,
            size_t app_data_len, bool null_output) {
            d1l_meshcore_oracle_packet_t output{};
            std::memset(&output, 0xA5, sizeof(output));
            const d1l_meshcore_oracle_packet_t before = output;
            if (d1l_meshcore_oracle_create_signed_advert_packet(
                    seed, packet_timestamp, app_data, app_data_len,
                    null_output ? nullptr : &output)) {
                failures.push_back(std::string(name) +
                                   " signed advert creation accepted");
            } else if (!null_output &&
                       std::memcmp(&output, &before, sizeof(output)) != 0) {
                failures.push_back(std::string(name) +
                                   " signed advert creation mutated output");
            }
        };
    expect_signed_advert_create_reject(
        "null seed", nullptr, packet_vector.app_data.data(),
        packet_vector.app_data.size(), false);
    expect_signed_advert_create_reject("null app data", kSignedAdvertSeed.data(),
                                       nullptr, 1U, false);
    expect_signed_advert_create_reject(
        "oversized app data", kSignedAdvertSeed.data(),
        packet_vector.app_data.data(),
        D1L_MESHCORE_ORACLE_MAX_ADVERT_DATA_BYTES + 1U, false);
    expect_signed_advert_create_reject(
        "null packet output", kSignedAdvertSeed.data(),
        packet_vector.app_data.data(), packet_vector.app_data.size(), true);

    auto expect_signed_advert_packet_reject =
        [&failures](const char *name,
                    const d1l_meshcore_oracle_packet_t *packet,
                    size_t app_data_capacity) {
            std::array<uint8_t, D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES>
                public_key{};
            public_key.fill(0xA5U);
            const auto public_key_before = public_key;
            uint32_t timestamp = 0xA5A5A5A5U;
            std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_ADVERT_DATA_BYTES>
                app_data{};
            app_data.fill(0xA5U);
            const auto app_data_before = app_data;
            size_t app_data_len = 0xA5A5U;
            if (d1l_meshcore_oracle_parse_signed_advert_packet(
                    packet, public_key.data(), &timestamp, app_data.data(),
                    app_data_capacity, &app_data_len)) {
                failures.push_back(std::string(name) +
                                   " signed advert packet accepted");
            } else if (public_key != public_key_before ||
                       timestamp != 0xA5A5A5A5U ||
                       app_data != app_data_before ||
                       app_data_len != 0xA5A5U) {
                failures.push_back(std::string(name) +
                                   " signed advert parse mutated output");
            }
        };
    expect_signed_advert_packet_reject(
        "null packet", nullptr, D1L_MESHCORE_ORACLE_MAX_ADVERT_DATA_BYTES);
    {
        std::array<uint8_t, D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES> public_key{};
        uint32_t timestamp = 0U;
        std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_ADVERT_DATA_BYTES> app_data{};
        size_t app_data_len = 0U;
        if (d1l_meshcore_oracle_parse_signed_advert_packet(
                &valid_advert_packet, nullptr, &timestamp, app_data.data(),
                app_data.size(), &app_data_len) ||
            d1l_meshcore_oracle_parse_signed_advert_packet(
                &valid_advert_packet, public_key.data(), nullptr,
                app_data.data(), app_data.size(), &app_data_len) ||
            d1l_meshcore_oracle_parse_signed_advert_packet(
                &valid_advert_packet, public_key.data(), &timestamp, nullptr,
                app_data.size(), &app_data_len) ||
            d1l_meshcore_oracle_parse_signed_advert_packet(
                &valid_advert_packet, public_key.data(), &timestamp,
                app_data.data(), app_data.size(), nullptr)) {
            failures.push_back("signed advert parser accepted null output");
        }
    }
    expect_signed_advert_packet_reject(
        "undersized app output", &valid_advert_packet,
        packet_vector.app_data.size() - 1U);
    auto wrong_advert_packet = valid_advert_packet;
    wrong_advert_packet.header =
        static_cast<uint8_t>(PAYLOAD_TYPE_GRP_TXT << PH_TYPE_SHIFT);
    expect_signed_advert_packet_reject(
        "wrong payload type", &wrong_advert_packet,
        D1L_MESHCORE_ORACLE_MAX_ADVERT_DATA_BYTES);
    wrong_advert_packet = valid_advert_packet;
    wrong_advert_packet.header |= static_cast<uint8_t>(PAYLOAD_VER_2
                                                       << PH_VER_SHIFT);
    expect_signed_advert_packet_reject(
        "future payload version", &wrong_advert_packet,
        D1L_MESHCORE_ORACLE_MAX_ADVERT_DATA_BYTES);
    wrong_advert_packet = valid_advert_packet;
    wrong_advert_packet.payload_len =
        D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES +
        D1L_MESHCORE_ORACLE_ADVERT_TIMESTAMP_BYTES +
        D1L_MESHCORE_ORACLE_SIGNATURE_BYTES - 1U;
    expect_signed_advert_packet_reject(
        "short payload", &wrong_advert_packet,
        D1L_MESHCORE_ORACLE_MAX_ADVERT_DATA_BYTES);
    wrong_advert_packet = valid_advert_packet;
    wrong_advert_packet.payload_len =
        D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES +
        D1L_MESHCORE_ORACLE_ADVERT_TIMESTAMP_BYTES +
        D1L_MESHCORE_ORACLE_SIGNATURE_BYTES +
        D1L_MESHCORE_ORACLE_MAX_ADVERT_DATA_BYTES + 1U;
    expect_signed_advert_packet_reject(
        "oversized payload", &wrong_advert_packet,
        D1L_MESHCORE_ORACLE_MAX_ADVERT_DATA_BYTES);
    for (const auto &mutation :
         std::array<std::pair<const char *, size_t>, 4U>{
             {{"tampered public key", 0U},
              {"tampered timestamp", D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES},
              {"tampered signature",
               D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES +
                   D1L_MESHCORE_ORACLE_ADVERT_TIMESTAMP_BYTES},
              {"tampered app data",
               D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES +
                   D1L_MESHCORE_ORACLE_ADVERT_TIMESTAMP_BYTES +
                   D1L_MESHCORE_ORACLE_SIGNATURE_BYTES}}}) {
        wrong_advert_packet = valid_advert_packet;
        wrong_advert_packet.payload[mutation.second] ^= 0x01U;
        expect_signed_advert_packet_reject(
            mutation.first, &wrong_advert_packet,
            D1L_MESHCORE_ORACLE_MAX_ADVERT_DATA_BYTES);
    }
    wrong_advert_packet = valid_advert_packet;
    std::memset(wrong_advert_packet.payload, 0,
                D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES);
    wrong_advert_packet.payload[0] = 0x01U;
    expect_signed_advert_packet_reject(
        "low-order public key", &wrong_advert_packet,
        D1L_MESHCORE_ORACLE_MAX_ADVERT_DATA_BYTES);
    wrong_advert_packet = valid_advert_packet;
    std::memset(&wrong_advert_packet.payload[
                    D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES +
                    D1L_MESHCORE_ORACLE_ADVERT_TIMESTAMP_BYTES],
                0, D1L_MESHCORE_ORACLE_SIGNATURE_BYTES);
    wrong_advert_packet.payload[D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES +
                                D1L_MESHCORE_ORACLE_ADVERT_TIMESTAMP_BYTES] =
        0x01U;
    expect_signed_advert_packet_reject(
        "low-order signature R", &wrong_advert_packet,
        D1L_MESHCORE_ORACLE_MAX_ADVERT_DATA_BYTES);
    wrong_advert_packet = valid_advert_packet;
    const auto noncanonical_signature =
        signature_plus_group_order(packet_vector.signature);
    std::memcpy(&wrong_advert_packet.payload[
                    D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES +
                    D1L_MESHCORE_ORACLE_ADVERT_TIMESTAMP_BYTES],
                noncanonical_signature.data(), noncanonical_signature.size());
    expect_signed_advert_packet_reject(
        "noncanonical signature S", &wrong_advert_packet,
        D1L_MESHCORE_ORACLE_MAX_ADVERT_DATA_BYTES);
    wrong_advert_packet = valid_advert_packet;
    wrong_advert_packet.path_len = 0xFFU;
    expect_signed_advert_packet_reject(
        "invalid path length", &wrong_advert_packet,
        D1L_MESHCORE_ORACLE_MAX_ADVERT_DATA_BYTES);
    wrong_advert_packet = valid_advert_packet;
    wrong_advert_packet.payload_len = 0U;
    expect_signed_advert_packet_reject(
        "empty payload", &wrong_advert_packet,
        D1L_MESHCORE_ORACLE_MAX_ADVERT_DATA_BYTES);
    const uint8_t empty_message = 0U;
    if (!d1l_ed25519_signature_s_is_canonical(
            kRfc8032EmptyMessageSignature.data())) {
        failures.push_back("canonical RFC 8032 signature rejected by S guard");
    }
    for (std::size_t repetition = 0; repetition < 64U; ++repetition) {
        if (ed25519_verify(kRfc8032EmptyMessageSignature.data(), &empty_message,
                          0U, kRfc8032PublicKey.data()) != 1) {
            failures.push_back("RFC 8032 empty-message vector rejected");
            break;
        }
    }
    auto tampered_rfc_signature = kRfc8032EmptyMessageSignature;
    tampered_rfc_signature[0] ^= 0x01U;
    if (ed25519_verify(tampered_rfc_signature.data(), &empty_message, 0U,
                       kRfc8032PublicKey.data()) != 0) {
        failures.push_back("tampered RFC 8032 signature accepted");
    }
    auto high_bit_rfc_signature = kRfc8032EmptyMessageSignature;
    high_bit_rfc_signature[63] |= 0xE0U;
    if (ed25519_verify(high_bit_rfc_signature.data(), &empty_message, 0U,
                       kRfc8032PublicKey.data()) != 0) {
        failures.push_back("high-bit RFC 8032 signature accepted");
    }
    if (ed25519_verify(kRfc8032EmptyMessageSignaturePlusOrder.data(),
                       &empty_message, 0U, kRfc8032PublicKey.data()) != 1 ||
        d1l_ed25519_signature_s_is_canonical(
            kRfc8032EmptyMessageSignaturePlusOrder.data())) {
        failures.push_back(
            "RFC 8032 S+L regression did not exercise the canonical-S guard");
    }

    auto expect_strict_point =
        [&failures](const char *name, const uint8_t *point, bool expected) {
            if (d1l_ed25519_encoded_point_is_strict(point) != expected) {
                failures.push_back(std::string(name) +
                                   " strict point validation mismatch");
            }
        };
    expect_strict_point("fixed-seed public key",
                        kSignedAdvertPublicKey.data(), true);
    expect_strict_point("RFC 8032 public key", kRfc8032PublicKey.data(), true);
    expect_strict_point("fixed-seed signature R",
                        signed_advert_vectors[1].signature.data(), true);
    expect_strict_point("RFC 8032 signature R",
                        kRfc8032EmptyMessageSignature.data(), true);

    std::array<uint8_t, D1L_ED25519_SCALAR_BYTES> identity_point{};
    identity_point[0] = 0x01U;
    auto negative_zero_identity = identity_point;
    negative_zero_identity[31] = 0x80U;
    std::array<uint8_t, D1L_ED25519_SCALAR_BYTES> zero_point{};
    auto signed_zero_point = zero_point;
    signed_zero_point[31] = 0x80U;
    std::array<uint8_t, D1L_ED25519_SCALAR_BYTES> minus_one_point{};
    minus_one_point.fill(0xFFU);
    minus_one_point[0] = 0xECU;
    minus_one_point[31] = 0x7FU;
    std::array<uint8_t, D1L_ED25519_SCALAR_BYTES> noncanonical_y{};
    noncanonical_y.fill(0xFFU);
    noncanonical_y[0] = 0xEDU;
    noncanonical_y[31] = 0x7FU;
    expect_strict_point("null point", nullptr, false);
    expect_strict_point("identity point", identity_point.data(), false);
    expect_strict_point("negative-zero identity point",
                        negative_zero_identity.data(), false);
    expect_strict_point("zero point", zero_point.data(), false);
    expect_strict_point("signed zero point", signed_zero_point.data(), false);
    expect_strict_point("minus-one point", minus_one_point.data(), false);
    expect_strict_point("noncanonical field encoding",
                        noncanonical_y.data(), false);

    auto expect_signed_advert_reject =
        [&failures](const char *name, const uint8_t *public_key,
                    const uint8_t *timestamp, const uint8_t *signature,
                    const uint8_t *app_data, size_t app_data_len) {
            if (d1l_meshcore_oracle_verify_signed_advert(
                    public_key, timestamp, signature, app_data, app_data_len)) {
                failures.push_back(std::string(name) +
                                   " signed advert accepted");
            }
        };
    const SignedAdvertVector &signed_advert = signed_advert_vectors[1];
    std::array<uint8_t, D1L_MESHCORE_ORACLE_SIGNATURE_BYTES>
        identity_forgery{};
    identity_forgery[0] = 0x01U;
    const SignedAdvertMessage identity_forgery_message =
        make_signed_advert_message(identity_point, signed_advert);
    if (ed25519_verify(identity_forgery.data(),
                       identity_forgery_message.bytes.data(),
                       identity_forgery_message.length,
                       identity_point.data()) != 1) {
        failures.push_back(
            "identity-point forgery no longer exercises the pinned verifier");
    }
    expect_signed_advert_reject(
        "identity-point forgery", identity_point.data(),
        signed_advert.timestamp.data(), identity_forgery.data(),
        signed_advert.app_data.data(), signed_advert.app_data.size());
    expect_signed_advert_reject(
        "null public key", nullptr, signed_advert.timestamp.data(),
        signed_advert.signature.data(), signed_advert.app_data.data(),
        signed_advert.app_data.size());
    expect_signed_advert_reject(
        "null timestamp", kSignedAdvertPublicKey.data(), nullptr,
        signed_advert.signature.data(), signed_advert.app_data.data(),
        signed_advert.app_data.size());
    expect_signed_advert_reject(
        "null signature", kSignedAdvertPublicKey.data(),
        signed_advert.timestamp.data(), nullptr, signed_advert.app_data.data(),
        signed_advert.app_data.size());
    expect_signed_advert_reject(
        "null app data", kSignedAdvertPublicKey.data(),
        signed_advert.timestamp.data(), signed_advert.signature.data(), nullptr,
        1U);
    expect_signed_advert_reject(
        "oversized app data", kSignedAdvertPublicKey.data(),
        signed_advert.timestamp.data(), signed_advert.signature.data(),
        signed_advert.app_data.data(),
        D1L_MESHCORE_ORACLE_MAX_ADVERT_DATA_BYTES + 1U);
    auto tampered_signature = signed_advert.signature;
    tampered_signature[0] ^= 0x01U;
    expect_signed_advert_reject(
        "tampered signature", kSignedAdvertPublicKey.data(),
        signed_advert.timestamp.data(), tampered_signature.data(),
        signed_advert.app_data.data(), signed_advert.app_data.size());
    auto tampered_public_key = kSignedAdvertPublicKey;
    tampered_public_key[0] ^= 0x01U;
    expect_signed_advert_reject(
        "tampered public key", tampered_public_key.data(),
        signed_advert.timestamp.data(), signed_advert.signature.data(),
        signed_advert.app_data.data(), signed_advert.app_data.size());
    auto tampered_timestamp = signed_advert.timestamp;
    tampered_timestamp[0] ^= 0x01U;
    expect_signed_advert_reject(
        "tampered timestamp", kSignedAdvertPublicKey.data(),
        tampered_timestamp.data(), signed_advert.signature.data(),
        signed_advert.app_data.data(), signed_advert.app_data.size());
    auto tampered_app_data = signed_advert.app_data;
    tampered_app_data[0] ^= 0x01U;
    expect_signed_advert_reject(
        "tampered app data", kSignedAdvertPublicKey.data(),
        signed_advert.timestamp.data(), signed_advert.signature.data(),
        tampered_app_data.data(), tampered_app_data.size());
    const auto malleable_signature =
        signature_plus_group_order(signed_advert.signature);
    const SignedAdvertMessage malleable_message =
        make_signed_advert_message(kSignedAdvertPublicKey, signed_advert);
    if (ed25519_verify(malleable_signature.data(), malleable_message.bytes.data(),
                       malleable_message.length,
                       kSignedAdvertPublicKey.data()) != 1) {
        failures.push_back(
            "production S+L regression no longer exercises the pinned verifier");
    }
    expect_signed_advert_reject(
        "noncanonical S+L signature", kSignedAdvertPublicKey.data(),
        signed_advert.timestamp.data(), malleable_signature.data(),
        signed_advert.app_data.data(), signed_advert.app_data.size());

    const std::array<uint8_t, 32U> expected_sha256_abc = {
        0xBAU, 0x78U, 0x16U, 0xBFU, 0x8FU, 0x01U, 0xCFU, 0xEAU,
        0x41U, 0x41U, 0x40U, 0xDEU, 0x5DU, 0xAEU, 0x22U, 0x23U,
        0xB0U, 0x03U, 0x61U, 0xA3U, 0x96U, 0x17U, 0x7AU, 0x9CU,
        0xB4U, 0x10U, 0xFFU, 0x61U, 0xF2U, 0x00U, 0x15U, 0xADU};
    std::array<uint8_t, 32U> sha256_abc{};
    SHA256 sha256;
    constexpr std::array<uint8_t, 3U> abc = {'a', 'b', 'c'};
    sha256.update(abc.data(), abc.size());
    sha256.finalize(sha256_abc.data(), sha256_abc.size());
    if (sha256_abc != expected_sha256_abc) {
        failures.push_back("FIPS SHA-256 abc KAT changed");
    }

    const std::array<uint8_t, 20U> hmac_key = {
        0x0BU, 0x0BU, 0x0BU, 0x0BU, 0x0BU, 0x0BU, 0x0BU,
        0x0BU, 0x0BU, 0x0BU, 0x0BU, 0x0BU, 0x0BU, 0x0BU,
        0x0BU, 0x0BU, 0x0BU, 0x0BU, 0x0BU, 0x0BU};
    constexpr std::array<uint8_t, 8U> hmac_data = {
        'H', 'i', ' ', 'T', 'h', 'e', 'r', 'e'};
    const std::array<uint8_t, 32U> expected_hmac = {
        0xB0U, 0x34U, 0x4CU, 0x61U, 0xD8U, 0xDBU, 0x38U, 0x53U,
        0x5CU, 0xA8U, 0xAFU, 0xCEU, 0xAFU, 0x0BU, 0xF1U, 0x2BU,
        0x88U, 0x1DU, 0xC2U, 0x00U, 0xC9U, 0x83U, 0x3DU, 0xA7U,
        0x26U, 0xE9U, 0x37U, 0x6CU, 0x2EU, 0x32U, 0xCFU, 0xF7U};
    std::array<uint8_t, 32U> hmac_result{};
    SHA256 hmac_sha256;
    hmac_sha256.resetHMAC(hmac_key.data(), hmac_key.size());
    hmac_sha256.update(hmac_data.data(), hmac_data.size());
    hmac_sha256.finalizeHMAC(hmac_key.data(), hmac_key.size(),
                             hmac_result.data(), hmac_result.size());
    if (hmac_result != expected_hmac) {
        failures.push_back("RFC 4231 HMAC-SHA-256 KAT changed");
    }

    const std::array<uint8_t, 16U> aes_key = {
        0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U,
        0x08U, 0x09U, 0x0AU, 0x0BU, 0x0CU, 0x0DU, 0x0EU, 0x0FU};
    const std::array<uint8_t, 16U> aes_plaintext = {
        0x00U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U,
        0x88U, 0x99U, 0xAAU, 0xBBU, 0xCCU, 0xDDU, 0xEEU, 0xFFU};
    const std::array<uint8_t, 16U> expected_aes_ciphertext = {
        0x69U, 0xC4U, 0xE0U, 0xD8U, 0x6AU, 0x7BU, 0x04U, 0x30U,
        0xD8U, 0xCDU, 0xB7U, 0x80U, 0x70U, 0xB4U, 0xC5U, 0x5AU};
    std::array<uint8_t, 16U> aes_ciphertext{};
    std::array<uint8_t, 16U> aes_roundtrip{};
    AES128 aes;
    aes.setKey(aes_key.data(), aes_key.size());
    aes.encryptBlock(aes_ciphertext.data(), aes_plaintext.data());
    aes.decryptBlock(aes_roundtrip.data(), aes_ciphertext.data());
    if (aes_ciphertext != expected_aes_ciphertext ||
        aes_roundtrip != aes_plaintext) {
        failures.push_back("FIPS-197 AES-128 KAT changed");
    }

    const std::array<uint8_t, D1L_MESHCORE_ORACLE_GROUP_SECRET_BYTES>
        public_secret = {
            0x8BU, 0x33U, 0x87U, 0xE9U, 0xC5U, 0xCDU, 0xEAU, 0x6AU,
            0xC9U, 0xE5U, 0xEDU, 0xBAU, 0xA1U, 0x15U, 0xCDU, 0x72U,
            0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
            0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U};
    std::array<uint8_t, D1L_MESHCORE_ORACLE_GROUP_SECRET_BYTES> full_secret{};
    for (std::size_t index = 0U; index < full_secret.size(); ++index) {
        full_secret[index] = static_cast<uint8_t>(index);
    }
    uint8_t public_hash = 0U;
    uint8_t full_hash = 0U;
    if (!d1l_meshcore_oracle_group_channel_hash(public_secret.data(),
                                                 &public_hash) ||
        public_hash != 0x11U ||
        !d1l_meshcore_oracle_group_channel_hash(full_secret.data(),
                                                 &full_hash) ||
        full_hash != 0x63U) {
        failures.push_back("BaseChatMesh group channel hash vectors changed");
    }

    const std::array<uint8_t, 6U> short_group_text = {
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 'a'};
    const std::array<uint8_t, 18U> group_text = {
        0x78U, 0x56U, 0x34U, 0x12U, 0x00U, 'o', 'r', 'a', 'c',
        'l',   'e',   ':',   ' ',   'h',   'e', 'l', 'l', 'o'};
    const std::array<uint8_t, 7U> group_data = {
        0xEFU, 0xBEU, 0x04U, 0xDEU, 0xADU, 0xBEU, 0xEFU};
    std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_GROUP_PLAINTEXT_BYTES>
        maximum_group_data{};
    maximum_group_data[0] = 0x12U;
    maximum_group_data[1] = 0x34U;
    maximum_group_data[2] = 165U;
    for (std::size_t index = 3U; index < maximum_group_data.size(); ++index) {
        maximum_group_data[index] = static_cast<uint8_t>(index - 3U);
    }
    const std::array<uint8_t, 19U> expected_short_group_payload = {
        0x11U, 0x9EU, 0x58U, 0xABU, 0xE5U, 0x62U, 0xD2U,
        0x9FU, 0x30U, 0xB8U, 0xCAU, 0x23U, 0x5EU, 0xDEU,
        0x0BU, 0xEAU, 0xF6U, 0xA1U, 0x57U};
    const std::array<uint8_t, 35U> expected_group_text_payload = {
        0x11U, 0xEFU, 0x1DU, 0x99U, 0x14U, 0x9EU, 0x82U,
        0xF4U, 0x7CU, 0x34U, 0x91U, 0x97U, 0x0AU, 0x25U,
        0xA5U, 0x37U, 0x3EU, 0xC5U, 0x1DU, 0x41U, 0xC9U,
        0x77U, 0x27U, 0x66U, 0x59U, 0xDDU, 0xB6U, 0xECU,
        0x63U, 0xA0U, 0x24U, 0x53U, 0xECU, 0xEEU, 0xB1U};
    const std::array<uint8_t, 19U> expected_group_data_payload = {
        0x63U, 0x44U, 0x2EU, 0xA7U, 0xEFU, 0xA0U, 0xB0U,
        0xFEU, 0x3FU, 0x9BU, 0x5AU, 0x57U, 0x74U, 0xDFU,
        0x21U, 0x19U, 0xC8U, 0x6BU, 0x7EU};
    const std::array<uint8_t, 32U> expected_max_group_payload_sha256 = {
        0x30U, 0xB6U, 0x8EU, 0xB0U, 0xFDU, 0x57U, 0xADU, 0x98U,
        0x29U, 0x35U, 0x73U, 0xB8U, 0xCCU, 0xCDU, 0x48U, 0x7BU,
        0xD9U, 0x3CU, 0xB3U, 0x2CU, 0xD8U, 0xE2U, 0x00U, 0xA6U,
        0x4EU, 0x72U, 0x8CU, 0x91U, 0x21U, 0xB5U, 0x3EU, 0xEAU};

    auto verify_group_roundtrip =
        [&failures](const char *name, uint8_t payload_type,
                    const uint8_t *secret, uint8_t channel_hash,
                    const uint8_t *plaintext, std::size_t plaintext_len,
                    const uint8_t *expected_payload,
                    std::size_t expected_payload_len,
                    const std::array<uint8_t, 32U> *expected_payload_sha) {
            d1l_meshcore_oracle_packet_t packet{};
            if (!d1l_meshcore_oracle_create_group_packet(
                    payload_type, channel_hash, secret, plaintext,
                    plaintext_len, &packet) ||
                packet.header !=
                    static_cast<uint8_t>(payload_type << PH_TYPE_SHIFT) ||
                packet.payload_len != expected_payload_len ||
                (expected_payload != nullptr &&
                 std::memcmp(packet.payload, expected_payload,
                             expected_payload_len) != 0)) {
                failures.push_back(std::string(name) +
                                   " group create vector changed");
                return;
            }
            if (expected_payload_sha != nullptr) {
                std::array<uint8_t, 32U> digest{};
                SHA256 payload_sha;
                payload_sha.update(packet.payload, packet.payload_len);
                payload_sha.finalize(digest.data(), digest.size());
                if (digest != *expected_payload_sha) {
                    failures.push_back(std::string(name) +
                                       " group payload digest changed");
                    return;
                }
            }
            uint8_t priority = 0U;
            const bool routed = payload_type == PAYLOAD_TYPE_GRP_TXT
                                    ? d1l_meshcore_oracle_prepare_flood(
                                          &packet, 1U, 0U, nullptr, &priority)
                                    : d1l_meshcore_oracle_prepare_direct(
                                          &packet, nullptr, 0U, &priority);
            std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_RAW_BYTES> raw{};
            size_t raw_len = 0U;
            d1l_meshcore_oracle_packet_t decoded{};
            if (!routed ||
                !d1l_meshcore_oracle_packet_encode(
                    &packet, raw.data(), raw.size(), &raw_len) ||
                !d1l_meshcore_oracle_packet_decode(raw.data(), raw_len,
                                                   &decoded)) {
                failures.push_back(std::string(name) +
                                   " group wire roundtrip failed");
                return;
            }
            std::array<uint8_t,
                       D1L_MESHCORE_ORACLE_MAX_GROUP_PLAINTEXT_BYTES +
                           D1L_MESHCORE_ORACLE_GROUP_BLOCK_BYTES>
                parsed{};
            size_t parsed_len = 0U;
            if (!d1l_meshcore_oracle_parse_group_packet(
                    &decoded, channel_hash, secret, parsed.data(),
                    parsed.size(), &parsed_len) ||
                parsed_len < plaintext_len ||
                std::memcmp(parsed.data(), plaintext, plaintext_len) != 0 ||
                std::any_of(parsed.begin() + plaintext_len,
                            parsed.begin() + parsed_len,
                            [](uint8_t value) { return value != 0U; })) {
                failures.push_back(std::string(name) +
                                   " group parse vector changed");
            }
        };
    verify_group_roundtrip(
        "short text", PAYLOAD_TYPE_GRP_TXT, public_secret.data(), public_hash,
        short_group_text.data(), short_group_text.size(),
        expected_short_group_payload.data(), expected_short_group_payload.size(),
        nullptr);
    verify_group_roundtrip(
        "two-block text", PAYLOAD_TYPE_GRP_TXT, public_secret.data(),
        public_hash, group_text.data(), group_text.size(),
        expected_group_text_payload.data(), expected_group_text_payload.size(),
        nullptr);
    verify_group_roundtrip(
        "group data", PAYLOAD_TYPE_GRP_DATA, full_secret.data(), full_hash,
        group_data.data(), group_data.size(), expected_group_data_payload.data(),
        expected_group_data_payload.size(), nullptr);
    verify_group_roundtrip(
        "maximum group data", PAYLOAD_TYPE_GRP_DATA, full_secret.data(),
        full_hash, maximum_group_data.data(), maximum_group_data.size(), nullptr,
        179U, &expected_max_group_payload_sha256);

    d1l_meshcore_oracle_packet_t valid_group{};
    if (!d1l_meshcore_oracle_create_group_packet(
            PAYLOAD_TYPE_GRP_TXT, public_hash, public_secret.data(),
            group_text.data(), group_text.size(), &valid_group)) {
        failures.push_back("group negative-vector fixture creation failed");
    }
    auto expect_group_create_reject =
        [&failures, &valid_group](const char *name, uint8_t payload_type,
                                  const uint8_t *secret,
                                  const uint8_t *plaintext,
                                  std::size_t plaintext_len,
                                  d1l_meshcore_oracle_packet_t *output) {
            d1l_meshcore_oracle_packet_t sentinel = valid_group;
            sentinel.header ^= 0x80U;
            if (output != nullptr) {
                *output = sentinel;
            }
            if (d1l_meshcore_oracle_create_group_packet(
                    payload_type, 0x11U, secret, plaintext, plaintext_len,
                    output) ||
                (output != nullptr && !packets_equal(*output, sentinel))) {
                failures.push_back(std::string(name) +
                                   " group create reject changed output");
            }
        };
    d1l_meshcore_oracle_packet_t rejected_group{};
    expect_group_create_reject(
        "unsupported type", PAYLOAD_TYPE_ACK, public_secret.data(),
        group_text.data(), group_text.size(), &rejected_group);
    expect_group_create_reject(
        "null secret", PAYLOAD_TYPE_GRP_TXT, nullptr, group_text.data(),
        group_text.size(), &rejected_group);
    expect_group_create_reject(
        "null plaintext", PAYLOAD_TYPE_GRP_TXT, public_secret.data(), nullptr,
        group_text.size(), &rejected_group);
    expect_group_create_reject(
        "empty plaintext", PAYLOAD_TYPE_GRP_TXT, public_secret.data(),
        group_text.data(), 0U, &rejected_group);
    expect_group_create_reject(
        "oversized plaintext", PAYLOAD_TYPE_GRP_TXT, public_secret.data(),
        maximum_group_data.data(),
        D1L_MESHCORE_ORACLE_MAX_GROUP_PLAINTEXT_BYTES + 1U,
        &rejected_group);
    expect_group_create_reject(
        "null output", PAYLOAD_TYPE_GRP_TXT, public_secret.data(),
        group_text.data(), group_text.size(), nullptr);
    if (d1l_meshcore_oracle_group_channel_hash(nullptr, &public_hash) ||
        d1l_meshcore_oracle_group_channel_hash(public_secret.data(), nullptr)) {
        failures.push_back("invalid group channel hash input accepted");
    }

    auto expect_group_parse_reject =
        [&failures, &public_secret, public_hash](
            const char *name, const d1l_meshcore_oracle_packet_t *packet,
            uint8_t hash, const uint8_t *secret, uint8_t *output,
            std::size_t capacity, size_t *output_len) {
            std::array<uint8_t, 192U> sentinel{};
            sentinel.fill(0xC7U);
            if (output != nullptr) {
                std::memcpy(output, sentinel.data(), capacity);
            }
            if (output_len != nullptr) {
                *output_len = 0xCAFEU;
            }
            if (d1l_meshcore_oracle_parse_group_packet(
                    packet, hash, secret, output, capacity, output_len) ||
                (output != nullptr &&
                 std::memcmp(output, sentinel.data(), capacity) != 0) ||
                (output_len != nullptr && *output_len != 0xCAFEU)) {
                failures.push_back(std::string(name) +
                                   " group parse reject changed output");
            }
            (void)public_secret;
            (void)public_hash;
        };
    std::array<uint8_t, 192U> rejected_plaintext{};
    size_t rejected_plaintext_len = 0U;
    expect_group_parse_reject(
        "null packet", nullptr, public_hash, public_secret.data(),
        rejected_plaintext.data(), rejected_plaintext.size(),
        &rejected_plaintext_len);
    expect_group_parse_reject(
        "null parse secret", &valid_group, public_hash, nullptr,
        rejected_plaintext.data(), rejected_plaintext.size(),
        &rejected_plaintext_len);
    expect_group_parse_reject(
        "null plaintext output", &valid_group, public_hash,
        public_secret.data(), nullptr, rejected_plaintext.size(),
        &rejected_plaintext_len);
    expect_group_parse_reject(
        "null plaintext length", &valid_group, public_hash,
        public_secret.data(), rejected_plaintext.data(),
        rejected_plaintext.size(), nullptr);
    d1l_meshcore_oracle_packet_t malformed_group = valid_group;
    malformed_group.header |= 0x40U;
    expect_group_parse_reject(
        "future payload version", &malformed_group, public_hash,
        public_secret.data(), rejected_plaintext.data(),
        rejected_plaintext.size(), &rejected_plaintext_len);
    malformed_group = valid_group;
    malformed_group.header = static_cast<uint8_t>(PAYLOAD_TYPE_ACK << PH_TYPE_SHIFT);
    expect_group_parse_reject(
        "non-group payload type", &malformed_group, public_hash,
        public_secret.data(), rejected_plaintext.data(),
        rejected_plaintext.size(), &rejected_plaintext_len);
    malformed_group = valid_group;
    malformed_group.payload_len = 18U;
    expect_group_parse_reject(
        "truncated encrypted payload", &malformed_group, public_hash,
        public_secret.data(), rejected_plaintext.data(),
        rejected_plaintext.size(), &rejected_plaintext_len);
    malformed_group = valid_group;
    malformed_group.payload_len -= 1U;
    expect_group_parse_reject(
        "non-block encrypted payload", &malformed_group, public_hash,
        public_secret.data(), rejected_plaintext.data(),
        rejected_plaintext.size(), &rejected_plaintext_len);
    expect_group_parse_reject(
        "wrong channel hash", &valid_group, static_cast<uint8_t>(public_hash ^ 1U),
        public_secret.data(), rejected_plaintext.data(),
        rejected_plaintext.size(), &rejected_plaintext_len);
    expect_group_parse_reject(
        "wrong channel secret", &valid_group, public_hash, full_secret.data(),
        rejected_plaintext.data(), rejected_plaintext.size(),
        &rejected_plaintext_len);
    malformed_group = valid_group;
    malformed_group.payload[1] ^= 0x01U;
    expect_group_parse_reject(
        "tampered group MAC", &malformed_group, public_hash,
        public_secret.data(), rejected_plaintext.data(),
        rejected_plaintext.size(), &rejected_plaintext_len);
    malformed_group = valid_group;
    malformed_group.payload[3] ^= 0x01U;
    expect_group_parse_reject(
        "tampered group ciphertext", &malformed_group, public_hash,
        public_secret.data(), rejected_plaintext.data(),
        rejected_plaintext.size(), &rejected_plaintext_len);
    expect_group_parse_reject(
        "undersized plaintext output", &valid_group, public_hash,
        public_secret.data(), rejected_plaintext.data(), 15U,
        &rejected_plaintext_len);

    struct LoginRequestVector {
        const char *name;
        uint32_t timestamp;
        uint8_t is_room;
        uint32_t sync_since;
        std::vector<uint8_t> password;
    };
    constexpr uint8_t login_destination_hash = 0xA1U;
    const std::array<LoginRequestVector, kLoginRequestRoundtripVectors>
        login_vectors = {{
            {"non-room empty", 0x00000000U, 0U, 0U, {}},
            {"non-room exact block", 0x12345678U, 0U, 0U,
             {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l'}},
            {"non-room maximum", 0xFFFFFFFFU, 0U, 0U,
             {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b',
              'c', 'd', 'e'}},
            {"room empty", 0x01020304U, 1U, 0x05060708U, {}},
            {"room exact block", 0x89ABCDEFU, 1U, 0x10203040U,
             {'r', 'o', 'o', 'm', 'p', 'a', 's', 's'}},
            {"room maximum", 0x0BADF00DU, 1U, 0x55667788U,
             {'g', 'u', 'e', 's', 't', '-', 'p', 'a', 's', 's', 'w', 'o',
              'r', 'd', '!'}},
        }};
    const std::array<uint8_t, 51U> expected_login_nonroom_exact_payload = {
        0xA1U, 0x03U, 0xA1U, 0x07U, 0xBFU, 0xF3U, 0xCEU, 0x10U, 0xBEU,
        0x1DU, 0x70U, 0xDDU, 0x18U, 0xE7U, 0x4BU, 0xC0U, 0x99U, 0x67U,
        0xE4U, 0xD6U, 0x30U, 0x9BU, 0xA5U, 0x0DU, 0x5FU, 0x1DU, 0xDCU,
        0x86U, 0x64U, 0x12U, 0x55U, 0x31U, 0xB8U, 0x4AU, 0x2EU, 0x26U,
        0x78U, 0x14U, 0xE2U, 0xBCU, 0x35U, 0x32U, 0x5AU, 0xC2U, 0xF1U,
        0x79U, 0xDAU, 0xCAU, 0x89U, 0x84U, 0xAFU};
    const std::array<uint8_t, 67U> expected_login_room_max_payload = {
        0xA1U, 0x03U, 0xA1U, 0x07U, 0xBFU, 0xF3U, 0xCEU, 0x10U, 0xBEU,
        0x1DU, 0x70U, 0xDDU, 0x18U, 0xE7U, 0x4BU, 0xC0U, 0x99U, 0x67U,
        0xE4U, 0xD6U, 0x30U, 0x9BU, 0xA5U, 0x0DU, 0x5FU, 0x1DU, 0xDCU,
        0x86U, 0x64U, 0x12U, 0x55U, 0x31U, 0xB8U, 0x65U, 0xC8U, 0x1FU,
        0xF0U, 0xD3U, 0xDDU, 0xCAU, 0x08U, 0xEBU, 0x2DU, 0xA2U, 0x4AU,
        0x30U, 0xBFU, 0x63U, 0xE5U, 0xFAU, 0x92U, 0xA7U, 0x3BU, 0x4DU,
        0x17U, 0x79U, 0x1AU, 0x8BU, 0x8BU, 0x21U, 0x71U, 0xE5U, 0x98U,
        0xB7U, 0x60U, 0xC0U, 0xECU};
    const std::array<uint8_t, 32U> expected_login_matrix_sha256 = {
        0xDEU, 0x98U, 0x16U, 0x70U, 0xDEU, 0xAEU, 0x0CU, 0x0BU,
        0x17U, 0xA3U, 0xB4U, 0x7BU, 0x72U, 0xBDU, 0xC9U, 0xBAU,
        0x45U, 0x5BU, 0x49U, 0xD6U, 0x97U, 0xA1U, 0x93U, 0x91U,
        0x7DU, 0x7DU, 0xD5U, 0xEAU, 0x2DU, 0x83U, 0x64U, 0x14U};
    constexpr std::array<uint8_t, 2U> login_direct_path = {0x21U, 0x43U};
    std::array<uint8_t, 1U> empty_login_password{};
    SHA256 login_matrix_sha;
    d1l_meshcore_oracle_packet_t valid_login{};
    for (std::size_t index = 0U; index < login_vectors.size(); ++index) {
        const LoginRequestVector &vector = login_vectors[index];
        const uint8_t *password = vector.password.empty()
            ? empty_login_password.data()
            : vector.password.data();
        d1l_meshcore_oracle_packet_t packet{};
        if (!d1l_meshcore_oracle_create_login_request_packet(
                login_destination_hash, kSignedAdvertPublicKey.data(),
                full_secret.data(), vector.timestamp, vector.is_room,
                vector.sync_since, password, vector.password.size(), &packet) ||
            packet.header !=
                static_cast<uint8_t>(PAYLOAD_TYPE_ANON_REQ << PH_TYPE_SHIFT) ||
            packet.path_len != 0U ||
            (packet.payload_len != 51U && packet.payload_len != 67U)) {
            failures.push_back(std::string(vector.name) +
                               " login request creation changed");
            continue;
        }
        if ((index == 1U &&
             (packet.payload_len != expected_login_nonroom_exact_payload.size() ||
              std::memcmp(packet.payload,
                          expected_login_nonroom_exact_payload.data(),
                          expected_login_nonroom_exact_payload.size()) != 0)) ||
            (index == 5U &&
             (packet.payload_len != expected_login_room_max_payload.size() ||
              std::memcmp(packet.payload, expected_login_room_max_payload.data(),
                          expected_login_room_max_payload.size()) != 0))) {
            failures.push_back(std::string(vector.name) +
                               " login request golden payload changed");
        }
        login_matrix_sha.update(packet.payload, packet.payload_len);
        if (index == 1U) {
            valid_login = packet;
        }

        d1l_meshcore_oracle_packet_t routed = packet;
        uint8_t priority = 0U;
        const bool route_ok = (index & 1U) == 0U
            ? d1l_meshcore_oracle_prepare_flood(
                  &routed, static_cast<uint8_t>((index % 3U) + 1U), 0U,
                  nullptr, &priority)
            : d1l_meshcore_oracle_prepare_direct(
                  &routed, login_direct_path.data(), 0x02U, &priority);
        std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_RAW_BYTES> wire{};
        size_t wire_len = 0U;
        d1l_meshcore_oracle_packet_t decoded{};
        const uint8_t expected_priority = (index & 1U) == 0U ? 1U : 0U;
        if (!route_ok || priority != expected_priority ||
            !d1l_meshcore_oracle_packet_encode(
                &routed, wire.data(), wire.size(), &wire_len) ||
            !d1l_meshcore_oracle_packet_decode(wire.data(), wire_len,
                                               &decoded)) {
            failures.push_back(std::string(vector.name) +
                               " login request wire roundtrip changed");
            continue;
        }
        uint32_t parsed_timestamp = 0U;
        uint32_t parsed_sync_since = 0U;
        std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_LOGIN_PASSWORD_BYTES>
            parsed_password{};
        size_t parsed_password_len = 0U;
        if (!d1l_meshcore_oracle_parse_login_request_packet(
                &decoded, login_destination_hash,
                kSignedAdvertPublicKey.data(), full_secret.data(),
                vector.is_room, &parsed_timestamp, &parsed_sync_since,
                parsed_password.data(), parsed_password.size(),
                &parsed_password_len) ||
            parsed_timestamp != vector.timestamp ||
            parsed_sync_since != vector.sync_since ||
            parsed_password_len != vector.password.size() ||
            (!vector.password.empty() &&
             std::memcmp(parsed_password.data(), vector.password.data(),
                         vector.password.size()) != 0)) {
            failures.push_back(std::string(vector.name) +
                               " login request authenticated parse changed");
        }
    }
    std::array<uint8_t, 32U> login_matrix_digest{};
    login_matrix_sha.finalize(login_matrix_digest.data(),
                              login_matrix_digest.size());
    if (login_matrix_digest != expected_login_matrix_sha256) {
        failures.push_back("anonymous login request matrix digest changed");
    }

    auto expect_login_create_reject =
        [&failures, &full_secret, &empty_login_password](
            const char *name, const uint8_t *sender_public_key,
            const uint8_t *secret, uint8_t is_room, uint32_t sync_since,
            const uint8_t *password, size_t password_len, bool null_output) {
            d1l_meshcore_oracle_packet_t output{};
            std::memset(&output, 0xA5, sizeof(output));
            const d1l_meshcore_oracle_packet_t before = output;
            if (d1l_meshcore_oracle_create_login_request_packet(
                    login_destination_hash, sender_public_key, secret,
                    0x12345678U, is_room, sync_since, password, password_len,
                    null_output ? nullptr : &output)) {
                failures.push_back(std::string(name) +
                                   " login request creation accepted");
            } else if (!null_output &&
                       std::memcmp(&output, &before, sizeof(output)) != 0) {
                failures.push_back(std::string(name) +
                                   " login request creation mutated output");
            }
            (void)full_secret;
            (void)empty_login_password;
        };
    std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_LOGIN_PASSWORD_BYTES + 1U>
        oversized_login_password{};
    oversized_login_password.fill('x');
    constexpr std::array<uint8_t, 3U> embedded_nul_password = {'a', 0U, 'b'};
    expect_login_create_reject(
        "null login sender", nullptr, full_secret.data(), 0U, 0U,
        empty_login_password.data(), 0U, false);
    expect_login_create_reject(
        "null login secret", kSignedAdvertPublicKey.data(), nullptr, 0U, 0U,
        empty_login_password.data(), 0U, false);
    expect_login_create_reject(
        "null login password", kSignedAdvertPublicKey.data(),
        full_secret.data(), 0U, 0U, nullptr, 0U, false);
    expect_login_create_reject(
        "embedded NUL login password", kSignedAdvertPublicKey.data(),
        full_secret.data(), 0U, 0U, embedded_nul_password.data(),
        embedded_nul_password.size(), false);
    expect_login_create_reject(
        "oversized login password", kSignedAdvertPublicKey.data(),
        full_secret.data(), 0U, 0U, oversized_login_password.data(),
        oversized_login_password.size(), false);
    expect_login_create_reject(
        "invalid login room flag", kSignedAdvertPublicKey.data(),
        full_secret.data(), 2U, 0U, empty_login_password.data(), 0U, false);
    expect_login_create_reject(
        "non-room login sync", kSignedAdvertPublicKey.data(),
        full_secret.data(), 0U, 1U, empty_login_password.data(), 0U, false);
    expect_login_create_reject(
        "null login output", kSignedAdvertPublicKey.data(), full_secret.data(),
        0U, 0U, empty_login_password.data(), 0U, true);

    auto expect_login_parse_reject =
        [&failures](const char *name,
                    const d1l_meshcore_oracle_packet_t *packet,
                    uint8_t destination_hash, const uint8_t *sender_public_key,
                    const uint8_t *secret, uint8_t is_room,
                    uint32_t *timestamp, uint32_t *sync_since,
                    uint8_t *password, size_t password_capacity,
                    size_t *password_len) {
            std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_LOGIN_PASSWORD_BYTES>
                password_sentinel{};
            password_sentinel.fill(0xD7U);
            if (timestamp != nullptr) {
                *timestamp = 0xAAAAAAAAU;
            }
            if (sync_since != nullptr) {
                *sync_since = 0xBBBBBBBBU;
            }
            if (password != nullptr) {
                std::memcpy(password, password_sentinel.data(),
                            password_sentinel.size());
            }
            if (password_len != nullptr) {
                *password_len = 0xBEEFU;
            }
            if (d1l_meshcore_oracle_parse_login_request_packet(
                    packet, destination_hash, sender_public_key, secret, is_room,
                    timestamp, sync_since, password, password_capacity,
                    password_len) ||
                (timestamp != nullptr && *timestamp != 0xAAAAAAAAU) ||
                (sync_since != nullptr && *sync_since != 0xBBBBBBBBU) ||
                (password != nullptr &&
                 std::memcmp(password, password_sentinel.data(),
                             password_sentinel.size()) != 0) ||
                (password_len != nullptr && *password_len != 0xBEEFU)) {
                failures.push_back(std::string(name) +
                                   " login request parse changed output");
            }
        };
    uint32_t rejected_login_timestamp = 0U;
    uint32_t rejected_login_sync_since = 0U;
    std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_LOGIN_PASSWORD_BYTES>
        rejected_login_password{};
    size_t rejected_login_password_len = 0U;
    expect_login_parse_reject(
        "null login packet", nullptr, login_destination_hash,
        kSignedAdvertPublicKey.data(), full_secret.data(), 0U,
        &rejected_login_timestamp, &rejected_login_sync_since,
        rejected_login_password.data(), rejected_login_password.size(),
        &rejected_login_password_len);
    expect_login_parse_reject(
        "null expected login sender", &valid_login, login_destination_hash,
        nullptr, full_secret.data(), 0U, &rejected_login_timestamp,
        &rejected_login_sync_since, rejected_login_password.data(),
        rejected_login_password.size(), &rejected_login_password_len);
    expect_login_parse_reject(
        "null login parse secret", &valid_login, login_destination_hash,
        kSignedAdvertPublicKey.data(), nullptr, 0U, &rejected_login_timestamp,
        &rejected_login_sync_since, rejected_login_password.data(),
        rejected_login_password.size(), &rejected_login_password_len);
    expect_login_parse_reject(
        "invalid login parse room flag", &valid_login, login_destination_hash,
        kSignedAdvertPublicKey.data(), full_secret.data(), 2U,
        &rejected_login_timestamp, &rejected_login_sync_since,
        rejected_login_password.data(), rejected_login_password.size(),
        &rejected_login_password_len);
    expect_login_parse_reject(
        "null login timestamp output", &valid_login, login_destination_hash,
        kSignedAdvertPublicKey.data(), full_secret.data(), 0U, nullptr,
        &rejected_login_sync_since, rejected_login_password.data(),
        rejected_login_password.size(), &rejected_login_password_len);
    expect_login_parse_reject(
        "null login sync output", &valid_login, login_destination_hash,
        kSignedAdvertPublicKey.data(), full_secret.data(), 0U,
        &rejected_login_timestamp, nullptr, rejected_login_password.data(),
        rejected_login_password.size(), &rejected_login_password_len);
    expect_login_parse_reject(
        "null login password output", &valid_login, login_destination_hash,
        kSignedAdvertPublicKey.data(), full_secret.data(), 0U,
        &rejected_login_timestamp, &rejected_login_sync_since, nullptr,
        rejected_login_password.size(), &rejected_login_password_len);
    expect_login_parse_reject(
        "null login password length", &valid_login, login_destination_hash,
        kSignedAdvertPublicKey.data(), full_secret.data(), 0U,
        &rejected_login_timestamp, &rejected_login_sync_since,
        rejected_login_password.data(), rejected_login_password.size(), nullptr);
    expect_login_parse_reject(
        "undersized login password output", &valid_login,
        login_destination_hash, kSignedAdvertPublicKey.data(),
        full_secret.data(), 0U, &rejected_login_timestamp,
        &rejected_login_sync_since, rejected_login_password.data(), 11U,
        &rejected_login_password_len);

    auto make_raw_login_packet =
        [&full_secret](const uint8_t *plaintext, size_t plaintext_len) {
            d1l_meshcore_oracle_packet_t packet{};
            packet.header =
                static_cast<uint8_t>(PAYLOAD_TYPE_ANON_REQ << PH_TYPE_SHIFT);
            packet.payload[0] = login_destination_hash;
            std::memcpy(&packet.payload[1], kSignedAdvertPublicKey.data(),
                        kSignedAdvertPublicKey.size());
            constexpr size_t outer_len =
                1U + D1L_MESHCORE_ORACLE_PUBLIC_KEY_BYTES;
            const int encrypted_len = mesh::Utils::encryptThenMAC(
                full_secret.data(), &packet.payload[outer_len], plaintext,
                static_cast<int>(plaintext_len));
            packet.payload_len = static_cast<uint16_t>(
                outer_len + static_cast<size_t>(encrypted_len));
            return packet;
        };
    auto expect_standard_login_parse_reject =
        [&](const char *name, const d1l_meshcore_oracle_packet_t *packet,
            uint8_t is_room = 0U) {
            expect_login_parse_reject(
                name, packet, login_destination_hash,
                kSignedAdvertPublicKey.data(), full_secret.data(), is_room,
                &rejected_login_timestamp, &rejected_login_sync_since,
                rejected_login_password.data(), rejected_login_password.size(),
                &rejected_login_password_len);
        };
    d1l_meshcore_oracle_packet_t malformed_login = valid_login;
    malformed_login.header =
        static_cast<uint8_t>(PAYLOAD_TYPE_REQ << PH_TYPE_SHIFT);
    expect_standard_login_parse_reject("wrong login payload type",
                                       &malformed_login);
    malformed_login = valid_login;
    malformed_login.header |=
        static_cast<uint8_t>(PAYLOAD_VER_2 << PH_VER_SHIFT);
    expect_standard_login_parse_reject("future login payload version",
                                       &malformed_login);
    malformed_login = valid_login;
    malformed_login.payload_len = 50U;
    expect_standard_login_parse_reject("short login payload",
                                       &malformed_login);
    malformed_login = valid_login;
    malformed_login.payload[malformed_login.payload_len++] = 0U;
    expect_standard_login_parse_reject("non-block login ciphertext",
                                       &malformed_login);
    std::array<uint8_t, 33U> three_block_login_plaintext{};
    malformed_login = make_raw_login_packet(three_block_login_plaintext.data(),
                                             three_block_login_plaintext.size());
    expect_standard_login_parse_reject("three-block login ciphertext",
                                       &malformed_login);
    expect_login_parse_reject(
        "wrong expected login destination", &valid_login,
        static_cast<uint8_t>(login_destination_hash ^ 1U),
        kSignedAdvertPublicKey.data(), full_secret.data(), 0U,
        &rejected_login_timestamp, &rejected_login_sync_since,
        rejected_login_password.data(), rejected_login_password.size(),
        &rejected_login_password_len);
    auto wrong_login_sender = kSignedAdvertPublicKey;
    wrong_login_sender[0] ^= 0x01U;
    expect_login_parse_reject(
        "wrong expected login sender", &valid_login, login_destination_hash,
        wrong_login_sender.data(), full_secret.data(), 0U,
        &rejected_login_timestamp, &rejected_login_sync_since,
        rejected_login_password.data(), rejected_login_password.size(),
        &rejected_login_password_len);
    expect_login_parse_reject(
        "wrong login secret", &valid_login, login_destination_hash,
        kSignedAdvertPublicKey.data(), public_secret.data(), 0U,
        &rejected_login_timestamp, &rejected_login_sync_since,
        rejected_login_password.data(), rejected_login_password.size(),
        &rejected_login_password_len);
    malformed_login = valid_login;
    malformed_login.payload[0] ^= 0x01U;
    expect_standard_login_parse_reject("tampered login destination",
                                       &malformed_login);
    malformed_login = valid_login;
    malformed_login.payload[1] ^= 0x01U;
    expect_standard_login_parse_reject("tampered outer login sender",
                                       &malformed_login);
    malformed_login = valid_login;
    malformed_login.payload[33] ^= 0x01U;
    expect_standard_login_parse_reject("tampered login MAC",
                                       &malformed_login);
    malformed_login = valid_login;
    malformed_login.payload[35] ^= 0x01U;
    expect_standard_login_parse_reject("tampered login ciphertext",
                                       &malformed_login);
    malformed_login = valid_login;
    malformed_login.path_len = 0xFFU;
    expect_standard_login_parse_reject("invalid login path length",
                                       &malformed_login);
    malformed_login = valid_login;
    malformed_login.payload_len = 0U;
    expect_standard_login_parse_reject("empty login payload",
                                       &malformed_login);
    std::array<uint8_t, 32U> redundant_login_plaintext{};
    redundant_login_plaintext[0] = 0x78U;
    redundant_login_plaintext[1] = 0x56U;
    redundant_login_plaintext[2] = 0x34U;
    redundant_login_plaintext[3] = 0x12U;
    malformed_login = make_raw_login_packet(redundant_login_plaintext.data(),
                                             redundant_login_plaintext.size());
    expect_standard_login_parse_reject("redundant login zero block",
                                       &malformed_login);
    std::array<uint8_t, 16U> noncanonical_login_plaintext{};
    noncanonical_login_plaintext[4] = 'a';
    noncanonical_login_plaintext[6] = 'b';
    malformed_login = make_raw_login_packet(
        noncanonical_login_plaintext.data(),
        noncanonical_login_plaintext.size());
    expect_standard_login_parse_reject("nonzero login padding",
                                       &malformed_login);
    std::array<uint8_t, 20U> overlong_login_plaintext{};
    overlong_login_plaintext.fill('x');
    overlong_login_plaintext[0] = 0x78U;
    overlong_login_plaintext[1] = 0x56U;
    overlong_login_plaintext[2] = 0x34U;
    overlong_login_plaintext[3] = 0x12U;
    malformed_login = make_raw_login_packet(overlong_login_plaintext.data(),
                                             overlong_login_plaintext.size());
    expect_standard_login_parse_reject("overlong authenticated login password",
                                       &malformed_login);
    std::array<uint8_t, 32U> redundant_room_login_plaintext{};
    redundant_room_login_plaintext[0] = 0x04U;
    redundant_room_login_plaintext[1] = 0x03U;
    redundant_room_login_plaintext[2] = 0x02U;
    redundant_room_login_plaintext[3] = 0x01U;
    redundant_room_login_plaintext[4] = 0x08U;
    redundant_room_login_plaintext[5] = 0x07U;
    redundant_room_login_plaintext[6] = 0x06U;
    redundant_room_login_plaintext[7] = 0x05U;
    malformed_login = make_raw_login_packet(
        redundant_room_login_plaintext.data(),
        redundant_room_login_plaintext.size());
    expect_standard_login_parse_reject("redundant room login zero block",
                                       &malformed_login, 1U);

    struct RequestResponseVector {
        const char *name;
        uint8_t payload_type;
        std::vector<uint8_t> plaintext;
    };
    std::vector<uint8_t> maximum_response_plaintext(
        D1L_MESHCORE_ORACLE_MAX_REQUEST_RESPONSE_PLAINTEXT_BYTES);
    for (std::size_t index = 0U; index < maximum_response_plaintext.size();
         ++index) {
        maximum_response_plaintext[index] =
            static_cast<uint8_t>(index * 7U + 3U);
    }
    const std::array<RequestResponseVector, kRequestResponseRoundtripVectors>
        request_response_vectors = {{
            {"one-byte request", PAYLOAD_TYPE_REQ, {0x00U}},
            {"15-byte request", PAYLOAD_TYPE_REQ,
             {0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U, 0x08U,
              0x09U, 0x0AU, 0x0BU, 0x0CU, 0x0DU, 0x0EU, 0x0FU}},
            {"exact-block request", PAYLOAD_TYPE_REQ,
             {0x10U, 0x11U, 0x12U, 0x13U, 0x14U, 0x15U, 0x16U, 0x17U,
              0x18U, 0x19U, 0x1AU, 0x1BU, 0x1CU, 0x1DU, 0x1EU, 0x1FU}},
            {"two-block request", PAYLOAD_TYPE_REQ,
             {0x20U, 0x21U, 0x22U, 0x23U, 0x24U, 0x25U, 0x26U, 0x27U,
              0x28U, 0x29U, 0x2AU, 0x2BU, 0x2CU, 0x2DU, 0x2EU, 0x2FU,
              0x30U}},
            {"exact-block response", PAYLOAD_TYPE_RESPONSE,
             {0x80U, 0x81U, 0x82U, 0x83U, 0x84U, 0x85U, 0x86U, 0x87U,
              0x88U, 0x89U, 0x8AU, 0x8BU, 0x8CU, 0x8DU, 0x8EU, 0x8FU}},
            {"maximum response", PAYLOAD_TYPE_RESPONSE,
             maximum_response_plaintext},
        }};
    const std::array<uint8_t, 36U> expected_two_block_request_payload = {
        0xA1U, 0xB2U, 0xCAU, 0xFCU, 0x5BU, 0xE8U, 0x7EU, 0x2EU, 0x5BU,
        0x44U, 0x7CU, 0x94U, 0x4BU, 0x21U, 0xC9U, 0xAFU, 0x77U, 0x56U,
        0xC0U, 0xD8U, 0x01U, 0x7AU, 0x8BU, 0xD9U, 0xECU, 0xD1U, 0x02U,
        0xBAU, 0x4BU, 0xB7U, 0x94U, 0x6DU, 0x3DU, 0x87U, 0x07U, 0xE0U};
    const std::array<uint8_t, 20U> expected_exact_block_response_payload = {
        0xA1U, 0xB2U, 0xABU, 0xB6U, 0xACU, 0x26U, 0x59U, 0x1CU, 0x0FU,
        0x8BU, 0xD8U, 0x0EU, 0xE7U, 0xC7U, 0xE3U, 0xA2U, 0xD1U, 0x4EU,
        0x2BU, 0x22U};
    const std::array<uint8_t, 32U> expected_maximum_response_sha256 = {
        0xE0U, 0xE6U, 0x17U, 0xB0U, 0xC2U, 0x60U, 0x1CU, 0x15U,
        0xAFU, 0x7BU, 0x56U, 0x05U, 0x6DU, 0x14U, 0x7CU, 0x47U,
        0xCBU, 0x4FU, 0xD6U, 0x86U, 0xE1U, 0x93U, 0x8AU, 0x37U,
        0x5AU, 0x8BU, 0x6AU, 0xC8U, 0x1EU, 0x17U, 0x47U, 0xA5U};
    const std::array<uint8_t, 32U> expected_request_response_matrix_sha256 = {
        0x77U, 0x22U, 0x70U, 0x76U, 0x61U, 0x3AU, 0xD0U, 0xFFU,
        0xEAU, 0x7BU, 0x58U, 0xABU, 0x90U, 0x3FU, 0x06U, 0xD8U,
        0x16U, 0xA0U, 0x5AU, 0x37U, 0xF1U, 0xE5U, 0x0EU, 0x9BU,
        0x85U, 0x98U, 0x22U, 0x2CU, 0xBEU, 0xF5U, 0xC2U, 0x9CU};
    constexpr uint8_t request_response_destination_hash = 0xA1U;
    constexpr uint8_t request_response_source_hash = 0xB2U;
    constexpr std::array<uint8_t, 3U> request_response_direct_path = {
        0x12U, 0x34U, 0x56U};
    SHA256 request_response_matrix_sha;
    d1l_meshcore_oracle_packet_t valid_request_response{};
    for (std::size_t index = 0U; index < request_response_vectors.size();
         ++index) {
        const RequestResponseVector &vector = request_response_vectors[index];
        d1l_meshcore_oracle_packet_t packet{};
        const std::size_t expected_ciphertext_len =
            ((vector.plaintext.size() + CIPHER_BLOCK_SIZE - 1U) /
             CIPHER_BLOCK_SIZE) *
            CIPHER_BLOCK_SIZE;
        if (!d1l_meshcore_oracle_create_request_response_packet(
                vector.payload_type, request_response_destination_hash,
                request_response_source_hash, full_secret.data(),
                vector.plaintext.data(), vector.plaintext.size(), &packet) ||
            packet.header !=
                static_cast<uint8_t>(vector.payload_type << PH_TYPE_SHIFT) ||
            packet.path_len != 0U ||
            packet.payload_len != 2U + CIPHER_MAC_SIZE + expected_ciphertext_len) {
            failures.push_back(std::string(vector.name) +
                               " request/response creation changed");
            continue;
        }
        if ((index == 3U &&
             (packet.payload_len != expected_two_block_request_payload.size() ||
              std::memcmp(packet.payload,
                          expected_two_block_request_payload.data(),
                          expected_two_block_request_payload.size()) != 0)) ||
            (index == 4U &&
             (packet.payload_len !=
                  expected_exact_block_response_payload.size() ||
              std::memcmp(packet.payload,
                          expected_exact_block_response_payload.data(),
                          expected_exact_block_response_payload.size()) != 0))) {
            failures.push_back(std::string(vector.name) +
                               " request/response golden payload changed");
        }
        if (index == 5U) {
            std::array<uint8_t, 32U> maximum_response_digest{};
            mesh::Utils::sha256(maximum_response_digest.data(),
                                maximum_response_digest.size(), packet.payload,
                                packet.payload_len);
            if (maximum_response_digest != expected_maximum_response_sha256) {
                failures.push_back("maximum response payload digest changed");
            }
        }
        request_response_matrix_sha.update(packet.payload, packet.payload_len);
        if (index == 3U) {
            valid_request_response = packet;
        }

        d1l_meshcore_oracle_packet_t routed = packet;
        uint8_t priority = 0U;
        const bool route_ok = (index & 1U) == 0U
            ? d1l_meshcore_oracle_prepare_flood(
                  &routed, static_cast<uint8_t>((index % 3U) + 1U), 0U,
                  nullptr, &priority)
            : d1l_meshcore_oracle_prepare_direct(
                  &routed, request_response_direct_path.data(), 0x03U,
                  &priority);
        std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_RAW_BYTES> wire{};
        size_t wire_len = 0U;
        d1l_meshcore_oracle_packet_t decoded{};
        const uint8_t expected_priority = (index & 1U) == 0U ? 1U : 0U;
        std::array<uint8_t,
                   D1L_MESHCORE_ORACLE_MAX_REQUEST_RESPONSE_PLAINTEXT_BYTES>
            parsed_plaintext{};
        if (!route_ok || priority != expected_priority ||
            !d1l_meshcore_oracle_packet_encode(
                &routed, wire.data(), wire.size(), &wire_len) ||
            !d1l_meshcore_oracle_packet_decode(wire.data(), wire_len,
                                               &decoded) ||
            !d1l_meshcore_oracle_parse_request_response_packet(
                &decoded, vector.payload_type,
                request_response_destination_hash,
                request_response_source_hash, full_secret.data(),
                vector.plaintext.size(), parsed_plaintext.data(),
                parsed_plaintext.size()) ||
            std::memcmp(parsed_plaintext.data(), vector.plaintext.data(),
                        vector.plaintext.size()) != 0) {
            failures.push_back(std::string(vector.name) +
                               " request/response authenticated wire roundtrip changed");
        }
    }
    std::array<uint8_t, 32U> request_response_matrix_digest{};
    request_response_matrix_sha.finalize(request_response_matrix_digest.data(),
                                         request_response_matrix_digest.size());
    if (request_response_matrix_digest !=
        expected_request_response_matrix_sha256) {
        failures.push_back("request/response packet matrix digest changed");
    }

    auto expect_request_response_create_reject =
        [&failures, &full_secret](const char *name, uint8_t payload_type,
                                  const uint8_t *secret,
                                  const uint8_t *plaintext,
                                  std::size_t plaintext_len,
                                  bool null_output) {
            d1l_meshcore_oracle_packet_t output{};
            std::memset(&output, 0xA5, sizeof(output));
            const d1l_meshcore_oracle_packet_t before = output;
            if (d1l_meshcore_oracle_create_request_response_packet(
                    payload_type, request_response_destination_hash,
                    request_response_source_hash, secret, plaintext,
                    plaintext_len, null_output ? nullptr : &output)) {
                failures.push_back(std::string(name) +
                                   " request/response creation accepted");
            } else if (!null_output &&
                       std::memcmp(&output, &before, sizeof(output)) != 0) {
                failures.push_back(std::string(name) +
                                   " request/response creation mutated output");
            }
            (void)full_secret;
        };
    std::array<uint8_t,
               D1L_MESHCORE_ORACLE_MAX_REQUEST_RESPONSE_PLAINTEXT_BYTES + 1U>
        oversized_request_response_plaintext{};
    constexpr std::array<uint8_t, 1U> one_byte_request = {0x42U};
    expect_request_response_create_reject(
        "unsupported request/response type", PAYLOAD_TYPE_TXT_MSG,
        full_secret.data(), one_byte_request.data(), one_byte_request.size(),
        false);
    expect_request_response_create_reject(
        "null request/response secret", PAYLOAD_TYPE_REQ, nullptr,
        one_byte_request.data(), one_byte_request.size(), false);
    expect_request_response_create_reject(
        "null request/response plaintext", PAYLOAD_TYPE_REQ,
        full_secret.data(), nullptr, one_byte_request.size(), false);
    expect_request_response_create_reject(
        "empty request/response plaintext", PAYLOAD_TYPE_REQ,
        full_secret.data(), one_byte_request.data(), 0U, false);
    expect_request_response_create_reject(
        "oversized request/response plaintext", PAYLOAD_TYPE_RESPONSE,
        full_secret.data(), oversized_request_response_plaintext.data(),
        oversized_request_response_plaintext.size(), false);
    expect_request_response_create_reject(
        "null request/response output", PAYLOAD_TYPE_REQ, full_secret.data(),
        one_byte_request.data(), one_byte_request.size(), true);

    auto expect_request_response_parse_reject =
        [&failures](const char *name,
                    const d1l_meshcore_oracle_packet_t *packet,
                    uint8_t payload_type, uint8_t destination_hash,
                    uint8_t source_hash, const uint8_t *secret,
                    std::size_t expected_plaintext_len, uint8_t *plaintext,
                    std::size_t plaintext_capacity) {
            std::array<uint8_t,
                       D1L_MESHCORE_ORACLE_MAX_REQUEST_RESPONSE_PLAINTEXT_BYTES>
                sentinel{};
            sentinel.fill(0xD7U);
            if (plaintext != nullptr) {
                std::memcpy(plaintext, sentinel.data(), sentinel.size());
            }
            if (d1l_meshcore_oracle_parse_request_response_packet(
                    packet, payload_type, destination_hash, source_hash, secret,
                    expected_plaintext_len, plaintext, plaintext_capacity) ||
                (plaintext != nullptr &&
                 std::memcmp(plaintext, sentinel.data(), sentinel.size()) !=
                     0)) {
                failures.push_back(std::string(name) +
                                   " request/response parse changed output");
            }
        };
    std::array<uint8_t,
               D1L_MESHCORE_ORACLE_MAX_REQUEST_RESPONSE_PLAINTEXT_BYTES>
        rejected_request_response_plaintext{};
    const std::size_t valid_request_response_len = 17U;
    auto expect_standard_request_response_parse_reject =
        [&](const char *name,
            const d1l_meshcore_oracle_packet_t *packet = nullptr) {
            expect_request_response_parse_reject(
                name, packet == nullptr ? &valid_request_response : packet,
                PAYLOAD_TYPE_REQ, request_response_destination_hash,
                request_response_source_hash, full_secret.data(),
                valid_request_response_len,
                rejected_request_response_plaintext.data(),
                rejected_request_response_plaintext.size());
        };
    expect_request_response_parse_reject(
        "null request/response packet", nullptr, PAYLOAD_TYPE_REQ,
        request_response_destination_hash, request_response_source_hash,
        full_secret.data(), valid_request_response_len,
        rejected_request_response_plaintext.data(),
        rejected_request_response_plaintext.size());
    expect_request_response_parse_reject(
        "unsupported expected request/response type", &valid_request_response,
        PAYLOAD_TYPE_TXT_MSG, request_response_destination_hash,
        request_response_source_hash, full_secret.data(),
        valid_request_response_len, rejected_request_response_plaintext.data(),
        rejected_request_response_plaintext.size());
    expect_request_response_parse_reject(
        "null request/response parse secret", &valid_request_response,
        PAYLOAD_TYPE_REQ, request_response_destination_hash,
        request_response_source_hash, nullptr, valid_request_response_len,
        rejected_request_response_plaintext.data(),
        rejected_request_response_plaintext.size());
    expect_request_response_parse_reject(
        "zero expected request/response length", &valid_request_response,
        PAYLOAD_TYPE_REQ, request_response_destination_hash,
        request_response_source_hash, full_secret.data(), 0U,
        rejected_request_response_plaintext.data(),
        rejected_request_response_plaintext.size());
    expect_request_response_parse_reject(
        "oversized expected request/response length", &valid_request_response,
        PAYLOAD_TYPE_REQ, request_response_destination_hash,
        request_response_source_hash, full_secret.data(),
        D1L_MESHCORE_ORACLE_MAX_REQUEST_RESPONSE_PLAINTEXT_BYTES + 1U,
        rejected_request_response_plaintext.data(),
        rejected_request_response_plaintext.size());
    expect_request_response_parse_reject(
        "null request/response plaintext output", &valid_request_response,
        PAYLOAD_TYPE_REQ, request_response_destination_hash,
        request_response_source_hash, full_secret.data(),
        valid_request_response_len, nullptr,
        rejected_request_response_plaintext.size());
    expect_request_response_parse_reject(
        "undersized request/response plaintext output", &valid_request_response,
        PAYLOAD_TYPE_REQ, request_response_destination_hash,
        request_response_source_hash, full_secret.data(),
        valid_request_response_len, rejected_request_response_plaintext.data(),
        valid_request_response_len - 1U);
    expect_request_response_parse_reject(
        "wrong expected request/response type", &valid_request_response,
        PAYLOAD_TYPE_RESPONSE, request_response_destination_hash,
        request_response_source_hash, full_secret.data(),
        valid_request_response_len, rejected_request_response_plaintext.data(),
        rejected_request_response_plaintext.size());
    d1l_meshcore_oracle_packet_t malformed_request_response =
        valid_request_response;
    malformed_request_response.header |=
        static_cast<uint8_t>(PAYLOAD_VER_2 << PH_VER_SHIFT);
    expect_standard_request_response_parse_reject(
        "future request/response version", &malformed_request_response);
    malformed_request_response = valid_request_response;
    malformed_request_response.header =
        static_cast<uint8_t>(PAYLOAD_TYPE_ACK << PH_TYPE_SHIFT);
    expect_standard_request_response_parse_reject(
        "unsupported packet request/response type",
        &malformed_request_response);
    malformed_request_response = valid_request_response;
    malformed_request_response.payload_len = 3U;
    expect_standard_request_response_parse_reject(
        "truncated request/response payload", &malformed_request_response);
    malformed_request_response = valid_request_response;
    malformed_request_response.payload_len -= 1U;
    expect_standard_request_response_parse_reject(
        "non-block request/response ciphertext", &malformed_request_response);
    expect_request_response_parse_reject(
        "wrong expected request/response destination", &valid_request_response,
        PAYLOAD_TYPE_REQ,
        static_cast<uint8_t>(request_response_destination_hash ^ 1U),
        request_response_source_hash, full_secret.data(),
        valid_request_response_len, rejected_request_response_plaintext.data(),
        rejected_request_response_plaintext.size());
    expect_request_response_parse_reject(
        "wrong expected request/response source", &valid_request_response,
        PAYLOAD_TYPE_REQ, request_response_destination_hash,
        static_cast<uint8_t>(request_response_source_hash ^ 1U),
        full_secret.data(), valid_request_response_len,
        rejected_request_response_plaintext.data(),
        rejected_request_response_plaintext.size());
    expect_request_response_parse_reject(
        "wrong request/response secret", &valid_request_response,
        PAYLOAD_TYPE_REQ, request_response_destination_hash,
        request_response_source_hash, public_secret.data(),
        valid_request_response_len, rejected_request_response_plaintext.data(),
        rejected_request_response_plaintext.size());
    malformed_request_response = valid_request_response;
    malformed_request_response.payload[0] ^= 0x01U;
    expect_standard_request_response_parse_reject(
        "tampered outer request/response destination",
        &malformed_request_response);
    malformed_request_response = valid_request_response;
    malformed_request_response.payload[1] ^= 0x01U;
    expect_standard_request_response_parse_reject(
        "tampered outer request/response source", &malformed_request_response);
    malformed_request_response = valid_request_response;
    malformed_request_response.payload[2] ^= 0x01U;
    expect_standard_request_response_parse_reject(
        "tampered request/response MAC", &malformed_request_response);
    malformed_request_response = valid_request_response;
    malformed_request_response.payload[4] ^= 0x01U;
    expect_standard_request_response_parse_reject(
        "tampered request/response ciphertext", &malformed_request_response);
    malformed_request_response = valid_request_response;
    malformed_request_response.path_len = 0xFFU;
    expect_standard_request_response_parse_reject(
        "invalid request/response path length", &malformed_request_response);
    malformed_request_response = valid_request_response;
    malformed_request_response.payload_len = 0U;
    expect_standard_request_response_parse_reject(
        "empty request/response payload", &malformed_request_response);
    expect_request_response_parse_reject(
        "wrong request/response logical block length", &valid_request_response,
        PAYLOAD_TYPE_REQ, request_response_destination_hash,
        request_response_source_hash, full_secret.data(), 16U,
        rejected_request_response_plaintext.data(),
        rejected_request_response_plaintext.size());
    auto make_raw_request_response_packet =
        [&full_secret](const uint8_t *plaintext, std::size_t plaintext_len) {
            d1l_meshcore_oracle_packet_t packet{};
            packet.header =
                static_cast<uint8_t>(PAYLOAD_TYPE_REQ << PH_TYPE_SHIFT);
            packet.payload[0] = request_response_destination_hash;
            packet.payload[1] = request_response_source_hash;
            const int encrypted_len = mesh::Utils::encryptThenMAC(
                full_secret.data(), &packet.payload[2], plaintext,
                static_cast<int>(plaintext_len));
            packet.payload_len = static_cast<uint16_t>(encrypted_len + 2);
            return packet;
        };
    std::array<uint8_t, 16U> noncanonical_request_response_plaintext{};
    noncanonical_request_response_plaintext[0] = 0x42U;
    noncanonical_request_response_plaintext[1] = 0x24U;
    malformed_request_response = make_raw_request_response_packet(
        noncanonical_request_response_plaintext.data(),
        noncanonical_request_response_plaintext.size());
    expect_request_response_parse_reject(
        "authenticated nonzero request/response padding",
        &malformed_request_response, PAYLOAD_TYPE_REQ,
        request_response_destination_hash, request_response_source_hash,
        full_secret.data(), 1U, rejected_request_response_plaintext.data(),
        rejected_request_response_plaintext.size());
    std::array<uint8_t, 17U> redundant_request_response_plaintext{};
    redundant_request_response_plaintext[0] = 0x42U;
    malformed_request_response = make_raw_request_response_packet(
        redundant_request_response_plaintext.data(),
        redundant_request_response_plaintext.size());
    expect_request_response_parse_reject(
        "authenticated redundant request/response block",
        &malformed_request_response, PAYLOAD_TYPE_REQ,
        request_response_destination_hash, request_response_source_hash,
        full_secret.data(), 1U, rejected_request_response_plaintext.data(),
        rejected_request_response_plaintext.size());

    constexpr uint8_t dm_destination_hash = 0xA1U;
    constexpr uint8_t dm_source_hash = 0xB2U;
    const std::array<uint8_t, 2U> short_dm_text = {'d', 'm'};
    const std::array<uint8_t, 20U> expected_attempt_zero_payload = {
        0xA1U, 0xB2U, 0x44U, 0xE4U, 0x29U, 0x8FU, 0xDCU,
        0x7DU, 0xA6U, 0x2CU, 0x81U, 0xABU, 0x4EU, 0x54U,
        0xC6U, 0x7DU, 0x6BU, 0xFFU, 0xE7U, 0x3EU};
    const std::array<uint8_t, 20U> expected_attempt_255_payload = {
        0xA1U, 0xB2U, 0xD8U, 0xBDU, 0x8BU, 0x6CU, 0x9BU,
        0x9DU, 0x88U, 0xEAU, 0x57U, 0x12U, 0x65U, 0xB1U,
        0xF0U, 0x89U, 0x17U, 0xA3U, 0x67U, 0x4FU};
    const std::array<uint8_t, 32U> expected_dm_matrix_sha256 = {
        0x65U, 0xF8U, 0x44U, 0xF4U, 0x2BU, 0x01U, 0xF8U, 0x80U,
        0x58U, 0x55U, 0x62U, 0x84U, 0x9FU, 0x4BU, 0xADU, 0x51U,
        0x1FU, 0x6AU, 0x0AU, 0xFAU, 0x59U, 0x90U, 0x86U, 0xBEU,
        0xF9U, 0x42U, 0x95U, 0x58U, 0x66U, 0x59U, 0x06U, 0xE1U};
    const std::array<uint8_t, 32U> expected_max_normal_dm_sha256 = {
        0xA3U, 0xB9U, 0x61U, 0xD2U, 0xEBU, 0xE7U, 0x11U, 0xBAU,
        0xB9U, 0x35U, 0x4AU, 0x63U, 0x75U, 0xF7U, 0x14U, 0x2EU,
        0xE5U, 0x20U, 0x4BU, 0x6DU, 0xCAU, 0x65U, 0x80U, 0x74U,
        0xA1U, 0xEFU, 0x67U, 0xB7U, 0x65U, 0x02U, 0x40U, 0xE7U};
    const std::array<uint8_t, 32U> expected_max_extended_dm_sha256 = {
        0x26U, 0xC5U, 0xD0U, 0x36U, 0xE5U, 0x2FU, 0xDDU, 0x54U,
        0xC3U, 0x19U, 0x80U, 0xF3U, 0x7EU, 0xB6U, 0x6CU, 0x5CU,
        0x08U, 0x82U, 0x33U, 0x71U, 0x09U, 0x47U, 0x20U, 0x52U,
        0x5AU, 0x30U, 0x45U, 0xA3U, 0x54U, 0x4DU, 0x3DU, 0xF2U};

    auto verify_dm_roundtrip =
        [&failures, &full_secret](
            const char *name, uint32_t timestamp, uint8_t attempt,
            const uint8_t *text, std::size_t text_len, bool flood,
            const uint8_t *expected_payload, std::size_t expected_payload_len,
            const std::array<uint8_t, 32U> *expected_payload_sha,
            SHA256 *matrix_sha) {
            d1l_meshcore_oracle_packet_t packet{};
            if (!d1l_meshcore_oracle_create_dm_packet(
                    dm_destination_hash, dm_source_hash, full_secret.data(),
                    timestamp, attempt, text, text_len, &packet) ||
                packet.header !=
                    static_cast<uint8_t>(PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT) ||
                (expected_payload != nullptr &&
                 (packet.payload_len != expected_payload_len ||
                  std::memcmp(packet.payload, expected_payload,
                              expected_payload_len) != 0))) {
                failures.push_back(std::string(name) +
                                   " DM create vector changed");
                return;
            }
            if (expected_payload_sha != nullptr) {
                std::array<uint8_t, 32U> digest{};
                SHA256 payload_sha;
                payload_sha.update(packet.payload, packet.payload_len);
                payload_sha.finalize(digest.data(), digest.size());
                if (digest != *expected_payload_sha) {
                    failures.push_back(std::string(name) +
                                       " DM payload digest changed");
                    return;
                }
            }
            if (matrix_sha != nullptr) {
                matrix_sha->update(packet.payload, packet.payload_len);
            }

            uint8_t priority = 0xA5U;
            const bool routed =
                flood ? d1l_meshcore_oracle_prepare_flood(
                            &packet, 1U, 0U, nullptr, &priority)
                      : d1l_meshcore_oracle_prepare_direct(
                            &packet, nullptr, 0U, &priority);
            std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_RAW_BYTES> raw{};
            size_t raw_len = 0U;
            d1l_meshcore_oracle_packet_t decoded{};
            if (!routed || priority != (flood ? 1U : 0U) ||
                !d1l_meshcore_oracle_packet_encode(
                    &packet, raw.data(), raw.size(), &raw_len) ||
                !d1l_meshcore_oracle_packet_decode(raw.data(), raw_len,
                                                   &decoded)) {
                failures.push_back(std::string(name) +
                                   " DM wire roundtrip failed");
                return;
            }

            uint32_t parsed_timestamp = 0U;
            uint8_t parsed_attempt = 0U;
            std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_DM_TEXT_BYTES>
                parsed_text{};
            size_t parsed_text_len = 0U;
            if (!d1l_meshcore_oracle_parse_dm_packet(
                    &decoded, dm_destination_hash, dm_source_hash,
                    full_secret.data(), &parsed_timestamp, &parsed_attempt,
                    parsed_text.data(), parsed_text.size(), &parsed_text_len) ||
                parsed_timestamp != timestamp || parsed_attempt != attempt ||
                parsed_text_len != text_len ||
                (text_len > 0U &&
                 std::memcmp(parsed_text.data(), text, text_len) != 0)) {
                failures.push_back(std::string(name) +
                                   " DM parse vector changed");
            }
        };

    SHA256 dm_matrix_sha;
    for (unsigned int attempt_value = 0U; attempt_value <= 0xFFU;
         ++attempt_value) {
        const uint8_t attempt = static_cast<uint8_t>(attempt_value);
        const uint32_t timestamp = 0x12345678U + attempt_value;
        const uint8_t *text = short_dm_text.data();
        const size_t text_len = attempt == 0U ? 0U : short_dm_text.size();
        const uint8_t *expected_payload = nullptr;
        size_t expected_payload_len = 0U;
        if (attempt == 0U) {
            expected_payload = expected_attempt_zero_payload.data();
            expected_payload_len = expected_attempt_zero_payload.size();
        } else if (attempt == 0xFFU) {
            expected_payload = expected_attempt_255_payload.data();
            expected_payload_len = expected_attempt_255_payload.size();
        }
        verify_dm_roundtrip("attempt matrix", timestamp, attempt, text,
                            text_len, (attempt & 1U) != 0U, expected_payload,
                            expected_payload_len, nullptr, &dm_matrix_sha);
    }
    std::array<uint8_t, 32U> dm_matrix_digest{};
    dm_matrix_sha.finalize(dm_matrix_digest.data(), dm_matrix_digest.size());
    if (dm_matrix_digest != expected_dm_matrix_sha256) {
        failures.push_back("DM attempt 0-255 matrix digest changed");
    }

    std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_DM_TEXT_BYTES>
        maximum_normal_dm_text{};
    for (size_t index = 0U; index < maximum_normal_dm_text.size(); ++index) {
        maximum_normal_dm_text[index] =
            static_cast<uint8_t>('A' + (index % 26U));
    }
    verify_dm_roundtrip(
        "maximum normal text", 0x89ABCDEFU, 3U,
        maximum_normal_dm_text.data(), maximum_normal_dm_text.size(), false,
        nullptr, 0U, &expected_max_normal_dm_sha256, nullptr);

    std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_DM_EXTENDED_TEXT_BYTES>
        maximum_extended_dm_text{};
    for (size_t index = 0U; index < maximum_extended_dm_text.size(); ++index) {
        maximum_extended_dm_text[index] =
            static_cast<uint8_t>('a' + (index % 26U));
    }
    verify_dm_roundtrip(
        "maximum extended text", 0x10203040U, 0xFFU,
        maximum_extended_dm_text.data(), maximum_extended_dm_text.size(), true,
        nullptr, 0U, &expected_max_extended_dm_sha256, nullptr);

    const std::array<uint8_t, 32U> expected_aligned_dm_matrix_sha256 = {
        0x48U, 0xE6U, 0x08U, 0x10U, 0x6CU, 0x57U, 0x89U, 0x95U,
        0x00U, 0x81U, 0xE3U, 0x1BU, 0x15U, 0x0FU, 0x27U, 0xF6U,
        0x7DU, 0xD9U, 0xD3U, 0x3FU, 0x5BU, 0x4CU, 0xAEU, 0x8EU,
        0xF1U, 0x18U, 0x50U, 0x2FU, 0x0AU, 0x77U, 0x7EU, 0x38U};
    SHA256 aligned_dm_matrix_sha;
    for (size_t block_count = 1U; block_count <= 10U; ++block_count) {
        std::vector<uint8_t> aligned_text(block_count * 16U - 5U);
        for (size_t index = 0U; index < aligned_text.size(); ++index) {
            aligned_text[index] = static_cast<uint8_t>(
                'A' + ((index + block_count) % 26U));
        }
        verify_dm_roundtrip(
            "exact-block normal text",
            static_cast<uint32_t>(0xA0B0C000U + block_count),
            static_cast<uint8_t>(block_count & 3U), aligned_text.data(),
            aligned_text.size(), false, nullptr, 0U, nullptr,
            &aligned_dm_matrix_sha);
    }
    std::array<uint8_t, 32U> aligned_dm_matrix_digest{};
    aligned_dm_matrix_sha.finalize(aligned_dm_matrix_digest.data(),
                                   aligned_dm_matrix_digest.size());
    if (aligned_dm_matrix_digest != expected_aligned_dm_matrix_sha256) {
        failures.push_back("exact-block normal DM matrix digest changed");
    }

    d1l_meshcore_oracle_packet_t valid_dm{};
    if (!d1l_meshcore_oracle_create_dm_packet(
            dm_destination_hash, dm_source_hash, full_secret.data(),
            0x55667788U, 3U, short_dm_text.data(), short_dm_text.size(),
            &valid_dm)) {
        failures.push_back("DM negative-vector fixture creation failed");
    }
    auto expect_dm_create_reject =
        [&failures, &valid_dm](const char *name, const uint8_t *secret,
                               uint8_t attempt, const uint8_t *text,
                               size_t text_len,
                               d1l_meshcore_oracle_packet_t *output) {
            d1l_meshcore_oracle_packet_t sentinel = valid_dm;
            sentinel.header ^= 0x80U;
            if (output != nullptr) {
                *output = sentinel;
            }
            if (d1l_meshcore_oracle_create_dm_packet(
                    dm_destination_hash, dm_source_hash, secret, 0x55667788U,
                    attempt, text, text_len, output) ||
                (output != nullptr && !packets_equal(*output, sentinel))) {
                failures.push_back(std::string(name) +
                                   " DM create reject changed output");
            }
        };
    d1l_meshcore_oracle_packet_t rejected_dm{};
    expect_dm_create_reject("null DM secret", nullptr, 0U,
                            short_dm_text.data(), short_dm_text.size(),
                            &rejected_dm);
    expect_dm_create_reject("null DM text", full_secret.data(), 0U, nullptr,
                            0U, &rejected_dm);
    const std::array<uint8_t, 3U> embedded_null_dm_text = {'a', 0U, 'b'};
    expect_dm_create_reject(
        "embedded NUL DM text", full_secret.data(), 0U,
        embedded_null_dm_text.data(), embedded_null_dm_text.size(),
        &rejected_dm);
    std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_DM_TEXT_BYTES + 1U>
        oversized_normal_dm_text{};
    oversized_normal_dm_text.fill('n');
    expect_dm_create_reject(
        "oversized normal DM text", full_secret.data(), 3U,
        oversized_normal_dm_text.data(), oversized_normal_dm_text.size(),
        &rejected_dm);
    std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_DM_EXTENDED_TEXT_BYTES + 1U>
        oversized_extended_dm_text{};
    oversized_extended_dm_text.fill('e');
    expect_dm_create_reject(
        "oversized extended DM text", full_secret.data(), 4U,
        oversized_extended_dm_text.data(), oversized_extended_dm_text.size(),
        &rejected_dm);
    expect_dm_create_reject("null DM output", full_secret.data(), 0U,
                            short_dm_text.data(), short_dm_text.size(), nullptr);

    auto expect_dm_parse_reject =
        [&failures](const char *name,
                    const d1l_meshcore_oracle_packet_t *packet,
                    uint8_t destination_hash, uint8_t source_hash,
                    const uint8_t *secret, uint32_t *timestamp,
                    uint8_t *attempt, uint8_t *text, size_t text_capacity,
                    size_t *text_len) {
            std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_DM_TEXT_BYTES>
                text_sentinel{};
            text_sentinel.fill(0xD7U);
            if (timestamp != nullptr) {
                *timestamp = 0xAAAAAAAAU;
            }
            if (attempt != nullptr) {
                *attempt = 0xCCU;
            }
            if (text != nullptr) {
                std::memcpy(text, text_sentinel.data(), text_capacity);
            }
            if (text_len != nullptr) {
                *text_len = 0xBEEFU;
            }
            if (d1l_meshcore_oracle_parse_dm_packet(
                    packet, destination_hash, source_hash, secret, timestamp,
                    attempt, text, text_capacity, text_len) ||
                (timestamp != nullptr && *timestamp != 0xAAAAAAAAU) ||
                (attempt != nullptr && *attempt != 0xCCU) ||
                (text != nullptr &&
                 std::memcmp(text, text_sentinel.data(), text_capacity) != 0) ||
                (text_len != nullptr && *text_len != 0xBEEFU)) {
                failures.push_back(std::string(name) +
                                   " DM parse reject changed output");
            }
        };
    auto make_raw_dm_packet =
        [&full_secret](const uint8_t *plaintext,
                       size_t plaintext_len) {
            d1l_meshcore_oracle_packet_t packet{};
            packet.header =
                static_cast<uint8_t>(PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT);
            packet.payload[0] = dm_destination_hash;
            packet.payload[1] = dm_source_hash;
            const int encrypted_len = mesh::Utils::encryptThenMAC(
                full_secret.data(), &packet.payload[2], plaintext,
                static_cast<int>(plaintext_len));
            packet.payload_len = static_cast<uint16_t>(encrypted_len + 2);
            return packet;
        };

    uint32_t rejected_dm_timestamp = 0U;
    uint8_t rejected_dm_attempt = 0U;
    std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_DM_TEXT_BYTES>
        rejected_dm_text{};
    size_t rejected_dm_text_len = 0U;
    expect_dm_parse_reject(
        "null DM packet", nullptr, dm_destination_hash, dm_source_hash,
        full_secret.data(), &rejected_dm_timestamp, &rejected_dm_attempt,
        rejected_dm_text.data(), rejected_dm_text.size(),
        &rejected_dm_text_len);
    expect_dm_parse_reject(
        "null DM parse secret", &valid_dm, dm_destination_hash, dm_source_hash,
        nullptr, &rejected_dm_timestamp, &rejected_dm_attempt,
        rejected_dm_text.data(), rejected_dm_text.size(),
        &rejected_dm_text_len);
    expect_dm_parse_reject(
        "null DM timestamp", &valid_dm, dm_destination_hash, dm_source_hash,
        full_secret.data(), nullptr, &rejected_dm_attempt,
        rejected_dm_text.data(), rejected_dm_text.size(),
        &rejected_dm_text_len);
    expect_dm_parse_reject(
        "null DM attempt", &valid_dm, dm_destination_hash, dm_source_hash,
        full_secret.data(), &rejected_dm_timestamp, nullptr,
        rejected_dm_text.data(), rejected_dm_text.size(),
        &rejected_dm_text_len);
    expect_dm_parse_reject(
        "null DM text output", &valid_dm, dm_destination_hash, dm_source_hash,
        full_secret.data(), &rejected_dm_timestamp, &rejected_dm_attempt,
        nullptr, rejected_dm_text.size(), &rejected_dm_text_len);
    expect_dm_parse_reject(
        "null DM text length", &valid_dm, dm_destination_hash, dm_source_hash,
        full_secret.data(), &rejected_dm_timestamp, &rejected_dm_attempt,
        rejected_dm_text.data(), rejected_dm_text.size(), nullptr);

    d1l_meshcore_oracle_packet_t malformed_dm = valid_dm;
    malformed_dm.header |= 0x40U;
    expect_dm_parse_reject(
        "future-version DM", &malformed_dm, dm_destination_hash,
        dm_source_hash, full_secret.data(), &rejected_dm_timestamp,
        &rejected_dm_attempt, rejected_dm_text.data(), rejected_dm_text.size(),
        &rejected_dm_text_len);
    malformed_dm = valid_dm;
    malformed_dm.header =
        static_cast<uint8_t>(PAYLOAD_TYPE_GRP_TXT << PH_TYPE_SHIFT);
    expect_dm_parse_reject(
        "non-DM payload type", &malformed_dm, dm_destination_hash,
        dm_source_hash, full_secret.data(), &rejected_dm_timestamp,
        &rejected_dm_attempt, rejected_dm_text.data(), rejected_dm_text.size(),
        &rejected_dm_text_len);
    malformed_dm = valid_dm;
    malformed_dm.payload_len = 19U;
    expect_dm_parse_reject(
        "truncated DM payload", &malformed_dm, dm_destination_hash,
        dm_source_hash, full_secret.data(), &rejected_dm_timestamp,
        &rejected_dm_attempt, rejected_dm_text.data(), rejected_dm_text.size(),
        &rejected_dm_text_len);
    malformed_dm = valid_dm;
    malformed_dm.payload[malformed_dm.payload_len++] = 0U;
    expect_dm_parse_reject(
        "non-block DM ciphertext", &malformed_dm, dm_destination_hash,
        dm_source_hash, full_secret.data(), &rejected_dm_timestamp,
        &rejected_dm_attempt, rejected_dm_text.data(), rejected_dm_text.size(),
        &rejected_dm_text_len);
    expect_dm_parse_reject(
        "wrong DM destination hash", &valid_dm,
        static_cast<uint8_t>(dm_destination_hash ^ 1U), dm_source_hash,
        full_secret.data(), &rejected_dm_timestamp, &rejected_dm_attempt,
        rejected_dm_text.data(), rejected_dm_text.size(),
        &rejected_dm_text_len);
    expect_dm_parse_reject(
        "wrong DM source hash", &valid_dm, dm_destination_hash,
        static_cast<uint8_t>(dm_source_hash ^ 1U), full_secret.data(),
        &rejected_dm_timestamp, &rejected_dm_attempt, rejected_dm_text.data(),
        rejected_dm_text.size(), &rejected_dm_text_len);
    expect_dm_parse_reject(
        "wrong DM secret", &valid_dm, dm_destination_hash, dm_source_hash,
        public_secret.data(), &rejected_dm_timestamp, &rejected_dm_attempt,
        rejected_dm_text.data(), rejected_dm_text.size(),
        &rejected_dm_text_len);
    malformed_dm = valid_dm;
    malformed_dm.payload[2] ^= 0x01U;
    expect_dm_parse_reject(
        "tampered DM MAC", &malformed_dm, dm_destination_hash, dm_source_hash,
        full_secret.data(), &rejected_dm_timestamp, &rejected_dm_attempt,
        rejected_dm_text.data(), rejected_dm_text.size(),
        &rejected_dm_text_len);
    malformed_dm = valid_dm;
    malformed_dm.payload[4] ^= 0x01U;
    expect_dm_parse_reject(
        "tampered DM ciphertext", &malformed_dm, dm_destination_hash,
        dm_source_hash, full_secret.data(), &rejected_dm_timestamp,
        &rejected_dm_attempt, rejected_dm_text.data(), rejected_dm_text.size(),
        &rejected_dm_text_len);

    std::array<uint8_t, 16U> malformed_dm_plaintext{};
    malformed_dm_plaintext[4] = 0x04U;
    malformed_dm = make_raw_dm_packet(malformed_dm_plaintext.data(),
                                      malformed_dm_plaintext.size());
    expect_dm_parse_reject(
        "unsupported DM text type", &malformed_dm, dm_destination_hash,
        dm_source_hash, full_secret.data(), &rejected_dm_timestamp,
        &rejected_dm_attempt, rejected_dm_text.data(), rejected_dm_text.size(),
        &rejected_dm_text_len);
    std::array<uint8_t, 176U> unterminated_overlong_dm_plaintext{};
    unterminated_overlong_dm_plaintext.fill('x');
    unterminated_overlong_dm_plaintext[4] = 0U;
    malformed_dm = make_raw_dm_packet(
        unterminated_overlong_dm_plaintext.data(),
        unterminated_overlong_dm_plaintext.size());
    expect_dm_parse_reject(
        "unterminated overlong DM", &malformed_dm, dm_destination_hash,
        dm_source_hash, full_secret.data(), &rejected_dm_timestamp,
        &rejected_dm_attempt, rejected_dm_text.data(), rejected_dm_text.size(),
        &rejected_dm_text_len);
    malformed_dm_plaintext.fill(0U);
    malformed_dm_plaintext[5] = 'x';
    malformed_dm_plaintext[7] = 5U;
    malformed_dm = make_raw_dm_packet(malformed_dm_plaintext.data(),
                                      malformed_dm_plaintext.size());
    expect_dm_parse_reject(
        "mismatched extended DM attempt", &malformed_dm, dm_destination_hash,
        dm_source_hash, full_secret.data(), &rejected_dm_timestamp,
        &rejected_dm_attempt, rejected_dm_text.data(), rejected_dm_text.size(),
        &rejected_dm_text_len);
    malformed_dm_plaintext.fill(0U);
    malformed_dm_plaintext[5] = 'x';
    malformed_dm_plaintext[7] = 1U;
    malformed_dm = make_raw_dm_packet(malformed_dm_plaintext.data(),
                                      malformed_dm_plaintext.size());
    expect_dm_parse_reject(
        "nonzero normal DM padding", &malformed_dm, dm_destination_hash,
        dm_source_hash, full_secret.data(), &rejected_dm_timestamp,
        &rejected_dm_attempt, rejected_dm_text.data(), rejected_dm_text.size(),
        &rejected_dm_text_len);
    malformed_dm_plaintext.fill(0U);
    malformed_dm_plaintext[5] = 'x';
    malformed_dm_plaintext[7] = 4U;
    malformed_dm_plaintext[8] = 1U;
    malformed_dm = make_raw_dm_packet(malformed_dm_plaintext.data(),
                                      malformed_dm_plaintext.size());
    expect_dm_parse_reject(
        "nonzero extended DM padding", &malformed_dm, dm_destination_hash,
        dm_source_hash, full_secret.data(), &rejected_dm_timestamp,
        &rejected_dm_attempt, rejected_dm_text.data(), rejected_dm_text.size(),
        &rejected_dm_text_len);

    std::array<uint8_t, 5U + D1L_MESHCORE_ORACLE_MAX_DM_TEXT_BYTES + 2U>
        overlong_normal_dm_plaintext{};
    overlong_normal_dm_plaintext.fill('n');
    overlong_normal_dm_plaintext[4] = 0U;
    overlong_normal_dm_plaintext.back() = 0U;
    malformed_dm = make_raw_dm_packet(overlong_normal_dm_plaintext.data(),
                                      overlong_normal_dm_plaintext.size());
    expect_dm_parse_reject(
        "overlong normal DM plaintext", &malformed_dm, dm_destination_hash,
        dm_source_hash, full_secret.data(), &rejected_dm_timestamp,
        &rejected_dm_attempt, rejected_dm_text.data(), rejected_dm_text.size(),
        &rejected_dm_text_len);
    std::array<uint8_t,
               5U + D1L_MESHCORE_ORACLE_MAX_DM_EXTENDED_TEXT_BYTES + 3U>
        overlong_extended_dm_plaintext{};
    overlong_extended_dm_plaintext.fill('e');
    overlong_extended_dm_plaintext[4] = 0U;
    overlong_extended_dm_plaintext[overlong_extended_dm_plaintext.size() - 2U] =
        0U;
    overlong_extended_dm_plaintext.back() = 4U;
    malformed_dm = make_raw_dm_packet(overlong_extended_dm_plaintext.data(),
                                      overlong_extended_dm_plaintext.size());
    expect_dm_parse_reject(
        "overlong extended DM plaintext", &malformed_dm, dm_destination_hash,
        dm_source_hash, full_secret.data(), &rejected_dm_timestamp,
        &rejected_dm_attempt, rejected_dm_text.data(), rejected_dm_text.size(),
        &rejected_dm_text_len);
    expect_dm_parse_reject(
        "undersized DM text output", &valid_dm, dm_destination_hash,
        dm_source_hash, full_secret.data(), &rejected_dm_timestamp,
        &rejected_dm_attempt, rejected_dm_text.data(), 1U,
        &rejected_dm_text_len);

    const std::array<std::array<uint8_t, D1L_MESHCORE_ORACLE_DM_ACK_BYTES>,
                     kExpectedAckDefinedBodyVectors>
        expected_dm_acks = {{
            {0xC9U, 0xD9U, 0xF2U, 0x07U, 0x00U, 0x11U},
            {0x4EU, 0xD9U, 0x51U, 0x6BU, 0x00U, 0x22U},
            {0x21U, 0x3FU, 0xBDU, 0x0CU, 0x04U, 0x33U},
            {0x8FU, 0xE7U, 0xCDU, 0x95U, 0xFFU, 0x44U},
        }};
    struct ExpectedAckInput {
        uint32_t timestamp;
        uint8_t attempt;
        const uint8_t *text;
        size_t text_len;
        uint8_t uniqueness_byte;
    };
    const std::array<ExpectedAckInput, kExpectedAckDefinedBodyVectors>
        expected_ack_inputs = {{
            {0x01020304U, 0U, short_dm_text.data(), 0U, 0x11U},
            {0x89ABCDEFU, 3U, maximum_normal_dm_text.data(),
             maximum_normal_dm_text.size(), 0x22U},
            {0x10203040U, 4U, short_dm_text.data(), short_dm_text.size(),
             0x33U},
            {0x55667788U, 0xFFU, maximum_extended_dm_text.data(),
             maximum_extended_dm_text.size(), 0x44U},
        }};
    for (size_t index = 0U; index < expected_ack_inputs.size(); ++index) {
        const ExpectedAckInput &input = expected_ack_inputs[index];
        std::array<uint8_t, D1L_MESHCORE_ORACLE_EXPECTED_ACK_BYTES>
            expected_hash{};
        std::array<uint8_t, D1L_MESHCORE_ORACLE_DM_ACK_BYTES> ack{};
        if (!d1l_meshcore_oracle_dm_expected_ack_hash(
                kSignedAdvertPublicKey.data(), input.timestamp, input.attempt,
                input.text, input.text_len, expected_hash.data()) ||
            std::memcmp(expected_hash.data(), expected_dm_acks[index].data(),
                        expected_hash.size()) != 0 ||
            !d1l_meshcore_oracle_dm_expected_ack(
                kSignedAdvertPublicKey.data(), input.timestamp, input.attempt,
                input.text, input.text_len, input.uniqueness_byte,
                ack.data()) ||
            ack != expected_dm_acks[index]) {
            failures.push_back("expected DM ACK vector changed " +
                               std::to_string(index));
        }
    }
    std::array<uint8_t, 11U> aligned_expected_ack_text{};
    for (size_t index = 0U; index < aligned_expected_ack_text.size(); ++index) {
        aligned_expected_ack_text[index] =
            static_cast<uint8_t>('A' + ((index + 1U) % 26U));
    }
    const std::array<uint8_t, D1L_MESHCORE_ORACLE_EXPECTED_ACK_BYTES>
        expected_aligned_ack_hash = {0x9BU, 0xECU, 0x2EU, 0x94U};
    std::array<uint8_t, D1L_MESHCORE_ORACLE_EXPECTED_ACK_BYTES>
        aligned_ack_hash{};
    if (!d1l_meshcore_oracle_dm_expected_ack_hash(
            kSignedAdvertPublicKey.data(), 0xA0B0C001U, 1U,
            aligned_expected_ack_text.data(), aligned_expected_ack_text.size(),
            aligned_ack_hash.data()) ||
        aligned_ack_hash != expected_aligned_ack_hash) {
        failures.push_back("exact-block expected ACK hash vector changed");
    }

    auto expect_expected_ack_hash_reject =
        [&failures](const char *name, const uint8_t *sender_public_key,
                    const uint8_t *text, size_t text_len, uint8_t *output) {
            std::array<uint8_t, D1L_MESHCORE_ORACLE_EXPECTED_ACK_BYTES>
                sentinel{};
            sentinel.fill(0xC2U);
            if (output != nullptr) {
                std::memcpy(output, sentinel.data(), sentinel.size());
            }
            if (d1l_meshcore_oracle_dm_expected_ack_hash(
                    sender_public_key, 0x01020304U, 0U, text, text_len,
                    output) ||
                (output != nullptr &&
                 std::memcmp(output, sentinel.data(), sentinel.size()) != 0)) {
                failures.push_back(std::string(name) +
                                   " expected ACK hash reject changed output");
            }
        };
    std::array<uint8_t, D1L_MESHCORE_ORACLE_EXPECTED_ACK_BYTES>
        rejected_expected_ack_hash{};
    expect_expected_ack_hash_reject(
        "null expected ACK hash sender", nullptr, short_dm_text.data(),
        short_dm_text.size(), rejected_expected_ack_hash.data());
    expect_expected_ack_hash_reject(
        "embedded NUL expected ACK hash", kSignedAdvertPublicKey.data(),
        embedded_null_dm_text.data(), embedded_null_dm_text.size(),
        rejected_expected_ack_hash.data());
    expect_expected_ack_hash_reject(
        "null expected ACK hash output", kSignedAdvertPublicKey.data(),
        short_dm_text.data(), short_dm_text.size(), nullptr);

    auto expect_expected_ack_reject =
        [&failures](
            const char *name, const uint8_t *sender_public_key,
            uint8_t attempt, const uint8_t *text, size_t text_len,
            uint8_t *output) {
            std::array<uint8_t, D1L_MESHCORE_ORACLE_DM_ACK_BYTES> sentinel{};
            sentinel.fill(0xD3U);
            if (output != nullptr) {
                std::memcpy(output, sentinel.data(), sentinel.size());
            }
            if (d1l_meshcore_oracle_dm_expected_ack(
                    sender_public_key, 0x01020304U, attempt, text, text_len,
                    0x55U, output) ||
                (output != nullptr &&
                 std::memcmp(output, sentinel.data(), sentinel.size()) != 0)) {
                failures.push_back(std::string(name) +
                                   " expected ACK reject changed output");
            }
        };
    std::array<uint8_t, D1L_MESHCORE_ORACLE_DM_ACK_BYTES>
        rejected_expected_ack{};
    expect_expected_ack_reject(
        "null sender public key", nullptr, 0U, short_dm_text.data(),
        short_dm_text.size(), rejected_expected_ack.data());
    expect_expected_ack_reject(
        "null expected ACK text", kSignedAdvertPublicKey.data(), 0U, nullptr,
        0U, rejected_expected_ack.data());
    expect_expected_ack_reject(
        "embedded NUL expected ACK text", kSignedAdvertPublicKey.data(), 0U,
        embedded_null_dm_text.data(), embedded_null_dm_text.size(),
        rejected_expected_ack.data());
    expect_expected_ack_reject(
        "oversized normal expected ACK text", kSignedAdvertPublicKey.data(),
        3U, oversized_normal_dm_text.data(), oversized_normal_dm_text.size(),
        rejected_expected_ack.data());
    expect_expected_ack_reject(
        "oversized extended expected ACK text", kSignedAdvertPublicKey.data(),
        4U, oversized_extended_dm_text.data(),
        oversized_extended_dm_text.size(), rejected_expected_ack.data());
    expect_expected_ack_reject(
        "null expected ACK output", kSignedAdvertPublicKey.data(), 0U,
        short_dm_text.data(), short_dm_text.size(), nullptr);
    expect_expected_ack_reject(
        "undefined exact-block ACK body", kSignedAdvertPublicKey.data(), 1U,
        aligned_expected_ack_text.data(), aligned_expected_ack_text.size(),
        rejected_expected_ack.data());

    struct AckPathInput {
        uint8_t encoded_path_len;
        std::vector<uint8_t> path;
    };
    std::vector<uint8_t> maximum_two_byte_ack_path(
        D1L_MESHCORE_ORACLE_MAX_ACK_PATH_BYTES);
    for (size_t index = 0U; index < maximum_two_byte_ack_path.size(); ++index) {
        maximum_two_byte_ack_path[index] =
            static_cast<uint8_t>(index ^ 0x5AU);
    }
    std::vector<uint8_t> maximum_three_byte_ack_path(63U);
    for (size_t index = 0U; index < maximum_three_byte_ack_path.size();
         ++index) {
        maximum_three_byte_ack_path[index] =
            static_cast<uint8_t>(0x80U + index);
    }
    const std::array<AckPathInput, kExpectedAckPathRoundtripVectors>
        ack_path_inputs = {{
            {0x00U, {}},
            {0x04U, {0x10U, 0x20U, 0x30U, 0x40U}},
            {0x60U, maximum_two_byte_ack_path},
            {0x95U, maximum_three_byte_ack_path},
        }};
    const std::array<uint8_t, 20U> expected_zero_ack_path_payload = {
        0xA1U, 0xB2U, 0x13U, 0x4FU, 0xEFU, 0xC0U, 0xFCU,
        0xA6U, 0x37U, 0x09U, 0x1BU, 0x78U, 0x9CU, 0x1AU,
        0x3AU, 0xB7U, 0x50U, 0x3CU, 0xBFU, 0xD3U};
    const std::array<uint8_t, 32U> expected_ack_path_matrix_sha256 = {
        0xFCU, 0x1DU, 0x2DU, 0xF3U, 0xFCU, 0x38U, 0x75U, 0xC8U,
        0x0DU, 0xFAU, 0x57U, 0x6FU, 0x0CU, 0x43U, 0xA7U, 0xE8U,
        0xD1U, 0x68U, 0x83U, 0x0FU, 0x69U, 0x01U, 0x69U, 0xE1U,
        0x33U, 0x9AU, 0x77U, 0xB9U, 0xBFU, 0xE8U, 0xB1U, 0xADU};
    SHA256 ack_path_matrix_sha;
    d1l_meshcore_oracle_packet_t valid_ack_path{};
    for (size_t index = 0U; index < ack_path_inputs.size(); ++index) {
        const AckPathInput &input = ack_path_inputs[index];
        d1l_meshcore_oracle_packet_t packet{};
        if (!d1l_meshcore_oracle_create_dm_ack_path_packet(
                dm_destination_hash, dm_source_hash, full_secret.data(),
                input.encoded_path_len,
                input.path.empty() ? nullptr : input.path.data(),
                expected_dm_acks[index].data(), &packet) ||
            packet.header !=
                static_cast<uint8_t>(PAYLOAD_TYPE_PATH << PH_TYPE_SHIFT) ||
            (index == 0U &&
             (packet.payload_len != expected_zero_ack_path_payload.size() ||
              std::memcmp(packet.payload,
                          expected_zero_ack_path_payload.data(),
                          expected_zero_ack_path_payload.size()) != 0))) {
            failures.push_back("DM ACK+PATH create vector changed " +
                               std::to_string(index));
            continue;
        }
        ack_path_matrix_sha.update(packet.payload, packet.payload_len);
        if (index == 1U) {
            valid_ack_path = packet;
        }

        uint8_t priority = 0xA5U;
        std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_RAW_BYTES> raw{};
        size_t raw_len = 0U;
        d1l_meshcore_oracle_packet_t decoded{};
        if (!d1l_meshcore_oracle_prepare_flood(
                &packet, 1U, 0U, nullptr, &priority) || priority != 2U ||
            !d1l_meshcore_oracle_packet_encode(
                &packet, raw.data(), raw.size(), &raw_len) ||
            !d1l_meshcore_oracle_packet_decode(raw.data(), raw_len,
                                               &decoded)) {
            failures.push_back("DM ACK+PATH wire roundtrip failed " +
                               std::to_string(index));
            continue;
        }

        uint8_t parsed_encoded_path_len = 0xFFU;
        std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_ACK_PATH_BYTES>
            parsed_path{};
        size_t parsed_path_bytes = 0U;
        std::array<uint8_t, D1L_MESHCORE_ORACLE_DM_ACK_BYTES> parsed_ack{};
        if (!d1l_meshcore_oracle_parse_dm_ack_path_packet(
                &decoded, dm_destination_hash, dm_source_hash,
                full_secret.data(), &parsed_encoded_path_len,
                parsed_path.data(), parsed_path.size(), &parsed_path_bytes,
                parsed_ack.data()) ||
            parsed_encoded_path_len != input.encoded_path_len ||
            parsed_path_bytes != input.path.size() ||
            (!input.path.empty() &&
             std::memcmp(parsed_path.data(), input.path.data(),
                         input.path.size()) != 0) ||
            parsed_ack != expected_dm_acks[index]) {
            failures.push_back("DM ACK+PATH parse vector changed " +
                               std::to_string(index));
        }
    }
    std::array<uint8_t, 32U> ack_path_matrix_digest{};
    ack_path_matrix_sha.finalize(ack_path_matrix_digest.data(),
                                 ack_path_matrix_digest.size());
    if (ack_path_matrix_digest != expected_ack_path_matrix_sha256) {
        failures.push_back("DM ACK+PATH matrix digest changed");
    }

    auto expect_ack_path_create_reject =
        [&failures, &valid_ack_path](
            const char *name, const uint8_t *secret,
            uint8_t encoded_path_len, const uint8_t *path,
            const uint8_t *ack, d1l_meshcore_oracle_packet_t *output) {
            d1l_meshcore_oracle_packet_t sentinel = valid_ack_path;
            sentinel.header ^= 0x80U;
            if (output != nullptr) {
                *output = sentinel;
            }
            if (d1l_meshcore_oracle_create_dm_ack_path_packet(
                    dm_destination_hash, dm_source_hash, secret,
                    encoded_path_len, path, ack, output) ||
                (output != nullptr && !packets_equal(*output, sentinel))) {
                failures.push_back(std::string(name) +
                                   " ACK+PATH create reject changed output");
            }
        };
    d1l_meshcore_oracle_packet_t rejected_ack_path{};
    const std::array<uint8_t, 1U> one_byte_path = {0x42U};
    expect_ack_path_create_reject(
        "null ACK+PATH secret", nullptr, 0U, nullptr,
        expected_dm_acks[0].data(), &rejected_ack_path);
    expect_ack_path_create_reject(
        "reserved ACK+PATH hash size", full_secret.data(), 0xC0U, nullptr,
        expected_dm_acks[0].data(), &rejected_ack_path);
    expect_ack_path_create_reject(
        "null nonempty ACK+PATH", full_secret.data(), 0x01U, nullptr,
        expected_dm_acks[0].data(), &rejected_ack_path);
    expect_ack_path_create_reject(
        "null ACK+PATH ACK", full_secret.data(), 0x01U,
        one_byte_path.data(), nullptr, &rejected_ack_path);
    expect_ack_path_create_reject(
        "null ACK+PATH output", full_secret.data(), 0x01U,
        one_byte_path.data(), expected_dm_acks[0].data(), nullptr);

    auto expect_ack_path_parse_reject =
        [&failures](
            const char *name, const d1l_meshcore_oracle_packet_t *packet,
            uint8_t destination_hash, uint8_t source_hash,
            const uint8_t *secret, uint8_t *encoded_path_len,
            uint8_t *path, size_t path_capacity, size_t *path_bytes,
            uint8_t *ack) {
            std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_ACK_PATH_BYTES>
                path_sentinel{};
            path_sentinel.fill(0xD5U);
            std::array<uint8_t, D1L_MESHCORE_ORACLE_DM_ACK_BYTES>
                ack_sentinel{};
            ack_sentinel.fill(0xE6U);
            if (encoded_path_len != nullptr) {
                *encoded_path_len = 0xAAU;
            }
            if (path != nullptr) {
                std::memcpy(path, path_sentinel.data(), path_capacity);
            }
            if (path_bytes != nullptr) {
                *path_bytes = 0xBEEFU;
            }
            if (ack != nullptr) {
                std::memcpy(ack, ack_sentinel.data(), ack_sentinel.size());
            }
            if (d1l_meshcore_oracle_parse_dm_ack_path_packet(
                    packet, destination_hash, source_hash, secret,
                    encoded_path_len, path, path_capacity, path_bytes, ack) ||
                (encoded_path_len != nullptr && *encoded_path_len != 0xAAU) ||
                (path != nullptr &&
                 std::memcmp(path, path_sentinel.data(), path_capacity) != 0) ||
                (path_bytes != nullptr && *path_bytes != 0xBEEFU) ||
                (ack != nullptr &&
                 std::memcmp(ack, ack_sentinel.data(), ack_sentinel.size()) !=
                     0)) {
                failures.push_back(std::string(name) +
                                   " ACK+PATH parse reject changed output");
            }
        };
    uint8_t rejected_ack_path_encoded_len = 0U;
    std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_ACK_PATH_BYTES>
        rejected_ack_path_bytes{};
    size_t rejected_ack_path_byte_len = 0U;
    std::array<uint8_t, D1L_MESHCORE_ORACLE_DM_ACK_BYTES>
        rejected_ack_path_ack{};
    expect_ack_path_parse_reject(
        "null ACK+PATH packet", nullptr, dm_destination_hash, dm_source_hash,
        full_secret.data(), &rejected_ack_path_encoded_len,
        rejected_ack_path_bytes.data(), rejected_ack_path_bytes.size(),
        &rejected_ack_path_byte_len, rejected_ack_path_ack.data());
    expect_ack_path_parse_reject(
        "null ACK+PATH parse secret", &valid_ack_path, dm_destination_hash,
        dm_source_hash, nullptr, &rejected_ack_path_encoded_len,
        rejected_ack_path_bytes.data(), rejected_ack_path_bytes.size(),
        &rejected_ack_path_byte_len, rejected_ack_path_ack.data());
    expect_ack_path_parse_reject(
        "null encoded ACK+PATH length", &valid_ack_path,
        dm_destination_hash, dm_source_hash, full_secret.data(), nullptr,
        rejected_ack_path_bytes.data(), rejected_ack_path_bytes.size(),
        &rejected_ack_path_byte_len, rejected_ack_path_ack.data());
    expect_ack_path_parse_reject(
        "null ACK+PATH path output", &valid_ack_path, dm_destination_hash,
        dm_source_hash, full_secret.data(), &rejected_ack_path_encoded_len,
        nullptr, rejected_ack_path_bytes.size(),
        &rejected_ack_path_byte_len, rejected_ack_path_ack.data());
    expect_ack_path_parse_reject(
        "null ACK+PATH byte length", &valid_ack_path, dm_destination_hash,
        dm_source_hash, full_secret.data(), &rejected_ack_path_encoded_len,
        rejected_ack_path_bytes.data(), rejected_ack_path_bytes.size(),
        nullptr, rejected_ack_path_ack.data());
    expect_ack_path_parse_reject(
        "null ACK+PATH ACK output", &valid_ack_path, dm_destination_hash,
        dm_source_hash, full_secret.data(), &rejected_ack_path_encoded_len,
        rejected_ack_path_bytes.data(), rejected_ack_path_bytes.size(),
        &rejected_ack_path_byte_len, nullptr);

    d1l_meshcore_oracle_packet_t malformed_ack_path = valid_ack_path;
    malformed_ack_path.header |= 0x40U;
    expect_ack_path_parse_reject(
        "future-version ACK+PATH", &malformed_ack_path, dm_destination_hash,
        dm_source_hash, full_secret.data(), &rejected_ack_path_encoded_len,
        rejected_ack_path_bytes.data(), rejected_ack_path_bytes.size(),
        &rejected_ack_path_byte_len, rejected_ack_path_ack.data());
    malformed_ack_path = valid_ack_path;
    malformed_ack_path.header =
        static_cast<uint8_t>(PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT);
    expect_ack_path_parse_reject(
        "wrong ACK+PATH type", &malformed_ack_path, dm_destination_hash,
        dm_source_hash, full_secret.data(), &rejected_ack_path_encoded_len,
        rejected_ack_path_bytes.data(), rejected_ack_path_bytes.size(),
        &rejected_ack_path_byte_len, rejected_ack_path_ack.data());
    malformed_ack_path = valid_ack_path;
    malformed_ack_path.payload_len -= 1U;
    expect_ack_path_parse_reject(
        "truncated ACK+PATH", &malformed_ack_path, dm_destination_hash,
        dm_source_hash, full_secret.data(), &rejected_ack_path_encoded_len,
        rejected_ack_path_bytes.data(), rejected_ack_path_bytes.size(),
        &rejected_ack_path_byte_len, rejected_ack_path_ack.data());
    malformed_ack_path = valid_ack_path;
    malformed_ack_path.payload[malformed_ack_path.payload_len++] = 0U;
    expect_ack_path_parse_reject(
        "non-block ACK+PATH", &malformed_ack_path, dm_destination_hash,
        dm_source_hash, full_secret.data(), &rejected_ack_path_encoded_len,
        rejected_ack_path_bytes.data(), rejected_ack_path_bytes.size(),
        &rejected_ack_path_byte_len, rejected_ack_path_ack.data());
    expect_ack_path_parse_reject(
        "wrong ACK+PATH destination", &valid_ack_path,
        static_cast<uint8_t>(dm_destination_hash ^ 1U), dm_source_hash,
        full_secret.data(), &rejected_ack_path_encoded_len,
        rejected_ack_path_bytes.data(), rejected_ack_path_bytes.size(),
        &rejected_ack_path_byte_len, rejected_ack_path_ack.data());
    expect_ack_path_parse_reject(
        "wrong ACK+PATH source", &valid_ack_path, dm_destination_hash,
        static_cast<uint8_t>(dm_source_hash ^ 1U), full_secret.data(),
        &rejected_ack_path_encoded_len, rejected_ack_path_bytes.data(),
        rejected_ack_path_bytes.size(), &rejected_ack_path_byte_len,
        rejected_ack_path_ack.data());
    expect_ack_path_parse_reject(
        "wrong ACK+PATH secret", &valid_ack_path, dm_destination_hash,
        dm_source_hash, public_secret.data(), &rejected_ack_path_encoded_len,
        rejected_ack_path_bytes.data(), rejected_ack_path_bytes.size(),
        &rejected_ack_path_byte_len, rejected_ack_path_ack.data());
    malformed_ack_path = valid_ack_path;
    malformed_ack_path.payload[2] ^= 0x01U;
    expect_ack_path_parse_reject(
        "tampered ACK+PATH MAC", &malformed_ack_path, dm_destination_hash,
        dm_source_hash, full_secret.data(), &rejected_ack_path_encoded_len,
        rejected_ack_path_bytes.data(), rejected_ack_path_bytes.size(),
        &rejected_ack_path_byte_len, rejected_ack_path_ack.data());
    malformed_ack_path = valid_ack_path;
    malformed_ack_path.payload[4] ^= 0x01U;
    expect_ack_path_parse_reject(
        "tampered ACK+PATH ciphertext", &malformed_ack_path,
        dm_destination_hash, dm_source_hash, full_secret.data(),
        &rejected_ack_path_encoded_len, rejected_ack_path_bytes.data(),
        rejected_ack_path_bytes.size(), &rejected_ack_path_byte_len,
        rejected_ack_path_ack.data());

    auto make_raw_ack_path_packet =
        [&full_secret](const uint8_t *plaintext, size_t plaintext_len) {
            d1l_meshcore_oracle_packet_t packet{};
            packet.header =
                static_cast<uint8_t>(PAYLOAD_TYPE_PATH << PH_TYPE_SHIFT);
            packet.payload[0] = dm_destination_hash;
            packet.payload[1] = dm_source_hash;
            const int encrypted_len = mesh::Utils::encryptThenMAC(
                full_secret.data(), &packet.payload[2], plaintext,
                static_cast<int>(plaintext_len));
            packet.payload_len = static_cast<uint16_t>(encrypted_len + 2);
            return packet;
        };
    std::array<uint8_t, 16U> malformed_ack_path_plaintext{};
    malformed_ack_path_plaintext[0] = 0xC0U;
    malformed_ack_path = make_raw_ack_path_packet(
        malformed_ack_path_plaintext.data(),
        malformed_ack_path_plaintext.size());
    expect_ack_path_parse_reject(
        "reserved embedded ACK+PATH hash size", &malformed_ack_path,
        dm_destination_hash, dm_source_hash, full_secret.data(),
        &rejected_ack_path_encoded_len, rejected_ack_path_bytes.data(),
        rejected_ack_path_bytes.size(), &rejected_ack_path_byte_len,
        rejected_ack_path_ack.data());
    malformed_ack_path_plaintext.fill(0U);
    malformed_ack_path_plaintext[0] = 0x3FU;
    malformed_ack_path = make_raw_ack_path_packet(
        malformed_ack_path_plaintext.data(),
        malformed_ack_path_plaintext.size());
    expect_ack_path_parse_reject(
        "short embedded ACK+PATH", &malformed_ack_path, dm_destination_hash,
        dm_source_hash, full_secret.data(), &rejected_ack_path_encoded_len,
        rejected_ack_path_bytes.data(), rejected_ack_path_bytes.size(),
        &rejected_ack_path_byte_len, rejected_ack_path_ack.data());
    malformed_ack_path_plaintext.fill(0U);
    malformed_ack_path_plaintext[1] = 0x02U;
    malformed_ack_path = make_raw_ack_path_packet(
        malformed_ack_path_plaintext.data(),
        malformed_ack_path_plaintext.size());
    expect_ack_path_parse_reject(
        "wrong embedded ACK+PATH extra type", &malformed_ack_path,
        dm_destination_hash, dm_source_hash, full_secret.data(),
        &rejected_ack_path_encoded_len, rejected_ack_path_bytes.data(),
        rejected_ack_path_bytes.size(), &rejected_ack_path_byte_len,
        rejected_ack_path_ack.data());
    malformed_ack_path_plaintext.fill(0U);
    malformed_ack_path_plaintext[1] = PAYLOAD_TYPE_ACK;
    malformed_ack_path_plaintext[8] = 0x01U;
    malformed_ack_path = make_raw_ack_path_packet(
        malformed_ack_path_plaintext.data(),
        malformed_ack_path_plaintext.size());
    expect_ack_path_parse_reject(
        "noncanonical ACK+PATH padding", &malformed_ack_path,
        dm_destination_hash, dm_source_hash, full_secret.data(),
        &rejected_ack_path_encoded_len, rejected_ack_path_bytes.data(),
        rejected_ack_path_bytes.size(), &rejected_ack_path_byte_len,
        rejected_ack_path_ack.data());
    expect_ack_path_parse_reject(
        "undersized ACK+PATH output", &valid_ack_path, dm_destination_hash,
        dm_source_hash, full_secret.data(), &rejected_ack_path_encoded_len,
        rejected_ack_path_bytes.data(), 3U, &rejected_ack_path_byte_len,
        rejected_ack_path_ack.data());

    auto check_prepared_packet =
        [&failures](const char *name,
                    const d1l_meshcore_oracle_packet_t &actual,
                    const d1l_meshcore_oracle_packet_t &expected,
                    uint8_t priority,
                    uint8_t expected_priority) {
            if (!packets_equal(actual, expected) || priority != expected_priority) {
                failures.push_back(std::string(name) +
                                   " route preparation mismatch");
                return;
            }
            std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_RAW_BYTES> raw{};
            size_t raw_len = 0U;
            d1l_meshcore_oracle_packet_t decoded{};
            if (!d1l_meshcore_oracle_packet_encode(
                    &actual, raw.data(), raw.size(), &raw_len) ||
                !d1l_meshcore_oracle_packet_decode(raw.data(), raw_len,
                                                   &decoded) ||
                !packets_equal(decoded, expected)) {
                failures.push_back(std::string(name) +
                                   " route packet did not round trip");
            }
        };

    struct PathReturnInput {
        uint8_t encoded_path_len;
        std::vector<uint8_t> path;
        uint8_t extra_type;
        std::vector<uint8_t> extra;
        bool unique;
    };
    std::vector<uint8_t> maximum_path_return_two_byte_path(
        D1L_MESHCORE_ORACLE_MAX_PATH_BYTES);
    std::vector<uint8_t> maximum_path_return_three_byte_path(63U);
    std::vector<uint8_t> maximum_two_byte_path_extra(97U);
    std::vector<uint8_t> maximum_three_byte_path_extra(98U);
    for (size_t index = 0U;
         index < maximum_path_return_two_byte_path.size(); ++index) {
        maximum_path_return_two_byte_path[index] =
            static_cast<uint8_t>(0x31U + index);
    }
    for (size_t index = 0U;
         index < maximum_path_return_three_byte_path.size(); ++index) {
        maximum_path_return_three_byte_path[index] =
            static_cast<uint8_t>(0xE0U - index);
    }
    for (size_t index = 0U; index < maximum_two_byte_path_extra.size();
         ++index) {
        maximum_two_byte_path_extra[index] =
            static_cast<uint8_t>(index ^ 0xA5U);
    }
    for (size_t index = 0U; index < maximum_three_byte_path_extra.size();
         ++index) {
        maximum_three_byte_path_extra[index] =
            static_cast<uint8_t>(index * 3U + 1U);
    }
    const std::array<PathReturnInput, kPathReturnRoundtripVectors>
        path_return_inputs = {{
            {0x00U, {}, PAYLOAD_TYPE_RESPONSE,
             {0xDEU, 0xADU, 0xBEU, 0xEFU}, false},
            {0x04U, {0x10U, 0x20U, 0x30U, 0x40U}, 0xF3U,
             {0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U}, false},
            {0x60U, maximum_path_return_two_byte_path, PAYLOAD_TYPE_RESPONSE,
             maximum_two_byte_path_extra, false},
            {0x95U, maximum_path_return_three_byte_path, PAYLOAD_TYPE_REQ,
             maximum_three_byte_path_extra, false},
            {0x00U, {}, 0xFFU, {0x01U, 0x23U, 0x45U, 0x67U}, true},
            {0x95U, maximum_path_return_three_byte_path, 0xFFU,
             {0x89U, 0xABU, 0xCDU, 0xEFU}, true},
        }};
    const std::array<uint8_t, 20U> expected_zero_path_return_payload = {
        0xA1U, 0xB2U, 0x51U, 0xA7U, 0xB0U, 0x2DU, 0xC6U,
        0x3AU, 0x41U, 0x9FU, 0x32U, 0xE2U, 0x20U, 0x0FU,
        0xD2U, 0xE2U, 0x6AU, 0x28U, 0xD8U, 0xA9U};
    const std::array<uint8_t, 32U> expected_path_return_matrix_sha256 = {
        0x30U, 0xF7U, 0xDBU, 0xBEU, 0xA8U, 0x5AU, 0x3CU, 0x22U,
        0x1EU, 0x79U, 0xE1U, 0x27U, 0x7AU, 0xEFU, 0x30U, 0x13U,
        0x66U, 0x3DU, 0xB5U, 0xBFU, 0xBAU, 0xB9U, 0x85U, 0x51U,
        0x05U, 0x26U, 0xFAU, 0x71U, 0x95U, 0x6AU, 0xB4U, 0xF6U};
    const uint16_t path_return_transport_codes[2] = {0xCAFEU, 0xBABEU};
    const std::array<uint8_t, 2U> path_return_direct_route = {0x71U, 0x72U};
    SHA256 path_return_matrix_sha;
    d1l_meshcore_oracle_packet_t valid_path_return_extra{};
    d1l_meshcore_oracle_packet_t valid_path_return_unique{};
    for (size_t index = 0U; index < path_return_inputs.size(); ++index) {
        const PathReturnInput &input = path_return_inputs[index];
        d1l_meshcore_oracle_packet_t packet{};
        const bool created = input.unique
                                 ? d1l_meshcore_oracle_create_path_return_unique_packet(
                                       dm_destination_hash, dm_source_hash,
                                       full_secret.data(), input.encoded_path_len,
                                       input.path.empty() ? nullptr
                                                          : input.path.data(),
                                       input.extra.data(), &packet)
                                 : d1l_meshcore_oracle_create_path_return_extra_packet(
                                       dm_destination_hash, dm_source_hash,
                                       full_secret.data(), input.encoded_path_len,
                                       input.path.empty() ? nullptr
                                                          : input.path.data(),
                                       input.extra_type, input.extra.data(),
                                       input.extra.size(), &packet);
        if (!created ||
            packet.header !=
                static_cast<uint8_t>(PAYLOAD_TYPE_PATH << PH_TYPE_SHIFT) ||
            (index == 0U &&
             (packet.payload_len != expected_zero_path_return_payload.size() ||
              std::memcmp(packet.payload,
                          expected_zero_path_return_payload.data(),
                          expected_zero_path_return_payload.size()) != 0))) {
            failures.push_back("PATH-return create vector changed " +
                               std::to_string(index));
            continue;
        }
        path_return_matrix_sha.update(packet.payload, packet.payload_len);
        if (index == 0U) {
            valid_path_return_extra = packet;
        } else if (index == 4U) {
            valid_path_return_unique = packet;
        }

        uint8_t path_priority = 0xA5U;
        bool route_ok = false;
        if (index == 0U) {
            route_ok = d1l_meshcore_oracle_prepare_flood(
                &packet, 1U, 0U, nullptr, &path_priority);
            route_ok = route_ok &&
                       (packet.header & PH_ROUTE_MASK) == ROUTE_TYPE_FLOOD &&
                       packet.path_len == 0x00U && path_priority == 2U;
        } else if (index == 1U) {
            route_ok = d1l_meshcore_oracle_prepare_flood(
                &packet, 2U, 1U, path_return_transport_codes,
                &path_priority);
            route_ok = route_ok &&
                       (packet.header & PH_ROUTE_MASK) ==
                           ROUTE_TYPE_TRANSPORT_FLOOD &&
                       packet.path_len == 0x40U && path_priority == 2U &&
                       packet.transport_codes[0] ==
                           path_return_transport_codes[0] &&
                       packet.transport_codes[1] ==
                           path_return_transport_codes[1];
        } else if (index == 2U) {
            route_ok = d1l_meshcore_oracle_prepare_direct(
                &packet, path_return_direct_route.data(), 0x02U,
                &path_priority);
            route_ok = route_ok &&
                       (packet.header & PH_ROUTE_MASK) == ROUTE_TYPE_DIRECT &&
                       packet.path_len == 0x02U && path_priority == 1U &&
                       std::memcmp(packet.path,
                                   path_return_direct_route.data(),
                                   path_return_direct_route.size()) == 0;
        } else if (index == 3U) {
            route_ok = d1l_meshcore_oracle_prepare_zero_hop(
                &packet, 1U, path_return_transport_codes, &path_priority);
            route_ok = route_ok &&
                       (packet.header & PH_ROUTE_MASK) ==
                           ROUTE_TYPE_TRANSPORT_DIRECT &&
                       packet.path_len == 0U && path_priority == 0U &&
                       packet.transport_codes[0] ==
                           path_return_transport_codes[0] &&
                       packet.transport_codes[1] ==
                           path_return_transport_codes[1];
        } else if (index == 4U) {
            route_ok = d1l_meshcore_oracle_prepare_zero_hop(
                &packet, 0U, nullptr, &path_priority);
            route_ok = route_ok &&
                       (packet.header & PH_ROUTE_MASK) == ROUTE_TYPE_DIRECT &&
                       packet.path_len == 0U && path_priority == 0U;
        } else {
            route_ok = d1l_meshcore_oracle_prepare_flood(
                &packet, 3U, 0U, nullptr, &path_priority);
            route_ok = route_ok &&
                       (packet.header & PH_ROUTE_MASK) == ROUTE_TYPE_FLOOD &&
                       packet.path_len == 0x80U && path_priority == 2U;
        }
        std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_RAW_BYTES> raw{};
        size_t raw_len = 0U;
        d1l_meshcore_oracle_packet_t decoded{};
        if (!route_ok ||
            !d1l_meshcore_oracle_packet_encode(
                &packet, raw.data(), raw.size(), &raw_len) ||
            !d1l_meshcore_oracle_packet_decode(raw.data(), raw_len, &decoded) ||
            !packets_equal(packet, decoded)) {
            failures.push_back("PATH-return route-code vector changed " +
                               std::to_string(index));
            continue;
        }

        uint8_t parsed_encoded_path_len = 0xFFU;
        std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_PATH_BYTES> parsed_path{};
        size_t parsed_path_bytes = 0U;
        std::array<uint8_t,
                   D1L_MESHCORE_ORACLE_MAX_PATH_RETURN_EXTRA_BYTES>
            parsed_extra{};
        uint8_t parsed_extra_type = 0xAAU;
        size_t parsed_extra_len = 0U;
        bool parsed = false;
        if (input.unique) {
            parsed = d1l_meshcore_oracle_parse_path_return_unique_packet(
                &decoded, dm_destination_hash, dm_source_hash,
                full_secret.data(), &parsed_encoded_path_len,
                parsed_path.data(), parsed_path.size(), &parsed_path_bytes,
                parsed_extra.data());
            parsed_extra_len =
                D1L_MESHCORE_ORACLE_PATH_RETURN_UNIQUENESS_BYTES;
            parsed_extra_type = 0x0FU;
        } else {
            parsed = d1l_meshcore_oracle_parse_path_return_extra_packet(
                &decoded, dm_destination_hash, dm_source_hash,
                full_secret.data(), input.extra.size(),
                &parsed_encoded_path_len, parsed_path.data(),
                parsed_path.size(), &parsed_path_bytes, &parsed_extra_type,
                parsed_extra.data(), parsed_extra.size(), &parsed_extra_len);
        }
        if (!parsed || parsed_encoded_path_len != input.encoded_path_len ||
            parsed_path_bytes != input.path.size() ||
            (!input.path.empty() &&
             std::memcmp(parsed_path.data(), input.path.data(),
                         input.path.size()) != 0) ||
            parsed_extra_type != (input.extra_type & 0x0FU) ||
            parsed_extra_len != input.extra.size() ||
            std::memcmp(parsed_extra.data(), input.extra.data(),
                        input.extra.size()) != 0) {
            failures.push_back("PATH-return parse vector changed " +
                               std::to_string(index));
        }
    }
    std::array<uint8_t, 32U> path_return_matrix_digest{};
    path_return_matrix_sha.finalize(path_return_matrix_digest.data(),
                                    path_return_matrix_digest.size());
    if (path_return_matrix_digest != expected_path_return_matrix_sha256) {
        failures.push_back("PATH-return matrix digest changed");
    }

    auto expect_path_return_extra_create_reject =
        [&failures, &valid_path_return_extra](
            const char *name, const uint8_t *secret, uint8_t path_len,
            const uint8_t *path, const uint8_t *extra, size_t extra_len,
            d1l_meshcore_oracle_packet_t *output) {
            d1l_meshcore_oracle_packet_t sentinel = valid_path_return_extra;
            sentinel.header ^= 0x80U;
            if (output != nullptr) {
                *output = sentinel;
            }
            if (d1l_meshcore_oracle_create_path_return_extra_packet(
                    dm_destination_hash, dm_source_hash, secret, path_len, path,
                    PAYLOAD_TYPE_RESPONSE, extra, extra_len, output) ||
                (output != nullptr && !packets_equal(*output, sentinel))) {
                failures.push_back(std::string(name) +
                                   " PATH-return extra create reject changed output");
            }
        };
    std::vector<uint8_t> oversized_path_return_extra(
        D1L_MESHCORE_ORACLE_MAX_PATH_RETURN_EXTRA_BYTES + 1U, 0x5AU);
    d1l_meshcore_oracle_packet_t rejected_path_return{};
    const std::array<uint8_t, 1U> general_one_byte_extra = {0x24U};
    expect_path_return_extra_create_reject(
        "null secret", nullptr, 0U, nullptr, general_one_byte_extra.data(),
        general_one_byte_extra.size(), &rejected_path_return);
    expect_path_return_extra_create_reject(
        "reserved path length", full_secret.data(), 0xC0U, nullptr,
        general_one_byte_extra.data(), general_one_byte_extra.size(),
        &rejected_path_return);
    expect_path_return_extra_create_reject(
        "missing path", full_secret.data(), 0x01U, nullptr,
        general_one_byte_extra.data(), general_one_byte_extra.size(),
        &rejected_path_return);
    expect_path_return_extra_create_reject(
        "missing extra", full_secret.data(), 0U, nullptr, nullptr, 1U,
        &rejected_path_return);
    expect_path_return_extra_create_reject(
        "empty extra", full_secret.data(), 0U, nullptr,
        general_one_byte_extra.data(), 0U, &rejected_path_return);
    expect_path_return_extra_create_reject(
        "oversized extra", full_secret.data(), 0U, nullptr,
        oversized_path_return_extra.data(), oversized_path_return_extra.size(),
        &rejected_path_return);
    expect_path_return_extra_create_reject(
        "null output", full_secret.data(), 0U, nullptr,
        general_one_byte_extra.data(), general_one_byte_extra.size(), nullptr);

    auto expect_path_return_unique_create_reject =
        [&failures, &valid_path_return_unique](
            const char *name, const uint8_t *secret, uint8_t path_len,
            const uint8_t *path, const uint8_t *uniqueness,
            d1l_meshcore_oracle_packet_t *output) {
            d1l_meshcore_oracle_packet_t sentinel = valid_path_return_unique;
            sentinel.header ^= 0x80U;
            if (output != nullptr) {
                *output = sentinel;
            }
            if (d1l_meshcore_oracle_create_path_return_unique_packet(
                    dm_destination_hash, dm_source_hash, secret, path_len, path,
                    uniqueness, output) ||
                (output != nullptr && !packets_equal(*output, sentinel))) {
                failures.push_back(std::string(name) +
                                   " PATH-return unique create reject changed output");
            }
        };
    const auto &valid_uniqueness = path_return_inputs[4].extra;
    expect_path_return_unique_create_reject(
        "null unique secret", nullptr, 0U, nullptr, valid_uniqueness.data(),
        &rejected_path_return);
    expect_path_return_unique_create_reject(
        "reserved unique path length", full_secret.data(), 0xC0U, nullptr,
        valid_uniqueness.data(), &rejected_path_return);
    expect_path_return_unique_create_reject(
        "missing unique path", full_secret.data(), 0x01U, nullptr,
        valid_uniqueness.data(), &rejected_path_return);
    expect_path_return_unique_create_reject(
        "null uniqueness", full_secret.data(), 0U, nullptr, nullptr,
        &rejected_path_return);
    expect_path_return_unique_create_reject(
        "null unique output", full_secret.data(), 0U, nullptr,
        valid_uniqueness.data(), nullptr);

    auto expect_path_return_extra_parse_reject =
        [&failures](const char *name,
                    const d1l_meshcore_oracle_packet_t *packet,
                    uint8_t destination_hash, uint8_t source_hash,
                    const uint8_t *secret, size_t expected_extra_len,
                    size_t path_capacity, size_t extra_capacity,
                    bool null_encoded_path, bool null_extra_type) {
            uint8_t encoded_path_len = 0xAAU;
            std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_PATH_BYTES> path{};
            path.fill(0xB5U);
            size_t path_bytes = 0xBEEFU;
            uint8_t extra_type = 0xCCU;
            std::array<uint8_t,
                       D1L_MESHCORE_ORACLE_MAX_PATH_RETURN_EXTRA_BYTES>
                extra{};
            extra.fill(0xD6U);
            size_t extra_len = 0xCAFEU;
            const auto path_before = path;
            const auto extra_before = extra;
            if (d1l_meshcore_oracle_parse_path_return_extra_packet(
                    packet, destination_hash, source_hash, secret,
                    expected_extra_len,
                    null_encoded_path ? nullptr : &encoded_path_len,
                    path.data(), path_capacity, &path_bytes,
                    null_extra_type ? nullptr : &extra_type, extra.data(),
                    extra_capacity, &extra_len) ||
                encoded_path_len != 0xAAU || path != path_before ||
                path_bytes != 0xBEEFU || extra_type != 0xCCU ||
                extra != extra_before || extra_len != 0xCAFEU) {
                failures.push_back(std::string(name) +
                                   " PATH-return extra parse reject changed output");
            }
        };
    expect_path_return_extra_parse_reject(
        "null packet", nullptr, dm_destination_hash, dm_source_hash,
        full_secret.data(), 4U, D1L_MESHCORE_ORACLE_MAX_PATH_BYTES,
        D1L_MESHCORE_ORACLE_MAX_PATH_RETURN_EXTRA_BYTES, false, false);
    expect_path_return_extra_parse_reject(
        "null parse secret", &valid_path_return_extra, dm_destination_hash,
        dm_source_hash, nullptr, 4U, D1L_MESHCORE_ORACLE_MAX_PATH_BYTES,
        D1L_MESHCORE_ORACLE_MAX_PATH_RETURN_EXTRA_BYTES, false, false);
    expect_path_return_extra_parse_reject(
        "zero expected extra", &valid_path_return_extra, dm_destination_hash,
        dm_source_hash, full_secret.data(), 0U,
        D1L_MESHCORE_ORACLE_MAX_PATH_BYTES,
        D1L_MESHCORE_ORACLE_MAX_PATH_RETURN_EXTRA_BYTES, false, false);
    expect_path_return_extra_parse_reject(
        "oversized expected extra", &valid_path_return_extra,
        dm_destination_hash, dm_source_hash, full_secret.data(),
        D1L_MESHCORE_ORACLE_MAX_PATH_RETURN_EXTRA_BYTES + 1U,
        D1L_MESHCORE_ORACLE_MAX_PATH_BYTES,
        D1L_MESHCORE_ORACLE_MAX_PATH_RETURN_EXTRA_BYTES, false, false);
    expect_path_return_extra_parse_reject(
        "wrong destination", &valid_path_return_extra,
        static_cast<uint8_t>(dm_destination_hash ^ 1U), dm_source_hash,
        full_secret.data(), 4U, D1L_MESHCORE_ORACLE_MAX_PATH_BYTES,
        D1L_MESHCORE_ORACLE_MAX_PATH_RETURN_EXTRA_BYTES, false, false);
    expect_path_return_extra_parse_reject(
        "wrong source", &valid_path_return_extra, dm_destination_hash,
        static_cast<uint8_t>(dm_source_hash ^ 1U), full_secret.data(), 4U,
        D1L_MESHCORE_ORACLE_MAX_PATH_BYTES,
        D1L_MESHCORE_ORACLE_MAX_PATH_RETURN_EXTRA_BYTES, false, false);
    expect_path_return_extra_parse_reject(
        "wrong secret", &valid_path_return_extra, dm_destination_hash,
        dm_source_hash, public_secret.data(), 4U,
        D1L_MESHCORE_ORACLE_MAX_PATH_BYTES,
        D1L_MESHCORE_ORACLE_MAX_PATH_RETURN_EXTRA_BYTES, false, false);
    d1l_meshcore_oracle_packet_t malformed_path_return =
        valid_path_return_extra;
    malformed_path_return.header |= 0x40U;
    expect_path_return_extra_parse_reject(
        "future version", &malformed_path_return, dm_destination_hash,
        dm_source_hash, full_secret.data(), 4U,
        D1L_MESHCORE_ORACLE_MAX_PATH_BYTES,
        D1L_MESHCORE_ORACLE_MAX_PATH_RETURN_EXTRA_BYTES, false, false);
    malformed_path_return = valid_path_return_extra;
    malformed_path_return.header =
        static_cast<uint8_t>(PAYLOAD_TYPE_RESPONSE << PH_TYPE_SHIFT);
    expect_path_return_extra_parse_reject(
        "wrong packet type", &malformed_path_return, dm_destination_hash,
        dm_source_hash, full_secret.data(), 4U,
        D1L_MESHCORE_ORACLE_MAX_PATH_BYTES,
        D1L_MESHCORE_ORACLE_MAX_PATH_RETURN_EXTRA_BYTES, false, false);
    malformed_path_return = valid_path_return_extra;
    malformed_path_return.payload_len -= 1U;
    expect_path_return_extra_parse_reject(
        "truncated packet", &malformed_path_return, dm_destination_hash,
        dm_source_hash, full_secret.data(), 4U,
        D1L_MESHCORE_ORACLE_MAX_PATH_BYTES,
        D1L_MESHCORE_ORACLE_MAX_PATH_RETURN_EXTRA_BYTES, false, false);
    malformed_path_return = valid_path_return_extra;
    malformed_path_return.payload[malformed_path_return.payload_len++] = 0U;
    expect_path_return_extra_parse_reject(
        "non-block packet", &malformed_path_return, dm_destination_hash,
        dm_source_hash, full_secret.data(), 4U,
        D1L_MESHCORE_ORACLE_MAX_PATH_BYTES,
        D1L_MESHCORE_ORACLE_MAX_PATH_RETURN_EXTRA_BYTES, false, false);
    malformed_path_return = valid_path_return_extra;
    malformed_path_return.payload[2] ^= 1U;
    expect_path_return_extra_parse_reject(
        "tampered MAC", &malformed_path_return, dm_destination_hash,
        dm_source_hash, full_secret.data(), 4U,
        D1L_MESHCORE_ORACLE_MAX_PATH_BYTES,
        D1L_MESHCORE_ORACLE_MAX_PATH_RETURN_EXTRA_BYTES, false, false);
    malformed_path_return = valid_path_return_extra;
    malformed_path_return.payload[4] ^= 1U;
    expect_path_return_extra_parse_reject(
        "tampered ciphertext", &malformed_path_return, dm_destination_hash,
        dm_source_hash, full_secret.data(), 4U,
        D1L_MESHCORE_ORACLE_MAX_PATH_BYTES,
        D1L_MESHCORE_ORACLE_MAX_PATH_RETURN_EXTRA_BYTES, false, false);
    expect_path_return_extra_parse_reject(
        "short expected extra", &valid_path_return_extra,
        dm_destination_hash, dm_source_hash, full_secret.data(), 3U,
        D1L_MESHCORE_ORACLE_MAX_PATH_BYTES,
        D1L_MESHCORE_ORACLE_MAX_PATH_RETURN_EXTRA_BYTES, false, false);
    expect_path_return_extra_parse_reject(
        "undersized path output", &valid_ack_path, dm_destination_hash,
        dm_source_hash, full_secret.data(),
        D1L_MESHCORE_ORACLE_DM_ACK_BYTES, 3U,
        D1L_MESHCORE_ORACLE_MAX_PATH_RETURN_EXTRA_BYTES, false, false);
    expect_path_return_extra_parse_reject(
        "undersized extra output", &valid_path_return_extra,
        dm_destination_hash, dm_source_hash, full_secret.data(), 4U,
        D1L_MESHCORE_ORACLE_MAX_PATH_BYTES, 3U, false, false);
    expect_path_return_extra_parse_reject(
        "null encoded path output", &valid_path_return_extra,
        dm_destination_hash, dm_source_hash, full_secret.data(), 4U,
        D1L_MESHCORE_ORACLE_MAX_PATH_BYTES,
        D1L_MESHCORE_ORACLE_MAX_PATH_RETURN_EXTRA_BYTES, true, false);
    expect_path_return_extra_parse_reject(
        "null extra type output", &valid_path_return_extra,
        dm_destination_hash, dm_source_hash, full_secret.data(), 4U,
        D1L_MESHCORE_ORACLE_MAX_PATH_BYTES,
        D1L_MESHCORE_ORACLE_MAX_PATH_RETURN_EXTRA_BYTES, false, true);

    std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_PATH_BYTES>
        rejected_unique_path{};
    rejected_unique_path.fill(0xA7U);
    uint8_t rejected_unique_path_len = 0xAAU;
    size_t rejected_unique_path_bytes = 0xBEEFU;
    std::array<uint8_t, D1L_MESHCORE_ORACLE_PATH_RETURN_UNIQUENESS_BYTES>
        rejected_uniqueness{};
    rejected_uniqueness.fill(0xC8U);
    const auto rejected_unique_path_before = rejected_unique_path;
    const auto rejected_uniqueness_before = rejected_uniqueness;
    auto expect_path_return_unique_parse_reject =
        [&](const char *name, const d1l_meshcore_oracle_packet_t *packet,
            uint8_t *uniqueness) {
            rejected_unique_path = rejected_unique_path_before;
            rejected_unique_path_len = 0xAAU;
            rejected_unique_path_bytes = 0xBEEFU;
            rejected_uniqueness = rejected_uniqueness_before;
            if (d1l_meshcore_oracle_parse_path_return_unique_packet(
                    packet, dm_destination_hash, dm_source_hash,
                    full_secret.data(), &rejected_unique_path_len,
                    rejected_unique_path.data(), rejected_unique_path.size(),
                    &rejected_unique_path_bytes, uniqueness) ||
                rejected_unique_path != rejected_unique_path_before ||
                rejected_unique_path_len != 0xAAU ||
                rejected_unique_path_bytes != 0xBEEFU ||
                rejected_uniqueness != rejected_uniqueness_before) {
                failures.push_back(std::string(name) +
                                   " PATH-return unique parse reject changed output");
            }
        };
    expect_path_return_unique_parse_reject(
        "wrong unique marker", &valid_path_return_extra,
        rejected_uniqueness.data());
    expect_path_return_unique_parse_reject(
        "null unique output", &valid_path_return_unique, nullptr);
    std::array<uint8_t, 16U> malformed_unique_plaintext{};
    malformed_unique_plaintext[0] = 0xC0U;
    malformed_unique_plaintext[1] = 0xFFU;
    std::memcpy(&malformed_unique_plaintext[2], valid_uniqueness.data(),
                valid_uniqueness.size());
    malformed_path_return = make_raw_ack_path_packet(
        malformed_unique_plaintext.data(), malformed_unique_plaintext.size());
    expect_path_return_unique_parse_reject(
        "reserved embedded unique path", &malformed_path_return,
        rejected_uniqueness.data());
    malformed_unique_plaintext.fill(0U);
    malformed_unique_plaintext[0] = 0x3FU;
    malformed_path_return = make_raw_ack_path_packet(
        malformed_unique_plaintext.data(), malformed_unique_plaintext.size());
    expect_path_return_unique_parse_reject(
        "short embedded unique path", &malformed_path_return,
        rejected_uniqueness.data());
    malformed_unique_plaintext.fill(0U);
    malformed_unique_plaintext[1] = 0xFFU;
    std::memcpy(&malformed_unique_plaintext[2], valid_uniqueness.data(),
                valid_uniqueness.size());
    malformed_unique_plaintext[6] = 1U;
    malformed_path_return = make_raw_ack_path_packet(
        malformed_unique_plaintext.data(), malformed_unique_plaintext.size());
    expect_path_return_unique_parse_reject(
        "noncanonical unique padding", &malformed_path_return,
        rejected_uniqueness.data());

    uint8_t priority = 0xA5U;
    d1l_meshcore_oracle_packet_t route_packet =
        make_route_packet(PAYLOAD_TYPE_TXT_MSG);
    if (!d1l_meshcore_oracle_prepare_flood(&route_packet, 1U, 0U, nullptr,
                                           &priority)) {
        failures.push_back("plain flood preparation rejected");
    } else {
        d1l_meshcore_oracle_packet_t expected =
            make_route_packet(PAYLOAD_TYPE_TXT_MSG);
        expected.header = static_cast<uint8_t>(
            (PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT) | ROUTE_TYPE_FLOOD);
        expected.transport_codes[0] = 0U;
        expected.transport_codes[1] = 0U;
        expected.path_len = 0x00U;
        check_prepared_packet("plain flood", route_packet, expected, priority,
                              1U);
    }

    route_packet = make_route_packet(PAYLOAD_TYPE_PATH);
    priority = 0xA5U;
    if (!d1l_meshcore_oracle_prepare_flood(&route_packet, 2U, 0U, nullptr,
                                           &priority)) {
        failures.push_back("PATH flood preparation rejected");
    } else {
        d1l_meshcore_oracle_packet_t expected =
            make_route_packet(PAYLOAD_TYPE_PATH);
        expected.header = static_cast<uint8_t>(
            (PAYLOAD_TYPE_PATH << PH_TYPE_SHIFT) | ROUTE_TYPE_FLOOD);
        expected.transport_codes[0] = 0U;
        expected.transport_codes[1] = 0U;
        expected.path_len = 0x40U;
        check_prepared_packet("PATH flood", route_packet, expected, priority,
                              2U);
    }

    const uint16_t transport_codes[2] = {0x1234U, 0xBEEFU};
    route_packet = make_route_packet(PAYLOAD_TYPE_ADVERT);
    priority = 0xA5U;
    if (!d1l_meshcore_oracle_prepare_flood(
            &route_packet, 3U, 1U, transport_codes, &priority)) {
        failures.push_back("transport advert flood preparation rejected");
    } else {
        d1l_meshcore_oracle_packet_t expected =
            make_route_packet(PAYLOAD_TYPE_ADVERT);
        expected.header = static_cast<uint8_t>(
            (PAYLOAD_TYPE_ADVERT << PH_TYPE_SHIFT) |
            ROUTE_TYPE_TRANSPORT_FLOOD);
        expected.transport_codes[0] = transport_codes[0];
        expected.transport_codes[1] = transport_codes[1];
        expected.path_len = 0x80U;
        check_prepared_packet("transport advert flood", route_packet, expected,
                              priority, 3U);
    }

    const std::array<uint8_t, 1> direct_path = {0x71U};
    route_packet = make_route_packet(PAYLOAD_TYPE_ACK);
    priority = 0xA5U;
    if (!d1l_meshcore_oracle_prepare_direct(
            &route_packet, direct_path.data(), 0x01U, &priority)) {
        failures.push_back("direct ACK preparation rejected");
    } else {
        d1l_meshcore_oracle_packet_t expected =
            make_route_packet(PAYLOAD_TYPE_ACK);
        expected.header = static_cast<uint8_t>(
            (PAYLOAD_TYPE_ACK << PH_TYPE_SHIFT) | ROUTE_TYPE_DIRECT);
        expected.transport_codes[0] = 0U;
        expected.transport_codes[1] = 0U;
        expected.path_len = 0x01U;
        expected.path[0] = direct_path[0];
        check_prepared_packet("direct ACK", route_packet, expected, priority,
                              0U);
    }

    const std::array<uint8_t, 4> wide_direct_path = {
        0x11U, 0x22U, 0x33U, 0x44U};
    route_packet = make_route_packet(PAYLOAD_TYPE_PATH);
    priority = 0xA5U;
    if (!d1l_meshcore_oracle_prepare_direct(
            &route_packet, wide_direct_path.data(), 0x42U, &priority)) {
        failures.push_back("direct PATH preparation rejected");
    } else {
        d1l_meshcore_oracle_packet_t expected =
            make_route_packet(PAYLOAD_TYPE_PATH);
        expected.header = static_cast<uint8_t>(
            (PAYLOAD_TYPE_PATH << PH_TYPE_SHIFT) | ROUTE_TYPE_DIRECT);
        expected.transport_codes[0] = 0U;
        expected.transport_codes[1] = 0U;
        expected.path_len = 0x42U;
        std::memcpy(expected.path, wide_direct_path.data(),
                    wide_direct_path.size());
        check_prepared_packet("direct PATH", route_packet, expected, priority,
                              1U);
    }

    route_packet = make_route_packet(PAYLOAD_TYPE_TXT_MSG);
    priority = 0xA5U;
    if (!d1l_meshcore_oracle_prepare_zero_hop(&route_packet, 0U, nullptr,
                                              &priority)) {
        failures.push_back("zero-hop direct preparation rejected");
    } else {
        d1l_meshcore_oracle_packet_t expected =
            make_route_packet(PAYLOAD_TYPE_TXT_MSG);
        expected.header = static_cast<uint8_t>(
            (PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT) | ROUTE_TYPE_DIRECT);
        expected.transport_codes[0] = 0U;
        expected.transport_codes[1] = 0U;
        expected.path_len = 0x00U;
        check_prepared_packet("zero-hop direct", route_packet, expected,
                              priority, 0U);
    }

    route_packet = make_route_packet(PAYLOAD_TYPE_ACK);
    priority = 0xA5U;
    if (!d1l_meshcore_oracle_prepare_zero_hop(
            &route_packet, 1U, transport_codes, &priority)) {
        failures.push_back("transport zero-hop preparation rejected");
    } else {
        d1l_meshcore_oracle_packet_t expected =
            make_route_packet(PAYLOAD_TYPE_ACK);
        expected.header = static_cast<uint8_t>(
            (PAYLOAD_TYPE_ACK << PH_TYPE_SHIFT) |
            ROUTE_TYPE_TRANSPORT_DIRECT);
        expected.transport_codes[0] = transport_codes[0];
        expected.transport_codes[1] = transport_codes[1];
        expected.path_len = 0x00U;
        check_prepared_packet("transport zero-hop", route_packet, expected,
                              priority, 0U);
    }

    auto flood_rejects_without_mutation =
        [&failures](const char *name, uint8_t payload_type,
                    uint8_t hash_size, uint8_t use_transport,
                    const uint16_t *codes) {
            d1l_meshcore_oracle_packet_t packet =
                make_route_packet(payload_type);
            const d1l_meshcore_oracle_packet_t before = packet;
            uint8_t rejected_priority = 0xA5U;
            if (d1l_meshcore_oracle_prepare_flood(
                    &packet, hash_size, use_transport, codes,
                    &rejected_priority) ||
                !packets_equal(packet, before) || rejected_priority != 0xA5U) {
                failures.push_back(std::string(name) +
                                   " flood rejection mutated output");
            }
        };
    flood_rejects_without_mutation("zero hash size", PAYLOAD_TYPE_ACK, 0U,
                                   0U, nullptr);
    flood_rejects_without_mutation("oversized hash size", PAYLOAD_TYPE_ACK,
                                   4U, 0U, nullptr);
    flood_rejects_without_mutation("TRACE", PAYLOAD_TYPE_TRACE, 1U, 0U,
                                   nullptr);
    flood_rejects_without_mutation("invalid transport flag", PAYLOAD_TYPE_ACK,
                                   1U, 2U, transport_codes);

    auto direct_rejects_without_mutation =
        [&failures](const char *name, uint8_t payload_type,
                    const uint8_t *path, uint8_t path_len) {
            d1l_meshcore_oracle_packet_t packet =
                make_route_packet(payload_type);
            const d1l_meshcore_oracle_packet_t before = packet;
            uint8_t rejected_priority = 0xA5U;
            if (d1l_meshcore_oracle_prepare_direct(
                    &packet, path, path_len, &rejected_priority) ||
                !packets_equal(packet, before) || rejected_priority != 0xA5U) {
                failures.push_back(std::string(name) +
                                   " direct rejection mutated output");
            }
        };
    direct_rejects_without_mutation("reserved path", PAYLOAD_TYPE_ACK,
                                    direct_path.data(), 0xC0U);
    direct_rejects_without_mutation("null path", PAYLOAD_TYPE_ACK, nullptr,
                                    0x01U);
    direct_rejects_without_mutation("TRACE", PAYLOAD_TYPE_TRACE,
                                    direct_path.data(), 0x01U);

    auto zero_hop_rejects_without_mutation =
        [&failures](const char *name, uint8_t payload_type,
                    uint8_t use_transport,
                    const uint16_t *codes) {
            d1l_meshcore_oracle_packet_t packet =
                make_route_packet(payload_type);
            const d1l_meshcore_oracle_packet_t before = packet;
            uint8_t rejected_priority = 0xA5U;
            if (d1l_meshcore_oracle_prepare_zero_hop(
                    &packet, use_transport, codes, &rejected_priority) ||
                !packets_equal(packet, before) || rejected_priority != 0xA5U) {
                failures.push_back(std::string(name) +
                                   " zero-hop rejection mutated output");
            }
        };
    zero_hop_rejects_without_mutation("invalid transport flag",
                                      PAYLOAD_TYPE_ACK, 2U, transport_codes);
    zero_hop_rejects_without_mutation("missing transport codes",
                                      PAYLOAD_TYPE_ACK, 1U, nullptr);
    zero_hop_rejects_without_mutation("TRACE", PAYLOAD_TYPE_TRACE, 0U,
                                      nullptr);

    std::vector<uint8_t> maximum_simple_ack(
        D1L_MESHCORE_ORACLE_MAX_PAYLOAD_BYTES);
    for (size_t index = 0U; index < maximum_simple_ack.size(); ++index) {
        maximum_simple_ack[index] = static_cast<uint8_t>(index ^ 0xA5U);
    }
    std::vector<uint8_t> maximum_multi_ack(
        D1L_MESHCORE_ORACLE_MAX_PAYLOAD_BYTES - 1U);
    for (size_t index = 0U; index < maximum_multi_ack.size(); ++index) {
        maximum_multi_ack[index] = static_cast<uint8_t>(index ^ 0x5AU);
    }
    const std::array<AckVector, kAckRoundtripVectors> ack_vectors = {{
        {{0x78U, 0x56U, 0x34U, 0x12U}, 0U, false},
        {{0xA7U, 0xB8U, 0xC9U, 0xDAU, 0xEBU}, 0U, false},
        {maximum_simple_ack, 0U, false},
        {{0xEFU, 0xBEU, 0xADU, 0xDEU}, 1U, true},
        {maximum_multi_ack, 15U, true},
    }};
    for (size_t index = 0U; index < ack_vectors.size(); ++index) {
        const AckVector &vector = ack_vectors[index];
        d1l_meshcore_oracle_packet_t packet{};
        const bool created = vector.multipart
            ? d1l_meshcore_oracle_create_multi_ack(
                  vector.ack.data(), vector.ack.size(), vector.remaining,
                  &packet)
            : d1l_meshcore_oracle_create_ack(
                  vector.ack.data(), vector.ack.size(), &packet);
        const uint8_t expected_type = vector.multipart
            ? PAYLOAD_TYPE_MULTIPART
            : PAYLOAD_TYPE_ACK;
        const size_t payload_offset = vector.multipart ? 1U : 0U;
        if (!created ||
            packet.header !=
                static_cast<uint8_t>(expected_type << PH_TYPE_SHIFT) ||
            packet.transport_codes[0] != 0U ||
            packet.transport_codes[1] != 0U || packet.path_len != 0U ||
            packet.payload_len != vector.ack.size() + payload_offset ||
            (vector.multipart &&
             packet.payload[0] !=
                 static_cast<uint8_t>((vector.remaining << 4U) |
                                      PAYLOAD_TYPE_ACK)) ||
            std::memcmp(&packet.payload[payload_offset], vector.ack.data(),
                        vector.ack.size()) != 0) {
            failures.push_back("ACK create changed vector " +
                               std::to_string(index));
            continue;
        }

        d1l_meshcore_oracle_packet_t routed = packet;
        uint8_t route_priority = 0xFFU;
        if (!d1l_meshcore_oracle_prepare_zero_hop(
                &routed, 0U, nullptr, &route_priority) ||
            (routed.header & PH_ROUTE_MASK) != ROUTE_TYPE_DIRECT ||
            route_priority != 0U) {
            failures.push_back("ACK route preparation rejected vector " +
                               std::to_string(index));
            continue;
        }

        std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_RAW_BYTES> raw{};
        size_t raw_len = 0U;
        d1l_meshcore_oracle_packet_t decoded{};
        if (!d1l_meshcore_oracle_packet_encode(
                &routed, raw.data(), raw.size(), &raw_len) ||
            !d1l_meshcore_oracle_packet_decode(raw.data(), raw_len, &decoded) ||
            !packets_equal(routed, decoded)) {
            failures.push_back("ACK packet did not round trip vector " +
                               std::to_string(index));
            continue;
        }

        std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_PAYLOAD_BYTES> parsed_ack{};
        size_t parsed_ack_len = 0U;
        uint8_t parsed_remaining = 0xFFU;
        uint8_t parsed_multipart = 0xFFU;
        if (!d1l_meshcore_oracle_parse_ack(
                &decoded, parsed_ack.data(), parsed_ack.size(),
                &parsed_ack_len, &parsed_remaining, &parsed_multipart) ||
            parsed_ack_len != vector.ack.size() ||
            std::memcmp(parsed_ack.data(), vector.ack.data(),
                        vector.ack.size()) != 0 ||
            parsed_remaining != vector.remaining ||
            parsed_multipart != (vector.multipart ? 1U : 0U)) {
            failures.push_back("ACK parse changed vector " +
                               std::to_string(index));
            continue;
        }

        d1l_meshcore_oracle_packet_t recreated{};
        const bool recreated_ok = parsed_multipart != 0U
            ? d1l_meshcore_oracle_create_multi_ack(
                  parsed_ack.data(), parsed_ack_len, parsed_remaining,
                  &recreated)
            : d1l_meshcore_oracle_create_ack(
                  parsed_ack.data(), parsed_ack_len, &recreated);
        if (!recreated_ok || !packets_equal(packet, recreated)) {
            failures.push_back("ACK recreate changed vector " +
                               std::to_string(index));
        }
    }

    std::array<uint8_t,
               D1L_MESHCORE_ORACLE_MAX_PAYLOAD_BYTES + 1U> oversized_ack{};
    d1l_meshcore_oracle_packet_t rejected_ack_packet;
    std::memset(&rejected_ack_packet, 0xA8, sizeof(rejected_ack_packet));
    const d1l_meshcore_oracle_packet_t rejected_ack_before =
        rejected_ack_packet;
    const std::array<uint8_t, 4> valid_ack = {
        0x78U, 0x56U, 0x34U, 0x12U};
    const std::array<uint8_t, 3> short_ack = {0x01U, 0x02U, 0x03U};
    auto ack_packet_was_preserved = [&]() {
        return std::memcmp(&rejected_ack_packet, &rejected_ack_before,
                           sizeof(rejected_ack_packet)) == 0;
    };
    if (d1l_meshcore_oracle_create_ack(
            nullptr, 1U, &rejected_ack_packet) ||
        !ack_packet_was_preserved()) {
        failures.push_back("null simple ACK changed output");
    }
    if (d1l_meshcore_oracle_create_ack(
            valid_ack.data(), 0U, &rejected_ack_packet) ||
        !ack_packet_was_preserved()) {
        failures.push_back("empty simple ACK changed output");
    }
    if (d1l_meshcore_oracle_create_ack(
            short_ack.data(), short_ack.size(), &rejected_ack_packet) ||
        !ack_packet_was_preserved()) {
        failures.push_back("short simple ACK changed output");
    }
    if (d1l_meshcore_oracle_create_ack(
            oversized_ack.data(), oversized_ack.size(),
            &rejected_ack_packet) ||
        !ack_packet_was_preserved()) {
        failures.push_back("oversized simple ACK changed output");
    }
    if (d1l_meshcore_oracle_create_ack(
            valid_ack.data(), valid_ack.size(), nullptr)) {
        failures.push_back("null simple ACK packet accepted");
    }
    if (d1l_meshcore_oracle_create_multi_ack(
            nullptr, 1U, 0U, &rejected_ack_packet) ||
        !ack_packet_was_preserved()) {
        failures.push_back("null multipart ACK changed output");
    }
    if (d1l_meshcore_oracle_create_multi_ack(
            valid_ack.data(), 0U, 0U, &rejected_ack_packet) ||
        !ack_packet_was_preserved()) {
        failures.push_back("empty multipart ACK changed output");
    }
    if (d1l_meshcore_oracle_create_multi_ack(
            short_ack.data(), short_ack.size(), 0U,
            &rejected_ack_packet) ||
        !ack_packet_was_preserved()) {
        failures.push_back("short multipart ACK changed output");
    }
    if (d1l_meshcore_oracle_create_multi_ack(
            oversized_ack.data(), D1L_MESHCORE_ORACLE_MAX_PAYLOAD_BYTES,
            0U, &rejected_ack_packet) ||
        !ack_packet_was_preserved()) {
        failures.push_back("oversized multipart ACK changed output");
    }
    if (d1l_meshcore_oracle_create_multi_ack(
            valid_ack.data(), valid_ack.size(), 16U,
            &rejected_ack_packet) ||
        !ack_packet_was_preserved()) {
        failures.push_back("overflow multipart remaining changed output");
    }

    auto expect_ack_parse_reject =
        [&failures](const char *name,
                    const d1l_meshcore_oracle_packet_t &packet,
                    size_t ack_capacity) {
            std::array<uint8_t,
                       D1L_MESHCORE_ORACLE_MAX_PAYLOAD_BYTES> parsed_ack;
            parsed_ack.fill(0xE2U);
            const auto parsed_ack_before = parsed_ack;
            size_t parsed_ack_len = 0xBEEFU;
            uint8_t parsed_remaining = 0xA6U;
            uint8_t parsed_multipart = 0xA7U;
            if (d1l_meshcore_oracle_parse_ack(
                    &packet, parsed_ack.data(), ack_capacity, &parsed_ack_len,
                    &parsed_remaining, &parsed_multipart) ||
                parsed_ack != parsed_ack_before || parsed_ack_len != 0xBEEFU ||
                parsed_remaining != 0xA6U || parsed_multipart != 0xA7U) {
                failures.push_back(std::string(name) +
                                   " ACK parse changed output");
            }
        };
    d1l_meshcore_oracle_packet_t malformed_ack =
        make_route_packet(PAYLOAD_TYPE_TXT_MSG);
    expect_ack_parse_reject("wrong payload type", malformed_ack,
                            D1L_MESHCORE_ORACLE_MAX_PAYLOAD_BYTES);
    malformed_ack = make_route_packet(PAYLOAD_TYPE_ACK);
    malformed_ack.payload_len = 3U;
    expect_ack_parse_reject("short simple ACK", malformed_ack,
                            D1L_MESHCORE_ORACLE_MAX_PAYLOAD_BYTES);
    malformed_ack = make_route_packet(PAYLOAD_TYPE_MULTIPART);
    malformed_ack.payload[0] =
        static_cast<uint8_t>((1U << 4U) | PAYLOAD_TYPE_GRP_DATA);
    expect_ack_parse_reject("wrong multipart subtype", malformed_ack,
                            D1L_MESHCORE_ORACLE_MAX_PAYLOAD_BYTES);
    malformed_ack = make_route_packet(PAYLOAD_TYPE_MULTIPART);
    malformed_ack.payload_len = 1U;
    malformed_ack.payload[0] = PAYLOAD_TYPE_ACK;
    expect_ack_parse_reject("empty multipart ACK", malformed_ack,
                            D1L_MESHCORE_ORACLE_MAX_PAYLOAD_BYTES);
    malformed_ack = make_route_packet(PAYLOAD_TYPE_MULTIPART);
    malformed_ack.payload[0] = PAYLOAD_TYPE_ACK;
    expect_ack_parse_reject("short multipart ACK", malformed_ack,
                            D1L_MESHCORE_ORACLE_MAX_PAYLOAD_BYTES);
    malformed_ack = make_route_packet(PAYLOAD_TYPE_ACK);
    malformed_ack.header |= static_cast<uint8_t>(PAYLOAD_VER_2 << PH_VER_SHIFT);
    expect_ack_parse_reject("future-version ACK", malformed_ack,
                            D1L_MESHCORE_ORACLE_MAX_PAYLOAD_BYTES);
    malformed_ack = make_route_packet(PAYLOAD_TYPE_ACK);
    expect_ack_parse_reject("undersized ACK destination", malformed_ack,
                            malformed_ack.payload_len - 1U);

    std::vector<uint8_t> maximum_trace_path(
        D1L_MESHCORE_ORACLE_MAX_TRACE_PATH_BYTES);
    for (size_t index = 0U; index < maximum_trace_path.size(); ++index) {
        maximum_trace_path[index] = static_cast<uint8_t>(index ^ 0x3CU);
    }
    const std::array<TraceVector, kTraceRoundtripVectors> trace_vectors = {{
        {0U, 0U, 0x00U, {}},
        {0x12345678U, 0xAABBCCDDU, 0x00U, {0x42U}},
        {0x87654321U, 0x10203040U, 0x00U,
         {0x11U, 0x12U, 0x21U, 0x22U}},
        {0xDEADBEEFU, 0xCAFEBABEU, 0x00U,
         {0x01U, 0x02U, 0x03U, 0x04U,
          0x11U, 0x12U, 0x13U, 0x14U}},
        {0x0BADF00DU, 0x55AA55AAU, 0x00U,
         {0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U,
          0x10U, 0x11U, 0x12U, 0x13U, 0x14U, 0x15U, 0x16U, 0x17U}},
        {0xFFFFFFFFU, 0x01020304U, 0x00U, maximum_trace_path},
    }};
    for (size_t index = 0U; index < trace_vectors.size(); ++index) {
        const TraceVector &vector = trace_vectors[index];
        d1l_meshcore_oracle_packet_t trace{};
        const std::array<uint8_t,
                         D1L_MESHCORE_ORACLE_TRACE_FIXED_BYTES> expected_prefix = {{
            static_cast<uint8_t>(vector.tag),
            static_cast<uint8_t>(vector.tag >> 8U),
            static_cast<uint8_t>(vector.tag >> 16U),
            static_cast<uint8_t>(vector.tag >> 24U),
            static_cast<uint8_t>(vector.auth_code),
            static_cast<uint8_t>(vector.auth_code >> 8U),
            static_cast<uint8_t>(vector.auth_code >> 16U),
            static_cast<uint8_t>(vector.auth_code >> 24U),
            vector.flags,
        }};
        if (!d1l_meshcore_oracle_create_trace(
                vector.tag, vector.auth_code, vector.flags, &trace) ||
            trace.header !=
                static_cast<uint8_t>(PAYLOAD_TYPE_TRACE << PH_TYPE_SHIFT) ||
            trace.transport_codes[0] != 0U ||
            trace.transport_codes[1] != 0U || trace.path_len != 0U ||
            trace.payload_len != D1L_MESHCORE_ORACLE_TRACE_FIXED_BYTES ||
            std::memcmp(trace.payload, expected_prefix.data(),
                        expected_prefix.size()) != 0) {
            failures.push_back("TRACE create changed vector " +
                               std::to_string(index));
            continue;
        }
        const d1l_meshcore_oracle_packet_t pre_route = trace;
        uint8_t trace_priority = 0xFFU;
        const uint8_t *path_hashes = vector.path_hashes.empty()
            ? nullptr
            : vector.path_hashes.data();
        if (!d1l_meshcore_oracle_prepare_trace_direct(
                &trace, path_hashes, vector.path_hashes.size(),
                &trace_priority) ||
            trace.header != static_cast<uint8_t>(
                (PAYLOAD_TYPE_TRACE << PH_TYPE_SHIFT) | ROUTE_TYPE_DIRECT) ||
            trace.transport_codes[0] != 0U ||
            trace.transport_codes[1] != 0U || trace.path_len != 0U ||
            trace.payload_len != D1L_MESHCORE_ORACLE_TRACE_FIXED_BYTES +
                                     vector.path_hashes.size() ||
            trace_priority != 5U ||
            (!vector.path_hashes.empty() &&
             std::memcmp(
                 &trace.payload[D1L_MESHCORE_ORACLE_TRACE_FIXED_BYTES],
                 vector.path_hashes.data(), vector.path_hashes.size()) != 0)) {
            failures.push_back("TRACE direct preparation changed vector " +
                               std::to_string(index));
            continue;
        }

        std::array<uint8_t, D1L_MESHCORE_ORACLE_MAX_RAW_BYTES> trace_raw{};
        size_t trace_raw_len = 0U;
        d1l_meshcore_oracle_packet_t decoded_trace{};
        if (!d1l_meshcore_oracle_packet_encode(
                &trace, trace_raw.data(), trace_raw.size(), &trace_raw_len) ||
            !d1l_meshcore_oracle_packet_decode(
                trace_raw.data(), trace_raw_len, &decoded_trace) ||
            !packets_equal(trace, decoded_trace)) {
            failures.push_back("TRACE packet did not round trip vector " +
                               std::to_string(index));
            continue;
        }

        uint32_t parsed_tag = 0U;
        uint32_t parsed_auth_code = 0U;
        uint8_t parsed_flags = 0U;
        std::array<uint8_t,
                   D1L_MESHCORE_ORACLE_MAX_TRACE_PATH_BYTES> parsed_path{};
        size_t parsed_path_len = 0U;
        if (!d1l_meshcore_oracle_parse_trace_source(
                &decoded_trace, &parsed_tag, &parsed_auth_code, &parsed_flags,
                parsed_path.data(), parsed_path.size(), &parsed_path_len) ||
            parsed_tag != vector.tag ||
            parsed_auth_code != vector.auth_code ||
            parsed_flags != vector.flags ||
            parsed_path_len != vector.path_hashes.size() ||
            (!vector.path_hashes.empty() &&
             std::memcmp(parsed_path.data(), vector.path_hashes.data(),
                         vector.path_hashes.size()) != 0)) {
            failures.push_back("TRACE parse changed vector " +
                               std::to_string(index));
            continue;
        }

        d1l_meshcore_oracle_packet_t recreated_trace{};
        uint8_t recreated_priority = 0xFFU;
        if (!d1l_meshcore_oracle_create_trace(
                parsed_tag, parsed_auth_code, parsed_flags,
                &recreated_trace) ||
            !packets_equal(pre_route, recreated_trace) ||
            !d1l_meshcore_oracle_prepare_trace_direct(
                &recreated_trace, parsed_path.data(), parsed_path_len,
                &recreated_priority) ||
            recreated_priority != 5U ||
            !packets_equal(trace, recreated_trace)) {
            failures.push_back("TRACE recreate changed vector " +
                               std::to_string(index));
        }
    }

    if (d1l_meshcore_oracle_create_trace(1U, 2U, 0U, nullptr)) {
        failures.push_back("null TRACE packet accepted");
    }
    d1l_meshcore_oracle_packet_t rejected_trace_flags;
    std::memset(&rejected_trace_flags, 0xA9,
                sizeof(rejected_trace_flags));
    const d1l_meshcore_oracle_packet_t rejected_trace_flags_before =
        rejected_trace_flags;
    if (d1l_meshcore_oracle_create_trace(
            1U, 2U, 1U, &rejected_trace_flags) ||
        std::memcmp(&rejected_trace_flags, &rejected_trace_flags_before,
                    sizeof(rejected_trace_flags)) != 0) {
        failures.push_back("unsupported TRACE flags changed output");
    }
    d1l_meshcore_oracle_packet_t base_trace{};
    if (!d1l_meshcore_oracle_create_trace(
            0x12345678U, 0xAABBCCDDU, 0U, &base_trace)) {
        failures.push_back("TRACE invalid-vector setup rejected");
    }
    uint8_t rejected_trace_priority = 0xA5U;
    if (d1l_meshcore_oracle_prepare_trace_direct(
            nullptr, nullptr, 0U, &rejected_trace_priority) ||
        rejected_trace_priority != 0xA5U) {
        failures.push_back("null TRACE preparation changed priority");
    }
    d1l_meshcore_oracle_packet_t rejected_trace = base_trace;
    const d1l_meshcore_oracle_packet_t rejected_trace_before = rejected_trace;
    if (d1l_meshcore_oracle_prepare_trace_direct(
            &rejected_trace, nullptr, 0U, nullptr) ||
        !packets_equal(rejected_trace, rejected_trace_before)) {
        failures.push_back("null TRACE priority changed packet");
    }
    auto expect_trace_prepare_reject =
        [&failures](const char *name,
                    const d1l_meshcore_oracle_packet_t &input,
                    const uint8_t *path_hashes,
                    size_t path_hashes_len) {
            d1l_meshcore_oracle_packet_t packet = input;
            const d1l_meshcore_oracle_packet_t before = packet;
            uint8_t priority = 0xA5U;
            if (d1l_meshcore_oracle_prepare_trace_direct(
                    &packet, path_hashes, path_hashes_len, &priority) ||
                !packets_equal(packet, before) || priority != 0xA5U) {
                failures.push_back(std::string(name) +
                                   " TRACE preparation changed output");
            }
        };
    const std::array<uint8_t, 1> one_trace_hash = {0x42U};
    rejected_trace = base_trace;
    rejected_trace.header = static_cast<uint8_t>(
        (PAYLOAD_TYPE_ACK << PH_TYPE_SHIFT) | ROUTE_TYPE_TRANSPORT_FLOOD);
    expect_trace_prepare_reject("wrong type", rejected_trace, nullptr, 0U);
    rejected_trace = base_trace;
    rejected_trace.header |=
        static_cast<uint8_t>(PAYLOAD_VER_2 << PH_VER_SHIFT);
    expect_trace_prepare_reject("future version", rejected_trace, nullptr, 0U);
    rejected_trace = base_trace;
    rejected_trace.payload_len++;
    expect_trace_prepare_reject("non-base payload", rejected_trace, nullptr,
                                0U);
    rejected_trace = base_trace;
    rejected_trace.path_len = 0x01U;
    rejected_trace.path[0] = 0x42U;
    expect_trace_prepare_reject("pre-existing path", rejected_trace, nullptr,
                                0U);
    expect_trace_prepare_reject("null path hashes", base_trace, nullptr, 1U);
    std::array<uint8_t,
               D1L_MESHCORE_ORACLE_MAX_TRACE_PATH_BYTES + 1U> overlong_trace{};
    expect_trace_prepare_reject("overlong path hashes", base_trace,
                                overlong_trace.data(), overlong_trace.size());
    rejected_trace = base_trace;
    rejected_trace.payload[8] = 0x01U;
    const std::array<uint8_t, 7> unsupported_flags_path{};
    expect_trace_prepare_reject("unsupported flags", rejected_trace,
                                unsupported_flags_path.data(),
                                unsupported_flags_path.size());

    d1l_meshcore_oracle_packet_t routed_trace = base_trace;
    uint8_t routed_trace_priority = 0xFFU;
    if (!d1l_meshcore_oracle_prepare_trace_direct(
            &routed_trace, one_trace_hash.data(), one_trace_hash.size(),
            &routed_trace_priority)) {
        failures.push_back("TRACE parse invalid-vector setup rejected");
    }
    auto expect_trace_parse_reject =
        [&failures](const char *name,
                    const d1l_meshcore_oracle_packet_t &packet,
                    size_t path_capacity) {
            uint32_t tag = 0xAAAAAAAAU;
            uint32_t auth_code = 0xBBBBBBBBU;
            uint8_t flags = 0xCCU;
            std::array<uint8_t,
                       D1L_MESHCORE_ORACLE_MAX_TRACE_PATH_BYTES> path_hashes;
            path_hashes.fill(0xDDU);
            const auto path_hashes_before = path_hashes;
            size_t path_hashes_len = 0xBEEFU;
            if (d1l_meshcore_oracle_parse_trace_source(
                    &packet, &tag, &auth_code, &flags, path_hashes.data(),
                    path_capacity, &path_hashes_len) ||
                tag != 0xAAAAAAAAU || auth_code != 0xBBBBBBBBU ||
                flags != 0xCCU || path_hashes != path_hashes_before ||
                path_hashes_len != 0xBEEFU) {
                failures.push_back(std::string(name) +
                                   " TRACE parse changed output");
            }
        };
    rejected_trace = routed_trace;
    rejected_trace.header = static_cast<uint8_t>(
        (PAYLOAD_TYPE_ACK << PH_TYPE_SHIFT) | ROUTE_TYPE_DIRECT);
    expect_trace_parse_reject("wrong type", rejected_trace,
                              D1L_MESHCORE_ORACLE_MAX_TRACE_PATH_BYTES);
    expect_trace_parse_reject("pre-route", base_trace,
                              D1L_MESHCORE_ORACLE_MAX_TRACE_PATH_BYTES);
    rejected_trace = routed_trace;
    rejected_trace.header |=
        static_cast<uint8_t>(PAYLOAD_VER_2 << PH_VER_SHIFT);
    expect_trace_parse_reject("future version", rejected_trace,
                              D1L_MESHCORE_ORACLE_MAX_TRACE_PATH_BYTES);
    rejected_trace = routed_trace;
    rejected_trace.payload_len =
        D1L_MESHCORE_ORACLE_TRACE_FIXED_BYTES - 1U;
    expect_trace_parse_reject("short payload", rejected_trace,
                              D1L_MESHCORE_ORACLE_MAX_TRACE_PATH_BYTES);
    rejected_trace = routed_trace;
    rejected_trace.path_len = 0x01U;
    rejected_trace.path[0] = 0x42U;
    expect_trace_parse_reject("forwarded SNR path", rejected_trace,
                              D1L_MESHCORE_ORACLE_MAX_TRACE_PATH_BYTES);
    rejected_trace = routed_trace;
    rejected_trace.payload[8] = 0x01U;
    expect_trace_parse_reject("unsupported flags", rejected_trace,
                              D1L_MESHCORE_ORACLE_MAX_TRACE_PATH_BYTES);
    expect_trace_parse_reject("undersized path destination", routed_trace,
                              0U);
    uint32_t null_path_tag = 0xAAAAAAAAU;
    uint32_t null_path_auth = 0xBBBBBBBBU;
    uint8_t null_path_flags = 0xCCU;
    size_t null_path_len = 0xBEEFU;
    if (d1l_meshcore_oracle_parse_trace_source(
            &routed_trace, &null_path_tag, &null_path_auth, &null_path_flags,
            nullptr, D1L_MESHCORE_ORACLE_MAX_TRACE_PATH_BYTES,
            &null_path_len) ||
        null_path_tag != 0xAAAAAAAAU || null_path_auth != 0xBBBBBBBBU ||
        null_path_flags != 0xCCU || null_path_len != 0xBEEFU) {
        failures.push_back("null path TRACE parse changed output");
    }

    const bool passed = failures.empty();
    for (const std::string &failure : failures) {
        std::cerr << failure << '\n';
    }
    std::cout << "{\"passed\":" << (passed ? "true" : "false")
              << ",\"coverage_boundary\":"
                  "\"pinned_upstream_packet_advert_group_dm_expected_ack_path_return_route_codes_ack_trace_and_signed_advert_creation_strict_verification_and_anonymous_login_request_and_regular_request_response_crypto\""
              << ",\"wp04_closure_eligible\":false"
              << ",\"abi_version\":" << D1L_MESHCORE_ORACLE_ABI_VERSION
              << ",\"upstream_commit\":\""
              << D1L_MESHCORE_ORACLE_UPSTREAM_COMMIT << "\""
              << ",\"vectors\":{\"roundtrip\":"
              << (kPacketRoundtripVectors + kAdvertRoundtripVectors +
                  kSignedAdvertPacketRoundtripVectors +
                   kGroupRoundtripVectors + kLoginRequestRoundtripVectors +
                   kRequestResponseRoundtripVectors +
                  kDmRoundtripVectors +
                  kExpectedAckPathRoundtripVectors +
                  kPathReturnRoundtripVectors +
                  kRouteRoundtripVectors + kAckRoundtripVectors +
                  kTraceRoundtripVectors)
              << ",\"valid\":"
              << (kSignedAdvertValidVectors + kVerifierKatValidVectors +
                  kPointValidationValidVectors + kCryptoAdapterKatValidVectors +
                  kExpectedAckValidVectors)
              << ",\"invalid\":"
              << (kPacketInvalidVectors + kAdvertInvalidVectors +
                  kSignedAdvertPacketInvalidVectors +
                  kSignedAdvertInvalidVectors + kVerifierKatInvalidVectors +
                  kPointValidationInvalidVectors +
                   kGroupInvalidVectors + kLoginRequestInvalidVectors +
                   kRequestResponseInvalidVectors +
                  kDmInvalidVectors +
                  kExpectedAckInvalidVectors +
                  kPathReturnInvalidVectors +
                  kRouteInvalidVectors + kAckInvalidVectors +
                  kTraceInvalidVectors)
              << ",\"semantic\":"
              << (kAdvertRoundtripVectors + kAdvertInvalidVectors +
                  kSignedAdvertPacketRoundtripVectors +
                  kSignedAdvertPacketInvalidVectors +
                  kSignedAdvertValidVectors +
                  kSignedAdvertInvalidVectors +
                  kPointValidationValidVectors +
                  kPointValidationInvalidVectors +
                  kGroupRoundtripVectors + kGroupInvalidVectors +
                   kLoginRequestRoundtripVectors +
                   kLoginRequestInvalidVectors +
                   kRequestResponseRoundtripVectors +
                   kRequestResponseInvalidVectors +
                  kDmRoundtripVectors + kDmInvalidVectors +
                  kExpectedAckValidVectors +
                  kExpectedAckPathRoundtripVectors +
                  kExpectedAckInvalidVectors +
                  kPathReturnRoundtripVectors +
                  kPathReturnInvalidVectors +
                  kRouteRoundtripVectors + kRouteInvalidVectors +
                  kAckRoundtripVectors + kAckInvalidVectors +
                  kTraceRoundtripVectors + kTraceInvalidVectors)
              << ",\"total\":"
              << (kPacketRoundtripVectors + kPacketInvalidVectors +
                  kAdvertRoundtripVectors + kAdvertInvalidVectors +
                  kSignedAdvertPacketRoundtripVectors +
                  kSignedAdvertPacketInvalidVectors +
                  kSignedAdvertValidVectors + kSignedAdvertInvalidVectors +
                  kVerifierKatValidVectors + kVerifierKatInvalidVectors +
                  kPointValidationValidVectors +
                  kPointValidationInvalidVectors +
                  kCryptoAdapterKatValidVectors +
                  kGroupRoundtripVectors + kGroupInvalidVectors +
                   kLoginRequestRoundtripVectors +
                   kLoginRequestInvalidVectors +
                   kRequestResponseRoundtripVectors +
                   kRequestResponseInvalidVectors +
                  kDmRoundtripVectors + kDmInvalidVectors +
                  kExpectedAckValidVectors +
                  kExpectedAckPathRoundtripVectors +
                  kExpectedAckInvalidVectors +
                  kPathReturnRoundtripVectors +
                  kPathReturnInvalidVectors +
                  kRouteRoundtripVectors + kRouteInvalidVectors +
                  kAckRoundtripVectors + kAckInvalidVectors +
                  kTraceRoundtripVectors + kTraceInvalidVectors)
              << ",\"packet_envelope\":{\"roundtrip\":"
              << kPacketRoundtripVectors << ",\"invalid\":"
              << kPacketInvalidVectors << ",\"semantic\":0,\"total\":"
              << (kPacketRoundtripVectors + kPacketInvalidVectors) << "}"
              << ",\"advert_data_fields\":{\"roundtrip\":"
              << kAdvertRoundtripVectors << ",\"invalid\":"
              << kAdvertInvalidVectors << ",\"semantic\":"
              << (kAdvertRoundtripVectors + kAdvertInvalidVectors)
              << ",\"total\":"
              << (kAdvertRoundtripVectors + kAdvertInvalidVectors) << "}"
              << ",\"signed_advert_packet_creation\":{\"roundtrip\":"
              << kSignedAdvertPacketRoundtripVectors << ",\"invalid\":"
              << kSignedAdvertPacketInvalidVectors << ",\"semantic\":"
              << (kSignedAdvertPacketRoundtripVectors +
                  kSignedAdvertPacketInvalidVectors)
              << ",\"total\":"
              << (kSignedAdvertPacketRoundtripVectors +
                  kSignedAdvertPacketInvalidVectors)
              << "}"
              << ",\"signed_advert_verification\":{\"valid\":"
              << kSignedAdvertValidVectors << ",\"invalid\":"
              << kSignedAdvertInvalidVectors << ",\"semantic\":"
              << (kSignedAdvertValidVectors + kSignedAdvertInvalidVectors)
              << ",\"total\":"
              << (kSignedAdvertValidVectors + kSignedAdvertInvalidVectors)
              << "}"
              << ",\"ed25519_verifier_kat\":{\"valid\":"
              << kVerifierKatValidVectors << ",\"invalid\":"
              << kVerifierKatInvalidVectors << ",\"semantic\":0,\"total\":"
              << (kVerifierKatValidVectors + kVerifierKatInvalidVectors)
              << "}"
              << ",\"ed25519_point_validation\":{\"valid\":"
              << kPointValidationValidVectors << ",\"invalid\":"
              << kPointValidationInvalidVectors << ",\"semantic\":"
              << (kPointValidationValidVectors +
                  kPointValidationInvalidVectors)
              << ",\"total\":"
              << (kPointValidationValidVectors +
                  kPointValidationInvalidVectors)
              << "}"
              << ",\"crypto_adapter_kat\":{\"valid\":"
              << kCryptoAdapterKatValidVectors
              << ",\"invalid\":0,\"semantic\":0,\"total\":"
              << kCryptoAdapterKatValidVectors << "}"
              << ",\"public_group_packets\":{\"roundtrip\":"
              << kGroupRoundtripVectors << ",\"invalid\":"
              << kGroupInvalidVectors << ",\"semantic\":"
              << (kGroupRoundtripVectors + kGroupInvalidVectors)
              << ",\"total\":"
              << (kGroupRoundtripVectors + kGroupInvalidVectors) << "}"
              << ",\"anonymous_login_request_packets\":{\"roundtrip\":"
              << kLoginRequestRoundtripVectors << ",\"invalid\":"
              << kLoginRequestInvalidVectors << ",\"semantic\":"
              << (kLoginRequestRoundtripVectors +
                  kLoginRequestInvalidVectors)
              << ",\"total\":"
               << (kLoginRequestRoundtripVectors +
                   kLoginRequestInvalidVectors)
               << "}"
               << ",\"regular_request_response_packets\":{\"roundtrip\":"
               << kRequestResponseRoundtripVectors << ",\"invalid\":"
               << kRequestResponseInvalidVectors << ",\"semantic\":"
               << (kRequestResponseRoundtripVectors +
                   kRequestResponseInvalidVectors)
               << ",\"total\":"
               << (kRequestResponseRoundtripVectors +
                   kRequestResponseInvalidVectors)
               << "}"
              << ",\"dm_encrypt_decrypt\":{\"roundtrip\":"
              << kDmRoundtripVectors << ",\"invalid\":"
              << kDmInvalidVectors << ",\"semantic\":"
              << (kDmRoundtripVectors + kDmInvalidVectors)
              << ",\"total\":"
              << (kDmRoundtripVectors + kDmInvalidVectors) << "}"
              << ",\"expected_ack_hash_and_ack_path\":{\"roundtrip\":"
              << kExpectedAckPathRoundtripVectors << ",\"valid\":"
              << kExpectedAckValidVectors << ",\"invalid\":"
              << kExpectedAckInvalidVectors << ",\"semantic\":"
              << (kExpectedAckPathRoundtripVectors +
                  kExpectedAckValidVectors + kExpectedAckInvalidVectors)
              << ",\"total\":"
              << (kExpectedAckPathRoundtripVectors +
                  kExpectedAckValidVectors + kExpectedAckInvalidVectors)
              << "}"
              << ",\"path_return_route_codes\":{\"roundtrip\":"
              << kPathReturnRoundtripVectors << ",\"invalid\":"
              << kPathReturnInvalidVectors << ",\"semantic\":"
              << (kPathReturnRoundtripVectors + kPathReturnInvalidVectors)
              << ",\"total\":"
              << (kPathReturnRoundtripVectors + kPathReturnInvalidVectors)
              << "}"
              << ",\"direct_flood_headers\":{\"roundtrip\":"
              << kRouteRoundtripVectors << ",\"invalid\":"
              << kRouteInvalidVectors << ",\"semantic\":"
              << (kRouteRoundtripVectors + kRouteInvalidVectors)
              << ",\"total\":"
              << (kRouteRoundtripVectors + kRouteInvalidVectors) << "}"
              << ",\"ack_frames\":{\"roundtrip\":"
              << kAckRoundtripVectors << ",\"invalid\":"
              << kAckInvalidVectors << ",\"semantic\":"
              << (kAckRoundtripVectors + kAckInvalidVectors)
              << ",\"total\":"
              << (kAckRoundtripVectors + kAckInvalidVectors) << "}"
              << ",\"trace_source_frames\":{\"roundtrip\":"
              << kTraceRoundtripVectors << ",\"invalid\":"
              << kTraceInvalidVectors << ",\"semantic\":"
              << (kTraceRoundtripVectors + kTraceInvalidVectors)
              << ",\"total\":"
              << (kTraceRoundtripVectors + kTraceInvalidVectors) << "}}"
              << ",\"capabilities\":{\"packet_envelope\":true"
              << ",\"advert_data_fields\":true"
              << ",\"signed_advert_packet_creation\":true"
              << ",\"signed_advert_verification\":true"
              << ",\"ed25519_point_validation\":true"
              << ",\"public_group_packets\":true"
               << ",\"anonymous_login_request_packets\":true"
               << ",\"regular_request_response_packets\":true"
              << ",\"dm_encrypt_decrypt\":true"
              << ",\"expected_ack_hash_and_ack_path\":true"
              << ",\"path_return_route_codes\":true"
              << ",\"direct_flood_headers\":true"
              << ",\"ack_frames\":true"
              << ",\"trace_source_frames\":true}"
              << ",\"failures\":" << failures.size() << "}\n";
    return passed ? 0 : 1;
}
