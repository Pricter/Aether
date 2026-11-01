#include <kernel/cpu.h>
#include <kernel/panic.h>

/* Permanent GICv2-sized CPU storage. DT discovery assigns logical indices and
 * immutable hardware affinities before any secondary is started. */
static core_t cpu_storage[BOOT_MAX_CPUS];
core_t *cpu_core_local = cpu_storage;
uint64_t coreCount = 1;

/* Establish per-CPU access before any asynchronous handler or heap lock. */
void arm64_cpu_initialize_boot(void) {
    cpu_storage[0].cpu_id = 0;
    cpu_storage[0].bsp = true;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(cpu_storage[0].hardware_id));
    cpu_storage[0].hardware_id &= 0xff00ffffffUL;
    __asm__ volatile("msr tpidr_el1, %0; isb" ::"r"(&cpu_storage[0]) : "memory");
}

/* Normalize firmware affinities to logical IDs with the already-running CPU 0
 * first. Existing boot-CPU timer/controller state must not be reset here. */
bool arm64_cpu_configure(const boot_memory_map_t *memory_map) {
    if (!memory_map->cpu_count || memory_map->cpu_count > BOOT_MAX_CPUS ||
        (memory_map->cpu_count > 1 && !memory_map->psci_conduit)) {
        return false;
    }

    unsigned int next_index = 1;
    bool found_boot = false;

    for (unsigned int source_index = 0; source_index < memory_map->cpu_count; ++source_index) {
        uint64_t hardware_id = memory_map->cpu_hardware_ids[source_index];

        if (hardware_id == cpu_storage[0].hardware_id) {
            found_boot = true;
            continue;
        }

        if (next_index == BOOT_MAX_CPUS) {
            return false;
        }

        cpu_storage[next_index].cpu_id = next_index;
        cpu_storage[next_index].hardware_id = hardware_id;
        ++next_index;
    }

    if (!found_boot || next_index != memory_map->cpu_count) {
        return false;
    }

    coreCount = next_index;
    __atomic_store_n(&cpu_storage[0].online, true, __ATOMIC_RELEASE);

    return true;
}

/* IRQs are masked by the spinlock caller; the counter is CPU-local. */
void kernel_spinlock_enter(void) {
    core_t *cpu = this_cpu();
    KERNEL_ASSERT(cpu != NULL);
    ++cpu->spinlock_depth;
}

/* Detect lock-accounting corruption before IRQ state can be restored. */
void kernel_spinlock_leave(void) {
    core_t *cpu = this_cpu();
    KERNEL_ASSERT(cpu && cpu->spinlock_depth);
    --cpu->spinlock_depth;
}

/* Scheduler entry uses this to reject sleeping/switching under a spinlock. */
unsigned long kernel_spinlock_depth(void) {
    core_t *cpu = this_cpu();

    return cpu ? cpu->spinlock_depth : 0;
}
