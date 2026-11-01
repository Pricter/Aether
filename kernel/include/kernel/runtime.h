#pragma once
#include <kernel/frame_allocator.h>

/* Bind the heap and initialize boot-CPU interrupts/time. Normal boot leaves
 * IRQs masked until scheduler setup. Only KERNEL_SELF_TESTS builds run probes
 * and return with IRQs enabled. Requires active identity mappings. */
bool arm64_runtime_initialize(frame_allocator_t *allocator);

/* Start normal execution or the separately compiled self-test coordinator.
 * Normal startup brings CPUs online and starts exactly one init. Allocator/map
 * storage is borrowed for the kernel lifetime. */
__attribute__((noreturn)) void arm64_scheduler_start(
    frame_allocator_t *allocator, const boot_memory_map_t *memory_map);
