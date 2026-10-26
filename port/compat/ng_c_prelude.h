/* Force-included into every C file: declare libc/POSIX functions the old code used undeclared
   (an implicit int return truncates 64-bit pointers). */
#pragma once
/* RLX #defines float32_t/float64_t; pull in ARM's typedefs first (header guard keeps them). */
#if defined(__aarch64__) || defined(__arm__)
#include <arm_neon.h>
#endif
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sched.h>
