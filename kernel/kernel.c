#include <stdint.h>
#include <stddef.h>
#include <limine.h>

#include <kernel/kprintf.h>
#include <kernel/version.h>
#include <kernel/cpu.h>
#include <kernel/ports.h>
#include <kernel/cpufeature.h>
#include <stdbool.h>
#include <kernel/scheduler.h>
#include <kernel/hpet.h>
#include <kernel/apic.h>

extern void debug_printf_init(void);
extern void gdt_init(void);
extern void mmu_init(void);
extern void slab_init(void);
extern void idt_init(void);
extern void symbols_init(void);
extern void printf_init(void);
extern void smp_init(void);
extern void cpuinfo_init(void);
extern void acpi_init(void);
extern void hpet_init(void);
extern void cpu_feature_init(void);

void kinit_func(void) {
	for(;;) {
		thread_yield(false);
	}
}

/**
 * The kernel start function. The kernel begins executing from
 * this function, this is called by the limine bootloader.
 * 
 * It prints the kernel information and initializes the kernel
*/
void _start(void) {
	/* Initialize memory */
    mmu_init();

	/* Initialize gdt */
	gdt_init();
	
	/* Setup isrs */
    idt_init();

	/* Initialize the slab allocator */
	slab_init();

	/* Initialize printf */
	printf_init();

	/* Print kernel info */
	kprintf("%s %d.%d.%d-%s running on %s\n",
        __kernel_name,
        __kernel_version_major,
        __kernel_version_minor,
        __kernel_version_lower,
        __kernel_version_suffix,
        __kernel_arch);
	kprintf("Kernel compiled by \"%s\" on \"%s %s\"\n",
		__kernel_compiler_version,
		__kernel_build_date,
		__kernel_build_time);
	
	cpu_feature_init();

	acpi_init();

	/* Initialize kernel symbols */
	symbols_init();

	/* Load the CPU information */
	cpuinfo_init();

	/* Disable the PIC because we are using the ioapic */
	outportb(0xA1, 0xff);
	outportb(0x21, 0xff);

	/* Initialize the HPET timer */
	hpet_init();

	scheduler_init();
	struct thread* kinit = scheduler_new_kthread((void*)kinit_func, NULL, true);

	/* Initialize multicore */
	smp_init();

	halt();
}