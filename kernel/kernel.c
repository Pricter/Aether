/**
 * kernel.c - Licensed under the MIT License
 * 
 * Start point of the kernel
 * Initializes the kernel
*/

#include <stdint.h>
#include <stddef.h>
#include <limine.h>

#include <kernel/kprintf.h>
#include <kernel/version.h>
#include <kernel/mmu.h>
#include <kernel/irq.h>
#include <string.h>
#include <kernel/symbols.h>
#include <string.h>

extern void debug_printf_init(void);
extern void gdt_init(void);
extern void mmu_init(void);
extern void slab_init(void);
extern void idt_init(void);
extern void symbols_init(void);
extern void printf_init(void);
extern void smp_init(void);
extern void cpuinfo_init(void);

extern uint64_t bytesOfBitmap;

/**
 * The kernel start function. The kernel begins executing from
 * this function, this is called by the limine bootloader.
 * 
 * It prints the kernel information and initializes the kernel
*/
void _start(void) {
	debug_printf_init();

	/* Initialize gdt */
	gdt_init();
	
	/* Setup isrs */
    idt_init();

	/* Initialize memory */
    mmu_init();

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
	kdprintf("Kernel compiled by \"%s\" on \"%s %s\"\n",
		__kernel_compiler_version,
		__kernel_build_date,
		__kernel_build_time);
	
	/* Initialize multicore */
	smp_init();

	/* Initialize kernel symbols */
	symbols_init();
	
	/* Load the CPU information */
	cpuinfo_init();

	asm ("int $0x20");

    // We are done. Hang up
    asm ("cli");
    for(;;) asm ("hlt");
}