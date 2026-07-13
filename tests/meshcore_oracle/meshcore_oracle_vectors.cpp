#include "meshcore_oracle.h"

#include "Packet.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kRoundtripVectors = 4U;
constexpr std::size_t kInvalidVectors = 5U;

struct Vector {
    uint8_t header;
    uint16_t transport_0;
    uint16_t transport_1;
    uint8_t path_len;
    std::vector<uint8_t> path;
    std::vector<uint8_t> payload;
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

    const std::array<Vector, kRoundtripVectors> vectors = {{
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

    const bool passed = failures.empty();
    for (const std::string &failure : failures) {
        std::cerr << failure << '\n';
    }
    std::cout << "{\"passed\":" << (passed ? "true" : "false")
              << ",\"coverage_boundary\":\"upstream_packet_envelope_adapter_only\""
              << ",\"wp04_closure_eligible\":false"
              << ",\"abi_version\":" << D1L_MESHCORE_ORACLE_ABI_VERSION
              << ",\"upstream_commit\":\""
              << D1L_MESHCORE_ORACLE_UPSTREAM_COMMIT << "\""
              << ",\"vectors\":{\"roundtrip\":" << kRoundtripVectors
              << ",\"invalid\":" << kInvalidVectors
              << ",\"semantic\":0,\"total\":"
              << (kRoundtripVectors + kInvalidVectors) << "}"
              << ",\"failures\":" << failures.size() << "}\n";
    return passed ? 0 : 1;
}
