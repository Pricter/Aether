/**
 * idt.c: Interrupt Descriptor table
 * 
 * Handles the loading, initializing of idt
 * Handles setting ISRs, Directing interrupts to be handled as exceptions or IRQs
 */

#include <kernel/cpu.h>
#include <kernel/int.h>
#include <stdint.h>
#include <stddef.h>
#include <kernel/kprintf.h>
#include <kernel/symbols.h>
#include <kernel/spinlock.h>
#include <kernel/mmu.h>
#include <kernel/cpu.h>
#include <kernel/macros.h>

static struct idt_pointer idtp;
static idt_entry_t idt[256];

static uint8_t free_vector = 32;

/**
 * @brief Initialize a gate
 */
void idt_set_gate(uint8_t num, void* handler, uint16_t selector, uint8_t flags, int userspace) {
	uintptr_t base = (uintptr_t)handler;
	idt[num].base_low  = (base) & 0xFFFF;
	idt[num].base_mid  = (base >> 16) & 0xFFFF;
	idt[num].base_high = (base >> 32) & 0xFFFFFFFF;
	idt[num].selector = selector;
	idt[num].zero = 0;
	idt[num].pad = 0;
	idt[num].flags = flags | (userspace ? 0x60 : 0);
}

/**
 * @brief: Return the next free vector
 */
uint8_t idt_allocate(void) {
	static spinlock_t lock = SPINLOCK_ZERO;
	bool int_state = spinlock_acquire(&lock);

	if(free_vector == 255) {
		panic("IDT Vectors exhauted\n", NULL);
	}

	uint8_t ret = free_vector++;
	spinlock_release(&lock, int_state);
	return ret;
}

/* All cores share the same idt */
irq_t *irqs = NULL;

/* isrs are defined in the int.S file */
extern void *isrs[];

/**
 * @brief Initializes the IDT and sets up gates for all interrupts.
 */
void __init idt_init(void) {
	idtp.limit = sizeof(idt);
	idtp.base  = (uintptr_t)&idt;

	for(uint64_t i = 0; i < 256; i++) {
		idt_set_gate(i, isrs[i], 0x8, 0x8e, 0);
	}

	idt_reload();
	irqs = malloc(sizeof(irq_t) * IRQ_COUNT);
}

/* Load the IDT */
void idt_reload(void) {
	asm volatile (
		"lidt %0"
		: : "m"(idtp)
	);
}

/**
 * Install a function as irq handler for a vector
 * 
 * @param irq: Function with signature [regs* (*irq_t)(struct regs* r)], is called when irq is called at @param index
 */
void irq_install(irq_t irq, int index) {
	irqs[index - 32] = irq;
	kprintf("irq: Install IRQ %d to %p\n", index - 32, symbols_search((uintptr_t)irqs[index - 32]));
}

/* Exception handler, makes things tidy */
static void _exception(struct regs* r, const char* description) {
	if((r->cs & 0x3) == 0) {
	 	panic(description, r);
	}
}

/* Initial Common IRQ Handler, makes things tidy. Just calls the specific handler and returns */
struct regs* _handle_irq(struct regs* r, int irqIndex) {
	irq_t handler = irqs[irqIndex];
	if(!handler) {
		panic("Received IRQ without handler", r);
	}
	return handler(r);
}

#define EXC(i, n) case i: _exception(r, n); break;
#define IRQ(i) case i: return _handle_irq(r, i - 32);

/* Reference to gs segment, all access to core_local will be converted to access from %gs:0 */
static core_t __seg_gs const* core_local = 0;

/* Called by asm isr_common, directs isrs to be handled as exceptions or irqs */
struct regs* isr_handler(struct regs* r) {
    switch (r->int_no) {
		/* Exceptions */
		EXC(0, "divide-by-zero")
		EXC(3, "breakpoint")
		EXC(4, "overflow")
		EXC(5, "bound range exceeded")
		EXC(6, "invalid opcode")
		EXC(7, "device not available")
		case 8: panic("Double fault", r); break;
		EXC(10, "invalid TSS")
		EXC(11, "segment not present")
		EXC(12, "stack-segment fault")
		case 13: panic("General protection fault", r); break;
		case 14: panic("page fault", r); break;
		EXC(16, "floating point exception")
		EXC(17, "alignment check")
		EXC(18, "machine check")
		EXC(19, "SIMD floating-point exception")
		EXC(20, "virtualization exception")
		EXC(21, "control protection exception")
		EXC(28, "hypervisor injection exception")
		EXC(29, "mmu communication exception")
		EXC(30, "security exception")

		/* IRQs */
		IRQ(32);

		/* HALT Signal */
		case 255: {
			kprintf("Received halt signal on core %lu\n", core_local->lapic_id);
			asm ("1: hlt; jmp 1b");
		} break;

		/* Interrupt on unknown vector */
		default: {
			kprintf("int: Received Unexpected interrupt on core %lu, vector = %lu\n", core_local->lapic_id, r->int_no);
		} break;
	}

	return r;
}