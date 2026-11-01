#include <kernel/smp.h>
#include <kernel/scheduler.h>
#include <kernel/time.h>
#include <kernel/heap.h>
#include <kernel/panic.h>
#include <platform/serial.h>

static wait_queue_t remote_queue;
static wait_queue_t turn_queue;
static unsigned long remote_ready;
static unsigned long remote_done;
static unsigned long turn;
static unsigned long exchanges;
static unsigned long stop_workers;
static unsigned long progress[BOOT_MAX_CPUS][2];
static unsigned long completed_workers;
static unsigned long isolated_timer_done;
#ifdef ARM64_TEST_HANDOFF
static thread_t *handoff_target;
static unsigned long handoff_paused;
static unsigned long handoff_resume;

/* Test-only pause occurs after state publication/unlock but BEFORE assembly
 * saves the outgoing context. CPU 0 must wake it without running it yet. */
void arm64_test_handoff_pause(thread_t *outgoing) {
    if (outgoing != __atomic_load_n(&handoff_target, __ATOMIC_ACQUIRE) ||
        __atomic_exchange_n(&handoff_paused, 1, __ATOMIC_ACQ_REL)) {
        return;
    }

    uint64_t deadline = ktime_get_ns() + 1000000000UL;

    while (!__atomic_load_n(&handoff_resume, __ATOMIC_ACQUIRE) && ktime_get_ns() < deadline) {
        kernel_cpu_relax();
    }

    KERNEL_ASSERT(__atomic_load_n(&handoff_resume, __ATOMIC_ACQUIRE));
}
#endif

/* Serialize diagnostics on CPU 0 only; workers never print or retain pointers. */
static void smp_write_value(const char *label, unsigned long value) {
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

/* Poll/yield only in bounded integration tests; this is not a sleep API. */
static void wait_counter(unsigned long *counter, unsigned long target) {
    uint64_t deadline = ktime_get_ns() + 2000000000UL;

    while (__atomic_load_n(counter, __ATOMIC_ACQUIRE) < target && ktime_get_ns() < deadline) {
        schedule();
    }

    KERNEL_ASSERT(__atomic_load_n(counter, __ATOMIC_ACQUIRE) >= target);
}

/* Reaped completion is published only after all stack/descriptor release. */
static void wait_reaped(unsigned long target) {
    uint64_t deadline = ktime_get_ns() + 2000000000UL;

    while (scheduler_reaped_threads() < target && ktime_get_ns() < deadline) {
        schedule();
    }

    KERNEL_ASSERT(scheduler_reaped_threads() >= target);
}

/* All test threads are pinned explicitly before publication. */
static void spawn_on(cpu_id_t cpu_id, void (*entry)(void)) {
    thread_t *thread = thread_create(&kernel_process, entry);
    KERNEL_ASSERT(thread != NULL);
    thread_ready_on(thread, cpu_id);
}

/* Stopping one CPU's timer must neither stop other CPUs nor its counter clock. */
static void timer_isolation_worker(void) {
    kernel_timer_stop();

    uint64_t ticks_before = kernel_timer_ticks();
    uint64_t start_time = ktime_get_ns();

    while (ktime_get_ns() - start_time < 50000000UL) {
        kernel_cpu_relax();
    }

    KERNEL_ASSERT(kernel_timer_ticks() == ticks_before);
    KERNEL_ASSERT(kernel_timer_start(200));
    __atomic_store_n(&isolated_timer_done, 1, __ATOMIC_RELEASE);
}

/* Each CPU needs its own periodic interrupt to share execution between these
 * non-yielding workers. Affinity and normal-context status must remain stable. */
static void busy_worker(unsigned int worker_index) {
    cpu_id_t expected_cpu = this_cpu()->cpu_id;
    uint64_t deadline = ktime_get_ns() + 2000000000UL;

    while (!__atomic_load_n(&stop_workers, __ATOMIC_ACQUIRE)) {
        KERNEL_ASSERT(this_cpu()->cpu_id == expected_cpu && !kernel_interrupt_depth());
        KERNEL_ASSERT(this_cpu()->current_thread->owner_cpu == expected_cpu);
        __atomic_fetch_add(&progress[expected_cpu][worker_index], 1, __ATOMIC_RELAXED);
        KERNEL_ASSERT(ktime_get_ns() < deadline);
    }
}

/* Public thread entries intentionally take no borrowed stack arguments. */
static void busy_first(void) {
    busy_worker(0);
}

static void busy_second(void) {
    busy_worker(1);
}

/* Atomic release/acquire publishes the producer condition before its wake. */
static bool remote_condition(void *context) {
    (void)context;

    return __atomic_load_n(&remote_ready, __ATOMIC_ACQUIRE) != 0;
}

/* With this CPU's timer stopped, only an SGI can resume the blocked thread.
 * A wake racing the context save must be deferred until on_cpu becomes false. */
static void remote_waiter(void) {
    KERNEL_ASSERT(this_cpu()->cpu_id == 1);
    kernel_timer_stop();
#ifdef ARM64_TEST_HANDOFF
    __atomic_store_n(&handoff_target, this_cpu()->current_thread, __ATOMIC_RELEASE);
#endif
    thread_wait_until(&remote_queue, remote_condition, NULL);
    KERNEL_ASSERT(this_cpu()->cpu_id == 1 && !kernel_interrupt_depth());
    KERNEL_ASSERT(kernel_timer_start(200));
    __atomic_store_n(&remote_done, 1, __ATOMIC_RELEASE);
}

/* This predicate runs under the scheduler state lock and acquires no locks. */
static bool turn_ready(void *context) {
    unsigned long desired_turn = (unsigned long)context;

    return __atomic_load_n(&turn, __ATOMIC_ACQUIRE) == desired_turn;
}

/* Rapid reciprocal wakeups exercise the before-save/after-save handoff window
 * while both CPUs allocate and free from the same slab caches and frame pool. */
static void exchange_worker(unsigned long worker_index) {
    for (unsigned long round = 0; round < 200; ++round) {
        thread_wait_until(&turn_queue, turn_ready, (void *)worker_index);
        KERNEL_ASSERT(this_cpu()->cpu_id == worker_index);

        unsigned long *payload = malloc(8192);
        KERNEL_ASSERT(payload != NULL);
        payload[0] = round ^ worker_index;
        schedule();
        KERNEL_ASSERT(payload[0] == (round ^ worker_index));
        free(payload);
        __atomic_fetch_add(&exchanges, 1, __ATOMIC_RELAXED);
        __atomic_store_n(&turn, 1 - worker_index, __ATOMIC_RELEASE);
        wait_queue_wake_all(&turn_queue);
    }

    __atomic_fetch_add(&completed_workers, 1, __ATOMIC_RELEASE);
}

/* Separate entry points pin exchange identities without temporary contexts. */
static void exchange_first(void) {
    exchange_worker(0);
}

static void exchange_second(void) {
    exchange_worker(1);
}

/* Simultaneous exits on multiple CPUs may be reaped only after their saves. */
static void exit_worker(void) {
    unsigned long *payload = malloc(64);
    KERNEL_ASSERT(payload != NULL);
    payload[0] = (unsigned long)this_cpu()->current_thread->tid;
    schedule();
    KERNEL_ASSERT(payload[0] == (unsigned long)this_cpu()->current_thread->tid);
    free(payload);
}

#ifdef ARM64_TEST_SMP_PANIC
/* The fatal owner is deliberately a secondary, not the boot coordinator. */
static void panic_worker(void) {
    SUBSYS_PANIC("smp", "intentional SMP panic");
}
#endif

/* Hold the full wave CREATED to warm identical descriptor-cache occupancy. */
static void exit_wave(void) {
    thread_t *created[BOOT_MAX_CPUS * 4];
    unsigned int count = (unsigned int)coreCount * 4;
    unsigned long target = scheduler_reaped_threads() + count;

    for (unsigned int thread_index = 0; thread_index < count; ++thread_index) {
        created[thread_index] = thread_create(&kernel_process, exit_worker);
        KERNEL_ASSERT(created[thread_index] != NULL);
    }

    for (unsigned int thread_index = 0; thread_index < count; ++thread_index) {
        thread_ready_on(created[thread_index], thread_index % coreCount);
    }

    wait_reaped(target);
}

/* Validate startup, independent timer programming, IPI-only wakeup, reciprocal
 * waits, shared heap work, and concurrent off-CPU reclamation on all cores. */
void arm64_smp_validate(frame_allocator_t *allocator) {
    smp_write_value("SMP_ONLINE ", coreCount);

    if (coreCount == 1) {
        platform_serial_write("ARM64 SMP skipped (one CPU)\n");

        return;
    }

    platform_serial_write("ARM64 SMP startup passed\n");
#ifdef ARM64_TEST_SMP_PANIC
    spawn_on(1, panic_worker);

    for (;;) {
        schedule();
    }
#endif
    unsigned long reaped_before = scheduler_reaped_threads();
    uint64_t boot_ticks = kernel_timer_ticks();
    spawn_on((cpu_id_t)coreCount - 1, timer_isolation_worker);
    wait_counter(&isolated_timer_done, 1);
    wait_reaped(reaped_before + 1);
    KERNEL_ASSERT(kernel_timer_ticks() > boot_ticks);

    uint64_t ticks_before[BOOT_MAX_CPUS];
    reaped_before = scheduler_reaped_threads();

    for (unsigned int cpu_index = 0; cpu_index < coreCount; ++cpu_index) {
        KERNEL_ASSERT(__atomic_load_n(&cpu_core_local[cpu_index].online, __ATOMIC_ACQUIRE));
        ticks_before[cpu_index] =
            __atomic_load_n(&cpu_core_local[cpu_index].timer_ticks, __ATOMIC_RELAXED);
        spawn_on(cpu_index, busy_first);
        spawn_on(cpu_index, busy_second);
    }

    uint64_t deadline = ktime_get_ns() + 150000000UL;

    while (ktime_get_ns() < deadline) {
        kernel_cpu_relax();
    }

    for (unsigned int cpu_index = 0; cpu_index < coreCount; ++cpu_index) {
        KERNEL_ASSERT(__atomic_load_n(&progress[cpu_index][0], __ATOMIC_RELAXED) &&
                      __atomic_load_n(&progress[cpu_index][1], __ATOMIC_RELAXED));
        KERNEL_ASSERT(__atomic_load_n(&cpu_core_local[cpu_index].timer_ticks, __ATOMIC_RELAXED) >
                      ticks_before[cpu_index] + 2);
    }

    __atomic_store_n(&stop_workers, 1, __ATOMIC_RELEASE);
    wait_reaped(reaped_before + 2 * coreCount);
    platform_serial_write("ARM64 per-CPU timers passed\n");

    wait_queue_init(&remote_queue);
    reaped_before = scheduler_reaped_threads();

    unsigned long ipis_before =
        __atomic_load_n(&cpu_core_local[1].reschedule_ipis, __ATOMIC_RELAXED);
    spawn_on(1, remote_waiter);
    deadline = ktime_get_ns() + 1000000000UL;

    while (scheduler_waiter_count(&remote_queue) != 1 && ktime_get_ns() < deadline) {
        schedule();
    }

    KERNEL_ASSERT(scheduler_waiter_count(&remote_queue) == 1);
#ifdef ARM64_TEST_HANDOFF
    wait_counter(&handoff_paused, 1);

    unsigned long deferred_before = scheduler_deferred_wakeups();
#endif
    __atomic_store_n(&remote_ready, 1, __ATOMIC_RELEASE);
    wait_queue_wake_one(&remote_queue);
#ifdef ARM64_TEST_HANDOFF
    KERNEL_ASSERT(scheduler_deferred_wakeups() > deferred_before);
    __atomic_store_n(&handoff_target, NULL, __ATOMIC_RELEASE);
    __atomic_store_n(&handoff_resume, 1, __ATOMIC_RELEASE);
#endif
    wait_counter(&remote_done, 1);
    wait_reaped(reaped_before + 1);
    KERNEL_ASSERT(
        __atomic_load_n(&cpu_core_local[1].reschedule_ipis, __ATOMIC_RELAXED) > ipis_before);
    platform_serial_write("ARM64 remote wakeup passed\n");
#ifdef ARM64_TEST_HANDOFF
    platform_serial_write("ARM64 forced handoff passed\n");
#endif

    wait_queue_init(&turn_queue);
    reaped_before = scheduler_reaped_threads();
    spawn_on(0, exchange_first);
    spawn_on(1, exchange_second);
    wait_counter(&completed_workers, 2);
    wait_reaped(reaped_before + 2);
    KERNEL_ASSERT(__atomic_load_n(&exchanges, __ATOMIC_RELAXED) == 400);
    platform_serial_write("ARM64 cross-core waits passed\n");

    exit_wave();

    /* Only CPU 0's reaper frees stacks; all workers are gone at each boundary. */
    unsigned long free_after_warmup = allocator->free_frames;
    exit_wave();
    KERNEL_ASSERT(kernel_process.thread_count == coreCount + 2);
    KERNEL_ASSERT(allocator->free_frames == free_after_warmup);
    platform_serial_write("ARM64 SMP reaping passed\n");

    for (unsigned int cpu_index = 0; cpu_index < coreCount; ++cpu_index) {
        smp_write_value("SMP_CPU ", cpu_index);
        smp_write_value("SMP_TICKS ",
            __atomic_load_n(&cpu_core_local[cpu_index].timer_ticks, __ATOMIC_RELAXED));
        smp_write_value("SMP_IPIS ",
            __atomic_load_n(&cpu_core_local[cpu_index].reschedule_ipis, __ATOMIC_RELAXED));
    }

    platform_serial_write("ARM64 SMP passed\n");
}
