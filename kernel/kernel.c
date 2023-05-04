#include <stdint.h>
#include <stddef.h>
#include <limine.h>

static volatile struct limine_terminal_request terminal_request = {
    .id = LIMINE_TERMINAL_REQUEST,
    .revision = 0,
};

static void hcf(void) {
    asm ("cli");
    for (;;) {
        asm ("hlt");
    }
}

void _start(void) {
    /* THIS IS DEPRECATED, REMOVE AFTER IMPLEMENTING TEFF */
    struct limine_terminal *terminal = terminal_request.response->terminals[0];
    terminal_request.response->write(terminal, "Hello, World!\n", 15);

    // We are done. Hang up
    asm ("cli");
    for(;;) asm ("hlt");
}