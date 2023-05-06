#include <kernel/mmu.h>
#include <stdint.h>
#include <stddef.h>
#include <limine.h>

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

void mmu_init() {
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
        total_memory += entry->length;
    }

    printf("mmu: Highest free memory address: %p\n", highest_free_address);
    printf("mmu: Highest free memory address size: %lu\n", highest_free_address_size);
    printf("mmu: Total memory: %lu\n", total_memory);
}