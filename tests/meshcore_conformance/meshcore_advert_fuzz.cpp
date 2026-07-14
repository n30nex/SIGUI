#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "mesh/advert_data.h"

namespace {

[[noreturn]] void fail()
{
    std::abort();
}

void require(bool condition)
{
    if (!condition) {
        fail();
    }
}

d1l_advert_data_t rejected_value()
{
    d1l_advert_data_t value;
    std::memset(&value, 0, sizeof(value));
    value.type_code = 'N';
    return value;
}

bool supported_type(char type_code)
{
    return type_code == 'N' || type_code == 'C' || type_code == 'P' ||
           type_code == 'R' || type_code == 'S';
}

void validate_name(const char *name)
{
    bool terminated = false;
    for (std::size_t i = 0; i < D1L_ADVERT_DATA_NAME_LEN; ++i) {
        const unsigned char value = static_cast<unsigned char>(name[i]);
        if (terminated) {
            require(value == 0U);
            continue;
        }
        if (value == 0U) {
            terminated = true;
            continue;
        }
        require(value >= 32U && value <= 126U);
        require(value != static_cast<unsigned char>('"'));
        require(value != static_cast<unsigned char>('\\'));
    }
    require(terminated);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    d1l_advert_data_t first;
    d1l_advert_data_t second;
    std::memset(&first, 0xa5, sizeof(first));
    std::memset(&second, 0x5a, sizeof(second));

    const std::uint8_t *input = size == 0U ? nullptr : data;
    const bool first_ok = d1l_advert_data_parse(input, size, &first);
    const bool second_ok = d1l_advert_data_parse(input, size, &second);

    require(first_ok == second_ok);
    require(std::memcmp(&first, &second, sizeof(first)) == 0);
    require(!d1l_advert_data_parse(input, size, nullptr));

    if (!first_ok) {
        const d1l_advert_data_t expected = rejected_value();
        require(std::memcmp(&first, &expected, sizeof(first)) == 0);
        return 0;
    }

    require(size <= D1L_ADVERT_DATA_MAX_LEN);
    require(supported_type(first.type_code));
    validate_name(first.name);
    if (first.location_valid) {
        require(first.lat_e6 >= -90000000 && first.lat_e6 <= 90000000);
        require(first.lon_e6 >= -180000000 && first.lon_e6 <= 180000000);
    } else {
        require(first.lat_e6 == 0);
        require(first.lon_e6 == 0);
    }

    return 0;
}
