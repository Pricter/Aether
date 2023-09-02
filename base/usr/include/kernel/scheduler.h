#pragma once

#include <kernel/dlist.h>
#include <kernel/mmu.h>
#include <stdint.h>
#include <kernel/cpu.h>
#include <stdbool.h>
#include <kernel/dlist.h>

struct process;

struct thread {
	struct thread* self;
	struct thread* previous;
	struct core* core;
	struct process* spawner;
	uint64_t startingAddress;
	bool reachedStartingAddress;
	struct thread* siblings; /* Includes self */
	struct regs* regs_ctx;
	uint64_t runningTime;
};

struct process {
	char* name;
	struct thread* threads;
	struct process* parent;
	pagemap_t* pagemap;
	uint64_t runningTime;
};

extern node_t* kernel_process;
extern node_t* threadQueue;
extern node_t* threadRunning;