#include <kernel/panic.h>
#include <kernel/smp.h>
#include <platform/serial.h>

/* Allocation-free invariant reporting. Register faults use exception_report.
 * SMP ownership and per-CPU nesting require no heap, locks, or formatted logging. */
__attribute__((noreturn)) void _kpanic_impl(panic_category_t category, const char *subsystem,
    const char *file, int line, const char *message, const arm64_exception_frame_t *registers,
    uintptr_t fault_address, uint64_t error_code) {
    (void)category;
    (void)file;
    (void)line;
    (void)registers;
    (void)fault_address;
    (void)error_code;
    __asm__ volatile("msr daifset, #0xf" ::: "memory");

    unsigned long panic_depth = arm64_panic_enter();

    if (panic_depth == 1) {
#ifdef ARM64_TEST_PANIC_PEERS
        platform_serial_write(arm64_panic_wait_peers() ? "ARM64 panic peers stopped\n"
                                                       : "ARM64 panic peers failed to stop\n");
#endif
        platform_serial_write("ARM64 panic: ");
        platform_serial_write(subsystem);
        platform_serial_write(": ");
        platform_serial_write(message);
        platform_serial_write("\n");
    } else if (panic_depth == 2) {
        platform_serial_write("ARM64 nested panic\n");
    }

    for (;;) {
        __asm__ volatile("wfi");
    }
}
