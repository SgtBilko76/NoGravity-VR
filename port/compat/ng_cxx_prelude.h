/* Force-included into C++ files: pull in std headers before the engine's min/max macros. */
#pragma once
/* RLX #defines float32_t/float64_t; pull in ARM's typedefs first (header guard keeps them). */
#if defined(__aarch64__) || defined(__arm__)
#include <arm_neon.h>
#endif
#ifdef __cplusplus
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include <functional>
#endif
