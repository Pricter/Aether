#include <kernel/kprintf.h>
#include <limine.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <deps/fb.h>
#include <deps/flanterm.h>
#include <deps/printf.h>
#include <kernel/spinlock.h>
#include <kernel/ports.h>
#include <kernel/cpu.h>
#include <kernel/init.h>

spinlock_t printlock = SPINLOCK_ZERO;

/* To crash if no framebuffers */
extern void fatal(void);

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

void _printf_wrap_free(void* ptr, size_t size) {
	(void)size;
	free(ptr);
}

#define COM1 0x3F8

/* Initialize flanterm */
void __init printf_init(void) {
	/* Enable DLAB (Divisor Latch Access Bit) */
	outportb(COM1 + 3, 0x80);

	/* Set divisor low byte (115200 baud) */
	outportb(COM1 + 0, 0x03);

	/* Set divisor high byte */
	outportb(COM1 + 1, 0x00);

	/* Set parity */
	outportb(COM1 + 3, 0x03);

	/* Enable FIFO, clear transmit and receive FIFO queues */
    outportb(COM1 + 2, 0xC7);

	/* Clear them */
    outportb(COM1 + 4, 0x0B);

	/* Enable interrupts */
	outportb(COM1 + 1, 0x01);

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
		malloc, _printf_wrap_free,
		framebuffer->address, framebuffer->width, framebuffer->height,
		framebuffer->pitch, NULL, NULL, &default_bg_black, &default_fg_yellow, NULL, NULL, NULL, 8, 16, 1, 1, 1, 0);
	
	/* We dont want the ugly block cursor */
	context->cursor_enabled = false;
}

void kprintf(const char* fmt, ...) {
	spinlock_acquire(&printlock);

	char buffer[1024];

	va_list args;
	va_start(args, fmt);

	int length = vsnprintf(buffer, 1024, fmt, args);
	flanterm_write(context, buffer, length);

#ifdef SERIAL_LOG
	for(int i = 0; i <= length; i++) {
		outportb(COM1, buffer[i]);
	}
#endif

	va_end(args);
	spinlock_release(&printlock);
}