#pragma once

#include <cstddef>

class Stream {
  public:
    std::size_t print(char) { return 1U; }
};
