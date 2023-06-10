#pragma once

#include <kernel/dlist.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <kernel/cpu.h>
#include <kernel/mmu.h>

#define STATE_RUNNING (1 << 0)
#define STATE_BLOCKED (1 << 1)
#define STATE_WAITING (1 << 2)

extern uint64_t g_pid;

static inline uint64_t alloc_pid(void) {
	return g_pid++;
}

struct process_block;

struct thread_struct {
	struct thread_struct* itself;
	struct regs* register_context;
	uintptr_t stack_bottom;

	uint64_t state;
	struct process_block* process;
	bool privilege;

	// TODO: Implement time metrics
};

struct process_block {
	char* process_name;
	char* process_description;
	dlist_node_t* threads;

	uint64_t pid;
};

void sched_init(void);
void sched_add_thread(struct thread_struct* thread);
void sched_remove_thread(struct thread_struct* thread);
struct thread_struct* kernel_thread(void* function);
void sched_switch_task(struct regs* r, struct thread_struct* thread);
void sched_die();
void schedule_threads(struct regs* r);
void sched_unreachable(void);