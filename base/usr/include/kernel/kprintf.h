#pragma once

typedef void (*printf_type)(const char* fmt, ...);

/* Print to framebuffer, also to serial if gcc -DSERIAL_LOG is passed */
extern printf_type kprintf;

void printf_init(void);
void kernel_printf(const char* fmt, ...);