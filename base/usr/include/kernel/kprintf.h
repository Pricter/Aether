#pragma once

#include <stdint.h>
#include <stddef.h>

void printf_init(void);

void kprintf(const char* fmt, ...);
void klog(const char* fmt, ...);

static inline void clear_screen(void) {
	kprintf("\033[2J");
}

static inline void reset_cursor(void) {
	kprintf("\x1b[0;0H");
}