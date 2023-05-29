#pragma once

#include <stdint.h>

extern uint64_t __kernel_ticks;

void sleep(uint64_t millis);