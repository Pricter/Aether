#include <kernel/userspace.h>
#include <kernel/cpu.h>
#include <kernel/page_memory.h>
#include <kernel/arm64_mmu.h>

/* All roots share privileged code/stacks, so TTBR can change before switching
 * stacks. Flush ASID zero locally and preserve each thread's user TLS register. */
void arm64_process_switch(thread_t *outgoing, thread_t *incoming) {
    uint64_t *kernel_root = arm64_user_root(NULL);

    if (!kernel_root) {
        return;
    }

    if (outgoing && outgoing->user_entry) {
        __atomic_fetch_add(&outgoing->parent->user_switches, 1, __ATOMIC_RELAXED);
    }

    if (outgoing) {
        __asm__ volatile("mrs %0, tpidr_el0" : "=r"(outgoing->user_tls));
    }

    __asm__ volatile("msr tpidr_el0, %0" ::"r"(incoming->user_tls));

    uint64_t *root =
        incoming->parent->address_space ? incoming->parent->page_table_root : kernel_root;
    uintptr_t current_root;
    __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(current_root));

    if ((current_root & ARM64_DESCRIPTOR_ADDRESS) != page_memory_physical(root)) {
        __asm__ volatile("dsb ish; msr ttbr0_el1, %0; isb; tlbi vmalle1; dsb ish; isb" ::"r"(
            page_memory_physical(root))
            : "memory");
    }

    __atomic_store_n(&this_cpu()->active_process,
        incoming->parent->address_space ? incoming->parent : NULL, __ATOMIC_RELEASE);
}

extern void arm64_enter_user(
    uintptr_t entry, uintptr_t stack, uintptr_t kernel_stack, uintptr_t status);

/* Fresh thread trampoline has already selected the process root and TLS. */
__attribute__((noreturn)) void arm64_user_thread_start(void) {
    thread_t *thread = this_cpu()->current_thread;
    arm64_enter_user(
        thread->user_entry, thread->ustack_top, thread->kstack_top, ARM64_USER_INITIAL_STATUS);
    __builtin_unreachable();
}

/* Decode synchronous EL0 exceptions and resolve permitted demand faults.
 * Bad user instructions/addresses terminate this thread without panicking EL1. */
void arm64_user_exception(arm64_exception_frame_t *frame) {
    thread_t *thread = this_cpu()->current_thread;

    if (!thread || !thread->user_entry || (frame->saved_status & ARM64_SPSR_MODE_MASK)) {
        arm64_exception_report(frame);
    }

    enum { USER_FAULT_SIGNAL = 11 };

    process_t *process = thread->parent;
    unsigned long exception_class = frame->syndrome >> ARM64_ESR_CLASS_SHIFT;

    if (exception_class == ARM64_ESR_SVC64) {
        /* IRQ entry has finished. Synchronous syscalls may wait/preempt on
         * their owned kernel stack; no raw EL0 pointer crosses into the VFS. */
        kernel_irq_restore(true);
        frame->general_registers[0] = arm64_syscall_dispatch(frame);
        kernel_irq_save();

        return;
    }

    if (exception_class == ARM64_ESR_INSTRUCTION_ABORT_LOWER ||
        exception_class == ARM64_ESR_DATA_ABORT_LOWER) {
        unsigned int fault_code = frame->syndrome & ARM64_ESR_FAULT_STATUS_MASK;
        unsigned int access = exception_class == ARM64_ESR_INSTRUCTION_ABORT_LOWER ? PROT_EXEC
                              : (frame->syndrome & ARM64_ESR_WRITE)                ? PROT_WRITE
                                                                                   : PROT_READ;

        /* Only translation/permission faults can be resolved. Alignment and
         * external aborts must not loop forever merely because a VMA exists. */
        if (((fault_code >= ARM64_FAULT_TRANSLATION_FIRST &&
                 fault_code <= ARM64_FAULT_TRANSLATION_LAST) ||
                (fault_code >= ARM64_FAULT_PERMISSION_FIRST &&
                    fault_code <= ARM64_FAULT_PERMISSION_LAST)) &&
            arm64_user_access(process, frame->fault_address, access)) {
            return;
        }
    }

    process->last_fault_address = frame->fault_address;
    process->last_syndrome = frame->syndrome;
    __atomic_fetch_add(&process->user_faults, 1, __ATOMIC_RELEASE);

    if (process->managed_process) {
        arm64_process_exit(USER_FAULT_SIGNAL, true);
    }

    thread_exit();
}
