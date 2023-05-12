#pragma once

#include <stdint.h>
#include <stddef.h>
#include <kernel/spinlock.h>

typedef uint64_t pagemap_t;

/* Size of each page, standard intel is 4KiB */
#define PAGE_SIZE 0x1000

/* The address mask */
#define PTE_ADDR_MASK 0x000ffffffffff000

/* Macros to get the address and flags */
#define PTE_GET_ADDR(VALUE) ((VALUE) & PTE_ADDR_MASK)
#define PTE_GET_FLAGS(VALUE) ((VALUE) & ~PTE_ADDR_MASK)

#define PTE_PRESENT (1 << 0) /* If the entry is present */
#define PTE_WRITABLE (1 << 1) /* If the entry has write access */
#define PTE_USER (1 << 2) /* Priviledge level */
#define PTE_WRITE_THOUGH (1 << 3) /* If bit is set, then write-through caching is set */
#define PTE_CACHE_DISABLE (1 << 4) /* If this is set, then the page will not be cached */
#define PTE_LARGER_PAGE (1 << 6) /* If this is set then the page will be a 4MiB page */
/**
 * NX: If the NXE bit is set in the EFER register, then
 * instructions are not allowed to be executed at addresses within
 * whatever page XD is set. if EFER.NXE is 0, then the XD bit is
 * reserved and should be set to 0.
*/
#define PTE_NX (1 << 63)

extern volatile struct limine_hhdm_request hhdm_request;

extern pagemap_t *mmu_kernel_pagemap;

#define HHDM_HIGHER_HALF (hhdm_request.response->offset)

void mmu_frame_clear(uintptr_t address);
void mmu_frame_set(uintptr_t address);
uintptr_t mmu_request_frame(void);
void mmu_map_page(pagemap_t* pagemap, uintptr_t virt, uintptr_t phys, uint64_t flags);