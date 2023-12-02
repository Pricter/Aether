#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef volatile struct {
    int lock;
} spinlock_t;

#define SPINLOCK_ZERO {0}

static inline bool spinlock_test_and_acq(spinlock_t* lock) {
	return __sync_bool_compare_and_swap(&lock->lock, 0, 1);
}

bool spinlock_acquire(spinlock_t* lock);
void spinlock_release(spinlock_t* lock, bool int_state);