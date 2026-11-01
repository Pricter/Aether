#include <kernel/user.h>
#include <kernel/cpu.h>
#include <kernel/panic.h>
#include <kernel/time.h>
#include <platform/serial.h>

extern const unsigned char arm64_user_payload[], arm64_user_payload_end[];

/* Wait for reaping, not merely user exit: process tables cannot be reclaimed
 * until the last kernel stack has switched away and been released. */
static void wait_process(process_t *process) {
    unsigned long deadline = ktime_get_ns() + 3000000000UL;

    for (;;) {
        bool saved_interrupts = spinlock_acquire(&process->lock);
        unsigned int remaining = process->thread_count;
        spinlock_release(&process->lock, saved_interrupts);

        if (!remaining) {
            return;
        }

        KERNEL_ASSERT(ktime_get_ns() < deadline);
        schedule();
    }
}

/* Prepare a static image without a filesystem or ELF loader. Code is RX from
 * its first user-visible PTE; the loader writes only through its physical alias. */
static process_t *make_process(const void *code, size_t length) {
    process_t *process = process_create("EL0 validation");
    KERNEL_ASSERT(process && length <= 4096);
    KERNEL_ASSERT(arm64_user_map(process, 0x400000, 4096, PROT_READ | PROT_EXEC));
    KERNEL_ASSERT(arm64_user_copy(process, 0x400000, (void *)code, length, true, true));
    KERNEL_ASSERT(arm64_user_map(
        process, ARM64_USER_STACK - USTACK_SIZE, USTACK_SIZE, PROT_READ | PROT_WRITE));
    return process;
}

/* Exercise nonresident reservations, permission retention and partial ranges. */
static void test_mapping(void) {
    process_t *process = process_create("mapping validation");
    KERNEL_ASSERT(process);
    KERNEL_ASSERT(!arm64_user_map(process, 0, 4096, PROT_READ));
    KERNEL_ASSERT(!arm64_user_map(process, ARM64_USER_LIMIT, 4096, PROT_READ));
    KERNEL_ASSERT(!arm64_user_map(process, 0x10000, (size_t)-4096, PROT_READ));
    KERNEL_ASSERT(!arm64_user_map(process, 0x10000, 4096, PROT_WRITE | PROT_EXEC));
    KERNEL_ASSERT(arm64_user_map(process, 0x10000, 12288, PROT_READ | PROT_WRITE));
    KERNEL_ASSERT(!arm64_user_map(process, 0x11000, 4096, PROT_READ));

    unsigned long value = 99;
    KERNEL_ASSERT(arm64_user_copy(process, 0x11000, &value, sizeof(value), false, false) && !value);
    value = 0x12345678;
    KERNEL_ASSERT(arm64_user_copy(process, 0x11000, &value, sizeof(value), true, false));
    KERNEL_ASSERT(arm64_user_protect(process, 0x11000, 4096, PROT_READ));
    KERNEL_ASSERT(!arm64_user_copy(process, 0x11000, &value, sizeof(value), true, false));
    KERNEL_ASSERT(arm64_user_protect(process, 0x11000, 4096, PROT_NONE));
    KERNEL_ASSERT(!arm64_user_access(process, 0x11000, PROT_READ));
    KERNEL_ASSERT(arm64_user_protect(process, 0x11000, 4096, PROT_READ));
    value = 0;
    KERNEL_ASSERT(arm64_user_copy(process, 0x11000, &value, sizeof(value), false, false) &&
                  value == 0x12345678);
    KERNEL_ASSERT(arm64_user_unmap(process, 0x11000, 4096));
    KERNEL_ASSERT(!arm64_user_access(process, 0x11000, PROT_READ));
    KERNEL_ASSERT(!arm64_user_protect(process, 0x10000, 12288, PROT_READ));
    KERNEL_ASSERT(arm64_user_access(process, 0x10000, PROT_WRITE));
    KERNEL_ASSERT(arm64_user_map(process, 0x11000, 4096, PROT_READ));
    value = 99;
    KERNEL_ASSERT(arm64_user_copy(process, 0x11000, &value, sizeof(value), false, false) && !value);
    process_destroy(process);
    platform_serial_write("ARM64 user mappings passed\n");
}

/* Fault snippets are A64 opcodes: choose an EL0-forbidden address, then access
 * it. A broken permission would reach exit(77), which the assertions reject. */
static void test_fault(
    const uint32_t *instructions, size_t length, uintptr_t address, bool readonly_data) {
    process_t *process = make_process(instructions, length);

    if (readonly_data) {
        KERNEL_ASSERT(arm64_user_map(process, 0x2000000, 4096, PROT_READ));
        KERNEL_ASSERT(arm64_user_access(process, 0x2000000, PROT_READ));
    }

    thread_t *thread = thread_create_user(process, 0x400000, ARM64_USER_STACK);
    KERNEL_ASSERT(thread);
    thread_ready_on(thread, coreCount > 1 ? 1 : 0);
    wait_process(process);
    KERNEL_ASSERT(process->user_faults == 1 && !process->user_exits &&
                  process->last_fault_address == address);
    if (readonly_data) {
        KERNEL_ASSERT((process->last_syndrome & 63) == 15);
    }

    process_destroy(process);
}

extern const unsigned char arm64_tlb_payload[], arm64_tlb_payload_end[];
extern void arm64_enter_user(
    uintptr_t entry, uintptr_t stack, uintptr_t kernel_stack, uintptr_t status);

/* Testing only: no interrupt or voluntary scheduling may clear this CPU's TLB
 * between warming the entry and a remote CPU revoking it. */
static void masked_user_entry(void) {
    thread_t *thread = this_cpu()->current_thread;
    arm64_enter_user(thread->user_entry, thread->ustack_top, thread->kstack_top, 0x3c0);
    __builtin_unreachable();
}

/* Prove hardware broadcast revocation, rather than incidentally flushing on a
 * context switch. The same test still exercises local revocation on one CPU. */
static void test_shootdown(bool remove_mapping) {
    process_t *process = make_process(arm64_tlb_payload, arm64_tlb_payload_end - arm64_tlb_payload);
    KERNEL_ASSERT(arm64_user_map(process, 0x2000000, 4096, PROT_READ | PROT_WRITE));
    KERNEL_ASSERT(arm64_user_map(process, 0x3000000, 4096, PROT_READ | PROT_WRITE));

    unsigned long control[2] = {0, 0};
    KERNEL_ASSERT(arm64_user_copy(process, 0x3000000, control, sizeof(control), true, true));

    thread_t *thread = thread_create_user(process, 0x400000, ARM64_USER_STACK);
    KERNEL_ASSERT(thread);

    if (coreCount > 1) {
        thread->context.saved_registers[0] = (uintptr_t)masked_user_entry;
    }

    thread_ready_on(thread, coreCount > 1 ? 1 : 0);

    unsigned long deadline = ktime_get_ns() + 2000000000UL;

    do {
        KERNEL_ASSERT(ktime_get_ns() < deadline);
        schedule();
        KERNEL_ASSERT(arm64_user_copy(process, 0x3000000, control, sizeof(control), false, false));
    } while (!control[0]);

    if (remove_mapping) {
        KERNEL_ASSERT(arm64_user_unmap(process, 0x2000000, 4096));
    } else {
        KERNEL_ASSERT(arm64_user_protect(process, 0x2000000, 4096, PROT_READ));
    }

    control[1] = remove_mapping ? 2 : 1;
    KERNEL_ASSERT(
        arm64_user_copy(process, 0x3000008, &control[1], sizeof(control[1]), true, false));
    wait_process(process);
    KERNEL_ASSERT(process->user_faults == 1 && !process->user_exits &&
                  process->last_fault_address == 0x2000000);
    if (coreCount > 1) {
        KERNEL_ASSERT(process->user_switches == 1);
    }

    process_destroy(process);
}

/* Same virtual addresses, different data and TLS, concurrently on every CPU.
 * Busy loops ensure timer IRQs enter from EL0 in addition to voluntary SVCs. */
void arm64_user_validate(frame_allocator_t *allocator) {
    test_mapping();

    process_t *processes[16];

    for (unsigned int process_index = 0; process_index < coreCount * 2; ++process_index) {
        process_t *process =
            make_process(arm64_user_payload, arm64_user_payload_end - arm64_user_payload);
        processes[process_index] = process;
        KERNEL_ASSERT(arm64_user_map(process, 0x2000000, 4096, PROT_READ | PROT_WRITE));
        KERNEL_ASSERT(arm64_user_map(process, 0x3000000, 4096, PROT_READ | PROT_WRITE));

        unsigned long cookie = 0x12340000 + process_index * 0x1000;
        KERNEL_ASSERT(arm64_user_copy(process, 0x2000000, &cookie, sizeof(cookie), true, true));

        thread_t *thread = thread_create_user(process, 0x400000, ARM64_USER_STACK);
        KERNEL_ASSERT(thread);
        thread_ready_on(thread, process_index % coreCount);
    }

    unsigned long total_interrupts = 0;

    for (unsigned int process_index = 0; process_index < coreCount * 2; ++process_index) {
        process_t *process = processes[process_index];
        wait_process(process);
        KERNEL_ASSERT(process->user_exits == 1 && !process->user_status && !process->user_faults);

        unsigned long cookie = 0;
        KERNEL_ASSERT(arm64_user_copy(process, 0x2000000, &cookie, sizeof(cookie), false, false));
        KERNEL_ASSERT(
            cookie == 0x12340000 + process_index * 0x1000 + 32 && process->demand_faults >= 4);
        total_interrupts += process->user_interrupts;
        process_destroy(process);
    }

    KERNEL_ASSERT(total_interrupts);
    platform_serial_write("ARM64 EL0 isolation passed\n");

    /* mov x19,#0x2000000; str xzr,[x19]; exit(77) */
    const uint32_t readonly_fault[] = {0xd2a04013, 0xf900027f, 0xd28009a0, 0xd2800ba8, 0xd4000001};
    test_fault(readonly_fault, sizeof(readonly_fault), 0x2000000, true);

    /* movz x19,#0x4008,lsl#16; ldr x0,[x19] */
    const uint32_t kernel_fault[] = {0xd2a80113, 0xf9400260, 0xd28009a0, 0xd2800ba8, 0xd4000001};
    test_fault(kernel_fault, sizeof(kernel_fault), 0x40080000, false);

    const uint32_t unmapped_fault[] = {0xd2a0c013, 0xf9400260, 0xd28009a0, 0xd2800ba8, 0xd4000001};
    test_fault(unmapped_fault, sizeof(unmapped_fault), 0x6000000, false);

    /* br x19 into a readable non-executable user data page. */
    const uint32_t execute_fault[] = {0xd2a04013, 0xd61f0260};
    test_fault(execute_fault, sizeof(execute_fault), 0x2000000, true);

    /* Invalid SP_EL0 must never become the exception handler's stack. */
    const uint32_t stack_fault[] = {0xd2a80113, 0x9100027f, 0xf90003ff};
    test_fault(stack_fault, sizeof(stack_fault), 0x40080000, false);
    platform_serial_write("ARM64 user faults passed\n");
    test_shootdown(false);
    test_shootdown(true);
    platform_serial_write("ARM64 TLB shootdown passed\n");

    unsigned long free_after_warmup = 0;

    for (unsigned int repeat_index = 0; repeat_index < 3; ++repeat_index) {
        process_t *process = process_create("reclamation validation");
        KERNEL_ASSERT(process);

        uintptr_t mapping = process_mmap(process, 0, 16 * 4096, PROT_READ | PROT_WRITE);
        KERNEL_ASSERT(mapping == MMAP_BASE);

        for (unsigned int page_index = 0; page_index < 16; ++page_index) {
            KERNEL_ASSERT(process_ensure_page(process, mapping + page_index * 4096));
        }

        KERNEL_ASSERT(process_mprotect(process, mapping + 4096, 1, PROT_READ) == 0);
        process_munmap(process, mapping + 8192, 1);
        KERNEL_ASSERT(!process_ensure_page(process, mapping + 8192));
        process_destroy(process);

        if (!repeat_index) {
            free_after_warmup = allocator->free_frames;
        } else {
            KERNEL_ASSERT(allocator->free_frames == free_after_warmup);
        }
    }

    platform_serial_write("ARM64 user reclamation passed\nARM64 EL0 passed\n");
}
