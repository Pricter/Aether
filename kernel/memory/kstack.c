/* Thread-owned stacks use the backend's permanent RAM mapping. Releasing a
 * stack must never alter mappings shared by other address spaces or threads. */
#include <kernel/scheduler.h>
#include <kernel/page_memory.h>
#include <kernel/panic.h>

/* Transfer four contiguous pages to a new thread and return aligned stack top. */
uintptr_t kstack_alloc(void) {
    paddr_t physical_address;

    if (!page_memory_allocate(KSTACK_SIZE / KERNEL_PAGE_SIZE, &physical_address)) {
        return 0;
    }

    return (uintptr_t)page_memory_pointer(physical_address) + KSTACK_SIZE;
}

/* The reaper calls this only after switching away from the finished stack. */
void kstack_free(uintptr_t stack_top) {
    if (!stack_top) {
        return;
    }

    KERNEL_ASSERT((stack_top & (KERNEL_PAGE_SIZE - 1)) == 0);

    paddr_t physical_address = page_memory_physical((void *)(stack_top - KSTACK_SIZE));

    if (!page_memory_release(physical_address, KSTACK_SIZE / KERNEL_PAGE_SIZE)) {
        SUBSYS_PANIC("scheduler", "invalid kernel stack release");
    }
}
