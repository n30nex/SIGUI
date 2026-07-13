#include "meshcore_oracle.h"

#include "Packet.h"

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
constexpr std::size_t kRouteRoundtripVectors = 7U;
constexpr std::size_t kRouteInvalidVectors = 10U;

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

    const bool passed = failures.empty();
    for (const std::string &failure : failures) {
        std::cerr << failure << '\n';
    }
    std::cout << "{\"passed\":" << (passed ? "true" : "false")
              << ",\"coverage_boundary\":"
                 "\"pinned_upstream_packet_canonical_advert_and_route_headers\""
              << ",\"wp04_closure_eligible\":false"
              << ",\"abi_version\":" << D1L_MESHCORE_ORACLE_ABI_VERSION
              << ",\"upstream_commit\":\""
              << D1L_MESHCORE_ORACLE_UPSTREAM_COMMIT << "\""
              << ",\"vectors\":{\"roundtrip\":"
              << (kPacketRoundtripVectors + kAdvertRoundtripVectors +
                  kRouteRoundtripVectors)
              << ",\"invalid\":"
              << (kPacketInvalidVectors + kAdvertInvalidVectors +
                  kRouteInvalidVectors)
              << ",\"semantic\":"
              << (kAdvertRoundtripVectors + kAdvertInvalidVectors +
                  kRouteRoundtripVectors + kRouteInvalidVectors)
              << ",\"total\":"
              << (kPacketRoundtripVectors + kPacketInvalidVectors +
                  kAdvertRoundtripVectors + kAdvertInvalidVectors +
                  kRouteRoundtripVectors + kRouteInvalidVectors)
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
              << ",\"direct_flood_headers\":{\"roundtrip\":"
              << kRouteRoundtripVectors << ",\"invalid\":"
              << kRouteInvalidVectors << ",\"semantic\":"
              << (kRouteRoundtripVectors + kRouteInvalidVectors)
              << ",\"total\":"
              << (kRouteRoundtripVectors + kRouteInvalidVectors) << "}}"
              << ",\"capabilities\":{\"packet_envelope\":true"
              << ",\"advert_data_fields\":true"
              << ",\"direct_flood_headers\":true}"
              << ",\"failures\":" << failures.size() << "}\n";
    return passed ? 0 : 1;
}
