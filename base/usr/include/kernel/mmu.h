#pragma once

#include <stdint.h>
#include <stddef.h>
#include <kernel/spinlock.h>
#include <limine.h>

typedef uint64_t pagemap_t;

/* Size of each page, standard intel is 4KiB */
#define PAGE_SIZE 0x1000

/* The address mask */
#define PTE_ADDR_MASK 0x000ffffffffff000

/* Macros to get the address and flags */
#define PTE_GET_ADDR(VALUE) ((VALUE) & PTE_ADDR_MASK)
#define PTE_GET_FLAGS(VALUE) ((VALUE) & ~PTE_ADDR_MASK)

#define PTE_PRESENT ((uint64_t)1 << 0) /* If the entry is present */
#define PTE_WRITABLE ((uint64_t)1 << 1) /* If the entry has write access */
#define PTE_USER ((uint64_t)1 << 2) /* Priviledge level */
#define PTE_WRITE_THOUGH ((uint64_t)1 << 3) /* If bit is set, then write-through caching is set */
#define PTE_CACHE_DISABLE ((uint64_t)1 << 4) /* If this is set, then the page will not be cached */
#define PTE_LARGER_PAGE ((uint64_t)1 << 6) /* If this is set then the page will be a 4MiB page */
/**
 * NX: If the NXE bit is set in the EFER register, then
 * instructions are not allowed to be executed at addresses within
 * whatever page XD is set. if EFER.NXE is 0, then the XD bit is
 * reserved and should be set to 0.
*/
#define PTE_NX ((uint64_t)1 << 63)

extern volatile struct limine_hhdm_request hhdm_request;

extern pagemap_t *mmu_kernel_pagemap;

#define HHDM_HIGHER_HALF (hhdm_request.response->offset)

void mmu_frame_clear(uintptr_t address);
void mmu_frame_set(uintptr_t address);
uintptr_t mmu_request_frame(void);
uintptr_t mmu_request_frames(uint64_t num);
void mmu_free_frames(void* addr, uint64_t pages);
void mmu_map_page(pagemap_t* pagemap, uintptr_t virt, uintptr_t phys, uint64_t flags);
void mmu_switch_pagemap(pagemap_t* pagemap);
uint64_t clean_reclaimable_memory(void);

/* Malloc */
void slab_init(void);
void *malloc(size_t size);
void *realloc(void *addr, size_t new_size);
void free(void *addr);