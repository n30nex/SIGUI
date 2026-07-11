#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>

// Packet.cpp references SHA256 for Packet::calculatePacketHash(). The bounded
// wire-envelope harness never calls that method. These fail-fast methods make
// that boundary executable: this stub cannot silently become a crypto oracle.
class SHA256 {
  public:
    void update(const void *, std::size_t) { std::abort(); }
    void finalize(uint8_t *, std::size_t) { std::abort(); }
    void resetHMAC(const void *, std::size_t) { std::abort(); }
    void finalizeHMAC(const void *, std::size_t, uint8_t *, std::size_t)
    {
        std::abort();
    }
};
