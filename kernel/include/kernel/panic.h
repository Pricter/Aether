#pragma once
#include <kernel/exception.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef enum {
    PANIC_GENERIC = 0,
    PANIC_CPU_EXCEPTION,
    PANIC_PAGE_FAULT,
    PANIC_ASSERT,
    PANIC_BUG,
    PANIC_OOM,
    PANIC_SMP,
    PANIC_HARDWARE
} panic_category_t;

/* Fatal ownership and nesting are CPU-local/SMP-safe. No allocation, shared
 * lock or formatted logging is allowed. Register faults use exception.c's full
 * report; invariant failures use this constant-string emergency path. */
__attribute__((noreturn)) void _kpanic_impl(panic_category_t category, const char *subsystem,
    const char *file, int line, const char *message, const arm64_exception_frame_t *frame,
    uintptr_t fault_address, uint64_t syndrome);

#define PANIC(message)                                                                             \
    _kpanic_impl(PANIC_GENERIC, "kernel", __FILE__, __LINE__, message, NULL, 0, 0)
#define SUBSYS_PANIC(subsystem, message)                                                           \
    _kpanic_impl(PANIC_GENERIC, subsystem, __FILE__, __LINE__, message, NULL, 0, 0)
#define KERNEL_ASSERT(condition)                                                                   \
    do {                                                                                           \
        if (__builtin_expect(!(condition), 0))                                                     \
            _kpanic_impl(PANIC_ASSERT, "kernel", __FILE__, __LINE__,                               \
                "assertion failed: " #condition, NULL, 0, 0);                                      \
    } while (0)
#define KERNEL_BUG(message)                                                                        \
    do {                                                                                           \
        _kpanic_impl(PANIC_BUG, "kernel", __FILE__, __LINE__, "BUG: " message, NULL, 0, 0);        \
        __builtin_unreachable();                                                                   \
    } while (0)
#define KERNEL_UNREACHABLE() KERNEL_BUG("unreachable code reached")
