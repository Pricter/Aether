#pragma once

#include <kernel/dlist.h>
#include <kernel/mmu.h>

struct process {
	const char* name;
	const char* description;
	node_t* threads;
	int pid;

	pagemap_t* pagemap;
};

struct context_regs {
	uint64_t rbx, rbp, r12, r13, r14, r15, rip;
};

struct thread {
	int tid;
	struct process* parent;
	struct context_regs context;
};