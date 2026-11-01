/* ARM SMP scheduler. One short state lock serializes lifecycle transitions and
 * per-CPU queue ownership. It is NEVER held across a physical context switch.
 * Frames/heap/process locks are acquired only outside this state lock.
 * A saved context becomes publishable from scheduler_switch_complete, running
 * on the incoming stack. This is the off-CPU handoff for wakeups and reaping. */

#include <kernel/userspace.h>
#include <kernel/scheduler.h>
#include <kernel/cpu.h>
#include <kernel/heap.h>
#include <kernel/panic.h>
#include <kernel/smp.h>
#include <platform/serial.h>

extern void context_switch(context_regs_t *outgoing, context_regs_t *incoming);
extern void context_enter(context_regs_t *incoming);
extern void thread_entry_trampoline(void);

process_t kernel_process;
static spinlock_t scheduler_state_lock = SPINLOCK_ZERO;
static list_head_t dead_list;
static wait_queue_t reaper_queue;
static thread_t *reaper_thread;
static int32_t next_thread_id = 1;
static int32_t next_process_id = 1;
static unsigned long reaped_threads;
static unsigned long context_switch_count;
static unsigned long timer_switch_count;
static unsigned long idle_entry_count;
static unsigned long deferred_wake_count;

_Static_assert(__builtin_offsetof(context_regs_t, stack_pointer) == 96, "ARM switch SP offset");
_Static_assert(__builtin_offsetof(context_regs_t, simd_registers) == 112, "ARM SIMD offset");
_Static_assert(sizeof(context_regs_t) == 640, "ARM switch context size");

/* All queue operations below require the sole scheduler-state lock. */
static void enqueue_ready_locked(thread_t *thread) {
    KERNEL_ASSERT(thread->state == THREAD_READY && !thread->on_cpu);
    KERNEL_ASSERT(thread->list_node.next == &thread->list_node);

    core_t *target = &cpu_core_local[thread->owner_cpu];
    list_push_back(&target->run_queue, &thread->list_node);
    __atomic_store_n(&target->reschedule_pending, true, __ATOMIC_RELEASE);

    if (thread->owner_cpu != this_cpu()->cpu_id || target->current_thread == target->idle_thread) {
        arm64_scheduler_notify(thread->owner_cpu);
    }
}

/* A remote wake may precede the outgoing context save. Mark it ready, but
 * leave enqueueing to the incoming CPU's completion hook until on_cpu clears. */
static void wake_thread_locked(thread_t *thread) {
    KERNEL_ASSERT(thread->state == THREAD_BLOCKED);
    thread->state = THREAD_READY;

    if (!thread->on_cpu) {
        enqueue_ready_locked(thread);
    } else {
        __atomic_fetch_add(&deferred_wake_count, 1, __ATOMIC_RELAXED);
    }
}

/* Remove queue membership under the same state lock as BLOCKED publication. */
static void wake_one_locked(wait_queue_t *queue) {
    list_node_t *node = list_pop_front(&queue->waiters);

    if (node) {
        wake_thread_locked(list_entry(node, thread_t, wq_node));
    }
}

/* Complete on the INCOMING stack with IRQs off. Before this point no waker,
 * remote CPU, or reaper may consume the outgoing context/stack. */
void scheduler_switch_complete(void) {
    core_t *cpu = this_cpu();
    thread_t *previous = cpu->switch_previous;

    if (!previous) {
        return;
    }

    KERNEL_ASSERT(!kernel_interrupt_depth() && !kernel_spinlock_depth());
    spinlock_acquire(&scheduler_state_lock);
    KERNEL_ASSERT(previous->on_cpu && previous != cpu->current_thread);
    previous->on_cpu = false;
    cpu->switch_previous = NULL;

    if (previous == cpu->idle_thread) {
        previous->state = THREAD_READY;
    } else if (previous->state == THREAD_READY) {
        enqueue_ready_locked(previous);
    } else if (previous->state == THREAD_DEAD) {
        list_push_back(&dead_list, &previous->list_node);
        wake_one_locked(&reaper_queue);
    } else {
        KERNEL_ASSERT(previous->state == THREAD_BLOCKED);
    }

    /* previous may be freed immediately by CPU 0 after this release. */
    spinlock_release(&scheduler_state_lock, false);
}

/* Pick only an off-CPU context. The old runnable context is enqueued later,
 * after assembly has saved it and moved SP, closing the cross-core race. */
static void switch_locked(bool restore_interrupts) {
    core_t *cpu = this_cpu();
    thread_t *current = cpu->current_thread;
    list_node_t *node = list_pop_front(&cpu->run_queue);
    thread_t *next = node ? list_entry(node, thread_t, list_node) : NULL;
    bool runnable = current && current != cpu->idle_thread && current->state == THREAD_RUNNING;

    if (!next && (runnable || current == cpu->idle_thread)) {
        spinlock_release(&scheduler_state_lock, false);
        irq_restore(restore_interrupts);

        return;
    }

    if (!next) {
        next = cpu->idle_thread;
    }

    KERNEL_ASSERT(next && next != current && !next->on_cpu && next->owner_cpu == cpu->cpu_id);

    if (runnable) {
        current->state = THREAD_READY;
    }

    next->state = THREAD_RUNNING;
    next->on_cpu = true;
    cpu->current_thread = next;
    cpu->switch_previous = current;
    __atomic_fetch_add(&context_switch_count, 1, __ATOMIC_RELAXED);

    if (cpu->preemption_switch) {
        __atomic_fetch_add(&timer_switch_count, 1, __ATOMIC_RELAXED);
        cpu->preemption_switch = false;
    }

    spinlock_release(&scheduler_state_lock, false);
#ifdef ARM64_TEST_HANDOFF
    extern void arm64_test_handoff_pause(thread_t * outgoing);
    arm64_test_handoff_pause(current);
#endif

    arm64_process_switch(current, next);
    context_switch(&current->context, &next->context);
    irq_restore(restore_interrupts);
}

/* Publish BLOCKED and list membership while producers cannot pass the state
 * lock. No external predicate/condition lock survives the actual switch. */
static void block_locked(wait_queue_t *queue, bool restore_interrupts) {
    thread_t *current = this_cpu()->current_thread;
    KERNEL_ASSERT(
        current && current != this_cpu()->idle_thread && current->state == THREAD_RUNNING);
    KERNEL_ASSERT(current->wq_node.next == &current->wq_node);

    current->state = THREAD_BLOCKED;
    list_push_back(&queue->waiters, &current->wq_node);
    switch_locked(restore_interrupts);
}

/* Idle stacks are permanent, pinned, and never appear in any ready queue. */
static void idle_entry(void) {
    __atomic_fetch_add(&idle_entry_count, 1, __ATOMIC_RELAXED);

    for (;;) {
        enable_interrupts();
        __asm__ volatile("wfi");
    }
}

/* Only fully off-CPU DEAD threads reach dead_list. CPU 0 owns the reaper; its
 * empty check and wait insertion use the same lock as exit notifications. */
static void reaper_entry(void) {
    for (;;) {
        bool saved_interrupts = irq_save();
        spinlock_acquire(&scheduler_state_lock);

        list_node_t *node = list_pop_front(&dead_list);

        if (!node) {
            block_locked(&reaper_queue, saved_interrupts);
            continue;
        }

        thread_t *finished = list_entry(node, thread_t, list_node);
        KERNEL_ASSERT(!finished->on_cpu && finished->state == THREAD_DEAD);

        process_t *parent = finished->parent;
        spinlock_release(&scheduler_state_lock, false);
        irq_restore(saved_interrupts);

        kstack_free(finished->kstack_top);
        free(finished);

        bool parent_interrupts = spinlock_acquire(&parent->lock);
        KERNEL_ASSERT(parent->thread_count);

        bool managed_process = parent->managed_process;
        --parent->thread_count;
        spinlock_release(&parent->lock, parent_interrupts);

        __atomic_fetch_add(&reaped_threads, 1, __ATOMIC_RELEASE);

        /* Unmanaged owners may destroy parent after observing zero threads.
         * Managed processes remain owned by the process tree until wait. */
        if (managed_process) {
            arm64_process_thread_reaped(parent);
        }
    }
}

/* IDs are independent of CPU affinity and assigned by explicit atomics. */
pid_t scheduler_alloc_pid(void) {
    return __atomic_fetch_add(&next_process_id, 1, __ATOMIC_RELAXED);
}

/* Build all per-CPU idle contexts before any PSCI secondary is started. */
void scheduler_init(void) {
    arm64_user_initialize();
    KERNEL_ASSERT(this_cpu()->cpu_id == 0 && !this_cpu()->current_thread);

    kernel_process.pid = scheduler_alloc_pid();
    kernel_process.name = "kernel";
    list_head_init(&kernel_process.children);
    wait_queue_init(&kernel_process.child_wait);

    list_head_init(&dead_list);
    wait_queue_init(&reaper_queue);

    for (unsigned int cpu_index = 0; cpu_index < coreCount; ++cpu_index) {
        list_head_init(&cpu_core_local[cpu_index].run_queue);

        thread_t *idle = thread_create(&kernel_process, idle_entry);
        KERNEL_ASSERT(idle != NULL);
        idle->owner_cpu = cpu_index;
        cpu_core_local[cpu_index].idle_thread = idle;
    }

    reaper_thread = thread_create(&kernel_process, reaper_entry);
    KERNEL_ASSERT(reaper_thread != NULL);
    platform_serial_write("ARM64 scheduler initialized (CPU 0)\n");
}

/* Start the one global reaper on CPU 0, never on the CPU issuing a wake. */
void scheduler_start_reaper(void) {
    thread_ready_on(reaper_thread, 0);
}

/* Caller owns CREATED; publication transfers ownership to scheduler/reaper. */
thread_t *thread_create(process_t *parent, void (*entry)(void)) {
    KERNEL_ASSERT(!kernel_interrupt_depth() && !kernel_spinlock_depth());

    if (!parent || !entry) {
        return NULL;
    }

    thread_t *thread = malloc(sizeof(*thread));

    if (!thread) {
        return NULL;
    }

    uintptr_t stack_top = kstack_alloc();

    if (!stack_top) {
        free(thread);

        return NULL;
    }

    thread->tid = __atomic_fetch_add(&next_thread_id, 1, __ATOMIC_RELAXED);
    thread->parent = parent;
    thread->state = THREAD_CREATED;
    thread->owner_cpu = this_cpu()->cpu_id;
    thread->kstack_top = stack_top;
    thread->kstack_bottom = stack_top - KSTACK_SIZE;

    thread->context.saved_registers[0] = (uintptr_t)entry;
    thread->context.saved_registers[11] = (uintptr_t)thread_entry_trampoline;
    thread->context.stack_pointer = stack_top;

    list_node_init(&thread->list_node);
    list_node_init(&thread->wq_node);

    bool saved_interrupts = spinlock_acquire(&parent->lock);
    ++parent->thread_count;
    spinlock_release(&parent->lock, saved_interrupts);

    return thread;
}

/* Validate both endpoints before publishing a fresh EL0 context. */
thread_t *thread_create_user(process_t *process, uintptr_t entry, uintptr_t stack_top) {
    if (!arm64_user_access(process, entry, PROT_EXEC) || !stack_top || (stack_top & 15) ||
        !arm64_user_access(process, stack_top - 1, PROT_WRITE)) {
        return NULL;
    }

    thread_t *thread = thread_create(process, arm64_user_thread_start);

    if (thread) {
        thread->user_entry = entry;
        thread->ustack_top = stack_top;
    }

    return thread;
}

/* Unpublished contexts are not reachable by another CPU or the reaper. */
void thread_destroy(thread_t *thread) {
    if (!thread) {
        return;
    }

    KERNEL_ASSERT(!kernel_interrupt_depth() && !kernel_spinlock_depth());

    bool saved_interrupts = spinlock_acquire(&scheduler_state_lock);
    KERNEL_ASSERT(thread->state == THREAD_CREATED && !thread->on_cpu);
    thread->state = THREAD_DEAD;
    spinlock_release(&scheduler_state_lock, saved_interrupts);

    process_t *parent = thread->parent;
    saved_interrupts = spinlock_acquire(&parent->lock);
    --parent->thread_count;
    spinlock_release(&parent->lock, saved_interrupts);
    kstack_free(thread->kstack_top);
    free(thread);
}

/* Explicit affinity assignment is permitted only before publication. */
void thread_ready_on(thread_t *thread, cpu_id_t core_id) {
    KERNEL_ASSERT(core_id < coreCount);

    bool saved_interrupts = spinlock_acquire(&scheduler_state_lock);
    KERNEL_ASSERT(thread->state == THREAD_CREATED && !thread->on_cpu);
    thread->owner_cpu = core_id;
    thread->state = THREAD_READY;
    enqueue_ready_locked(thread);
    spinlock_release(&scheduler_state_lock, saved_interrupts);
}

/* New threads default to the producer's CPU; wakeups use retained affinity. */
void thread_ready(thread_t *thread) {
    thread_ready_on(thread, this_cpu()->cpu_id);
}

/* First entry needs no outgoing context; completion sees switch_previous NULL. */
__attribute__((noreturn)) void scheduler_enter(void) {
    KERNEL_ASSERT(!kernel_interrupt_depth() && !kernel_spinlock_depth());
    irq_save();

    core_t *cpu = this_cpu();
    KERNEL_ASSERT(!cpu->current_thread);
    spinlock_acquire(&scheduler_state_lock);

    list_node_t *node = list_pop_front(&cpu->run_queue);
    thread_t *first = node ? list_entry(node, thread_t, list_node) : cpu->idle_thread;
    KERNEL_ASSERT(first && !first->on_cpu);
    first->state = THREAD_RUNNING;
    first->on_cpu = true;
    cpu->current_thread = first;
    spinlock_release(&scheduler_state_lock, false);
    arm64_process_switch(NULL, first);
    context_enter(&first->context);
    __builtin_unreachable();
}

/* Normal yield or IRQ-tail preemption; no lock or live IRQ may cross switch. */
void schedule(void) {
    KERNEL_ASSERT(!kernel_interrupt_depth() && !kernel_spinlock_depth());

    if (!this_cpu()->current_thread) {
        return;
    }

    bool saved_interrupts = irq_save();
    spinlock_acquire(&scheduler_state_lock);
    switch_locked(saved_interrupts);
}

/* Unconditional wait; use thread_wait_until when a producer updates a condition. */
void thread_block(wait_queue_t *queue) {
    KERNEL_ASSERT(!kernel_interrupt_depth() && !kernel_spinlock_depth());

    bool saved_interrupts = irq_save();
    spinlock_acquire(&scheduler_state_lock);
    block_locked(queue, saved_interrupts);
}

/* Check and insertion share the state lock with wakeups. Producers publish
 * condition data before waking; predicates may not acquire locks or allocate. */
void thread_wait_until(wait_queue_t *queue, bool (*ready)(void *context), void *context) {
    KERNEL_ASSERT(ready && !kernel_interrupt_depth() && !kernel_spinlock_depth());

    bool saved_interrupts = irq_save();

    for (;;) {
        spinlock_acquire(&scheduler_state_lock);

        if (ready(context)) {
            spinlock_release(&scheduler_state_lock, false);
            irq_restore(saved_interrupts);

            return;
        }

        block_locked(queue, false);
    }
}

/* Exit is not visible to reaping until the completion hook switches stacks. */
__attribute__((noreturn)) void thread_exit(void) {
    KERNEL_ASSERT(!kernel_interrupt_depth() && !kernel_spinlock_depth());
    irq_save();
    spinlock_acquire(&scheduler_state_lock);

    thread_t *current = this_cpu()->current_thread;
    KERNEL_ASSERT(current && current != this_cpu()->idle_thread);
    current->state = THREAD_DEAD;
    switch_locked(false);
    KERNEL_BUG("exited thread resumed");
}

/* Wake operations are IRQ-safe and never allocate or hold external locks. */
void wait_queue_wake_one(wait_queue_t *queue) {
    bool saved_interrupts = spinlock_acquire(&scheduler_state_lock);
    wake_one_locked(queue);
    spinlock_release(&scheduler_state_lock, saved_interrupts);
}

/* Serialize the whole broadcast so each waiter is detached exactly once. */
void wait_queue_wake_all(wait_queue_t *queue) {
    bool saved_interrupts = spinlock_acquire(&scheduler_state_lock);

    while (!list_empty(&queue->waiters)) {
        wake_one_locked(queue);
    }

    spinlock_release(&scheduler_state_lock, saved_interrupts);
}

/* Timer and IPI callbacks only request scheduling; dispatch completes EOI first. */
void scheduler_timer_tick(void *context) {
    (void)context;
    __atomic_store_n(&this_cpu()->reschedule_pending, true, __ATOMIC_RELEASE);
}

/* An exchange preserves a concurrent new request instead of clearing it late. */
void scheduler_interrupt_exit(void) {
    core_t *cpu = this_cpu();

    if (!cpu->current_thread ||
        !__atomic_exchange_n(&cpu->reschedule_pending, false, __ATOMIC_ACQ_REL)) {
        return;
    }

    cpu->preemption_switch = true;
    schedule();
    cpu->preemption_switch = false;
}

/* Snapshot queue membership under its actual SMP owner, not the per-thread lock. */
unsigned long scheduler_waiter_count(wait_queue_t *queue) {
    bool saved_interrupts = spinlock_acquire(&scheduler_state_lock);
    unsigned long count = 0;

    for (list_node_t *node = queue->waiters.next; node != &queue->waiters; node = node->next) {
        ++count;
    }

    spinlock_release(&scheduler_state_lock, saved_interrupts);

    return count;
}

/* Diagnostic counters publish no pointers into asynchronously reaped storage. */
unsigned long scheduler_reaped_threads(void) {
    return __atomic_load_n(&reaped_threads, __ATOMIC_ACQUIRE);
}

unsigned long scheduler_context_switches(void) {
    return __atomic_load_n(&context_switch_count, __ATOMIC_RELAXED);
}

unsigned long scheduler_timer_switches(void) {
    return __atomic_load_n(&timer_switch_count, __ATOMIC_RELAXED);
}

unsigned long scheduler_idle_entries(void) {
    return __atomic_load_n(&idle_entry_count, __ATOMIC_RELAXED);
}

unsigned long scheduler_deferred_wakeups(void) {
    return __atomic_load_n(&deferred_wake_count, __ATOMIC_RELAXED);
}
