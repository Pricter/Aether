#include <kernel/dlist.h>
#include <kernel/mmu.h>
#include <kernel/kprintf.h>
#include <kernel/cpu.h>
#include <kernel/sched.h>
#include <string.h>
#include <kernel/spinlock.h>

void* globalToRun = NULL;
void* globalBlocked = NULL;

uint32_t tid = 0;

struct process* kernel_process = NULL;

void scheduler_init(void) {
	globalToRun = DLIST_EMPTY;
	globalBlocked = DLIST_EMPTY;

	kernel_process = (struct process*)malloc(sizeof(struct process));
	kernel_process->child_threads = DLIST_EMPTY;
	kernel_process->name = strdup("Main kernel process");
	kernel_process->pml = mmu_kernel_pagemap;
}

void scheduler_add_queue(struct thread* thread) {
	DLIST_PUSH_BACK(globalToRun, thread);
}

void scheduler_add_blocked(struct thread* thread) {
	DLIST_PUSH_BACK(globalBlocked, thread);
}

void scheduler_remove_queue(struct thread* thread) {
	DLIST_DESTROY(globalToRun, thread);
}

void scheduler_remove_blocked(struct thread* thread) {
	DLIST_DESTROY(globalBlocked, thread);
}

struct thread* scheduler_new_kthread(void* pc, void* arg) {
	struct thread* thread = (struct thread*)malloc(sizeof(struct thread));
	struct regs* regs_context = (struct regs*)malloc(sizeof(struct regs));

	regs_context->rsp = mmu_request_frames(10) + HHDM_HIGHER_HALF;
	regs_context->rip = (uintptr_t)pc;
	regs_context->rdi = (uintptr_t)arg;
	regs_context->rflags = 0x202;
	regs_context->cs = 0x28;

	thread->regs_context = regs_context;
	thread->self = thread;
	thread->previous = NULL;
	thread->spawner = kernel_process;
	thread->tid = get_tid();

	return thread;
}

struct thread* get_next_thread(void) {
	spinlock_t spinlock = SPINLOCK_ZERO;
	spinlock_acquire(&spinlock);

	struct thread* current_thread = get_gs_register();

	spinlock_release(&spinlock);
	return NULL;
}

void schedule(void) {
	struct thread* next_thread = get_next_thread();
}