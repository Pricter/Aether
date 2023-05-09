#pragma once

#include <stdint.h>
#include <stddef.h>

extern char kernel_symbols_start[];
extern char kernel_symbols_end[];

typedef struct {
	uintptr_t addr;
	char name[];
} kernel_symbol_t;