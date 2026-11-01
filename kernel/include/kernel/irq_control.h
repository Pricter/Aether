#pragma once
#include <kernel/types.h>

/* Save/disable IRQs only; FIQ, SError and debug masks stay unchanged. No heap or
 * per-CPU storage is required, so these primitives are valid during early boot. */
static inline irq_state_t kernel_irq_save(void) {
    unsigned long saved_flags;
    __asm__ volatile("mrs %0, daif; msr daifset, #2" : "=r"(saved_flags)::"memory");

    return !(saved_flags & (1UL << 7));
}

/* Restore the caller's exact IRQ enable state, including nested masking. */
static inline void kernel_irq_restore(irq_state_t previously_enabled) {
    if (previously_enabled) {
        __asm__ volatile("msr daifclr, #2" ::: "memory");
    } else {
        __asm__ volatile("msr daifset, #2" ::: "memory");
    }
}

/* Hardware spin hint, never a scheduling point. */
static inline void kernel_cpu_relax(void) {
    __asm__ volatile("yield");
}

/* CPU-local accounting prevents allocations/scheduling in interrupt context. */
unsigned long kernel_interrupt_depth(void);

/* Debug accounting enforces the no-lock-across-context-switch invariant. */
void kernel_spinlock_enter(void);
void kernel_spinlock_leave(void);
unsigned long kernel_spinlock_depth(void);
