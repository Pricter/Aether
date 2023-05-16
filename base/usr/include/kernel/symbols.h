#pragma once

#include <stdint.h>
#include <stddef.h>

typedef struct ksym_func {
	char* name; /* Address to the name (use malloc for the string) */
	uintptr_t addr; /* Address of the func */
} ksym_func_t;

extern ksym_func_t* function_table;

void symbols_init(void);
char* symbols_search(uintptr_t addr);