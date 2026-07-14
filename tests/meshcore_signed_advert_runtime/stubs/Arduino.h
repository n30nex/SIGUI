#pragma once

#include "Stream.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

inline char *ltoa(long value, char *destination, int radix)
{
    if (destination == nullptr || radix != 10) {
        return nullptr;
    }
    std::snprintf(destination, 24U, "%ld", value);
    return destination;
}
