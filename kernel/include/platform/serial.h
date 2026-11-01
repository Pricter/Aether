#pragma once

/* Polling early console: no allocation, locks, or interrupts required. */
void platform_serial_init(void);
void platform_serial_write(const char *message);

#include <stddef.h>
/* Emit exact buffer bytes; embedded NULs are data, not terminators. */
void platform_serial_write_bytes(const void *buffer, size_t length);
