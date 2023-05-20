#include <kernel/kprintf.h>
#include <limine.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <deps/fb.h>
#include <deps/flanterm.h>
#include <kernel/mmu.h>
#include <deps/printf.h>
#include <kernel/spinlock.h>

spinlock_t printlock = SPINLOCK_ZERO;

/* To crash if no framebuffers */
extern void fatal();

/* Needed by eyalroz printf */
void putchar_(char c) {
	kprintf("%c", c);
}

/* Request limine for a framebuffer to print to */
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

/* Framebuffer */
struct limine_framebuffer* framebuffer = NULL;

/* Printf context */
struct flanterm_context* context = NULL;

/* Initialize flanterm */
void printf_init(void) {
	/* Set the first framebuffer */
	framebuffer = framebuffer_request.response->framebuffers[0];

	/* If it is NULL then crash */
	if(framebuffer == NULL) {
		fatal();
	}

	/* Set default colors */
	uint32_t default_bg_black = 0x000000;
	uint32_t default_fg_yellow = 0xffff00;

	/* Initialize flanterm context */
	context = flanterm_fb_init(
		malloc,
		framebuffer->address, framebuffer->width, framebuffer->height,
		framebuffer->pitch, NULL, NULL, NULL, &default_bg_black, &default_fg_yellow, NULL, NULL, NULL, 0, 0, 0, 1, 1, 0);
	
	/* We dont want the ugly block cursor */
	context->cursor_enabled = false;
}

void kprintf(const char* fmt, ...) {
	spinlock_acquire(&printlock);

	char *buffer = malloc(1024);

	va_list args;
	va_start(args, fmt);

	int length = vsnprintf(buffer, 1024, fmt, args);
	flanterm_write(context, buffer, length);

	va_end(args);
	spinlock_release(&printlock);
}