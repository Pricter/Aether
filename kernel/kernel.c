#include <stdint.h>
#include <stddef.h>
#include <limine.h>

#include <deps/printf.h>
#include <kernel/version.h>
#include <kernel/mmu.h>
#include <kernel/irq.h>

extern void clock_initialize(void);

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

void set_core_base(uintptr_t base) {
    asm volatile ("wrmsr" : : "c" (0xc0000101), "d" ((uint32_t)(base >> 32)), "a" ((uint32_t)(base & 0xFFFFFFFF)));
    asm volatile ("wrmsr" : : "c" (0xc0000102), "d" ((uint32_t)(base >> 32)), "a" ((uint32_t)(base & 0xFFFFFFFF)));
    asm volatile ("swapgs");
}

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

    /* Time and TSC and get the initial boot time from RTC. */
    clock_initialize();

    gdt_init();

    /* Initialize memory */
    mmu_init();

    idt_init();
    asm("int $0x0"); // For testing

    /* Test if the frame allocator is working */
    for(int i = 0; i < 2; i++) {
        printf("%p\n", mmu_request_frame());
    }

    // We are done. Hang up
    asm ("cli");
    for(;;) asm ("hlt");
}