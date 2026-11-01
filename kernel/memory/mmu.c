#include <kernel/mmu.h>

#include <kernel/arm64_mmu.h>

#define QEMU_UART_PAGE 0x09000000UL
#define QEMU_GIC_DISTRIBUTOR_PAGE 0x08000000UL
#define QEMU_GIC_CPU_PAGE 0x08010000UL

/* MAIR indices match PAGE_NORMAL_MEMORY; TCR fields describe 48-bit TTBR0,
 * write-back inner-shareable walks, disabled TTBR1 and its 4 KiB granule. */
#define MAIR_NORMAL_WRITE_BACK (0xffUL << 8)
#define TCR_TTBR0_SIZE_48_BIT 16UL
#define TCR_TTBR0_INNER_WRITE_BACK (1UL << 8)
#define TCR_TTBR0_OUTER_WRITE_BACK (1UL << 10)
#define TCR_TTBR0_INNER_SHAREABLE (3UL << 12)
#define TCR_TTBR1_SIZE_48_BIT (16UL << 16)
#define TCR_TTBR1_DISABLE (1UL << 23)
#define TCR_TTBR1_GRANULE_4K (2UL << 30)
#define TCR_PHYSICAL_SIZE_SHIFT 32U
#define SCTLR_MMU_ENABLE (1UL << 0)
#define SCTLR_DATA_CACHE_ENABLE (1UL << 2)
#define SCTLR_STACK_ALIGNMENT_EL1 (1UL << 3)
#define SCTLR_STACK_ALIGNMENT_EL0 (1UL << 4)
#define SCTLR_INSTRUCTION_CACHE_ENABLE (1UL << 12)
#define SCTLR_WRITE_IMPLIES_NO_EXECUTE (1UL << 19)
#define MMFR0_GRANULE_4K_SHIFT 28U
#define MMFR0_FIELD_MASK 0xfUL
#define MMFR0_GRANULE_UNSUPPORTED 0xfUL
#define MMFR0_PHYSICAL_48_BIT 5UL
#define MMFR0_PHYSICAL_52_BIT 6UL

extern char __text_start[], __text_end[], __rodata_start[], __rodata_end[];

/* New tables are private physical frames until their parent descriptor is
 * published. Identity addressing permits direct access before/after enable. */
static paddr_t allocate_table(arm64_mmu_t *state, frame_allocator_t *allocator) {
    paddr_t table_address;

    if (!frame_allocator_allocate(allocator, 1, &table_address)) {
        return 0;
    }

    unsigned long *table = (unsigned long *)table_address;

    for (unsigned long entry_index = 0; entry_index < ARM64_TABLE_ENTRIES; ++entry_index) {
        table[entry_index] = 0;
    }

    ++state->table_frames;

    return table_address;
}

/* Reclaim an unpublished tree on initialization failure. No leaf RAM belongs
 * to the MMU; only the intermediate tables and root are released here. */
static void release_tables(
    paddr_t table_address, unsigned int level, frame_allocator_t *allocator) {
    unsigned long *table = (unsigned long *)table_address;

    if (level < ARM64_LEAF_LEVEL) {
        for (unsigned long entry_index = 0; entry_index < ARM64_TABLE_ENTRIES; ++entry_index) {
            if ((table[entry_index] & ARM64_DESCRIPTOR_TABLE_OR_PAGE) ==
                ARM64_DESCRIPTOR_TABLE_OR_PAGE) {
                release_tables(table[entry_index] & ARM64_DESCRIPTOR_ADDRESS, level + 1, allocator);
            }
        }
    }

    frame_allocator_release(allocator, table_address, 1);
}

/* Populate an inactive four-level tree. No block descriptors or live remaps
 * are used, avoiding break-before-make requirements during this milestone. */
static bool map_page(
    arm64_mmu_t *state, frame_allocator_t *allocator, paddr_t address, unsigned long attributes) {
    unsigned long *table = (unsigned long *)state->root_table;

    for (unsigned int level = 0; level < ARM64_LEAF_LEVEL; ++level) {
        unsigned long entry_index =
            (address >> (ARM64_LEVEL_ZERO_SHIFT - ARM64_TABLE_INDEX_BITS * level)) &
            ARM64_TABLE_INDEX_MASK;
        if (!(table[entry_index] & ARM64_DESCRIPTOR_VALID)) {
            paddr_t child_table = allocate_table(state, allocator);

            if (!child_table) {
                return false;
            }

            table[entry_index] = child_table | ARM64_DESCRIPTOR_TABLE_OR_PAGE;
        }

        table = (unsigned long *)(table[entry_index] & ARM64_DESCRIPTOR_ADDRESS);
    }

    unsigned long page_index = (address >> ARM64_PAGE_SHIFT) & ARM64_TABLE_INDEX_MASK;

    if (table[page_index] & ARM64_DESCRIPTOR_VALID) {
        return false;
    }

    table[page_index] = address | attributes | ARM64_DESCRIPTOR_TABLE_OR_PAGE;

    return true;
}

/* Select EL1 permissions from page-aligned linker boundaries. Never grant
 * EL0 access; only kernel text may execute, and it is never writable. */
static unsigned long ram_attributes(paddr_t address) {
    unsigned long attributes = ARM64_PAGE_ACCESS_FLAG | ARM64_PAGE_INNER_SHAREABLE |
                               ARM64_PAGE_NORMAL_MEMORY | ARM64_PAGE_USER_NO_EXECUTE;
    if (address >= (paddr_t)__text_start && address < (paddr_t)__text_end) {
        return attributes | ARM64_PAGE_READ_ONLY;
    }

    attributes |= ARM64_PAGE_PRIVILEGED_NO_EXECUTE;

    if (address >= (paddr_t)__rodata_start && address < (paddr_t)__rodata_end) {
        attributes |= ARM64_PAGE_READ_ONLY;
    }

    return attributes;
}

/* Install tables only after all writes reach memory. Inner-shareable WB walks
 * use MAIR index 1 (normal WB); index 0 is Device-nGnRnE for the serial UART.
 * T0SZ=16 selects 48-bit TTBR0 VA; TTBR1 walks stay disabled. */
static void activate_tables(paddr_t root_table, unsigned long physical_size_encoding) {
    unsigned long memory_attributes = MAIR_NORMAL_WRITE_BACK;
    unsigned long translation_control =
        TCR_TTBR0_SIZE_48_BIT | TCR_TTBR0_INNER_WRITE_BACK | TCR_TTBR0_OUTER_WRITE_BACK |
        TCR_TTBR0_INNER_SHAREABLE | TCR_TTBR1_SIZE_48_BIT | TCR_TTBR1_DISABLE |
        TCR_TTBR1_GRANULE_4K | (physical_size_encoding << TCR_PHYSICAL_SIZE_SHIFT);
    __asm__ volatile("dsb sy\n"
                     "msr mair_el1, %0\n"
                     "msr tcr_el1, %1\n"
                     "msr ttbr0_el1, %2\n"
                     "msr ttbr1_el1, xzr\n"
                     "isb\n"
                     "tlbi vmalle1\n"
                     "ic iallu\n"
                     "dsb sy\n"
                     "isb" ::"r"(memory_attributes),
        "r"(translation_control), "r"(root_table)
        : "memory");
    unsigned long system_control;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(system_control));

    /* Enable translation, both caches, aligned stacks, and write-implies-NX.
     * Preserve architecture-mandated RES1 bits from the existing register. */
    system_control |= SCTLR_MMU_ENABLE | SCTLR_DATA_CACHE_ENABLE | SCTLR_STACK_ALIGNMENT_EL1 |
                      SCTLR_STACK_ALIGNMENT_EL0 | SCTLR_INSTRUCTION_CACHE_ENABLE |
                      SCTLR_WRITE_IMPLIES_NO_EXECUTE;
    __asm__ volatile("msr sctlr_el1, %0; isb" ::"r"(system_control) : "memory");
}

/* Build the whole tree before activation, so any recoverable allocation
 * failure can roll back without modifying the running translation regime. */
bool arm64_mmu_enable(
    arm64_mmu_t *state, const boot_memory_map_t *memory_map, frame_allocator_t *allocator) {
    if (state->enabled || state->root_table || !allocator->initialized) {
        return false;
    }

    unsigned long system_control;
    unsigned long memory_features;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(system_control));
    __asm__ volatile("mrs %0, id_aa64mmfr0_el1" : "=r"(memory_features));

    if ((system_control & (SCTLR_MMU_ENABLE | SCTLR_DATA_CACHE_ENABLE)) ||
        ((memory_features >> MMFR0_GRANULE_4K_SHIFT) & MMFR0_FIELD_MASK) ==
            MMFR0_GRANULE_UNSUPPORTED ||
        !memory_map->ram_count || memory_map->ram_count > BOOT_MEMORY_MAX_RAM) {
        return false;
    }

    unsigned long physical_size_encoding = memory_features & MMFR0_FIELD_MASK;

    if (physical_size_encoding > MMFR0_PHYSICAL_52_BIT) {
        return false;
    }

    if (physical_size_encoding > MMFR0_PHYSICAL_48_BIT) {
        physical_size_encoding = MMFR0_PHYSICAL_48_BIT;
    }

    static const unsigned int physical_bits[] = {32, 36, 40, 42, 44, 48};

    unsigned long physical_limit = 1UL << physical_bits[physical_size_encoding];

    for (unsigned int range_index = 0; range_index < memory_map->ram_count; ++range_index) {
        boot_memory_range_t ram_range = memory_map->ram[range_index];

        if (ram_range.base >= ram_range.end || ram_range.end > physical_limit ||
            ((ram_range.base | ram_range.end) & (FRAME_SIZE - 1)) ||
            (ram_range.base <= QEMU_UART_PAGE && ram_range.end > QEMU_UART_PAGE)) {
            return false;
        }
    }

    state->root_table = allocate_table(state, allocator);

    if (!state->root_table) {
        return false;
    }

    bool success = true;

    for (unsigned int range_index = 0; success && range_index < memory_map->ram_count;
        ++range_index) {
        boot_memory_range_t ram_range = memory_map->ram[range_index];

        for (paddr_t address = ram_range.base; address < ram_range.end; address += FRAME_SIZE) {
            if (!map_page(state, allocator, address, ram_attributes(address))) {
                success = false;
                break;
            }
        }
    }

    if (success) {
        success = map_page(state, allocator, QEMU_UART_PAGE,
            ARM64_PAGE_ACCESS_FLAG | ARM64_PAGE_PRIVILEGED_NO_EXECUTE | ARM64_PAGE_USER_NO_EXECUTE);
    }

    if (success) {
        success = map_page(state, allocator, QEMU_GIC_DISTRIBUTOR_PAGE,
                      ARM64_PAGE_ACCESS_FLAG | ARM64_PAGE_PRIVILEGED_NO_EXECUTE |
                          ARM64_PAGE_USER_NO_EXECUTE) &&
                  map_page(state, allocator, QEMU_GIC_CPU_PAGE,
                      ARM64_PAGE_ACCESS_FLAG | ARM64_PAGE_PRIVILEGED_NO_EXECUTE |
                          ARM64_PAGE_USER_NO_EXECUTE);
    }

    if (!success) {
        release_tables(state->root_table, 0, allocator);
        *state = (arm64_mmu_t){0};

        return false;
    }

    activate_tables(state->root_table, physical_size_encoding);
    state->enabled = true;

    return true;
}

/* PAR_EL1.F distinguishes a translation fault from a valid physical result.
 * ISB completes the AT operation before reading its architectural result. */
bool arm64_mmu_translate(vaddr_t virtual_address, paddr_t *physical_address) {
    unsigned long translation_result;
    __asm__ volatile("at s1e1r, %1; isb; mrs %0, par_el1"
        : "=r"(translation_result)
        : "r"(virtual_address)
        : "memory");
    if (translation_result & 1) {
        *physical_address = 0;

        return false;
    }
    *physical_address =
        (translation_result & ARM64_DESCRIPTOR_ADDRESS) | (virtual_address & (FRAME_SIZE - 1));
    return true;
}
