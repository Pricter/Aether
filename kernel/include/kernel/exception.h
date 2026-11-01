#pragma once

/* Borrowed snapshot on the interrupted kernel stack; never retained or freed.
 * Register slots match vectors.S. SP describes the handler's pre-entry stack,
 * while user_stack separately captures SP_EL0. SIMD is disabled by -mgeneral-regs-only. */
typedef struct {
    unsigned long general_registers[31];
    unsigned long interrupted_stack;
    unsigned long return_address;
    unsigned long saved_status;
    unsigned long syndrome;
    unsigned long fault_address;
    unsigned long vector_index;
    unsigned long user_stack;
} arm64_exception_frame_t;
_Static_assert(sizeof(arm64_exception_frame_t) == 304, "ARM exception frame layout");

/* Dispatch an IRQ and return to assembly, or enter the fatal reporter. */
void arm64_exception_dispatch(arm64_exception_frame_t *exception_frame);

/* Fatal path: atomic SMP owner and CPU-local nesting, no allocation or locks.
 * Requires a valid kernel stack and initialized polling serial console.
 * Nested faults get minimal output, then an immediate halt at depth three. */
__attribute__((noreturn)) void arm64_exception_report(
    const arm64_exception_frame_t *exception_frame);

/* Architectural exception vector slots and ESR_EL1 fields. */
#define ARM64_VECTOR_KERNEL_IRQ 5UL
#define ARM64_VECTOR_USER_SYNC 8UL
#define ARM64_VECTOR_USER_IRQ 9UL
#define ARM64_ESR_CLASS_SHIFT 26U
#define ARM64_ESR_SVC64 0x15UL
#define ARM64_ESR_INSTRUCTION_ABORT_LOWER 0x20UL
#define ARM64_ESR_DATA_ABORT_LOWER 0x24UL
#define ARM64_ESR_FAULT_STATUS_MASK 0x3fUL
#define ARM64_ESR_WRITE (1UL << 6)
#define ARM64_FAULT_TRANSLATION_FIRST 4U
#define ARM64_FAULT_TRANSLATION_LAST 7U
#define ARM64_FAULT_PERMISSION_FIRST 12U
#define ARM64_FAULT_PERMISSION_LAST 15U
#define ARM64_SPSR_MODE_MASK 0xfUL
/* Enter EL0t with debug, SError and FIQ masked; IRQ remains enabled. */
#define ARM64_USER_INITIAL_STATUS ((1UL << 9) | (1UL << 8) | (1UL << 6))
