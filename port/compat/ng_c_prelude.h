/* Force-included into every C file: declare libc/POSIX functions the old code used undeclared
   (an implicit int return truncates 64-bit pointers). */
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sched.h>
