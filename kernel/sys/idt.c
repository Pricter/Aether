/**
 * idt.c - Licensed under the MIT License
 * 
 * initialize the idt and functions for setting the handler
*/

#include <kernel/regs.h>
#include <kernel/irq.h>
#include <stdint.h>
#include <stddef.h>
#include <kernel/kprintf.h>
#include <kernel/symbols.h>
#include <kernel/spinlock.h>
#include <kernel/symbols.h>

static struct idt_pointer idtp;
static idt_entry_t idt[256];

static uint8_t free_vector = 32;

/**
 * @brief Initialize a gate
 */
void idt_set_gate(uint8_t num, interrupt_handler_t handler, uint16_t selector, uint8_t flags, int userspace) {
	uintptr_t base = (uintptr_t)handler;
	idt[num].base_low  = (base) & 0xFFFF;
	idt[num].base_mid  = (base >> 16) & 0xFFFF;
	idt[num].base_high = (base >> 32) & 0xFFFFFFFF;
	idt[num].selector = selector;
	idt[num].zero = 0;
	idt[num].pad = 0;
	idt[num].flags = flags | (userspace ? 0x60 : 0);
}

uint8_t idt_allocate(void) {
	static spinlock_t lock = SPINLOCK_ZERO;
	
	spinlock_acquire(&lock);

	if(free_vector == 0xf0) {
		// TODO: Panic
	}

	uint8_t ret = free_vector++;
	spinlock_release(&lock);
	return ret;
}

/**
 * @brief Initializes the IDT and sets up gates for all interrupts.
 */
void idt_init(void) {
	idtp.limit = sizeof(idt);
	idtp.base  = (uintptr_t)&idt;

	/** ISRs */
	idt_set_gate(0,  _isr0,  0x28, 0x8E, 0);
	idt_set_gate(1,  _isr1,  0x28, 0x8E, 0);
	idt_set_gate(2,  _isr2,  0x28, 0x8E, 0);
	idt_set_gate(3,  _isr3,  0x28, 0x8E, 0);
	idt_set_gate(4,  _isr4,  0x28, 0x8E, 0);
	idt_set_gate(5,  _isr5,  0x28, 0x8E, 0);
	idt_set_gate(6,  _isr6,  0x28, 0x8E, 0);
	idt_set_gate(7,  _isr7,  0x28, 0x8E, 0);
	idt_set_gate(8,  _isr8,  0x28, 0x8E, 0);
	idt_set_gate(9,  _isr9,  0x28, 0x8E, 0);
	idt_set_gate(10, _isr10, 0x28, 0x8E, 0);
	idt_set_gate(11, _isr11, 0x28, 0x8E, 0);
	idt_set_gate(12, _isr12, 0x28, 0x8E, 0);
	idt_set_gate(13, _isr13, 0x28, 0x8E, 0);
	idt_set_gate(14, _isr14, 0x28, 0x8E, 0);
	idt_set_gate(15, _isr15, 0x28, 0x8E, 0);
	idt_set_gate(16, _isr16, 0x28, 0x8E, 0);
	idt_set_gate(17, _isr17, 0x28, 0x8E, 0);
	idt_set_gate(18, _isr18, 0x28, 0x8E, 0);
	idt_set_gate(19, _isr19, 0x28, 0x8E, 0);
	idt_set_gate(20, _isr20, 0x28, 0x8E, 0);
	idt_set_gate(21, _isr21, 0x28, 0x8E, 0);
	idt_set_gate(22, _isr22, 0x28, 0x8E, 0);
	idt_set_gate(23, _isr23, 0x28, 0x8E, 0);
	idt_set_gate(24, _isr24, 0x28, 0x8E, 0);
	idt_set_gate(25, _isr25, 0x28, 0x8E, 0);
	idt_set_gate(26, _isr26, 0x28, 0x8E, 0);
	idt_set_gate(27, _isr27, 0x28, 0x8E, 0);
	idt_set_gate(28, _isr28, 0x28, 0x8E, 0);
	idt_set_gate(29, _isr29, 0x28, 0x8E, 0);
	idt_set_gate(30, _isr30, 0x28, 0x8E, 0);
	idt_set_gate(31, _isr31, 0x28, 0x8E, 0);

	idt_set_gate(32, _isr32, 0x28, 0x8E, 1); /* PIT System Clock IRQ 0 */
	idt_set_gate(128, _isr128, 0x08, 0x8E, 1); /* Legacy system call entry point, called by userspace. */

	idt_reload();
}

void idt_reload(void) {
	asm volatile (
		"lidt %0"
		: : "m"(idtp)
	);
}

void fatal(void) {
	asm ("cli");
	for(;;) {
		asm volatile (
			"hlt\n"
		);
	}
}

spinlock_t paniclock = SPINLOCK_ZERO;

/**
 * Handle fatal exceptions.
 * 
 * prints information and cause of the panic and dumps register state
 * 
 * @param desc Textual description
 * @param r Interrupt register context
 * @param faulting_address When available, the address leading to this fault
*/
static void panic(const char* desc, struct regs* r) {
	spinlock_acquire(&paniclock);
	kprintf("\nJeff kernel panic! (%s) at %p\n", desc, r->rip);

	kprintf("Registers at interrupt:\n");
	kprintf("  $rip=0x%016lx\n", r->rip);
	kprintf("  $rsi=0x%016lx, $rdi=0x%016lx, $rbp=0x%016lx, $rsp=0x%016lx\n",
		r->rsi, r->rdi, r->rbp, r->rsp);
	kprintf("  $rax=0x%016lx, $rbx=0x%016lx, $rcx=0x%016lx, $rdx=0x%016lx\n",
		r->rax, r->rbx, r->rcx, r->rdx);
	kprintf("  $r8=0x%016lx,  $r9=0x%016lx,  $r10=0x%016lx, $r11=0x%016lx\n",
		r->r8, r->r9, r->r10, r->r11);
	kprintf("  $r12=0x%016lx, $r13=0x%016lx, $r14=0x%016lx, $r15=0x%016lx\n",
		r->r12, r->r13, r->r14, r->r15);
	kprintf("  cs=0x%016lx ss=0x%016lx rflags=0x%016lx int=0x%02lx err=0x%02lx\n",
		r->cs, r->ss, r->rflags, r->int_no, r->err_code);
	
	uint32_t gs_base_low, gs_base_high;
	asm volatile ( "rdmsr" : "=a" (gs_base_low), "=d" (gs_base_high): "c" (0xc0000101) );
	uint32_t kgs_base_low, kgs_base_high;
	asm volatile ( "rdmsr" : "=a" (kgs_base_low), "=d" (kgs_base_high): "c" (0xc0000102) );
	kprintf("  gs=0x%08x%08x kgs=0x%08x%08x\n",
		gs_base_high, gs_base_low, kgs_base_high, kgs_base_low);
	
	stacktrace();
	
	spinlock_release(&paniclock);
	fatal();
}

static void _exception(struct regs* r, const char* description) {
	if((r->cs & 0x3) == 0) {
	 	panic(description, r);
	}
}

irq_t irqs[IRQ_COUNT] = {0};

static void _handle_irq(struct regs* r, int irqIndex) {
	irq_t handler = irqs[irqIndex];
	if(!handler) {
		panic("Received IRQ without a handler", r);
	}
	handler();
}

#define EXC(i, n) case i: _exception(r, n); break;
#define IRQ(i) case i: _handle_irq(r, i - 32); break;

struct regs* isr_handler_inner(struct regs* r) {
	kdprintf("idt: Received interrupt on %lu at address %p\n", r->int_no, r->rip);
	switch (r->int_no) {
		EXC(0, "divide-by-zero")
		EXC(3, "breakpoint") /* TODO: map to ptrace event */
		EXC(4, "overflow")
		EXC(5, "bound range exceeded")
		EXC(6, "invalid opcode")
		EXC(7, "device not available")
		case 8: break; // TODO: Make a handler
		EXC(10, "invalid TSS")
		EXC(11, "segment not present")
		EXC(12, "stack-segment fault")
		case 13: panic("Double fault", r); break;
		EXC(14, "page fault") // TODO: Make a handler
		EXC(16, "floating point exception")
		EXC(17, "alignment check")
		EXC(18, "machine check")
		EXC(19, "SIMD floating-point exception")
		EXC(20, "virtualization exception")
		EXC(21, "control protection exception")
		EXC(28, "hypervisor injection exception")
		EXC(29, "mmu communication exception")
		EXC(30, "security exception")

		IRQ(32);

		default: panic("Unexpected interrupt", r);
	}

	return r;
}

struct regs* isr_handler(struct regs* r) {
    struct regs* out = isr_handler_inner(r);
    
	return out;
}