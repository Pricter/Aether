#include <kernel/smp.h>
#include <kernel/scheduler.h>
#include <kernel/interrupts.h>
#include <kernel/time.h>
#include <kernel/panic.h>

#define RESCHEDULE_SGI 2U
#define HALT_SGI 3U

/* PSCI entry reads this with MMU/data caching off. Each block occupies a whole
 * cache line and is cleaned to PoC before firmware receives its physical address. */
typedef struct __attribute__((aligned(64))) {
    uintptr_t stack_top;
    uint64_t root_table;
    uint64_t translation_control;
    uint64_t memory_attributes;
    uint64_t system_control;
    core_t *cpu;
} secondary_boot_t;
static secondary_boot_t boot_blocks[BOOT_MAX_CPUS];
static int panic_owner = -1;
extern void arm64_secondary_entry(void);

_Static_assert(__builtin_offsetof(secondary_boot_t, cpu) == 40, "secondary context offset");

/* Reschedule IPIs request a tail switch, never a switch with a live GIC token. */
static void reschedule_interrupt(void *context) {
    (void)context;
    __atomic_fetch_add(&this_cpu()->reschedule_ipis, 1, __ATOMIC_RELAXED);
    scheduler_timer_tick(NULL);
}

/* Fatal shutdown does not allocate, acquire a lock, or return to the dispatcher. */
__attribute__((noreturn)) static void halt_interrupt(void *context) {
    (void)context;
    __asm__ volatile("msr daifset, #0xf" ::: "memory");
    __atomic_store_n(&this_cpu()->panic_stopped, true, __ATOMIC_RELEASE);

    for (;;) {
        __asm__ volatile("wfi");
    }
}

/* Private callbacks must be installed separately on every CPU's banked SGIs. */
bool arm64_smp_initialize_ipis(void) {
    if (this_cpu()->ipis_ready) {
        return true;
    }

    bool ready = kernel_irq_register(RESCHEDULE_SGI, reschedule_interrupt, NULL) &&
                 kernel_irq_register(HALT_SGI, halt_interrupt, NULL);
    this_cpu()->ipis_ready = ready;

    return ready;
}

/* May be called with the scheduler state lock held: no other lock is taken. */
void arm64_scheduler_notify(cpu_id_t target_cpu) {
    if (__atomic_load_n(&cpu_core_local[target_cpu].online, __ATOMIC_ACQUIRE)) {
        kernel_irq_send_to(target_cpu, RESCHEDULE_SGI);
    }
}

/* Use the DT-selected PSCI conduit, not a platform-specific guessed call path.
 * SMCCC permits x0..x17 to be clobbered; input registers are explicitly bound. */
static int64_t psci_cpu_on(unsigned int conduit, uint64_t hardware_id, uintptr_t context) {
    register uint64_t function_id __asm__("x0") = 0xc4000003;
    register uint64_t target_id __asm__("x1") = hardware_id;
    register uint64_t entry_address __asm__("x2") = (uintptr_t)arm64_secondary_entry;
    register uint64_t context_address __asm__("x3") = context;

    if (conduit == 1) {
        __asm__ volatile("hvc #0"
            : "+r"(function_id), "+r"(target_id), "+r"(entry_address), "+r"(context_address)::"x4",
            "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17",
            "cc", "memory");
    } else {
        __asm__ volatile("smc #0"
            : "+r"(function_id), "+r"(target_id), "+r"(entry_address), "+r"(context_address)::"x4",
            "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17",
            "cc", "memory");
    }

    return (int64_t)function_id;
}

/* Firmware's MMU-off secondary must see the initialized boot block in memory. */
static void clean_boot_block(secondary_boot_t *block) {
    uint64_t cache_type;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(cache_type));

    uintptr_t line_bytes = 4UL << ((cache_type >> 16) & 15);
    uintptr_t begin = (uintptr_t)block & ~(line_bytes - 1);
    uintptr_t end = (uintptr_t)block + sizeof(*block);

    for (uintptr_t address = begin; address < end; address += line_bytes) {
        __asm__ volatile("dc cvac, %0" ::"r"(address) : "memory");
    }

    __asm__ volatile("dsb sy" ::: "memory");
}

/* Start one core at a time. Timed-out firmware handoffs retain their stacks:
 * a delayed CPU_ON must never enter memory that has been freed and reused. */
bool arm64_smp_start(const boot_memory_map_t *memory_map) {
    KERNEL_ASSERT(this_cpu()->cpu_id == 0 && !kernel_interrupt_depth());

    if (!arm64_smp_initialize_ipis()) {
        return false;
    }

    for (unsigned int cpu_index = 1; cpu_index < coreCount; ++cpu_index) {
        core_t *cpu = &cpu_core_local[cpu_index];
        cpu->boot_stack_top = kstack_alloc();

        if (!cpu->boot_stack_top) {
            return false;
        }

        secondary_boot_t *block = &boot_blocks[cpu_index];
        block->stack_top = cpu->boot_stack_top;
        block->cpu = cpu;
        __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(block->root_table));
        __asm__ volatile("mrs %0, tcr_el1" : "=r"(block->translation_control));
        __asm__ volatile("mrs %0, mair_el1" : "=r"(block->memory_attributes));
        __asm__ volatile("mrs %0, sctlr_el1" : "=r"(block->system_control));
        clean_boot_block(block);

        if (psci_cpu_on(memory_map->psci_conduit, cpu->hardware_id, (uintptr_t)block) != 0) {
            return false;
        }

        uint64_t deadline = ktime_get_ns() + 2000000000UL;

        while (!__atomic_load_n(&cpu->online, __ATOMIC_ACQUIRE) && ktime_get_ns() < deadline) {
            kernel_cpu_relax();
        }

        if (!__atomic_load_n(&cpu->online, __ATOMIC_ACQUIRE)) {
            return false;
        }
    }

    return true;
}

/* The assembly entry has already installed identity translation, VBAR, SP,
 * and TPIDR. Publish online only after local IRQ/timer initialization succeeds. */
__attribute__((noreturn)) void arm64_secondary_main(core_t *cpu) {
    KERNEL_ASSERT(this_cpu() == cpu && cpu->cpu_id > 0);

    uint64_t hardware_id;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(hardware_id));
    KERNEL_ASSERT((hardware_id & 0xff00ffffffUL) == cpu->hardware_id);
    KERNEL_ASSERT(arm64_interrupt_initialize_local() && arm64_smp_initialize_ipis());
    KERNEL_ASSERT(kernel_timer_set_hook(scheduler_timer_tick, NULL) && kernel_timer_start(200));
    __atomic_store_n(&cpu->online, true, __ATOMIC_RELEASE);
    scheduler_enter();
}

/* One atomic owner renders diagnostics; peers stop without lock dependency. */
unsigned long arm64_panic_enter(void) {
    __asm__ volatile("msr daifset, #0xf" ::: "memory");

    core_t *cpu = this_cpu();
    unsigned long depth = ++cpu->panic_depth;
    int expected_owner = -1;

    if (!__atomic_compare_exchange_n(&panic_owner, &expected_owner, (int)cpu->cpu_id, false,
            __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) &&
        expected_owner != (int)cpu->cpu_id) {
        halt_interrupt(NULL);
    }

    if (depth == 1) {
        for (unsigned int target_cpu = 0; target_cpu < coreCount; ++target_cpu) {
            if (target_cpu != cpu->cpu_id) {
                kernel_irq_send_to(target_cpu, HALT_SGI);
            }
        }
    }

    return depth;
}

/* Test-only caller observes peer-stop acknowledgement without allocating or
 * taking any lock. Normal panic reporting never waits for peers to cooperate. */
bool arm64_panic_wait_peers(void) {
    uint64_t deadline = ktime_get_ns() + 500000000UL;

    for (;;) {
        bool stopped = true;

        for (unsigned int cpu_index = 0; cpu_index < coreCount; ++cpu_index) {
            if (cpu_index != this_cpu()->cpu_id &&
                __atomic_load_n(&cpu_core_local[cpu_index].online, __ATOMIC_ACQUIRE) &&
                !__atomic_load_n(&cpu_core_local[cpu_index].panic_stopped, __ATOMIC_ACQUIRE)) {
                stopped = false;
            }
        }

        if (stopped || ktime_get_ns() >= deadline) {
            return stopped;
        }

        kernel_cpu_relax();
    }
}
