/**
 * smp.c: Handles the process of starting all other cores
 */

#include <stdint.h>
#include <stddef.h>
#include <limine.h>
#include <kernel/kprintf.h>
#include <kernel/mmu.h>
#include <kernel/cpu.h>
#include <kernel/int.h>
#include <kernel/macros.h>
#include <kernel/cpufeature.h>
#include <kernel/apic.h>
#include <kernel/msr.h>

uint32_t bsp_lapic_id = 0;
uint64_t coreCount = 0;

/* Request limine for all cores information */
__attribute__((used, section(".requests")))
static volatile struct limine_smp_request smp_request = {
	.id = LIMINE_SMP_REQUEST,
	.revision = 0,
	.flags = 1,
};

/* Number of cores initialized */
static uint64_t initialized = 0;

/* Local core list to keep track of cores */
core_t *cpu_core_local = NULL;

extern struct regs* lapic_irq_handler(struct regs* r);
extern void lapic_init(void);
extern void gdt_reload();

/* Spinlock to prevent cores from initializing simultaneously */
spinlock_t lock = SPINLOCK_ZERO;

/* Start's a single core, this function will run in the core being initialized */
void core_start(struct limine_smp_info *core) {
	/* Load gdt in the core */
	gdt_reload();

	/* Load idt in the core */
	idt_reload();

	bool int_state = spinlock_acquire(&lock);

	/* Load pagemap in the core */
	mmu_switch_pagemap(mmu_kernel_pagemap);

	/* Set GS register as local core */
	core_t *core_local = (core_t*)core->extra_argument;
	set_gs_register(core_local);

	/* Set the struct fields to their appropriate values */
	core_local->lapic_id = core->lapic_id;

	/* Initialize LAPIC */
	if(!cpu_has_feature(CPU_FEATURE_APIC)) {
		panic("LAPIC is not supported", NULL);
	}

	lapic_init();
	lapic_timer_calibrate(10000000);

	/* Enable SSE */
	asm volatile(
		"mov %%cr0, %%rax;"
		"and $0xFFFB, %%ax;"
		"or  $2, %%eax;"
		"mov %%rax, %%cr0;"
		"mov %%cr4, %%rax;"
		"or  $0b11000000000, %%rax;"
		"mov %%rax, %%cr4;"
		: : : "rax");

	/* set NE in cr0 and reset x87 fpu */
	asm volatile(
		"fninit;"
		"mov %%cr0, %%rax;"
		"or $0b100000, %%rax;"
		"mov %%rax, %%cr0;"
		: : : "rax"
	);


	kprintf("smp: Processor #%ld online\n", core_local->lapic_id);

	/* Add the initialized statement */
	initialized++;
	spinlock_release(&lock, int_state);

	/* Exiting from this causes a triple fault */
	if(core_local->bsp != true) {
		enable_interrupts();
		asm volatile ("1: hlt; jmp 1b");
	}
}

void __init smp_init(void) {
	/* Get Limine's smp info to initialize all cores */
	struct limine_smp_response *smp_response = smp_request.response;
	struct limine_smp_info **cpu_cores = smp_response->cpus;	

	kprintf("smp: X2APIC Enabled? %s\n", smp_response->flags == 1 ? "true" : "false");

	coreCount = smp_response->cpu_count;

	/* Local array to keep track of the cores */
	cpu_core_local = malloc(sizeof(core_t) * coreCount);
	if(((uintptr_t)cpu_core_local % _Alignof(core_t)) != 0) {
		/* cpu_core_local must be aligned or UBSAN will be actiavated */
		kprintf("cpu_core_local found unaligned. Core count: %d, Size of the struct: %d, size of allocated memory: %d, alignment: %d, address returned: %p\n",
			coreCount, sizeof(core_t), sizeof(cpu_core_local), _Alignof(core_t), cpu_core_local);
		fatal();
	}

	/* Get the ID of the BSP core */
	bsp_lapic_id = smp_response->bsp_lapic_id;

	irq_install(lapic_irq_handler, 32);

	/* Loop through all the cores */
	for(uint64_t i = 0; i < coreCount; i++) {
		struct limine_smp_info* core = cpu_cores[i];

		/* Get the core_t from cpu_core_local */
		core_t* current = &cpu_core_local[i];

		core->extra_argument = (uint64_t)current;

		/* If core is bsp then goto the function */
		if(core->lapic_id != smp_response->bsp_lapic_id) {
			core->goto_address = core_start; /* Jump to core start */
		} else {
			current->bsp = true;
			core_start(core);
		}

		/* Ensure smooth and ordered initialization of cores */
		while (initialized != (i + 1)) {
			asm ("pause");
		}
	}

	enable_interrupts();
}