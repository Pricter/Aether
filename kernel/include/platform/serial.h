#pragma once
#include <stdbool.h>
#include <stddef.h>

/* Polling early console: no allocation, locks, or interrupts required. */
void platform_serial_init(void);
void platform_serial_write(const char *message);

/* Emit exact buffer bytes; embedded NULs are data, not terminators. */
void platform_serial_write_bytes(const void *buffer, size_t length);

/* Start once on CPU 0 after IRQ/scheduler initialization. Borrows a permanent,
 * allocation-free IRQ callback, invoked after releasing the input lock.
 * Fixed 256-byte FIFO drops newest bytes on overflow; no allocation. */
bool platform_serial_start_input(void (*notify)(void));

/* Lock-free readiness snapshot, suitable for scheduler wait predicates. */
bool platform_serial_input_ready(void);

/* Nonblocking, IRQ-safe consume into caller-owned storage; takes input lock.
 * Returns up to length raw bytes; zero means no buffered input. */
size_t platform_serial_read_bytes(void *buffer, size_t length);

/* Serialized output without CR/LF translation, for terminal raw output mode. */
void platform_serial_write_raw_bytes(const void *buffer, size_t length);
