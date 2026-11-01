#pragma once

#include <kernel/types.h>
#include <kernel/frame_allocator.h>

#define KERNEL_PAGE_SIZE FRAME_SIZE

/* Allocate contiguous physical pages; caller owns successful results and must
 * release them or document permanent ownership. Contents are unspecified.
 * Return false/zero on exhaustion. Allocation/release use IRQ-safe innermost
 * frame-allocator locking. */
bool page_memory_allocate(unsigned long page_count, paddr_t *physical_address);
bool page_memory_release(paddr_t physical_address, unsigned long page_count);

/* Convert explicitly through the backend's permanent direct mapping. The kernel uses
 * permanent identity addresses. Call only for mapped, owned RAM pages. */
void *page_memory_pointer(paddr_t physical_address);
paddr_t page_memory_physical(const void *direct_pointer);

/* Bind permanent allocator storage once, after MMU activation and before SMP. */
void page_memory_initialize(frame_allocator_t *allocator);
