#pragma once

#include <kernel/types.h>

#define BOOT_MEMORY_MAX_RAM 16
#define BOOT_MEMORY_MAX_RESERVED 32
#define BOOT_MEMORY_MAX_AVAILABLE 64
#define BOOT_MAX_CPUS 8

/* Half-open physical interval. No virtual-address offsets are implicit. */
typedef struct {
    paddr_t base;
    paddr_t end;
    const char *description;
} boot_memory_range_t;

/* Caller-owned, fixed-capacity early map; requires no allocator or locks.
 * All arrays contain copied values, never pointers into the borrowed DTB.
 * Available intervals are sorted, page-aligned, and exclude reservations. */
typedef struct {
    boot_memory_range_t ram[BOOT_MEMORY_MAX_RAM];
    boot_memory_range_t reserved[BOOT_MEMORY_MAX_RESERVED];
    boot_memory_range_t available[BOOT_MEMORY_MAX_AVAILABLE];
    unsigned int ram_count;
    unsigned int reserved_count;
    unsigned int available_count;
    unsigned long device_tree_size;
    uint64_t cpu_hardware_ids[BOOT_MAX_CPUS];
    unsigned int cpu_count;
    unsigned int psci_conduit; /* 0 absent, 1 HVC, 2 SMC; PSCI 0.2+ only. */
} boot_memory_map_t;

/* Parse a borrowed, readable DTB within a caller-validated memory window.
 * Single-CPU boot context only. Returns false on malformed/unsupported input
 * or capacity exhaustion; output is unusable on failure. Never allocates.
 * Fixed /reserved-memory regions and the DTB memreserve block are honored;
 * dynamic reservations and translated reserved-memory buses are rejected. */
bool boot_memory_discover(const void *device_tree, size_t readable_bytes,
    paddr_t device_tree_address, boot_memory_map_t *memory_map);

/* Add a caller-owned physical reservation, then derive page-safe free RAM.
 * Both operations fail closed on invalid intervals or capacity exhaustion. */
bool boot_memory_reserve(
    boot_memory_map_t *memory_map, paddr_t base, paddr_t end, const char *description);
bool boot_memory_finalize(boot_memory_map_t *memory_map);
