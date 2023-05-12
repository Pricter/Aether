#include <kernel/spinlock.h>

void spinlock_acquire(spinlock_t *lock) {
    for (;;) {
        if (spinlock_test_and_acq(lock)) break;
#if defined (__x86_64__)
        asm volatile ("pause");
#endif
    }
    lock->last_acquirer = __builtin_return_address(0);
    return;
}