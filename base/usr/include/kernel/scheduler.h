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
	void* startingAddress;
	bool reachedStartingAddress;
	struct thread* siblings; /* Includes self */
	struct regs* regs_ctx;
	uint16_t state;
	uint64_t runningTime;
	uint64_t tid;
};

struct process {
	char* name;
	uint64_t pid;
	node_t* threads;
	struct process* parent;
	pagemap_t* pagemap;
	uint64_t runningTime;
};

extern struct process* kernel_process;
extern node_t* threadQueue;
extern node_t* threadRunning;

extern uint64_t pid;
extern uint64_t tid;

void scheduler_enqueue(struct thread* thread);
struct thread* scheduler_dequeue(struct thread* thread);
void scheduler_add_running(struct thread* thread);
struct thread* scheduler_remove_running(struct thread* thread);

void scheduler_init(void);
struct thread* scheduler_new_kthread(void* pc, void* arg, bool enqueue);
struct thread* scheduler_get_next_thread(void);

struct regs* schedule(struct regs* r);