/**
 * mmu.c - Licensed under the MIT License
 * 
 * Setup the memory map and page tables
*/

#include <kernel/mmu.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>
#include <memory.h>
#include <kernel/kprintf.h>

/* Hangs the system */
extern void fatal();

/* Request limine for a memmap */
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0,
};

/* Request limine for hhdm */
volatile struct limine_hhdm_request hhdm_request = {
	.id = LIMINE_HHDM_REQUEST,
	.revision = 0,
};

/* Request limine for the kernel address */
static volatile struct limine_kernel_address_request kaddr_request = {
	.id = LIMINE_KERNEL_ADDRESS_REQUEST,
	.revision = 0,
};

/* Strings to be used to print entry info */
char* memmap_strings[8] = {
    "MEMMAP_USABLE                 ",
    "MEMMAP_RESERVED               ",
    "MEMMAP_ACPI_RECLAIMABLE       ",
    "MEMMAP_ACPI_NVS               ",
    "MEMMAP_BAD_MEMORY             ",
    "MEMMAP_BOOTLOADER_RECLAIMABLE ",
    "MEMMAP_KERNEL_AND_MODULES     ",
    "MEMMAP_FRAMEBUFFER            ",
};

/* Largest free memory segment */
static uintptr_t highest_free_address = 0;

/* The size of the largest free memory segment */
static uint64_t highest_free_address_size = 0;

/* Any free memory segment that can fit our bitmap, can also be highest_free_address */
static uintptr_t free_segment_bitmap = 0;

/* Total system memory size */
uint64_t total_memory = 0;

/* How many bitmap entries we need to store the whole memory */
uint64_t nframes = 0;

/* How many bytes do we need to store the whole memory */
uint64_t bytesOfBitmap = 0;

/* The actual bitmap to store the memory information */
uint8_t* bitmap = NULL;

/* Information about memory usage */
uint64_t usedMemory = 0; /* Amount of memory used */
uint64_t freeMemory = 0; /* Amount of memory free */

/* A variable to keep track of the next last allocated frame */
uintptr_t lastFrame = 0;

/* kernel pagemap */
pagemap_t* mmu_kernel_pagemap = NULL;

/* Macro to align linker symbols to page size */
#define ALIGN_FORWARD(x, a) ((x) / (a)) * (a)

/* Macro to align linker symbols to page size */
#define ALIGN_BACK(x, a) (((x) + ((a) - (1))) / (a)) * (a)

extern char text_start[], text_end[];
extern char rodata_start[], rodata_end[];
extern char data_start[], data_end[];

/**
 * mmu_frame_clear()
 * 
 * Sets the frame at the address to unused
 * 
 * @param address the frame to set unused
*/
void mmu_frame_clear(uintptr_t address) {
    /* Calculate the bitmap index */
    uint64_t index = address / 4096;

    /* Check if the index is less than bitmap size */
    if(index < nframes) {
        uint64_t byteIndex = index / 8;
        uint8_t bitIndex = index % 8;

        /* Set the bitIndex bit to 1 by ANDing it */
        bitmap[byteIndex] &= ~(1 << bitIndex);

        /* Update memory usage info */
        usedMemory -= 4096;
        freeMemory += 4096;
    }
}

/**
 * mmu_frame_set()
 * 
 * Sets the frame at the address to used
 * 
 * @param address the frame to set used
*/
void mmu_frame_set(uintptr_t address) {
    /* Calculate the bitmap index */
    uint64_t index = address / 4096;

    /* Check if the index is less than bitmap size */
    if(index < nframes) {
        uint64_t byteIndex = index / 8;
        uint8_t bitIndex = index % 8;

        /* Set the bitIndex bit to 1 by ORing it */
        bitmap[byteIndex] |= (1 << bitIndex);

        /* Update memory usage info */
        usedMemory += 4096;
        freeMemory -= 4096;
    }
}

/**
 * mmu_test_frame(uintptr_t address)
 * 
 * @param address The address to check if used or free
 * 
 * @returns true if being used and false if free
*/
bool mmu_test_frame(uintptr_t address) {
    uint64_t index = address / 4096;

    /* Check if the address is less than available memory */
    if(index < nframes) {
        uint64_t byteIndex = index / 8;
        uint8_t bitIndex = index % 8;

        /* Weird stuff */
        return (bitmap[byteIndex] & (1 << bitIndex)) != 0;
    }
	return false;
}

/**
 * mmu_request_frame()
 * 
 * Uses lastFrame to not loop over all the frames again
 * when allocating more than 1 frame
 * 
 * @returns The address of the next free frame
*/
uintptr_t mmu_request_frame(void) {
    /* Loop through all frames */
    for(; lastFrame < nframes; lastFrame++) {
        /* Calculate the address (i * 4096) */
        uintptr_t address = lastFrame * PAGE_SIZE;

        /* If the frame is free then set it to used and return it */
        if(!mmu_test_frame(address)) {
            mmu_frame_set(address);
            return address;
        }
    }
	kprintf("mmu: Fatal: Out of memory!!\n");
	fatal();
    return 0; /* TODO: Use page file */
}

/**
 * mmu_request_frames()
 * 
 * Requests multiple frames from mmu_request_frame
 * 
 * @param num The number of frames to allocate
 */
uintptr_t mmu_request_frames(uint64_t num) {
    if(num < 1) return 0; /* Return if 0 */

    uint64_t start_frame = 0;
    uint64_t free_frames = 0;
    for(; lastFrame < nframes; lastFrame++) {
        uintptr_t address = lastFrame * PAGE_SIZE;

        if(!mmu_test_frame(address)) {
            if(free_frames == 0) {
                /* This is the start of a new free block */
                start_frame = lastFrame;
            }
            free_frames++;

            if(free_frames == num) {
                /* Found a continuous block of free frames */
                for(uint64_t i = start_frame; i < start_frame + num; i++) {
                    mmu_frame_set(i * PAGE_SIZE);
                }
                return start_frame * PAGE_SIZE;
            }
        } else {
            /* This frame is used, reset the counter */
            free_frames = 0;
        }
    }
	kprintf("mmu: Fatal: Out of memory!!\n");
	fatal();
    return 0; /* TODO: Use page file */
}

/**
 * mmu_free_frames()
 * 
 * Frees frames
 * 
 * @param addr The address to free
 * @param pages The number of pages to free
*/
void mmu_free_frames(void* addr, uint64_t pages) {
	for(uint64_t i = 0; i < pages; i++) {
		mmu_frame_clear(addr + (i * 4096));
	}
}

/**
 * get_next_level
 * 
 * Returns the lower level of the pagemap of index idx
 * 
 * @param top_level The pagemap to search
 * @param idx The index to return
 * @param allocate Create if not present
*/
static uint64_t *get_next_level(uint64_t* top_level, size_t idx, bool allocate) {
	/* Check if the entry is present */
	if((top_level[idx] & PTE_PRESENT) != 0) {
		/* Return the address + hhdm */
		return (uint64_t*)(PTE_GET_ADDR(top_level[idx]) + HHDM_HIGHER_HALF);
	}

	/* We do not allocate and return NULL because the user does not want it to be allocated */
	if(!allocate) return NULL;

	/* Request frame */
	void* next_level = mmu_request_frame();
	
	/**
     * Because we return next_level + hhdm and any function is using that we need to 
	 * memset next_level + hhdm instead of the address returned by mmu_request_frame
	 */
	memset(next_level + HHDM_HIGHER_HALF, 0, PAGE_SIZE);
	
	/* Set the flags to present, writable, user accessable */
	top_level[idx] = (uint64_t)next_level | PTE_PRESENT | PTE_WRITABLE | PTE_USER;

	return next_level + HHDM_HIGHER_HALF;
}

/**
 * mmu_switch_pagemap
 * 
 * Switch to new pagemap
 * 
 * @param the new pagemap to switch to
*/
void mmu_switch_pagemap(pagemap_t* pagemap) {
	asm volatile (
		"mov %0, %%cr3"
		:
		: "r" ((void*)pagemap - HHDM_HIGHER_HALF)
		: "memory"
	);
}

/**
 * mmu_map_page
 * 
 * Map a physical address to a virtual address with changable flags
 * 
 * @param pagemap The pml to use
 * @param virt The virtual address to map to
 * @param phys The physical address to map
 * @param flags The flags to set in the map entry
*/
void mmu_map_page(pagemap_t* pagemap, uintptr_t virt, uintptr_t phys, uint64_t flags) {
	/* Get the indexes using the virtual address */
	uint64_t pml_index = (virt >> 39) & 0x1FF;
	uint64_t pdp_index = (virt >> 30) & 0x1FF;
	uint64_t pd_index = (virt >> 21) & 0x1FF;
	uint64_t pt_index = (virt >> 12) & 0x1FF;

	/* Get the next level in the pagemap, create if it does not exist */
	uint64_t* pdp = get_next_level(pagemap, pml_index, true);
	uint64_t* pd = get_next_level(pdp, pdp_index, true);
	uint64_t* pt = get_next_level(pd, pd_index, true);
	
	/* Set it to phys | flags */
	pt[pt_index] = phys | flags;
}

/**
 * mmu_init()
 * 
 * Initializes the frame allocator
*/
void mmu_init(void) {
    /* Check if the bootloader returns a memmap, if not catch fire */
    struct limine_memmap_response* response = memmap_request.response;
    if(response == NULL || response->entry_count == 0) {
		/* If no memmap then hang */
        asm ("cli");
        for(;;) asm ("hlt");
    }

    struct limine_memmap_entry** entries = response->entries;
    /* Find the largest free memory segment */
    for(uint64_t i = 0; i < response->entry_count; i++) {
        struct limine_memmap_entry* entry = entries[i];

        /* Check if the entry is usable and larger and the previous entry */
        if(entry->type == LIMINE_MEMMAP_USABLE &&
           entry->length > highest_free_address_size)
        {
            highest_free_address = entry->base;
            highest_free_address_size = entry->length;
        }

        /* Add the entry length to the total memory */
        total_memory += entry->length;
    }

    /* Divide the total memory by 4096 (Page size) */ 
    nframes = total_memory >> 12;

    /* Divide the nframes by 8 (1 byte) for the size in bytes of the bitmap */
    bytesOfBitmap = nframes / 8;

    /* Set used memory to full */
    usedMemory = total_memory;

    /**
     * Since there can be many usable entries, using the largest entry
     * may waste memory, so it should be stored in the first usable
     * entry that can fit bytesOfBitmap
     */
    for(uint64_t i = 0; i < response->entry_count; i++) {
        struct limine_memmap_entry* entry = entries[i];

        /* Check if the entry is usable and fits our bitmap */
        if(entry->type == LIMINE_MEMMAP_USABLE && entry->length >= bytesOfBitmap) {
            /* Set the bitmap starting address to this entry base */
            free_segment_bitmap = entry->base;
        }
    }

    /* Set the bitmap address to the free_segment_bitmap */
    bitmap = free_segment_bitmap;

    /* Set all the bits to 1, all the frames will be set to used */
    memset(bitmap, 0xff, bytesOfBitmap);

    /* Set usable memory to 0 in the bitmap */
    for(uint64_t i = 0; i < response->entry_count; i++) {
        struct limine_memmap_entry *entry = entries[i];

        /* Check if the entry is usable */
        if(entry->type == LIMINE_MEMMAP_USABLE) {
            for(uint64_t j = entry->base; j < (entry->base + entry->length); j += 0x1000) {
                /* Clear the frame */
                mmu_frame_clear(j);
            }
        }
    }

	/* Assign a page in the hhdm */
	mmu_kernel_pagemap = mmu_request_frame() + HHDM_HIGHER_HALF;

	struct limine_kernel_address_response *kaddr = kaddr_request.response;

	for(uint64_t i = 0; i < response->entry_count; i++) {
		uintptr_t base = entries[i]->base;
		uintptr_t end = entries[i]->base + entries[i]->length;
		for(uintptr_t i = base; i < end; i += 0x1000) {
			mmu_map_page(mmu_kernel_pagemap, i, i, PTE_PRESENT | PTE_WRITABLE);
			mmu_map_page(mmu_kernel_pagemap, i + HHDM_HIGHER_HALF, i, PTE_PRESENT | PTE_WRITABLE);
		}
	}

	/* Map the text section */
	for(uintptr_t i = text_start; i < text_end; i += 0x1000) {
        mmu_map_page(
            mmu_kernel_pagemap,
            i,
            kaddr->physical_base + (i - kaddr->virtual_base),
            PTE_PRESENT);
    }

	/* Map the rodata section */
    for(uintptr_t i = rodata_start; i < rodata_end; i += 0x1000) {
        mmu_map_page(
            mmu_kernel_pagemap,
            i,
            kaddr->physical_base + (i - kaddr->virtual_base),
            PTE_PRESENT | PTE_NX);
    }

	/* Map the data and bss sections (both have the same end in the linker file) */
    for(uintptr_t i = data_start; i < data_end; i += 0x1000) {
        mmu_map_page(
            mmu_kernel_pagemap,
            i,
            kaddr->physical_base + (i - kaddr->virtual_base),
            PTE_PRESENT | PTE_WRITABLE);
    }

	/* Switch to our pagemap */
	mmu_switch_pagemap(mmu_kernel_pagemap);
}