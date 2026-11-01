#pragma once

#include <kernel/frame_allocator.h>

/* Boot CPU owns this state and all table frames permanently after enable.
 * The early identity map is not the final higher-half kernel/user layout. */
typedef struct {
    paddr_t root_table;
    unsigned long table_frames;
    bool enabled;
} arm64_mmu_t;

/* Build 4 KiB identity mappings for discovered RAM and QEMU's UART. Kernel
 * text is RX, rodata R/NX, other RAM RW/NX, and UART device RW/NX; EL0 has
 * no access. Requires initialized allocator, MMU/data cache off, masked IRQs,
 * and the QEMU virt physical layout. Never calls higher kernel subsystems.
 * Failure before activation releases all table frames; allocator retains
 * ownership of its metadata. Success transfers table ownership to state for
 * the kernel lifetime. Do not reinitialize an active state. */
bool arm64_mmu_enable(
    arm64_mmu_t *state, const boot_memory_map_t *memory_map, frame_allocator_t *allocator);

/* Query the active EL1 read translation using AT/PAR. No mapping changes.
 * Return false for an unmapped address; success writes a physical address. */
bool arm64_mmu_translate(vaddr_t virtual_address, paddr_t *physical_address);
