/**
 * kernel.c - Licensed under the MIT License
 * 
 * Start point of the kernel
 * Initializes the kernel
*/

#include <stdint.h>
#include <stddef.h>
#include <limine.h>

#include <deps/printf.h>
#include <kernel/version.h>
#include <kernel/mmu.h>
#include <kernel/irq.h>
#include <string.h>
#include <kernel/symboltable.h>

extern void gdt_init(void);
extern void mmu_init(void);
extern void idt_init(void);

extern unsigned long tsc_mhz;

static volatile struct limine_terminal_request terminal_request = {
    .id = LIMINE_TERMINAL_REQUEST,
    .revision = 0,
};

/* THIS IS DEPRECATED, REMOVE AFTER USING FLANTERM */
void putchar_(char c) {
    struct limine_terminal *terminal = terminal_request.response->terminals[0];
    char s[2] = {c, '\0'};
    terminal_request.response->write(terminal, s, 2);
}


static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

/**
 * The kernel start function. The kernel begins executing from
 * this function, this is called by the limine bootloader.
 * 
 * It prints the kernel information and initializes the kernel
*/
void _start(void) {
    printf("%s %d.%d.%d-%s %s compiled by \"%s\" on \"%s %s\"\n",
        __kernel_name,
        __kernel_version_major,
        __kernel_version_minor,
        __kernel_version_lower,
        __kernel_version_suffix,
        __kernel_arch,
        __kernel_compiler_version,
        __kernel_build_date,
        __kernel_build_time);

	gdt_init();
    idt_init();

	/* Initialize memory */
    mmu_init();

	// TODO: Framebuffer and terminal dont work, fix
	struct limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];
	for (size_t i = 0; i < 100; i++) {
        uint32_t *fb_ptr = framebuffer->address;
        fb_ptr[i * (framebuffer->pitch / 4) + i] = 0xffffff;
    }

	// printf("symbols: Start at %p\n", kernel_symbols_start);
	// printf("symbols: End at %p\n", kernel_symbols_end);

	// printf("Kernel symbols:\n");
	// kernel_symbol_t* k = (kernel_symbol_t*)&kernel_symbols_start;
	// while((uintptr_t)k < (uintptr_t)&kernel_symbols_end) {
	// 	printf("\t(%p) %s\n", k->addr, k->name);
	// 	k = (kernel_symbol_t*)((uintptr_t)k + sizeof(*k) + strlen(k->name) + 1);
	// }

    // We are done. Hang up
    asm ("cli");
    for(;;) asm ("hlt");
}