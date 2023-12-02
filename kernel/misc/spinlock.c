#include <kernel/spinlock.h>
#include <kernel/cpu.h>

bool spinlock_acquire(spinlock_t *lock) {
	bool int_state = interrupt_state();
	disable_interrupts();
	volatile size_t deadlock_counter = 0;
    for (;;) {
        if (spinlock_test_and_acq(lock)) break;
        if (++deadlock_counter >= 100000000) {
            goto deadlock;
        }
#if defined (__x86_64__)
        asm volatile ("pause");
#endif
    }
    return int_state;

deadlock:
	halt();
	return 0;
}

void spinlock_release(spinlock_t* lock, bool int_state) {
	__atomic_store_n(&lock->lock, 0, __ATOMIC_SEQ_CST);
	if (int_state == true) enable_interrupts();
}