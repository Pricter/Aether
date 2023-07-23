#pragma once

#include <stdint.h>
#include <kernel/cpu.h>
#include <kernel/mmu.h>
#include <kernel/dlist.h>

extern uint32_t tid;

static inline uint32_t get_tid(void) {
	return tid++;
}

struct process;

struct thread {
	struct thread* self;
	struct thread* previous;

	struct process* spawner;
	uint32_t tid;

	struct regs* regs_context;

	uint16_t state; // Blocked OR Running OR Waiting
};

struct process {
	const char* name;
	pagemap_t* pml;
	void* child_threads;
};

void scheduler_add_waiting(struct thread* thread);
void scheduler_add_running(struct thread* thread);
void scheduler_remove_waiting(struct thread* thread);
void scheduler_remove_running(struct thread* thread);
struct thread* scheduler_new_kthread(void* pc, void* arg);
void schedule(struct regs* r, struct thread* current);
void scheduler_thread_die(void);