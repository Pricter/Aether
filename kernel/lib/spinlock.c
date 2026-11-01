#include <kernel/spinlock.h>
#include <kernel/irq_control.h>

/* Disable IRQs before acquiring a ticket so the same CPU cannot reenter. */
bool spinlock_acquire(spinlock_t *lock) {
    bool interrupts_enabled = kernel_irq_save();

    /* Grab our position in line */
    uint16_t ticket = __atomic_fetch_add(&lock->next_ticket, 1, __ATOMIC_RELAXED);

    /* Spin until the lock holder finishes and serves our ticket.
     * ACQUIRE pairs with the RELEASE in spinlock_release, ensuring all
     * stores made inside the previous critical section are visible to us. */
    while (__atomic_load_n(&lock->now_serving, __ATOMIC_ACQUIRE) != ticket) {
        kernel_cpu_relax();
    }

    kernel_spinlock_enter();

    return interrupts_enabled;
}

/* Publish protected writes, then restore the caller's prior IRQ state. */
void spinlock_release(spinlock_t *lock, bool interrupts_enabled) {
    kernel_spinlock_leave();

    /* Advance now_serving to hand off to the next waiter.
     * RELEASE makes our critical-section stores visible before the handoff. */
    __atomic_fetch_add(&lock->now_serving, 1, __ATOMIC_RELEASE);
    kernel_irq_restore(interrupts_enabled);
}
