#include <kernel/time.h>
#include <kernel/interrupts.h>
#include <kernel/cpu.h>

static uint64_t counter_frequency;
static uint64_t counter_origin;

/* ISB orders surrounding instructions before sampling the physical counter. */
static uint64_t read_counter(void) {
    uint64_t counter;
    __asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(counter)::"memory");

    return counter;
}

/* The counter frequency is architectural data, independent of timer IRQ rate. */
bool kernel_time_initialize(void) {
    if (counter_frequency || kernel_interrupt_depth()) {
        return false;
    }

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(counter_frequency));

    if (!counter_frequency || counter_frequency > 0xffffffffUL) {
        counter_frequency = 0;

        return false;
    }

    counter_origin = read_counter();

    return true;
}

/* Quotient/remainder conversion avoids overflowing counter*1e9 at runtime. */
uint64_t ktime_get_ns(void) {
    if (!counter_frequency) {
        return 0;
    }

    uint64_t elapsed = read_counter() - counter_origin;

    return (elapsed / counter_frequency) * NANOSECONDS_PER_SECOND +
           ((elapsed % counter_frequency) * NANOSECONDS_PER_SECOND) / counter_frequency;
}

/* Stable frequency, published once before normal IRQ delivery begins. */
uint64_t kernel_time_frequency(void) {
    return counter_frequency;
}

/* Rearm before EOI; skip elapsed periods so delayed delivery cannot storm. */
static void physical_timer_interrupt(void *context) {
    (void)context;

    core_t *cpu = this_cpu();
    uint64_t now = read_counter();
    cpu->timer_deadline += cpu->timer_interval;

    if ((int64_t)(cpu->timer_deadline - now) <= 0) {
        cpu->timer_deadline +=
            ((now - cpu->timer_deadline) / cpu->timer_interval + 1) * cpu->timer_interval;
    }

    __asm__ volatile("msr cntp_cval_el0, %0; isb" ::"r"(cpu->timer_deadline) : "memory");
    __atomic_fetch_add(&this_cpu()->timer_ticks, 1, __ATOMIC_RELAXED);

    if (cpu->timer_hook) {
        cpu->timer_hook(cpu->timer_context);
    }
}

/* Start with IRQs masked until callback state and timer comparator are valid. */
bool kernel_timer_start(unsigned int frequency_hz) {
    core_t *cpu = this_cpu();

    if (!counter_frequency || !frequency_hz || frequency_hz > counter_frequency ||
        cpu->timer_running || kernel_interrupt_depth()) {
        return false;
    }

    bool previously_enabled = kernel_irq_save();
    cpu->timer_interval = counter_frequency / frequency_hz;
    cpu->timer_deadline = read_counter() + cpu->timer_interval;

    if (!kernel_irq_register(ARM64_PHYSICAL_TIMER_IRQ, physical_timer_interrupt, NULL)) {
        kernel_irq_restore(previously_enabled);

        return false;
    }

    cpu->timer_running = true;
    __asm__ volatile(
        "msr cntp_cval_el0, %0; mov x9, #1; msr cntp_ctl_el0, x9; isb" ::"r"(cpu->timer_deadline)
        : "x9", "memory");
    kernel_irq_restore(previously_enabled);

    return true;
}

/* Disable the level source before removing its interrupt callback. */
void kernel_timer_stop(void) {
    core_t *cpu = this_cpu();

    if (!cpu->timer_running || kernel_interrupt_depth()) {
        return;
    }

    bool previously_enabled = kernel_irq_save();
    __asm__ volatile("msr cntp_ctl_el0, xzr; isb" ::: "memory");
    kernel_irq_unregister(ARM64_PHYSICAL_TIMER_IRQ);
    cpu->timer_running = false;
    kernel_irq_restore(previously_enabled);
}

/* Counting delivered interrupts is intentionally distinct from monotonic time. */
uint64_t kernel_timer_ticks(void) {
    return __atomic_load_n(&this_cpu()->timer_ticks, __ATOMIC_RELAXED);
}

/* Hooks are borrowed; masked publication prevents callback/context mismatch. */
bool kernel_timer_set_hook(void (*hook)(void *context), void *context) {
    core_t *cpu = this_cpu();

    if (kernel_interrupt_depth()) {
        return false;
    }

    bool previously_enabled = kernel_irq_save();
    cpu->timer_hook = hook;
    cpu->timer_context = context;
    kernel_irq_restore(previously_enabled);

    return true;
}
