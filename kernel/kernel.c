#include <stdint.h>
#include <stddef.h>
#include <limine.h>
#include <deps/printf.h>
#include <kernel/version.h>

extern void arch_clock_initialize(void);

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


    arch_clock_initialize();

    // We are done. Hang up
    asm ("cli");
    for(;;) asm ("hlt");
}