#include <kernel/page_memory.h>
#include <kernel/spinlock.h>
#include <kernel/irq_control.h>
#include <kernel/panic.h>

static frame_allocator_t *bound_allocator;
static spinlock_t frame_lock = SPINLOCK_ZERO;

/* Bind permanent allocator storage before any concurrent IRQ-driven work. */
void page_memory_initialize(frame_allocator_t *allocator) {
    if (bound_allocator || !allocator->initialized) {
        SUBSYS_PANIC("memory", "invalid page backend binding");
    }

    bound_allocator = allocator;
}

/* PMM locking is innermost; this backend never calls heap or scheduling code. */
bool page_memory_allocate(unsigned long page_count, paddr_t *physical_address) {
    *physical_address = 0;

    if (!bound_allocator) {
        return false;
    }

    bool interrupts_enabled = spinlock_acquire(&frame_lock);
    bool allocated = frame_allocator_allocate(bound_allocator, page_count, physical_address);
    spinlock_release(&frame_lock, interrupts_enabled);

    return allocated;
}

/* Release only caller-owned pages; invalid ranges leave allocator state intact. */
bool page_memory_release(paddr_t physical_address, unsigned long page_count) {
    if (!bound_allocator) {
        return false;
    }

    bool interrupts_enabled = spinlock_acquire(&frame_lock);
    bool released = frame_allocator_release(bound_allocator, physical_address, page_count);
    spinlock_release(&frame_lock, interrupts_enabled);

    return released;
}

/* Identity RAM mappings are permanent during the current ARM kernel stage. */
void *page_memory_pointer(paddr_t physical_address) {
    return (void *)physical_address;
}

/* Keep physical/virtual conversion explicit even while their values match. */
paddr_t page_memory_physical(const void *direct_pointer) {
    return (paddr_t)direct_pointer;
}
