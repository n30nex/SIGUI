extern "C" {
#include "mesh/meshcore_wire.h"
}

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {

constexpr std::size_t kGuardSize = 16U;
constexpr uint8_t kCanary = 0x6DU;

[[noreturn]] void fail()
{
    std::abort();
}

bool points_within(const uint8_t *pointer,
                   std::size_t length,
                   const uint8_t *data,
                   std::size_t size)
{
    if (length == 0U) {
        return pointer == nullptr;
    }
    if (!pointer || !data) {
        return false;
    }
    const uintptr_t start = reinterpret_cast<uintptr_t>(data);
    const uintptr_t end = start + size;
    const uintptr_t value = reinterpret_cast<uintptr_t>(pointer);
    return end >= start && value >= start && value <= end && length <= end - value;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, std::size_t size)
{
    d1l_meshcore_wire_packet_t packet;
    std::memset(&packet, 0xA5, sizeof(packet));
    d1l_meshcore_wire_packet_t before;
    std::memcpy(&before, &packet, sizeof(packet));
    const bool decoded = d1l_meshcore_wire_decode(data, size, &packet);
    if (!decoded) {
        if (std::memcmp(&before, &packet, sizeof(packet)) != 0) {
            fail();
        }
        return 0;
    }

    if (size < 3U || size > D1L_MESHCORE_MAX_RAW_PACKET ||
        packet.header != data[0] ||
        packet.route != (data[0] & 0x03U) ||
        packet.type != ((data[0] >> 2U) & 0x0FU) ||
        packet.version != ((data[0] >> 6U) & 0x03U) ||
        packet.payload_len == 0U ||
        packet.payload_len > D1L_MESHCORE_MAX_PACKET_PAYLOAD ||
        packet.path_byte_len > D1L_MESHCORE_MAX_PATH_BYTES ||
        !d1l_meshcore_wire_path_len_valid(packet.path_len) ||
        packet.path_hash_bytes !=
            d1l_meshcore_wire_path_hash_size(packet.path_len) ||
        packet.path_hops != d1l_meshcore_wire_path_hash_count(packet.path_len) ||
        packet.path_byte_len != d1l_meshcore_wire_path_byte_len(packet.path_len) ||
        !points_within(packet.path, packet.path_byte_len, data, size) ||
        !points_within(packet.payload, packet.payload_len, data, size)) {
        fail();
    }

    std::array<uint8_t, D1L_MESHCORE_MAX_RAW_PACKET + (2U * kGuardSize)> encoded{};
    encoded.fill(kCanary);
    uint8_t *dest = encoded.data() + kGuardSize;
    std::size_t encoded_len = 0U;
    if (!d1l_meshcore_wire_encode(&packet, dest, D1L_MESHCORE_MAX_RAW_PACKET,
                                  &encoded_len) ||
        encoded_len != size || std::memcmp(dest, data, size) != 0) {
        fail();
    }
    for (std::size_t i = 0; i < kGuardSize; ++i) {
        if (encoded[i] != kCanary ||
            encoded[kGuardSize + D1L_MESHCORE_MAX_RAW_PACKET + i] != kCanary) {
            fail();
        }
    }
    return 0;
}
