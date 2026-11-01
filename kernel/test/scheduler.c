#include <kernel/userspace.h>
/* Integration checks run as actual scheduled threads, not simulated contexts.
 * Test objects are permanent where timer callbacks retain pointers to them. */
#include <kernel/runtime.h>
#include <kernel/cpu.h>
#include <kernel/scheduler.h>
#include <kernel/time.h>
#include <kernel/heap.h>
#include <kernel/panic.h>
#include <platform/serial.h>
#include <kernel/smp.h>

static frame_allocator_t *test_allocator;
static const boot_memory_map_t *test_memory_map;
static wait_queue_t watchdog_queue;
static wait_queue_t worker_queue;
static wait_queue_t alarm_queue;
static bool stop_workers;
static volatile bool worker_condition;
static volatile bool alarm_fired;
static volatile uint64_t alarm_deadline;
static volatile uint64_t broadcast_deadline;
static unsigned long worker_progress[2];
static unsigned long awakened_workers;
static unsigned long batch_completions;
extern unsigned long arm64_verify_saved_registers(void);

/* Output checks without borrowing normal formatted logging or new allocations. */
static void scheduler_write_value(const char *label, unsigned long value) {
    const char *digits = "0123456789abcdef";
    char formatted[19] = "0x0000000000000000";

    for (unsigned int digit_index = 0; digit_index < 16; ++digit_index) {
        formatted[17 - digit_index] = digits[value & 15];
        value >>= 4;
    }

    platform_serial_write(label);
    platform_serial_write(formatted);
    platform_serial_write("\n");
}

/* This IRQ callback wakes sleepers but never allocates or switches stacks.
 * Switching is deferred to the dispatch tail after EOI/depth teardown. */
static void scheduler_test_tick(void *context) {
    (void)context;
#ifdef ARM64_TEST_SCHEDULER_IRQ
    thread_block(&alarm_queue); /* Must be rejected before any queue mutation. */
#endif
    uint64_t now = ktime_get_ns();

    if (alarm_deadline && now >= alarm_deadline) {
        alarm_deadline = 0;
        alarm_fired = true;
        wait_queue_wake_one(&alarm_queue);
    }

    if (broadcast_deadline && now >= broadcast_deadline) {
        broadcast_deadline = 0;
        worker_condition = true;
        wait_queue_wake_all(&worker_queue);
    }

    wait_queue_wake_all(&watchdog_queue);
    scheduler_timer_tick(NULL);
}

/* An expired predicate releases the coordinator to report a bounded failure. */
typedef struct {
    unsigned long *counter;
    unsigned long target;
    uint64_t deadline;
} completion_wait_t;

/* Counter updates are atomic; the timer supplies periodic watchdog wakeups. */
static bool completion_ready(void *context) {
    completion_wait_t *completion = context;

    return __atomic_load_n(completion->counter, __ATOMIC_ACQUIRE) >= completion->target ||
           ktime_get_ns() >= completion->deadline;
}

/* Wait without busy-yielding so the idle thread and IRQ wake paths also run. */
static void wait_for_completions(unsigned long *counter, unsigned long target) {
    completion_wait_t completion = {counter, target, ktime_get_ns() + 1000000000UL};
    thread_wait_until(&watchdog_queue, completion_ready, &completion);
    KERNEL_ASSERT(__atomic_load_n(counter, __ATOMIC_ACQUIRE) >= target);
}

/* Reaper completion means stack and descriptor release, not merely thread exit. */
static void wait_for_reaping(unsigned long target) {
    uint64_t deadline = ktime_get_ns() + 1000000000UL;

    while (scheduler_reaped_threads() < target && ktime_get_ns() < deadline) {
        schedule();
    }

    KERNEL_ASSERT(scheduler_reaped_threads() >= target);
    KERNEL_ASSERT(!kernel_interrupt_depth() && !kernel_spinlock_depth());
}

/* No voluntary yields: both workers need timer preemption to make progress. */
static void preemption_worker(unsigned int worker_index) {
    uint64_t deadline = ktime_get_ns() + 1000000000UL;

    while (!__atomic_load_n(&stop_workers, __ATOMIC_ACQUIRE)) {
        __atomic_fetch_add(&worker_progress[worker_index], 1, __ATOMIC_RELAXED);
        KERNEL_ASSERT(ktime_get_ns() < deadline);
        KERNEL_ASSERT(this_cpu()->cpu_id == 0 && this_cpu()->current_thread != NULL);
    }
}

/* Distinct no-argument entry points match the public kernel-thread API. */
static void preemption_worker_first(void) {
    preemption_worker(0);
}

static void preemption_worker_second(void) {
    preemption_worker(1);
}

/* Keep another runnable thread available during the assembly context test. */
static void cooperative_worker(void) {
    while (!__atomic_load_n(&stop_workers, __ATOMIC_ACQUIRE)) {
        schedule();
    }
}

/* Predicates are evaluated with IRQs masked, closing check-before-sleep races. */
static bool workers_ready(void *context) {
    (void)context;

    return worker_condition;
}

/* Return from entry exercises automatic thread_exit in the ARM trampoline. */
static void waiting_worker(void) {
    thread_wait_until(&worker_queue, workers_ready, NULL);
    KERNEL_ASSERT(!kernel_interrupt_depth() && !kernel_spinlock_depth());
    __atomic_fetch_add(&awakened_workers, 1, __ATOMIC_RELEASE);
}

/* Count queue membership under its owner lock, never via freed thread pointers. */
static unsigned long waiter_count(wait_queue_t *queue) {
    return scheduler_waiter_count(queue);
}

/* Let all three workers enter their wait queue before issuing one/all wakeups. */
static void wait_for_waiters(unsigned long target) {
    uint64_t deadline = ktime_get_ns() + 1000000000UL;

    while (waiter_count(&worker_queue) != target && ktime_get_ns() < deadline) {
        schedule();
    }

    KERNEL_ASSERT(waiter_count(&worker_queue) == target);
}

/* Spawn transfers descriptor/stack ownership to the scheduler and reaper. */
static void spawn_worker(void (*entry)(void)) {
    thread_t *worker = thread_create(&kernel_process, entry);
    KERNEL_ASSERT(worker != NULL && (worker->kstack_top & 15) == 0);
    thread_ready_on(worker, 0);
}

/* Small heap work verifies resumed threads are no longer marked as IRQ context. */
static void batch_worker(void) {
    unsigned long *allocation = malloc(64);
    KERNEL_ASSERT(allocation != NULL);
    allocation[0] = (unsigned long)this_cpu()->current_thread->tid;
    schedule();
    KERNEL_ASSERT(allocation[0] == (unsigned long)this_cpu()->current_thread->tid);
    free(allocation);
    __atomic_fetch_add(&batch_completions, 1, __ATOMIC_RELEASE);
}

/* Hold the entire batch CREATED first, guaranteeing the same peak heap/cache
 * occupancy in both waves even if timer interrupts arrive during creation. */
static void run_batch(void) {
    thread_t *created_threads[16];
    unsigned long target = scheduler_reaped_threads() + 16;

    for (unsigned int thread_index = 0; thread_index < 16; ++thread_index) {
        created_threads[thread_index] = thread_create(&kernel_process, batch_worker);
        KERNEL_ASSERT(created_threads[thread_index] != NULL);
    }

    for (unsigned int thread_index = 0; thread_index < 16; ++thread_index) {
        thread_ready(created_threads[thread_index]);
    }

    wait_for_reaping(target);
}

/* A lone coordinator sleeps until a timer IRQ wakes it from the idle thread. */
static bool alarm_ready(void *context) {
    (void)context;

    return alarm_fired;
}

/* Verify cooperative state, genuine preemption, all wake paths, and reclamation. */
static void scheduler_coordinator(void) {
    KERNEL_ASSERT(this_cpu() == cpu_core_local && this_cpu()->cpu_id == 0);
    KERNEL_ASSERT(this_cpu()->current_thread->state == THREAD_RUNNING);
#ifdef ARM64_TEST_SCHEDULER_LOCK
    spinlock_t forbidden_lock = SPINLOCK_ZERO;
    spinlock_acquire(&forbidden_lock);
    thread_block(&alarm_queue); /* Must panic rather than sleep with a lock held. */
#endif
    /* SIMD enlarged descriptors can fill the first slab page with idle,
     * reaper and coordinator contexts. Warm one cancellation before measuring. */
    thread_t *warm_cancelled = thread_create(&kernel_process, batch_worker);
    KERNEL_ASSERT(warm_cancelled);
    thread_destroy(warm_cancelled);

    unsigned long frames_before_cancel = test_allocator->free_frames;
    unsigned int threads_before_cancel = kernel_process.thread_count;
    KERNEL_ASSERT(thread_create(NULL, NULL) == NULL);

    thread_t *cancelled = thread_create(&kernel_process, batch_worker);
    KERNEL_ASSERT(cancelled != NULL);
    thread_destroy(cancelled);
    KERNEL_ASSERT(test_allocator->free_frames == frames_before_cancel &&
                  kernel_process.thread_count == threads_before_cancel);
    platform_serial_write("ARM64 thread cancellation passed\n");

    unsigned long reaped_before = scheduler_reaped_threads();
    spawn_worker(cooperative_worker);
    KERNEL_ASSERT(arm64_verify_saved_registers() == 0);
    __atomic_store_n(&stop_workers, true, __ATOMIC_RELEASE);
    wait_for_reaping(reaped_before + 1);
    platform_serial_write("ARM64 thread contexts passed\n");

    __atomic_store_n(&stop_workers, false, __ATOMIC_RELEASE);
    reaped_before = scheduler_reaped_threads();
    spawn_worker(preemption_worker_first);
    spawn_worker(preemption_worker_second);

    uint64_t deadline = ktime_get_ns() + 100000000UL;

    /* Deliberately do not yield: the coordinator must itself be preempted. */
    while (ktime_get_ns() < deadline) {
        kernel_cpu_relax();
    }

    KERNEL_ASSERT(__atomic_load_n(&worker_progress[0], __ATOMIC_RELAXED) &&
                  __atomic_load_n(&worker_progress[1], __ATOMIC_RELAXED) &&
                  scheduler_timer_switches() >= 4);
    __atomic_store_n(&stop_workers, true, __ATOMIC_RELEASE);
    wait_for_reaping(reaped_before + 2);
    platform_serial_write("ARM64 preemption passed\n");

    reaped_before = scheduler_reaped_threads();

    for (unsigned int worker_index = 0; worker_index < 3; ++worker_index) {
        spawn_worker(waiting_worker);
    }

    wait_for_waiters(3);

    bool saved_interrupts = irq_save();
    worker_condition = true;
    wait_queue_wake_one(&worker_queue);
    irq_restore(saved_interrupts);
    wait_for_completions(&awakened_workers, 1);
    wait_for_reaping(reaped_before + 1);
    KERNEL_ASSERT(__atomic_load_n(&awakened_workers, __ATOMIC_RELAXED) == 1 &&
                  waiter_count(&worker_queue) == 2);
    wait_queue_wake_all(&worker_queue);
    wait_for_completions(&awakened_workers, 3);
    wait_for_reaping(reaped_before + 3);

    /* A preexisting true condition must not sleep or require another wakeup. */
    thread_wait_until(&worker_queue, workers_ready, NULL);

    saved_interrupts = irq_save();
    worker_condition = false;
    irq_restore(saved_interrupts);
    reaped_before = scheduler_reaped_threads();

    for (unsigned int worker_index = 0; worker_index < 3; ++worker_index) {
        spawn_worker(waiting_worker);
    }

    wait_for_waiters(3);
    saved_interrupts = irq_save();
    broadcast_deadline = ktime_get_ns() + 20000000UL;
    irq_restore(saved_interrupts);
    wait_for_completions(&awakened_workers, 6);
    wait_for_reaping(reaped_before + 3);
    platform_serial_write("ARM64 wait queues passed\n");

    saved_interrupts = irq_save();
    alarm_fired = false;
    alarm_deadline = ktime_get_ns() + 20000000UL;
    irq_restore(saved_interrupts);
    thread_wait_until(&alarm_queue, alarm_ready, NULL);
    KERNEL_ASSERT(scheduler_idle_entries() && !kernel_interrupt_depth());
    platform_serial_write("ARM64 idle wakeup passed\n");

    run_batch();

    unsigned long free_after_warmup = test_allocator->free_frames;
    run_batch();
    KERNEL_ASSERT(__atomic_load_n(&batch_completions, __ATOMIC_RELAXED) == 32 &&
                  kernel_process.thread_count == coreCount + 2);
    KERNEL_ASSERT(test_allocator->free_frames == free_after_warmup);
    KERNEL_ASSERT(kernel_timer_set_hook(scheduler_timer_tick, NULL));
    platform_serial_write("ARM64 thread reaping passed\n");
    KERNEL_ASSERT(arm64_smp_start(test_memory_map));
    arm64_smp_validate(test_allocator);
    scheduler_write_value("SMP_REAPED ", scheduler_reaped_threads());
    arm64_user_validate(test_allocator);
    arm64_userspace_boot(test_allocator);
    scheduler_write_value("SCHEDULER_SWITCHES ", scheduler_context_switches());
    scheduler_write_value("SCHEDULER_PREEMPTIONS ", scheduler_timer_switches());
    scheduler_write_value("SCHEDULER_REAPED ", scheduler_reaped_threads());
    platform_serial_write("ARM64 scheduler passed\nARM64 idle\n");

    /* Return automatically exits/reaps the coordinator, leaving idle + reaper. */
}

/* Replace the runtime validation hook only after all queues are initialized.
 * The boot stack is permanent reserved memory, not a reapable thread stack. */
__attribute__((noreturn)) void arm64_scheduler_start(
    frame_allocator_t *allocator, const boot_memory_map_t *memory_map) {
    irq_save();
    KERNEL_ASSERT(arm64_cpu_configure(memory_map));
    test_allocator = allocator; /* Borrow permanent boot storage, never a stack object. */
    test_memory_map = memory_map;
    wait_queue_init(&watchdog_queue);
    wait_queue_init(&worker_queue);
    wait_queue_init(&alarm_queue);
    scheduler_init();
    spawn_worker(scheduler_coordinator);
    scheduler_start_reaper();
    KERNEL_ASSERT(arm64_smp_initialize_ipis());
    KERNEL_ASSERT(kernel_timer_set_hook(scheduler_test_tick, NULL));
    scheduler_enter();
}
