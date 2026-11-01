#include <kernel/runtime.h>
#include <kernel/page_memory.h>
#include <kernel/heap.h>
#include <kernel/interrupts.h>
#include <kernel/time.h>
#include <platform/serial.h>

#ifdef KERNEL_SELF_TESTS
/* Timer callback storage is permanent because delivery continues in idle. */
static uint64_t hook_calls;
static bool hook_context_invalid;

/* Print scalar diagnostics without normal logging or heap dependencies. */
static void runtime_write_value(const char *label, uint64_t value) {
    const char *digits = "0123456789abcdef";
    char formatted[19] = "0x0000000000000000";

    for (unsigned int digit_index = 0; digit_index < 16; ++digit_index) {
        formatted[17 - digit_index] = digits[value & 15];
        value >>= 4;
    }

    platform_serial_write(label);
    platform_serial_write(formatted);
    platform_serial_write("\n");
}

/* Check all cache classes, large allocations, failure ownership, and resizing. */
static bool verify_heap(frame_allocator_t *allocator) {
    static const size_t sizes[] = {0, 1, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 2049, 8192};

    for (size_t size_index = 0; size_index < sizeof(sizes) / sizeof(sizes[0]); ++size_index) {
        size_t size = sizes[size_index];
        unsigned char *allocation = malloc(size);

        if (!allocation || ((uintptr_t)allocation & 7)) {
            return false;
        }

        for (size_t byte_index = 0; byte_index < size; ++byte_index) {
            if (allocation[byte_index]) {
                return false;
            }

            allocation[byte_index] = (unsigned char)(byte_index ^ 0xa5);
        }

        unsigned char *replacement = realloc(allocation, size + 4096);

        if (!replacement) {
            free(allocation);

            return false;
        }

        for (size_t byte_index = 0; byte_index < size; ++byte_index) {
            if (replacement[byte_index] != (unsigned char)(byte_index ^ 0xa5)) {
                return false;
            }
        }

        free(replacement);
    }

    unsigned long initial_free = allocator->free_frames;
    unsigned char *large = malloc(8192);

    if (!large) {
        return false;
    }

    large[0] = 0x5a;

    if (realloc(large, (size_t)-1) || large[0] != 0x5a || malloc((size_t)-1) ||
        malloc(allocator->total_frames * FRAME_SIZE)) {
        return false;
    }

    free(large);
    void *minimum = malloc(0);

    if (!minimum || realloc(minimum, 0) != NULL || allocator->free_frames != initial_free) {
        return false;
    }

    free(NULL);

    return true;
}

/* Validate IRQ context without allocation, logging, or any blocking operation. */
static void timer_test_hook(void *context) {
    (void)context;

    bool previously_enabled = kernel_irq_save();

    if (kernel_interrupt_depth() != 1 || previously_enabled || !ktime_get_ns()) {
        __atomic_store_n(&hook_context_invalid, true, __ATOMIC_RELAXED);
    }

    kernel_irq_restore(previously_enabled);
    __atomic_fetch_add(&hook_calls, 1, __ATOMIC_RELAXED);
#ifdef ARM64_TEST_HEAP_IRQ
    /* Must fail before touching heap metadata or acquiring a cache lock. */
    void *forbidden_allocation = malloc(8);
    (void)forbidden_allocation;
#endif
}

/* The caller owns this temporary context until both SGIs complete/unregister. */
static void software_test_handler(void *context) {
    uint64_t *deliveries = context;

    if (kernel_interrupt_depth() != 1) {
        __atomic_store_n(&hook_context_invalid, true, __ATOMIC_RELAXED);
    }

    __atomic_fetch_add(deliveries, 1, __ATOMIC_RELAXED);
}

/* Poll the counter for a bounded test wait; production sleeping awaits scheduler. */
static bool wait_for_ticks(uint64_t target) {
    uint64_t deadline = ktime_get_ns() + 1000000000UL;

    while (kernel_timer_ticks() < target && ktime_get_ns() < deadline) {
        kernel_cpu_relax();
    }

    return kernel_timer_ticks() >= target;
}

/* Two deliveries of the same SGI verify acknowledgement, EOI, and ERET. */
static bool verify_software_interrupts(void) {
    uint64_t *deliveries = malloc(sizeof(*deliveries));

    if (!deliveries || !kernel_irq_register(1, software_test_handler, deliveries)) {
        free(deliveries);

        return false;
    }

    bool passed = true;

    for (uint64_t expected = 1; expected <= 2; ++expected) {
        if (!kernel_irq_send_self(1)) {
            passed = false;
            break;
        }

        uint64_t deadline = ktime_get_ns() + 1000000000UL;

        while (
            __atomic_load_n(deliveries, __ATOMIC_RELAXED) < expected && ktime_get_ns() < deadline) {
            kernel_cpu_relax();
        }

        if (__atomic_load_n(deliveries, __ATOMIC_RELAXED) != expected) {
            passed = false;
            break;
        }
    }

    bool removed = kernel_irq_unregister(1);
    free(deliveries);

    return passed && removed;
}

/* Exercise actual interrupted registers: an IRQ handler may clobber its C
 * caller-saved registers, but the vector must restore the interrupted values. */
static bool verify_interrupt_registers(void) {
    unsigned long difference;
    __asm__ volatile("mov x0, #0x1234; mov x1, #0x5678; wfi; "
                     "mov x2, #0x1234; mov x3, #0x5678; "
                     "eor x0, x0, x2; eor x1, x1, x3; orr %0, x0, x1"
        : "=&r"(difference)::"x0", "x1", "x2", "x3", "memory");
    return difference == 0;
}

/* Repeated heap work with IRQs active catches lock restore and register bugs. */
static bool verify_heap_under_timer(void) {
    uint64_t starting_ticks = kernel_timer_ticks();
    uint64_t deadline = ktime_get_ns() + 60000000UL;
    unsigned long iteration = 0;

    while (ktime_get_ns() < deadline) {
        size_t size = iteration % 3072 + 1;
        unsigned char *allocation = malloc(size);

        if (!allocation) {
            return false;
        }

        allocation[0] = 0x5a;
        allocation[size - 1] = 0xa5;

        if (allocation[size - 1] != 0xa5 || (size > 1 && allocation[0] != 0x5a)) {
            return false;
        }

        free(allocation);
        ++iteration;
    }

    return iteration && kernel_timer_ticks() > starting_ticks && !kernel_interrupt_depth();
}

/* Verify nested masking, timer delivery/stop/restart, and monotonic time. */
static bool verify_timer(void) {
    bool outer_state = kernel_irq_save();
    bool inner_state = kernel_irq_save();
    kernel_irq_restore(inner_state);

    bool restored_inner = kernel_irq_save();
    kernel_irq_restore(outer_state);

    if (!outer_state || inner_state || restored_inner || !wait_for_ticks(5) ||
        !verify_interrupt_registers() || !verify_heap_under_timer()) {
        return false;
    }

    uint64_t time_before_stop = ktime_get_ns();
    kernel_timer_stop();

    uint64_t stopped_ticks = kernel_timer_ticks();

    while (ktime_get_ns() - time_before_stop < 30000000UL) {
        kernel_cpu_relax();
    }

    if (kernel_timer_ticks() != stopped_ticks || !kernel_timer_start(200) ||
        !wait_for_ticks(stopped_ticks + 5) || ktime_get_ns() <= time_before_stop) {
        return false;
    }

    bool previous_state = kernel_irq_save();
    bool valid = !__atomic_load_n(&hook_context_invalid, __ATOMIC_RELAXED) &&
                 __atomic_load_n(&hook_calls, __ATOMIC_RELAXED) == kernel_timer_ticks() &&
                 kernel_interrupt_depth() == 0;
    kernel_irq_restore(previous_state);

    return valid;
}

#endif

/* Initialize in dependency order; no handlers are exposed before heap/MMIO
 * readiness. Timer callback storage remains valid after this function returns. */
bool arm64_runtime_initialize(frame_allocator_t *allocator) {
    page_memory_initialize(allocator);
    slab_init();
#ifdef KERNEL_SELF_TESTS
    if (!verify_heap(allocator)) {
        return false;
    }

    platform_serial_write("ARM64 shared heap passed\n");
#ifdef ARM64_TEST_HEAP_DOUBLE_FREE
    void *duplicate = malloc(32);
    free(duplicate);
    free(duplicate);
    platform_serial_write("ARM64 double-free test unexpectedly returned\n");
#endif
    if (!kernel_time_initialize() || !kernel_interrupt_initialize() ||
        !kernel_timer_set_hook(timer_test_hook, NULL) || !kernel_timer_start(100)) {
        platform_serial_write("ARM64 controller/time setup failed\n");

        return false;
    }

    kernel_irq_restore(true);

    bool software_valid = verify_software_interrupts();
    bool timer_valid = software_valid && verify_timer();

    if (!software_valid || !timer_valid) {
        platform_serial_write(software_valid ? "ARM64 timer verification failed\n"
                                             : "ARM64 software IRQ verification failed\n");
        kernel_timer_stop();
        kernel_irq_save();

        return false;
    }

    runtime_write_value("COUNTER_FREQUENCY ", kernel_time_frequency());
    runtime_write_value("TIMER_TICKS ", kernel_timer_ticks());
    runtime_write_value("MONOTONIC_NS ", ktime_get_ns());
    platform_serial_write("ARM64 interrupts passed\nARM64 timer passed\n");

    return true;
#else
    /* Runtime initialization is required for boot; delivery/stress probes are
     * compiled solely into the self-test image. IRQs stay masked until queues
     * and the scheduler timer hook have been published by normal startup. */
    return kernel_time_initialize() && kernel_interrupt_initialize() && kernel_timer_start(200);
#endif
}
