#pragma once

#include <stdint.h>
#include <stddef.h>

typedef struct ksym_func {
	char* name; /* Address to the name (use malloc for the string) */
	uintptr_t addr; /* Address of the func */
	struct ksym_func* next;
} ksym_func_t;

/* Stack frame struct for stack tracing */
typedef struct stack_frame {
	struct stack_frame* rbp;
	uint64_t rip;
} stack_frame_t;

extern ksym_func_t* function_table;

void symbols_init(void);
char* symbols_search(uintptr_t addr);