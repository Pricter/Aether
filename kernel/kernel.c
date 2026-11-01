#include <platform/serial.h>
#include <kernel/boot_memory.h>
#include <kernel/frame_allocator.h>
#include <kernel/mmu.h>
#include <kernel/runtime.h>
#include <kernel/cpu.h>
#include <kernel/version.h>

volatile unsigned long arm64_bss_probe;

/* Permanent boot metadata, owned by the boot CPU; no borrowed DTB pointers. */
static boot_memory_map_t boot_memory_map;
static frame_allocator_t boot_frames;
static arm64_mmu_t boot_mmu;
extern char __image_start[], __kernel_end[], __stack_bottom[], __stack_top[];
extern char __rodata_start[];

#ifdef KERNEL_SELF_TESTS
/* Print physical addresses without depending on heap-backed normal logging. */
static void early_write_hex(unsigned long value) {
    const char *digits = "0123456789abcdef";
    char message[19] = "0x0000000000000000";

    for (unsigned int digit_index = 0; digit_index < 16; ++digit_index) {
        message[17 - digit_index] = digits[value & 15];
        value >>= 4;
    }

    platform_serial_write(message);
}

/* Display half-open physical intervals in a machine-checkable format. */
static void early_write_range(const char *kind, const boot_memory_range_t *range) {
    platform_serial_write(kind);
    platform_serial_write(" ");
    early_write_hex(range->base);
    platform_serial_write(" ");
    early_write_hex(range->end);
    platform_serial_write(" ");
    platform_serial_write(range->description);
    platform_serial_write("\n");
}

#endif

/* Confirm all live boot objects lie wholly within reported physical RAM. */
static bool early_range_in_ram(paddr_t base, paddr_t end) {
    for (unsigned int range_index = 0; range_index < boot_memory_map.ram_count; ++range_index) {
        boot_memory_range_t ram_range = boot_memory_map.ram[range_index];

        if (base >= ram_range.base && end <= ram_range.end) {
            return true;
        }
    }

    return false;
}

/* Reserve metadata before initializing the allocator, so it can never return
 * its own bitmap as free RAM. Storage belongs to boot_frames permanently. */
static bool early_initialize_frames(void) {
    size_t bitmap_bytes = frame_allocator_metadata_size(&boot_memory_map);

    if (!bitmap_bytes || bitmap_bytes > ~0UL - (FRAME_SIZE - 1)) {
        return false;
    }

    size_t storage_bytes = (bitmap_bytes + FRAME_SIZE - 1) & ~(FRAME_SIZE - 1);
    paddr_t bitmap_address = 0;

    for (unsigned int range_index = 0; range_index < boot_memory_map.available_count;
        ++range_index) {
        boot_memory_range_t available = boot_memory_map.available[range_index];

        if (available.end - available.base >= storage_bytes) {
            bitmap_address = available.base;
            break;
        }
    }

    if (!bitmap_address ||
        !boot_memory_reserve(
            &boot_memory_map, bitmap_address, bitmap_address + storage_bytes, "frame bitmap") ||
        !boot_memory_finalize(&boot_memory_map)) {
        return false;
    }

    return frame_allocator_initialize(
        &boot_frames, &boot_memory_map, (void *)bitmap_address, storage_bytes);
}

#ifdef KERNEL_SELF_TESTS
/* Exercise physical ownership and persistence across the MMU transition.
 * Scratch frames are owned here and released before successful return. */
static bool early_enable_memory(void) {
    unsigned long initial_free = boot_frames.free_frames;
    paddr_t scratch_address;

    if (!frame_allocator_allocate(&boot_frames, 3, &scratch_address)) {
        return false;
    }

    volatile unsigned long *scratch = (volatile unsigned long *)scratch_address;
    scratch[0] = 0x123456789abcdef0UL;
    scratch[FRAME_SIZE / sizeof(unsigned long)] = 0xfedcba9876543210UL;

    if (frame_allocator_release(&boot_frames, scratch_address + 1, 1) ||
        scratch[FRAME_SIZE / sizeof(unsigned long)] != 0xfedcba9876543210UL ||
        !frame_allocator_release(&boot_frames, scratch_address, 3) ||
        frame_allocator_release(&boot_frames, scratch_address, 3) ||
        boot_frames.free_frames != initial_free) {
        return false;
    }

    if (!frame_allocator_allocate(&boot_frames, 1, &scratch_address)) {
        return false;
    }

    scratch = (volatile unsigned long *)scratch_address;
    scratch[0] = 0x123456789abcdef0UL;
    platform_serial_write("ARM64 frame allocator passed\n");

    if (!arm64_mmu_enable(&boot_mmu, &boot_memory_map, &boot_frames)) {
        frame_allocator_release(&boot_frames, scratch_address, 1);

        return false;
    }

    unsigned long system_control;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(system_control));

    paddr_t translated_address;

    if (!(system_control & 1) || scratch[0] != 0x123456789abcdef0UL ||
        !arm64_mmu_translate((vaddr_t)scratch_address, &translated_address) ||
        translated_address != scratch_address ||
        !arm64_mmu_translate((vaddr_t)__image_start, &translated_address) ||
        translated_address != (paddr_t)__image_start ||
        arm64_mmu_translate(0, &translated_address)) {
        return false;
    }

    scratch[0] = 0xfedcba9876543210UL;

    if (scratch[0] != 0xfedcba9876543210UL ||
        !frame_allocator_release(&boot_frames, scratch_address, 1)) {
        return false;
    }

    /* Allocation remains usable after activation; no frame aliases survive
     * release in this function. Page-table frames retain permanent ownership. */
    if (!frame_allocator_allocate(&boot_frames, 2, &scratch_address)) {
        return false;
    }

    scratch = (volatile unsigned long *)scratch_address;
    scratch[0] = 0x55aa55aa;

    if (scratch[0] != 0x55aa55aa || !frame_allocator_release(&boot_frames, scratch_address, 2) ||
        boot_frames.free_frames != initial_free - boot_mmu.table_frames) {
        return false;
    }

    platform_serial_write("MMU_TABLE_FRAMES ");
    early_write_hex(boot_mmu.table_frames);
    platform_serial_write("\nFRAME_FREE_COUNT ");
    early_write_hex(boot_frames.free_frames);
    platform_serial_write("\nARM64 MMU enabled\nARM64 memory access passed\n");

    return true;
}

#else
/* Normal boot constructs the address space without running allocator probes. */
static bool early_enable_memory(void) {
    return arm64_mmu_enable(&boot_mmu, &boot_memory_map, &boot_frames);
}
#endif

#ifdef ARM64_TEST_MMU_ROLLBACK
/* Give a temporary allocator only four owned pages: table construction must
 * exhaust them and roll back. The parent allocator owns the backing frames. */
static bool early_verify_mmu_rollback(void) {
    paddr_t backing_address;

    if (!frame_allocator_allocate(&boot_frames, 4, &backing_address)) {
        return false;
    }

    boot_memory_map_t limited_map = {0};
    limited_map.available_count = 1;
    limited_map.available[0] =
        (boot_memory_range_t){backing_address, backing_address + 4 * FRAME_SIZE, "test tables"};
    unsigned char temporary_bitmap[1];
    frame_allocator_t limited_frames;
    arm64_mmu_t limited_mmu = {0};
    bool passed = frame_allocator_initialize(
                      &limited_frames, &limited_map, temporary_bitmap, sizeof(temporary_bitmap)) &&
                  !arm64_mmu_enable(&limited_mmu, &boot_memory_map, &limited_frames) &&
                  !limited_mmu.enabled && !limited_mmu.root_table && !limited_mmu.table_frames &&
                  limited_frames.free_frames == 4 && temporary_bitmap[0] == 0;
    bool released = frame_allocator_release(&boot_frames, backing_address, 4);

    return passed && released;
}
#endif

/* Park the CPU with exceptions masked. No scheduler exists at this stage. */
__attribute__((noreturn)) static void arm64_idle(void) {
    for (;;) {
        __asm__ volatile("wfi");
    }
}

/* Validate the assembly handoff before announcing successful ARM bring-up.
 * QEMU supplies a readable DTB via the standard arm64 Image boot protocol. */
__attribute__((noreturn)) void arm64_early_main(
    unsigned long initial_stack, paddr_t device_tree_address) {
    unsigned long exception_level;
    unsigned long translation_control;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(exception_level));
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(translation_control));
    platform_serial_init();
    arm64_cpu_initialize_boot();

    if (exception_level != 4 || (initial_stack & 15) || arm64_bss_probe != 0 ||
        (translation_control & 1)) {
        platform_serial_write("ARM64 boot checks failed\n");
        arm64_idle();
    }

    platform_serial_write(OS_NAME " ARM64 booted\n");
    platform_serial_write(OS_NAME " kernel " KERNEL_VERSION "\n");
    platform_serial_write("EL1; stack aligned; BSS cleared; MMU off\n");

    /* The bootloader must supply a readable, 8-byte-aligned DTB no larger
     * than 2 MiB. Physical pointers are usable directly only while MMU is off. */
    if (device_tree_address < 0x40000000UL || (device_tree_address & 7) ||
        device_tree_address > ~0UL - 2 * 1024 * 1024 ||
        !boot_memory_discover((const void *)device_tree_address, 2 * 1024 * 1024,
            device_tree_address, &boot_memory_map)) {
        platform_serial_write("ARM64 memory discovery failed: invalid device tree\n");
        arm64_idle();
    }

    paddr_t device_tree_end = device_tree_address + boot_memory_map.device_tree_size;

    if (!early_range_in_ram((paddr_t)__image_start, (paddr_t)__stack_top) ||
        !early_range_in_ram(device_tree_address, device_tree_end) ||
        (device_tree_address < (paddr_t)__stack_top && device_tree_end > (paddr_t)__image_start) ||
        !boot_memory_reserve(
            &boot_memory_map, (paddr_t)__image_start, (paddr_t)__kernel_end, "kernel") ||
        !boot_memory_reserve(
            &boot_memory_map, (paddr_t)__stack_bottom, (paddr_t)__stack_top, "boot stack") ||
        !boot_memory_finalize(&boot_memory_map) || !early_initialize_frames()) {
        platform_serial_write("ARM64 memory discovery failed: unsafe memory map\n");
        arm64_idle();
    }
#ifdef KERNEL_SELF_TESTS
    unsigned long available_bytes = 0;

    for (unsigned int range_index = 0; range_index < boot_memory_map.ram_count; ++range_index) {
        early_write_range("RAM", &boot_memory_map.ram[range_index]);
    }

    for (unsigned int range_index = 0; range_index < boot_memory_map.reserved_count;
        ++range_index) {
        early_write_range("RESERVED", &boot_memory_map.reserved[range_index]);
    }

    for (unsigned int range_index = 0; range_index < boot_memory_map.available_count;
        ++range_index) {
        early_write_range("AVAILABLE", &boot_memory_map.available[range_index]);
        available_bytes += boot_memory_map.available[range_index].end -
                           boot_memory_map.available[range_index].base;
    }

    platform_serial_write("AVAILABLE_BYTES ");
    early_write_hex(available_bytes);
    platform_serial_write("\nARM64 memory discovery passed\n");
#endif
#ifdef ARM64_TEST_MMU_ROLLBACK
    if (!early_verify_mmu_rollback()) {
        platform_serial_write("ARM64 MMU rollback failed\n");
        arm64_idle();
    }

    platform_serial_write("ARM64 MMU rollback passed\n");
#endif
    if (!early_enable_memory()) {
        platform_serial_write("ARM64 memory initialization failed\n");
        arm64_idle();
    }
#if defined(ARM64_TEST_MMU_UNMAPPED) || defined(ARM64_TEST_MMU_READONLY) ||                        \
    defined(ARM64_TEST_MMU_EXECUTE)
    /* Separate test builds deliberately violate the active map's permissions. */
    paddr_t fault_target = 0;
#ifdef ARM64_TEST_MMU_READONLY
    fault_target = (paddr_t)__rodata_start;
#elif defined(ARM64_TEST_MMU_EXECUTE)
    if (!frame_allocator_allocate(&boot_frames, 1, &fault_target)) {
        platform_serial_write("ARM64 MMU fault test allocation failed\n");
        arm64_idle();
    }
    *(volatile unsigned int *)fault_target = 0xd65f03c0; /* RET if NX is broken. */
#endif
    platform_serial_write("MMU fault target=");
    early_write_hex(fault_target);
    platform_serial_write("\n");
#ifdef ARM64_TEST_MMU_READONLY
    __asm__ volatile("str xzr, [%0]" ::"r"(fault_target) : "memory");
#elif defined(ARM64_TEST_MMU_EXECUTE)
    __asm__ volatile("blr %0" ::"r"(fault_target) : "x30", "memory");
#else
    __asm__ volatile("ldr x0, [%0]" ::"r"(fault_target) : "x0", "memory");
#endif
    platform_serial_write("ARM64 MMU fault test unexpectedly returned\n");
#endif
#ifdef ARM64_TEST_EXCEPTION
    /* A deliberate BRK must enter current-EL/SP_EL1 synchronous slot 4.
     * Seed caller-saved registers to verify assembly saves original values. */
    platform_serial_write("ARM64 fault test: BRK\n");
    __asm__ volatile("mov x0, #0x1234; mov x1, #0x5678; brk #0x1234" ::: "x0", "x1", "memory");
    platform_serial_write("ARM64 fault test unexpectedly returned\n");
#endif
#ifndef KERNEL_SELF_TESTS
    platform_serial_write("Memory ready\n");
#endif
    if (!arm64_runtime_initialize(&boot_frames)) {
        platform_serial_write("ARM64 runtime initialization failed\n");
        arm64_idle();
    }

    arm64_scheduler_start(&boot_frames, &boot_memory_map);
}
