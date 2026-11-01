#pragma once
#include <kernel/boot_memory.h>
#include <kernel/cpu.h>
#include <kernel/frame_allocator.h>

/* Bring up each DT-described secondary via PSCI with permanent boot stacks,
 * the existing identity map, a private GIC CPU interface, and a local timer.
 * Must follow scheduler_init; only CPU 0 may initiate startup. */
bool arm64_smp_start(const boot_memory_map_t *memory_map);
void arm64_scheduler_notify(cpu_id_t target_cpu);
bool arm64_smp_initialize_ipis(void);
__attribute__((noreturn)) void arm64_secondary_main(core_t *cpu);

/* Claim fatal diagnostic ownership and stop online peers without allocation or
 * spinlocks. Non-owner CPUs halt. Returns the owner's per-CPU nesting depth. */
unsigned long arm64_panic_enter(void);
bool arm64_panic_wait_peers(void);

/* Scheduler integration checks run on all online CPUs; called by CPU 0 only. */
void arm64_smp_validate(frame_allocator_t *allocator);
