#include <stdint.h>
#include <stddef.h>
#include <limine.h>
#include <deps/printf.h>

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
    printf("Kernel start...\n");

    // We are done. Hang up
    asm ("cli");
    for(;;) asm ("hlt");
}