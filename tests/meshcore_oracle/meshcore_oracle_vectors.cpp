#include "meshcore_oracle.h"

#include "Packet.h"
#include "ed_25519.h"
#include "mesh/ed25519_canonical.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kPacketRoundtripVectors = 4U;
constexpr std::size_t kPacketInvalidVectors = 5U;
constexpr std::size_t kAdvertRoundtripVectors = 4U;
constexpr std::size_t kAdvertInvalidVectors = 11U;
constexpr std::size_t kSignedAdvertProductionVectors = 3U;
constexpr std::size_t kSignedAdvertValidVectors = 3U;
constexpr std::size_t kSignedAdvertInvalidVectors = 10U;
constexpr std::size_t kVerifierKatValidVectors = 1U;
constexpr std::size_t kVerifierKatInvalidVectors = 3U;
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
    }
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
                 "\"pinned_upstream_packet_advert_route_ack_trace_and_signed_advert_verification\""
              << ",\"wp04_closure_eligible\":false"
              << ",\"abi_version\":" << D1L_MESHCORE_ORACLE_ABI_VERSION
              << ",\"upstream_commit\":\""
              << D1L_MESHCORE_ORACLE_UPSTREAM_COMMIT << "\""
              << ",\"vectors\":{\"roundtrip\":"
              << (kPacketRoundtripVectors + kAdvertRoundtripVectors +
                  kRouteRoundtripVectors + kAckRoundtripVectors +
                  kTraceRoundtripVectors)
              << ",\"valid\":"
              << (kSignedAdvertValidVectors + kVerifierKatValidVectors)
              << ",\"invalid\":"
              << (kPacketInvalidVectors + kAdvertInvalidVectors +
                  kSignedAdvertInvalidVectors + kVerifierKatInvalidVectors +
                  kRouteInvalidVectors + kAckInvalidVectors +
                  kTraceInvalidVectors)
              << ",\"semantic\":"
              << (kAdvertRoundtripVectors + kAdvertInvalidVectors +
                  kSignedAdvertValidVectors +
                  kSignedAdvertInvalidVectors +
                  kRouteRoundtripVectors + kRouteInvalidVectors +
                  kAckRoundtripVectors + kAckInvalidVectors +
                  kTraceRoundtripVectors + kTraceInvalidVectors)
              << ",\"total\":"
              << (kPacketRoundtripVectors + kPacketInvalidVectors +
                  kAdvertRoundtripVectors + kAdvertInvalidVectors +
                  kSignedAdvertValidVectors + kSignedAdvertInvalidVectors +
                  kVerifierKatValidVectors + kVerifierKatInvalidVectors +
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
              << ",\"signed_advert_verification\":true"
              << ",\"direct_flood_headers\":true"
              << ",\"ack_frames\":true"
              << ",\"trace_source_frames\":true}"
              << ",\"failures\":" << failures.size() << "}\n";
    return passed ? 0 : 1;
}
