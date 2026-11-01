#pragma once
#include <kernel/types.h>
#include <kernel/list.h>
#include <kernel/spinlock.h>
#include <kernel/irq_control.h>
#include <kernel/boot_memory.h>

struct thread;

/* TPIDR_EL1 selects immutable logical/MPIDR identity and CPU-owned IRQ/timer
 * state. Scheduler queue/current/previous fields use the global state lock.
 * Remote requests, online publication, and diagnostic counters use atomics.
 * CPU hotplug and automatic thread migration are not implemented. */
typedef struct core {
    uint64_t hardware_id;
    cpu_id_t cpu_id;
    bool bsp;
    unsigned int interrupt_depth;
    unsigned int spinlock_depth;
    bool reschedule_pending;
    bool preemption_switch;
    struct thread *current_thread;
    list_head_t run_queue;
    struct thread *idle_thread;
    struct thread *switch_previous;
    uintptr_t boot_stack_top;
    unsigned int gic_target_mask;
    bool online;
    bool ipis_ready;
    struct process *active_process; /* release-published after TTBR switch */
    uint64_t timer_interval;
    uint64_t timer_deadline;
    uint64_t timer_ticks;
    bool timer_running;
    void (*timer_hook)(void *context);
    void *timer_context;
    unsigned long reschedule_ipis;
    unsigned long panic_depth;
    bool panic_stopped;
} core_t;

extern uint64_t coreCount;
extern core_t *cpu_core_local;

/* Valid after boot CPU initialization, before any heap or IRQ registration. */
void arm64_cpu_initialize_boot(void);
bool arm64_cpu_configure(const boot_memory_map_t *memory_map);

/* TPIDR_EL1 is kernel per-CPU storage, distinct from userspace TLS. */
static inline core_t *this_cpu(void) {
    core_t *cpu;
    __asm__ volatile("mrs %0, tpidr_el1" : "=r"(cpu));

    return cpu;
}

/* IRQ primitives for scheduler execution; never alter other masks. */
static inline void enable_interrupts(void) {
    kernel_irq_restore(true);
}

static inline void disable_interrupts(void) {
    kernel_irq_restore(false);
}

static inline irq_state_t irq_save(void) {
    return kernel_irq_save();
}

static inline void irq_restore(irq_state_t enabled) {
    kernel_irq_restore(enabled);
}
