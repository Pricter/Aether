// #include <kernel/scheduler.h>
// #include <kernel/dlist.h>
// #include <kernel/mmu.h>
// #include <kernel/cpu.h>
// #include <kernel/kprintf.h>
// #include <kernel/apic.h>

// uint64_t pid;
// uint64_t tid;
// struct process* kernel_process;
// node_t* threadQueue;
// node_t* threadRunning;
// node_t* threadSleeping;

// spinlock_t list_lock;
// void scheduler_enqueue(struct thread* thread) {
// 	bool int_state = spinlock_acquire(&list_lock);
// 	dlist_push(threadQueue, thread);
// 	spinlock_release(&list_lock, int_state);
// }

// void scheduler_dequeue(struct thread* thread) {
// 	bool int_state = spinlock_acquire(&list_lock);
// 	dlist_remove_item(threadQueue, thread);
// 	spinlock_release(&list_lock, int_state);
// }

// void scheduler_add_running(struct thread* thread) {
// 	bool int_state = spinlock_acquire(&list_lock);
// 	dlist_push(threadRunning, thread);
// 	spinlock_release(&list_lock, int_state);
// }

// void scheduler_remove_running(struct thread* thread) {
// 	bool int_state = spinlock_acquire(&list_lock);
// 	dlist_remove_item(threadRunning, thread);
// 	spinlock_release(&list_lock, int_state);
// }

// #define KERNEL_STACK_SIZE 0x1000 * 10
// #define THREAD_STATE_WAITING 0x1
// #define THREAD_STATE_UNDEFINED 0x2
// #define THREAD_STATE_RUNNING 0x3
// #define THREAD_STATE_EXITED 0x4

// void _thread_continue(void);

// spinlock_t manage_lock;
// struct thread* scheduler_new_kthread(void* pc, bool enqueue) {
// 	bool int_state = spinlock_acquire(&manage_lock);
// 	if(pc == NULL) {
// 		kprintf("scheduler: NULL Passed to `pc` in scheduler_new_kthread");
// 		return NULL;
// 	}
// 	struct thread* thread = malloc(sizeof(struct thread));
// 	struct Context* c = malloc(sizeof(struct Context));
// 	struct init_stack* init_stack = malloc(sizeof(struct init_stack));
	
// 	c->rip = (uint64_t)_thread_continue;
// 	c->rbx = (uint64_t)init_stack;

// 	thread->self = thread;
// 	thread->core = NULL;
// 	thread->reachedStartingAddress = false;
// 	thread->context = c;
// 	thread->spawner = kernel_process;
// 	thread->tid = tid++;
// 	thread->state = enqueue == true ? THREAD_STATE_WAITING : THREAD_STATE_UNDEFINED;
	
// 	init_stack->rip = (uintptr_t)pc;
// 	init_stack->rsp = mmu_request_frames(KERNEL_STACK_SIZE / PAGE_SIZE) + HHDM_HIGHER_HALF + KERNEL_STACK_SIZE;
// 	init_stack->cs = 0x8;
// 	init_stack->ss = 0x10;
// 	init_stack->rflags = 0x246;

// 	dlist_push(thread->spawner->threads, thread);

// 	if(enqueue == true) scheduler_enqueue(thread);

// 	spinlock_release(&manage_lock, int_state);
// 	return thread;
// }

// void scheduler_destroy_thread(struct thread* thread) {
// 	bool int_state = spinlock_acquire(&manage_lock);
// 	if(thread == NULL) {
// 		kprintf("scheduler: NULL Passed to scheduler_destroy_thread\n");
// 		return;
// 	}
// 	if(thread->state == THREAD_STATE_WAITING) {
// 		scheduler_dequeue(thread);
// 	} else if(thread->state == THREAD_STATE_RUNNING) {
// 		scheduler_dequeue(thread);
// 	} else {
// 		kprintf("Panic due to thread #%lu\n", thread->tid);
// 		panic("Scheduler does not know how to remove thread #%lu\n", NULL);
// 	}
// 	free(thread);
// 	spinlock_release(&manage_lock, int_state);
// }

// struct process* scheduler_new_process(char* name, pagemap_t* pagemap, struct process* parent) {
// 	bool int_state = spinlock_acquire(&manage_lock);
// 	struct process* new = malloc(sizeof(struct process));
// 	new->name = name;
// 	new->pagemap = pagemap;
// 	new->parent = parent;
// 	new->threads = dlist_create_empty();
// 	new->pid = pid++;
// 	spinlock_release(&manage_lock, int_state);
// 	return new;
// }

// void scheduler_init(void) {
// 	threadQueue = dlist_create_empty();
// 	threadRunning = dlist_create_empty();

// 	kernel_process = scheduler_new_process("Kernel process", mmu_kernel_pagemap, NULL);
// }

// struct thread* scheduler_get_next_thread(void) {
// 	struct thread* current = get_gs_register();
// 	struct core* core = current->core;
// 	if(dlist_get_length(threadQueue) == 0) {
// 		return current;
// 	}

// 	struct thread* first = dlist_get(threadQueue, 0);
// 	struct thread* next = first;
// 	for(uint64_t i = 0; i < dlist_get_length(threadQueue); i++) {
// 		if(next->state == THREAD_STATE_WAITING) break;
// 		next = dlist_get(threadQueue, i);
// 	}
// 	if(next->state != THREAD_STATE_WAITING) {
// 		return current;
// 	}
// 	scheduler_dequeue(next);
// 	scheduler_add_running(next);
// 	if(current != core->idleThread) {
// 		scheduler_remove_running(current);
// 		scheduler_enqueue(current);
// 	}

// 	return next;
// }

// void _switch(struct Context** old, struct Context* new);

// spinlock_t sched_lock = SPINLOCK_ZERO;
// void schedule() {
// 	bool int_state = spinlock_acquire(&sched_lock);
// 	struct thread* current = get_gs_register();
// 	struct core* core = current->core;
// 	struct thread* next = scheduler_get_next_thread();
// 	set_gs_register(next);
// 	next->reachedStartingAddress = true;
// 	next->core = core;
// 	core->current = next;
// 	current->state = THREAD_STATE_WAITING;
// 	next->state = THREAD_STATE_RUNNING;
// 	spinlock_release(&sched_lock, int_state);
// 	_switch(&current->context, next->context);
// }