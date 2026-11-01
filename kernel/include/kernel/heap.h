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

#define HEAP_CACHE_COUNT 9U
typedef struct {
    size_t retained_pages;
    size_t live_objects[HEAP_CACHE_COUNT];
} heap_snapshot_t;

/* Allocation-free diagnostics under each cache lock. Normal context, no locks
 * held. A globally consistent snapshot requires other heap users quiescent.
 * Retained cache pages are permanent ownership, not unreclaimed live objects. */
void heap_snapshot(heap_snapshot_t *snapshot);
