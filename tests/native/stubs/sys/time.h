#pragma once

#include_next <sys/time.h>

#ifdef _WIN32
int d1l_test_settimeofday(const struct timeval *value,
                          const void *timezone_value);
#else
struct timezone;
int d1l_test_settimeofday(const struct timeval *value,
                          const struct timezone *timezone_value);
#endif
