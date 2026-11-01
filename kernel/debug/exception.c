#include <kernel/exception.h>
#include <platform/serial.h>
#include <kernel/smp.h>

/* Frame layout is shared with the vector assembly. Fatal ownership and nesting
 * are coordinated by the SMP panic entry, independent of scheduler locks. */

_Static_assert(sizeof(arm64_exception_frame_t) == 304, "exception frame size");
_Static_assert(
    __builtin_offsetof(arm64_exception_frame_t, interrupted_stack) == 248, "saved stack offset");
_Static_assert(
    __builtin_offsetof(arm64_exception_frame_t, vector_index) == 288, "vector index offset");

/* Remain stopped with all asynchronous exceptions masked. */
__attribute__((noreturn)) static void exception_halt(void) {
    __asm__ volatile("msr daifset, #0xf" ::: "memory");

    for (;;) {
        __asm__ volatile("wfi");
    }
}

/* Format a register without printf, heap use, or shared scratch buffers. */
static void exception_write_value(const char *label, unsigned long value) {
    static const char hexadecimal_digits[] = "0123456789abcdef";

    char formatted_value[19] = "0x0000000000000000";

    for (unsigned int digit_index = 0; digit_index < 16; ++digit_index) {
        formatted_value[17 - digit_index] = hexadecimal_digits[value & 15];
        value >>= 4;
    }

    platform_serial_write(label);
    platform_serial_write(formatted_value);
    platform_serial_write("\n");
}

/* Decode only classes needed during early bring-up; retain raw ESR too. */
static const char *exception_class_name(unsigned long exception_class) {
    switch (exception_class) {
    case 0x00:
        return "unknown instruction";
    case 0x20:
        return "instruction abort from lower EL";
    case 0x21:
        return "instruction abort at current EL";
    case 0x24:
        return "data abort from lower EL";
    case 0x25:
        return "data abort at current EL";
    case 0x3c:
        return "AArch64 breakpoint";
    default:
        return "unclassified";
    }
}

/* Report the first fatal exception fully. A second fault uses only constant
 * strings; a third attempts no output. This cannot rescue an invalid stack. */
__attribute__((noreturn)) void arm64_exception_report(
    const arm64_exception_frame_t *exception_frame) {
    unsigned long current_depth = arm64_panic_enter();

    if (current_depth >= 3) {
        exception_halt();
    }

    if (current_depth == 2) {
        platform_serial_write("ARM64 nested exception: emergency halt\n");
        exception_halt();
    }

    platform_serial_write("ARM64 fatal exception\n");
    static const char *const exception_kinds[] = {"synchronous", "IRQ", "FIQ", "SError"};
    platform_serial_write(exception_kinds[exception_frame->vector_index & 3]);
    platform_serial_write("\n");
    exception_write_value("VECTOR=", exception_frame->vector_index);
    exception_write_value("ESR_EL1=", exception_frame->syndrome);
    exception_write_value("ELR_EL1=", exception_frame->return_address);
    exception_write_value("SPSR_EL1=", exception_frame->saved_status);

    /* FAR is not valid for every exception (including BRK). Label it as raw,
     * rather than presenting a stale value as the cause of the exception. */
    exception_write_value("FAR_EL1(raw)=", exception_frame->fault_address);
    exception_write_value("SP_EL1=", exception_frame->interrupted_stack);

    if ((exception_frame->vector_index & 3) == 0) {
        platform_serial_write(exception_class_name((exception_frame->syndrome >> 26) & 63));
        platform_serial_write("\n");
    }

    for (unsigned int register_index = 0; register_index < 31; ++register_index) {
        char register_label[] = "x00=";
        register_label[1] = '0' + register_index / 10;
        register_label[2] = '0' + register_index % 10;
        exception_write_value(register_label, exception_frame->general_registers[register_index]);
    }
#ifdef ARM64_TEST_NESTED_EXCEPTION
    /* Test-only second fault exercises the emergency path without recursion. */
    __asm__ volatile("brk #0x4321");
#endif
    platform_serial_write("ARM64 exception halted\n");
    exception_halt();
}
