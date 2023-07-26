#pragma once

#include <stdint.h>
#include <stddef.h>

void printf_init(void);
void set_print_color(uint32_t bg, uint32_t fg);
void set_print_cursor(size_t x, size_t y);

void kprintf(const char* fmt, ...);
void klog(const char* fmt, ...);