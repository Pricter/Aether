#include <stdint.h>
#include <stddef.h>
#include <limine.h>
#include <kernel/kprintf.h>
#include <kernel/mmu.h>
#include <kernel/cpu.h>
#include <kernel/gdt.h>
#include <kernel/irq.h>
#include <kernel/init.h>
#include <kernel/cpufeature.h>
#include <kernel/apic.h>

uint32_t bsp_lapic_id = 0;
uint64_t coreCount = 0;

/* Request limine for all cores information */
static volatile struct limine_smp_request smp_request = {
	.id = LIMINE_SMP_REQUEST,
	.revision = 0,
	.flags = 1,
};

/* Number of cores initialized */
static uint64_t initialized = 0;

/* Local core list to keep track of cores */
core_t *cpu_core_local = NULL;

extern void lapic_irq_handler(struct regs*);
extern void lapic_init(void);

bool lapic_initialized = false;

void core_start(struct limine_smp_info *core) {
	core_t *core_local = (core_t*)core->extra_argument;

	/* Set the struct fields to their appropriate values */
	core_local->lapic_id = core->lapic_id;

	/* Load gdt in the core */
	gdt_reload();

	/* Load idt in the core */
	idt_reload();

	/* Load pagemap in the core */
	mmu_switch_pagemap(mmu_kernel_pagemap);

	/* Initialize LAPIC */
	static spinlock_t lock = SPINLOCK_ZERO;
	spinlock_acquire(&lock);
	if(get_cpu_feature_value(CPU_FEATURE_APIC) == 1) {
		lapic_init();
		lapic_initialized = true;
	}
	spinlock_release(&lock);

	kprintf("smp: Processor #%ld online\n", core_local->lapic_id);

	/* Add the initialized statement */
	initialized++;

	/* Exiting from this causes a triple fault */
	if(!core_local->bsp) for(;;);
}

void __init smp_init(void) {
	struct limine_smp_response *smp_response = smp_request.response;
	struct limine_smp_info **cpu_cores = smp_response->cpus;

	kprintf("smp: X2APIC Enabled? %s\n", smp_response->flags == 1 ? "true" : "false");

	coreCount = smp_response->cpu_count;

	/* Local array to keep track of the cores */
	cpu_core_local = malloc(sizeof(core_t) * coreCount);

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
	}

	/* Safe guard */
	while(initialized != coreCount) {
		asm ("pause");
	}
}