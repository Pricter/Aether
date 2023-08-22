#pragma once

#include <kernel/dlist.h>
#include <kernel/mmu.h>
#include <stdint.h>
#include <kernel/cpu.h>
#include <stdbool.h>

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

extern struct process* kernel_process;
extern struct thread* threadQueue;
extern struct thread* threadRunning;