#pragma once
#include <kernel/types.h>

#define NANOSECONDS_PER_SECOND 1000000000UL
#define ARM64_PHYSICAL_TIMER_IRQ 30U

/* Boot CPU initializes once from the shared architectural counter.
 * Monotonic nanoseconds since initialization, independent of IRQ delivery.
 * Reading the clock is allocation-free and safe in IRQ/panic context. */
bool kernel_time_initialize(void);
uint64_t ktime_get_ns(void);
uint64_t kernel_time_frequency(void);

/* Current CPU's periodic timer. Delivered ticks, not elapsed intervals, count.
 * Start/stop are normal-context operations; stop removes the IRQ registration.
 * Hook/context are borrowed until replaced while IRQs are masked. Hooks must
 * obey interrupt restrictions. Missed periods are skipped without an IRQ storm. */
bool kernel_timer_start(unsigned int frequency_hz);
void kernel_timer_stop(void);
uint64_t kernel_timer_ticks(void);
bool kernel_timer_set_hook(void (*hook)(void *context), void *context);
