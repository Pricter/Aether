#pragma once

#include <stddef.h>

/* Initialize after page allocation and its permanent direct map are available.
 * Cache pages are retained for the kernel lifetime. Single initialization only. */
void slab_init(void);

/* Normal context only; never allocate or free from an interrupt handler.
 * Allocation returns zero-filled, at least 8-byte-aligned caller-owned storage.
 * malloc(0) allocates a minimum object; free(NULL) is harmless. Failure returns
 * NULL on exhaustion. realloc failure preserves the
 * original allocation; realloc(pointer,0) frees it. No hidden sleeping occurs. */
void *malloc(size_t size);
void *realloc(void *address, size_t new_size);
void free(void *address);
