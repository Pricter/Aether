#include <kernel/runtime.h>
#include <kernel/userspace.h>
#include <kernel/smp.h>
#include <kernel/time.h>
#include <kernel/panic.h>
#include <platform/serial.h>

static const boot_memory_map_t *startup_memory_map;
extern const unsigned char arm64_init_image[], arm64_init_image_end[];

/* The boot coordinator brings up CPUs and starts exactly one normal init.
 * Returning reaps this coordinator, leaving the scheduler's idle/reaper tasks. */
static void startup_coordinator(void) {
    KERNEL_ASSERT(arm64_smp_start(startup_memory_map));
    KERNEL_ASSERT(arm64_filesystems_initialize());
    platform_serial_write("CPUs and filesystems ready\nStarting /sbin/init\n");

    thread_t *init_thread;
    process_t *init_process = arm64_elf_load(arm64_init_image,
        arm64_init_image_end - arm64_init_image, "boot", &kernel_process, &init_thread);
    KERNEL_ASSERT(init_process && init_thread);

    pid_t init_identifier = init_process->pid;
    arm64_process_attach(init_process, &kernel_process);
    thread_ready_on(init_thread, 0);

    int exit_status;
    KERNEL_ASSERT(
        arm64_process_wait(&kernel_process, init_identifier, &exit_status, 0) == init_identifier);
    KERNEL_ASSERT(exit_status == 0);
    platform_serial_write("Init exited. System idle.\n");
}

/* Publish queues, the timer hook and IPIs before enabling scheduler execution.
 * Unlike the test entry, this path never creates validation workers. */
__attribute__((noreturn)) void arm64_scheduler_start(
    frame_allocator_t *allocator, const boot_memory_map_t *memory_map) {
    (void)allocator;
    irq_save();
    KERNEL_ASSERT(arm64_cpu_configure(memory_map));
    startup_memory_map = memory_map;
    scheduler_init();

    thread_t *coordinator = thread_create(&kernel_process, startup_coordinator);
    KERNEL_ASSERT(coordinator);
    thread_ready_on(coordinator, 0);
    scheduler_start_reaper();
    KERNEL_ASSERT(arm64_smp_initialize_ipis());
    KERNEL_ASSERT(kernel_timer_set_hook(scheduler_timer_tick, NULL));
    scheduler_enter();
}
