#include <kernel/dlist.h>
#include <kernel/mmu.h>
#include <kernel/kprintf.h>
#include <kernel/cpu.h>
#include <kernel/scheduler.h>
#include <string.h>
#include <kernel/spinlock.h>
#include <memory.h>
#include <kernel/init.h>
#include <stdbool.h>
#include <kernel/apic.h>
#include <kernel/hpet.h>

dlist_node_t* globalWaiting = NULL;
dlist_node_t* globalRunning = NULL;

struct thread* idleThread = NULL;
void idleForever(void) { for(;;); }

uint32_t tid = 0;

struct process* kernel_process = NULL;

bool scheduler_initialized = false;
char scheduler_timer = 0;

void __init scheduler_init(void) {
	globalWaiting = DLIST_EMPTY;
	globalRunning = DLIST_EMPTY;

	kernel_process = (struct process*)malloc(sizeof(struct process));
	memset(kernel_process, 0, sizeof(struct process));
	kernel_process->child_threads = DLIST_EMPTY;
	kernel_process->name = strdup("Main kernel process");
	kernel_process->pml = mmu_kernel_pagemap;

	idleThread = scheduler_new_kthread(idleForever, NULL);

	if(lapic_initialized == true) {
		lapic_timer_calibrate();
		scheduler_timer = SCHEDULER_USING_LAPIC;
	} else {
		panic("No implemented timers available for scheduler", NULL);
	}
}

void scheduler_add_waiting(struct thread* thread) {
	DLIST_PUSH_BACK(globalWaiting, thread);
}

void scheduler_add_running(struct thread* thread) {
	DLIST_PUSH_BACK(globalRunning, thread);
}

void scheduler_remove_waiting(struct thread* thread, bool notFoundPanic) {
	for(uint64_t i = 0; i < DLIST_LENGTH(globalWaiting); i++) {
		if(DLIST_GET_ITEM(globalWaiting, i, struct thread) == thread) {
			DLIST_REMOVE(globalWaiting, i); return;
		}
	}
	if(notFoundPanic == true) {
		panic("Called scheduler_remove_waiting but thread was not found in waiting list", NULL);
	}
}

void scheduler_remove_running(struct thread* thread, bool notFoundPanic) {
	for(uint64_t i = 0; i < DLIST_LENGTH(globalRunning); i++) {
		struct thread* toTest = DLIST_GET_ITEM(globalRunning, i, struct thread);
		if(toTest == thread) {
			DLIST_REMOVE(globalRunning, i); return;
		}
	}
	kprintf("scheduler: Failed to remove thread %p from running list", thread);
	if(notFoundPanic == true) {
		panic("Called scheduler_remove_running but thread was not found in the running list", NULL);
	}
}

void scheduler_destroy_thread(struct thread* thread) {
	scheduler_remove_running(thread, false);
	scheduler_remove_waiting(thread, false);

	for(uint64_t i = 0; i < DLIST_LENGTH(thread->siblings); i++) {
		struct thread* toTest = DLIST_GET_ITEM(thread->siblings, i, struct thread);
		if(toTest == thread) {
			klog("Destroying thread %p", thread);
			DLIST_DESTROY(thread->siblings, i); return;
		}
	}
}

struct thread* scheduler_new_kthread(void* pc, void* arg) {
	struct thread* thread = (struct thread*)malloc(sizeof(struct thread));
	struct regs* regs_context = (struct regs*)malloc(sizeof(struct regs));

	memset(thread, 0, sizeof(struct thread));
	memset(regs_context, 0, sizeof(struct regs));

	regs_context->rsp = mmu_request_frames(10) + HHDM_HIGHER_HALF + (10 * 1024);
	regs_context->rip = (uintptr_t)pc;
	regs_context->rdi = (uintptr_t)arg;
	regs_context->rflags = 0x202;
	regs_context->cs = 0x28;

	thread->regs_context = regs_context;
	thread->self = thread;
	thread->previous = NULL;
	thread->spawner = kernel_process;
	thread->parent = kernel_process;
	thread->siblings = kernel_process->child_threads;
	thread->tid = get_tid();
	thread->reachedStartAddress = false;
	set_thread_waiting(thread);

	DLIST_PUSH_BACK(kernel_process->child_threads, thread);

	return thread;
}

static __attribute__((noreturn)) void thread_spinup(struct thread* thread) {
	asm volatile (
		"mov %0, %%rsp\n\t"
		"pop %%r15\n"
		"pop %%r14\n"
		"pop %%r13\n"
		"pop %%r12\n"
		"pop %%r11\n"
		"pop %%r10\n"
		"pop %%r9\n"
		"pop %%r8\n"
		"pop %%rbp\n"
		"pop %%rdi\n"
		"pop %%rsi\n"
		"pop %%rdx\n"
		"pop %%rcx\n"
		"pop %%rbx\n"
		"pop %%rax\n"
		"add $16, %%rsp\n"
 		"swapgs\n"
		"iretq\n"
		: : "rm" (thread->regs_context)
		: "memory"
	);

	__builtin_unreachable();
}

void scheduler_thread_die(void) {
	struct thread* current = get_gs_register();
	scheduler_destroy_thread(current);
	set_gs_register(idleThread);

	schedule(NULL, NULL);
}

struct thread* get_next_thread(void) {
	spinlock_t spinlock = SPINLOCK_ZERO;
	spinlock_acquire(&spinlock);

	struct thread* currentThread = get_gs_register();
	if(DLIST_LENGTH(globalWaiting) > 0) {
		scheduler_remove_running(currentThread, true);
		scheduler_add_waiting(currentThread);
		set_thread_waiting(currentThread);
		struct thread* nextThread = DLIST_GET_ITEM(globalWaiting, 0, struct thread);
		scheduler_remove_waiting(nextThread, true);
		scheduler_add_running(nextThread);
		set_thread_running(nextThread);
		nextThread->previous = currentThread;
		return nextThread;
	} else {
		currentThread->previous = currentThread;
		return currentThread;
	};

	spinlock_release(&spinlock);
	goto fallback;

fallback:
	return idleThread; 
}

void schedule(struct regs* r, struct thread* current) {
	if((current != NULL) && (current->reachedStartAddress == true)) current->regs_context = r;
	struct thread* next_thread = get_next_thread();
	set_gs_register(next_thread);
	next_thread->reachedStartAddress = true;
	thread_spinup(next_thread);
}