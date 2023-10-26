#include <kernel/scheduler.h>
#include <kernel/dlist.h>
#include <kernel/mmu.h>
#include <kernel/cpu.h>
#include <kernel/kprintf.h>
#include <kernel/apic.h>

uint64_t pid;
uint64_t tid;
struct process* kernel_process;
node_t* threadQueue;
node_t* threadRunning;
node_t* threadSleeping;

spinlock_t list_lock;
void scheduler_enqueue(struct thread* thread) {
	bool int_state = spinlock_acquire(&list_lock);
	dlist_push(threadQueue, thread);
	spinlock_release(&list_lock, int_state);
}

void scheduler_dequeue(struct thread* thread) {
	bool int_state = spinlock_acquire(&list_lock);
	dlist_remove_item(threadQueue, thread);
	spinlock_release(&list_lock, int_state);
}

void scheduler_add_running(struct thread* thread) {
	bool int_state = spinlock_acquire(&list_lock);
	dlist_push(threadRunning, thread);
	spinlock_release(&list_lock, int_state);
}

void scheduler_remove_running(struct thread* thread) {
	bool int_state = spinlock_acquire(&list_lock);
	dlist_remove_item(threadRunning, thread);
	spinlock_release(&list_lock, int_state);
}

#define KERNEL_STACK_SIZE 0x1000 * 10
#define THREAD_STATE_WAITING 0x1
#define THREAD_STATE_UNDEFINED 0x2
#define THREAD_STATE_RUNNING 0x3

struct thread* scheduler_new_kthread(void* pc, void* arg, bool enqueue) {
	if(pc == NULL) {
		kprintf("scheduler: NULL Passed to `pc` in scheduler_new_kthread");
		return NULL;
	}
	struct thread* thread = malloc(sizeof(struct thread));
	struct regs* r = malloc(sizeof(struct regs));

	thread->self = thread;
	thread->core = NULL;
	thread->reachedStartingAddress = false;
	thread->regs_ctx = r;
	thread->runningTime = 0;
	thread->spawner = kernel_process;
	thread->startingAddress = pc;
	thread->tid = tid++;
	thread->state = enqueue == true ? THREAD_STATE_WAITING : THREAD_STATE_UNDEFINED;
	
	r->rip = (uintptr_t)thread->startingAddress;
	r->rdi = (uintptr_t)arg;
	r->rsp = mmu_request_frames(KERNEL_STACK_SIZE / 10) + HHDM_HIGHER_HALF;
	r->cs = 0x8;
	r->ss = 0x10;
	r->rflags = 0x246;

	dlist_push(thread->spawner->threads, thread);

	if(enqueue == true) scheduler_enqueue(thread);

	return thread;
}

void scheduler_destroy_thread(struct thread* thread) {
	if(thread == NULL) {
		kprintf("scheduler: NULL Passed to scheduler_destroy_thread\n");
		return;
	}
	if(thread->state == THREAD_STATE_WAITING) {
		scheduler_dequeue(thread);
	} else if(thread->state == THREAD_STATE_RUNNING) {
		scheduler_dequeue(thread);
	} else {
		kprintf("Panic due to thread #%lu\n", thread->tid);
		panic("Scheduler does not know how to remove thread #%lu\n", NULL);
	}
	free(thread);
}

void scheduler_init(void) {
	threadQueue = dlist_create_empty();
	threadRunning = dlist_create_empty();

	kernel_process = malloc(sizeof(struct process));
	kernel_process->name = "Kernel Process";
	kernel_process->pagemap = mmu_kernel_pagemap;
	kernel_process->parent = NULL;
	kernel_process->threads = dlist_create_empty();
	kernel_process->pid = pid++;
	kernel_process->runningTime = 0;
}

struct thread* scheduler_get_next_thread(void) {
	struct thread* current = get_gs_register();
	struct core* core = current->core;
	if(dlist_get_length(threadQueue) == 0) {
		return current;
	}

	struct thread* next = dlist_get(threadQueue, 0);
	scheduler_dequeue(next);
	scheduler_add_running(next);
	if(current != core->idleThread) {
		scheduler_remove_running(current);
		scheduler_enqueue(current);
	}

	return next;
}

extern void switch_context(void* context);

spinlock_t sched_lock;
void schedule(struct regs* r) {
	disable_interrupts();
	bool int_state = spinlock_acquire(&sched_lock);
	struct thread* current = get_gs_register();
	struct core* core = current->core;
	if(current != core->idleThread) {
		// Time represented in us
		current->runningTime += 10000;
		current->spawner->runningTime += 10000;
	}
	if(current != core->idleThread) {
		current->regs_ctx = r;
	}
	struct thread* next = scheduler_get_next_thread();
	set_gs_register(next);
	next->reachedStartingAddress = true;
	next->core = core;
	core->current = next;
	current->state = THREAD_STATE_WAITING;
	next->state = THREAD_STATE_RUNNING;
	spinlock_release(&sched_lock, int_state);
	switch_context(next->regs_ctx);
}