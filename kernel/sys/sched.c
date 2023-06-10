#include <kernel/sched.h>
#include <kernel/mmu.h>
#include <kernel/cpu.h>
#include <string.h>
#include <kernel/dlist.h>
#include <kernel/kprintf.h>
#include <memory.h>
#include <kernel/apic.h>

#define KERNEL_STACK_SIZE 0x10000

uint64_t g_pid = 0;
dlist_node_t* thread_list = NULL;

void sched_init(void) {
	thread_list = DLIST_EMPTY;
}

void sched_unreachable(void) {
	for(;;) asm ("hlt");
}

void sched_add_thread(struct thread_struct* thread) {
	DLIST_PUSH_BACK(thread_list, thread);
}

void sched_remove_thread(struct thread_struct* thread) {
	DLIST_REMOVE(thread_list, thread);
}

struct thread_struct* kernel_thread(void* function) {
	struct thread_struct* thread = malloc(sizeof(struct thread_struct));
	thread->itself = thread;
	thread->privilege = true;
	thread->process = NULL; /* Kernel threads dont have any process */
	thread->stack_bottom = (mmu_request_frames(KERNEL_STACK_SIZE / PAGE_SIZE) + KERNEL_STACK_SIZE) + HHDM_HIGHER_HALF; /* Stack grows downwards */
	thread->stack_bottom -= 8 * 6;
	thread->state = STATE_WAITING;

	struct regs* registers = malloc(sizeof(struct regs));
	memset(registers, 0, sizeof(struct regs));
	registers->rip = (uintptr_t)function;
	registers->rsp = thread->stack_bottom;

	thread->register_context = registers;

	return thread;
}

/**
 * TODO: Implement Saving FPU/MMX/SSE registers
*/
void sched_switch_task(struct regs* r, struct thread_struct* thread) {
	struct thread_struct* current_thread = get_gs_register();
	asm volatile (
		"push %%rbp\n"
		"push %%rbx\n"
		"push %%r12\n"
		"push %%r13\n"
		"push %%r14\n"
		"push %%r15\n"
		"mov %0, %%rsp\n"
		"pop %%r15\n"
		"pop %%r14\n"
		"pop %%r13\n"
		"pop %%r12\n"
		"pop %%rbx\n"
		"pop %%rbp\n"
		"mov %1, %%rax\n"
		"push %%rax\n"
		"ret\n"
		: : "r" (thread->stack_bottom), "r" (thread->register_context->rip)
	);
}

void schedule_threads(struct regs* r) {
	lapic_eoi();
	enable_interrupts();

	sched_switch_task(r, DLIST_GET_ITEM(thread_list, 0, struct thread_struct));
}