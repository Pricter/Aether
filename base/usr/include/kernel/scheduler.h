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
	struct core* core;
	struct process* spawner;
	void* startingAddress;
	bool reachedStartingAddress;
	struct regs* regs_ctx;
	struct Context* context;
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
extern node_t* threadSleeping;

extern uint64_t pid;
extern uint64_t tid;

void scheduler_enqueue(struct thread* thread);
void scheduler_dequeue(struct thread* thread);
void scheduler_add_running(struct thread* thread);
void scheduler_remove_running(struct thread* thread);

void scheduler_init(void);
struct thread* scheduler_get_next_thread(void);

struct thread* scheduler_new_kthread(void* pc, void* arg, bool enqueue);
struct process* scheduler_new_process(char* name, pagemap_t* pagemap, struct process* parent);

void schedule(struct regs* r);
void thread_exit();