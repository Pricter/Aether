#include <stdint.h>
#include <stddef.h>
#include <limine.h>
#include <kernel/kprintf.h>
#include <kernel/mmu.h>
#include <kernel/cpu.h>
#include <kernel/gdt.h>
#include <kernel/irq.h>

extern void lapic_init(void);

/* Request limine for all cores information */
static volatile struct limine_smp_request smp_request = {
	.id = LIMINE_SMP_REQUEST,
	.revision = 0,
	.flags = 0,
};

/* Number of cores initialized */
static uint64_t initialized = 0;

/* Local core list to keep track of cores */
core_t *cpu_core_local = NULL;

void core_start(struct limine_smp_info *core) {
	core_t *core_local = (core_t*)core->extra_argument;

	/* Set the struct fields to their appropriate values */
	core_local->lapic_id = core->lapic_id;
	core_local->pagemap = mmu_kernel_pagemap;

	/* Load gdt in the core */
	gdt_reload();

	/* Load idt in the core */
	idt_reload();

	/* Load pagemap in the core */
	mmu_switch_pagemap(mmu_kernel_pagemap);

	kdprintf("smp: Processor #%ld online\n", core_local->core_number);

	/* Add the initialized statement */
	initialized++;

	/* Exiting from this causes a triple fault */
	if(!core_local->bsp) halt();
}

void smp_init(void) {
	struct limine_smp_response *smp_response = smp_request.response;
	struct limine_smp_info **cpu_cores = smp_response->cpus;

	/* Local array to keep track of the cores */
	cpu_core_local = malloc(sizeof(core_t) * smp_response->cpu_count);

	/* Loop through all the cores */
	for(uint64_t i = 0; i < smp_response->cpu_count; i++) {
		struct limine_smp_info* core = cpu_cores[i];

		/* Get the core_t from cpu_core_local */
		core_t* current = &cpu_core_local[i];
		current->core_number = i;

		core->extra_argument = (uint64_t)current;
		
		/* If core is bsp then goto the function */
		if(core->lapic_id != smp_response->bsp_lapic_id) {
			core->goto_address = core_start; /* Jump to core start */
		} else {
			current->bsp = true;
			core_start(core);
		}
	}

	/* Safe guard, core 0 does not jump to core_start as it is already running */
	while(initialized != smp_response->cpu_count - 1) {
		asm ("pause");
	}
}