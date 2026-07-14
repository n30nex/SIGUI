#include <RNG.h>

#include <cstdlib>

RNGClass RNG;

RNGClass::RNGClass() = default;
RNGClass::~RNGClass() = default;

void RNGClass::rand(uint8_t *, size_t)
{
    std::abort();
}
