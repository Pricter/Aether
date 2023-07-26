#pragma once

#include <stdint.h>
#include <kernel/cpu.h>
#include <kernel/mmu.h>
#include <kernel/dlist.h>

#define TASK_RUNNING (1lu >> 0)
#define TASK_WAITING (1lu >> 1)
#define TASK_BLOCKED (1lu >> 2)

extern uint32_t tid;

static inline uint32_t get_tid(void) {
	return tid++;
}

struct process;

struct thread {
	struct thread* self;
	struct thread* previous;

	struct process* spawner;
	struct process* parent;
	dlist_node_t* siblings;
	uint32_t tid;

	struct regs* regs_context;

	uint64_t state; // Blocked OR Running OR Waiting

	bool reachedStartAddress;
};

struct process {
	const char* name;
	pagemap_t* pml;
	dlist_node_t* child_threads;
};

#define SCHEDULER_USING_LAPIC 1
#define SCHEDULER_USING_HPET 2
#define SCHEDULER_USING_PIT 3

extern bool scheduler_initialized;
extern char scheduler_timer;

void scheduler_add_waiting(struct thread* thread);
void scheduler_add_running(struct thread* thread);
void scheduler_remove_waiting(struct thread* thread, bool notFoundPanic);
void scheduler_remove_running(struct thread* thread, bool notFoundPanic);
struct thread* scheduler_new_kthread(void* pc, void* arg);
void schedule(struct regs* r, struct thread* current);
void scheduler_thread_die(void);

static inline void set_thread_running(struct thread* thread) {
    thread->state |= TASK_RUNNING;
    thread->state &= ~(TASK_WAITING | TASK_BLOCKED);
}

// Function to set the thread state to TASK_WAITING
static inline void set_thread_waiting(struct thread* thread) {
    thread->state |= TASK_WAITING;
    thread->state &= ~(TASK_RUNNING | TASK_BLOCKED);
}

// Function to set the thread state to TASK_BLOCKED
static inline void set_thread_blocked(struct thread* thread) {
    thread->state |= TASK_BLOCKED;
    thread->state &= ~(TASK_RUNNING | TASK_WAITING);
}

// Function to get the thread state
static inline uint64_t get_thread_state(struct thread* thread) {
    return thread->state;
}