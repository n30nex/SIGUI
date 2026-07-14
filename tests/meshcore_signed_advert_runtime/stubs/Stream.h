#pragma once

#include <cstddef>
#include <cstdint>

class Stream {
  public:
    virtual ~Stream() = default;

    virtual std::size_t readBytes(uint8_t *, std::size_t) { return 0U; }
    virtual std::size_t write(const uint8_t *, std::size_t length)
    {
        return length;
    }
    virtual std::size_t print(char) { return 1U; }
    virtual std::size_t print(const char *) { return 0U; }
    virtual std::size_t println() { return 1U; }
};
