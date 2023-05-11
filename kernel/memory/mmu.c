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
#include <deps/printf.h>

/* Request limine for a memmap */
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
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

/* Size of each page, standard intel is 4KiB */
#define PAGE_SIZE 0x1000

/* The flags in an entry set using macro functions */
/**
 * ExecuteDisable: If the NXE bit is set in the EFER register, then
 * instructions are not allowed to be executed at addresses within
 * whatever page XD is set. if EFER.NXE is 0, then the XD bit is
 * reserved and should be set to 0.
*/
typedef enum {
	Present = 0, /* If the entry is present */
	ReadWrite =  1, /* If the entry has write access */
	UserSupervisor = 2, /* Priviledge level */
	WriteThrough = 3, /* If bit is set, then write-through caching is set */
	CacheDisable = 4, /* If this is set, then the page will not be cached */
	Accessed = 5, /* If the entry was read during virtual address translation */
	PageSize = 7, /* If this is set then the page will be a 4MiB page */
	ExecuteDisable = 63 /* See above */
} PAGE_FLAGS;

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
        uintptr_t address = lastFrame * 4096;

        /* If the frame is free then set it to used and return it */
        if(!mmu_test_frame(address)) {
            mmu_frame_set(address);
            return address;
        }
    }
    return NULL; /* TODO: Use page file */
}

/**
 * mmu_init()
 * 
 * Initializes the frame allocator
*/
void mmu_init(void) {
    printf("mmu: Initialized\n");

    /* Check if the bootloader returns a memmap, if not catch fire */
    struct limine_memmap_response* response = memmap_request.response;
    if(response == NULL || response->entry_count == 0) {
        printf("mmu: Cannot continue without memmap\n");
        asm ("cli");
        for(;;) asm ("hlt");
    }

    struct limine_memmap_entry** entries = response->entries;
    /* Find the largest free memory segment */
    printf("<memmap_type> at <base>: <size>\n\n");
    for(uint64_t i = 0; i < response->entry_count; i++) {
        struct limine_memmap_entry* entry = entries[i];

        /* Print the entry info */
        printf("%s at %p: %012lu\n",
            memmap_strings[entry->type], entry->base, entry->length);

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

    /* Print the information */
    printf("mmu: Highest free memory address: %p\n", highest_free_address);
    printf("mmu: Highest free memory address size: %lu\n", highest_free_address_size);
    printf("mmu: Total memory: %lu\n", total_memory);
    printf("mmu: bitmap entries: %lu\n", nframes);
    printf("mmu: bitmap size: %lu\n", bytesOfBitmap);
    printf("mmu: bitmap starting address: %p\n", bitmap);
    printf("mmu: Used memory: %lu\n", usedMemory);
    printf("mmu: Free memory: %lu\n", freeMemory);
}