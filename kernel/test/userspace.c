#include <kernel/userspace.h>
#include <kernel/cpu.h>
#include <kernel/heap.h>
#include <kernel/strings.h>
#include <kernel/panic.h>
#include <kernel/linux_abi.h>
#include <platform/serial.h>

extern const unsigned char arm64_init_image[], arm64_init_image_end[];

/* Exercise the device contract directly: the syscall bounce buffer must not
 * hide an oversized write or a setter that modifies its input. */
static void validate_terminal_buffers(void) {
    enum { GUARD_BYTES = 32, SENTINEL = 0xa5 };

    int descriptor;
    KERNEL_ASSERT(vfs_open(&kernel_process, "/dev/tty", O_RDWR, 0, &descriptor) == EOK);

    unsigned char storage[LINUX_TERMIOS_SIZE + GUARD_BYTES];
    memset(storage, SENTINEL, sizeof(storage));
    KERNEL_ASSERT(vfs_ioctl(&kernel_process, descriptor, LINUX_TCGETS, (uintptr_t)storage) == EOK);

    for (size_t byte_index = 0; byte_index < sizeof(storage); ++byte_index) {
        KERNEL_ASSERT(storage[byte_index] == (byte_index < LINUX_TERMIOS_SIZE ? 0 : SENTINEL));
    }

    memset(storage, SENTINEL, sizeof(storage));
    KERNEL_ASSERT(vfs_ioctl(&kernel_process, descriptor, LINUX_TCSETSW, (uintptr_t)storage) == EOK);
    KERNEL_ASSERT(vfs_ioctl(&kernel_process, descriptor, LINUX_TCSETSF, (uintptr_t)storage) == EOK);

    for (size_t byte_index = 0; byte_index < sizeof(storage); ++byte_index) {
        KERNEL_ASSERT(storage[byte_index] == SENTINEL);
    }

    int descriptor_flags;
    KERNEL_ASSERT(
        vfs_fcntl(&kernel_process, descriptor, F_SETFD, FD_CLOEXEC, &descriptor_flags) == EOK);
    KERNEL_ASSERT(vfs_close(&kernel_process, descriptor) == EOK);
    KERNEL_ASSERT(vfs_dup2(&kernel_process, descriptor, descriptor) == -EBADF);

    int endpoints[2];
    KERNEL_ASSERT(vfs_pipe(&kernel_process, endpoints) == EOK);

    for (size_t endpoint_index = 0; endpoint_index < 2; ++endpoint_index) {
        KERNEL_ASSERT(vfs_fcntl(&kernel_process, endpoints[endpoint_index], F_GETFD, 0,
                          &descriptor_flags) == EOK &&
                      descriptor_flags == 0);
        KERNEL_ASSERT(vfs_close(&kernel_process, endpoints[endpoint_index]) == EOK);
    }
}

/* Reject malformed metadata before any thread becomes visible. Mutations are
 * independent so one rejection cannot accidentally mask another broken check. */
static void validate_elf_rejections(size_t image_size) {
    thread_t *thread;
    KERNEL_ASSERT(!arm64_elf_load(arm64_init_image, 63, "parent", &kernel_process, &thread));

    unsigned char *copy = malloc(image_size);
    KERNEL_ASSERT(copy);
    memcpy(copy, arm64_init_image, image_size);
    copy[18] = 62; /* unsupported machine in an otherwise valid ELF64 */
    KERNEL_ASSERT(!arm64_elf_load(copy, image_size, "parent", &kernel_process, &thread));
    memcpy(copy, arm64_init_image, image_size);

    uint64_t invalid_offset = UINT64_MAX;
    memcpy(copy + 32, &invalid_offset, 8);
    KERNEL_ASSERT(!arm64_elf_load(copy, image_size, "parent", &kernel_process, &thread));
    memcpy(copy, arm64_init_image, image_size);

    uint64_t invalid_entry = 0x40080000;
    memcpy(copy + 24, &invalid_entry, 8);
    KERNEL_ASSERT(!arm64_elf_load(copy, image_size, "parent", &kernel_process, &thread));
    memcpy(copy, arm64_init_image, image_size);

    uint64_t program_offset;
    uint16_t program_count;
    memcpy(&program_offset, copy + 32, 8);
    memcpy(&program_count, copy + 56, 2);

    for (unsigned int index = 0; index < program_count; ++index) {
        uint32_t type;
        memcpy(&type, copy + program_offset + index * 56, 4);

        if (type == 1) {
            uint32_t invalid_flags = 7;
            memcpy(copy + program_offset + index * 56 + 4, &invalid_flags, 4);
            break;
        }
    }

    KERNEL_ASSERT(!arm64_elf_load(copy, image_size, "parent", &kernel_process, &thread));
    memcpy(copy, arm64_init_image, image_size);
    copy[5] = 2; /* big-endian metadata is not this ABI */
    KERNEL_ASSERT(!arm64_elf_load(copy, image_size, "parent", &kernel_process, &thread));
    memcpy(copy, arm64_init_image, image_size);

    for (unsigned int index = 0; index < program_count; ++index) {
        uint32_t type;
        memcpy(&type, copy + program_offset + index * 56, 4);

        if (type == 7) {
            uint64_t invalid_tls_size = UINT64_MAX;
            memcpy(copy + program_offset + index * 56 + 40, &invalid_tls_size, 8);
            break;
        }
    }

    KERNEL_ASSERT(!arm64_elf_load(copy, image_size, "parent", &kernel_process, &thread));
    free(copy);
    platform_serial_write("ARM64 ELF validation passed\n");
}

/* Launch one real parent/child pair per CPU. Child status is consumed by musl's
 * waitpid; the kernel coordinator then waits for and owns each init process. */
static void run_init_wave(size_t image_size) {
    pid_t parent_identifiers[8];
    thread_t *parent_threads[8], *child_threads[8];

    for (unsigned int cpu_index = 0; cpu_index < coreCount; ++cpu_index) {
        thread_t *parent_thread, *child_thread;

        process_t *parent =
            arm64_elf_load(arm64_init_image, image_size, "parent", &kernel_process, &parent_thread);
        KERNEL_ASSERT(parent && parent_thread);

        process_t *child =
            arm64_elf_load(arm64_init_image, image_size, "child", parent, &child_thread);
        KERNEL_ASSERT(child && child_thread);
        parent_identifiers[cpu_index] = parent->pid;
        arm64_process_attach(parent, &kernel_process);
        arm64_process_attach(child, parent);

        int pending_status;
        KERNEL_ASSERT(arm64_process_wait(parent, child->pid, &pending_status, 1) == 0);
        KERNEL_ASSERT(arm64_process_wait(parent, INT32_MAX, &pending_status, 1) == -ECHILD);
        parent_threads[cpu_index] = parent_thread;
        child_threads[cpu_index] = child_thread;
    }

    for (unsigned int cpu_index = 0; cpu_index < coreCount; ++cpu_index) {
        thread_ready_on(parent_threads[cpu_index], cpu_index);
        thread_ready_on(child_threads[cpu_index], cpu_index);
    }

    for (unsigned int cpu_index = 0; cpu_index < coreCount; ++cpu_index) {
        int status;
        KERNEL_ASSERT(arm64_process_wait(&kernel_process, parent_identifiers[cpu_index], &status,
                          0) == parent_identifiers[cpu_index]);
        KERNEL_ASSERT(status == 0);
    }

    /* A successful parent consumed its child. Any orphan indicates broken
     * status/lifetime handling even if the parent happened to return zero. */
    int ignored_status;
    KERNEL_ASSERT(arm64_process_wait(&kernel_process, -1, &ignored_status, 1) == -ECHILD);
}

/* Exiting parents transfer outstanding children to the kernel. A fatal EL0
 * fault completes with a signal-style wait status, never a kernel panic. */
static void validate_adoption_and_fault(size_t image_size) {
    thread_t *parent_thread, *child_thread;

    process_t *parent =
        arm64_elf_load(arm64_init_image, image_size, "orphan", &kernel_process, &parent_thread);
    KERNEL_ASSERT(parent);

    process_t *child = arm64_elf_load(arm64_init_image, image_size, "child", parent, &child_thread);
    KERNEL_ASSERT(child);

    pid_t parent_identifier = parent->pid, child_identifier = child->pid;
    arm64_process_attach(parent, &kernel_process);
    arm64_process_attach(child, parent);
    thread_ready_on(parent_thread, 0);
    thread_ready_on(child_thread, coreCount > 1 ? 1 : 0);

    int status;
    KERNEL_ASSERT(
        arm64_process_wait(&kernel_process, parent_identifier, &status, 0) == parent_identifier &&
        status == (19 << 8));
    KERNEL_ASSERT(
        arm64_process_wait(&kernel_process, child_identifier, &status, 0) == child_identifier &&
        status == (37 << 8));
    process_t *faulted =
        arm64_elf_load(arm64_init_image, image_size, "fault", &kernel_process, &parent_thread);
    KERNEL_ASSERT(faulted);

    pid_t fault_identifier = faulted->pid;
    arm64_process_attach(faulted, &kernel_process);
    thread_ready_on(parent_thread, 0);
    KERNEL_ASSERT(
        arm64_process_wait(&kernel_process, fault_identifier, &status, 0) == fault_identifier &&
        status == 11);
    platform_serial_write("ARM64 orphan adoption and fault exit passed\n");
}

/* Run after the kernel/SMP/EL0 foundation checks and before declaring idle. */
void arm64_userspace_boot(frame_allocator_t *allocator) {
    KERNEL_ASSERT(arm64_filesystems_initialize());
    platform_serial_write("ARM64 shared filesystems passed\n");
    validate_terminal_buffers();

    size_t image_size = arm64_init_image_end - arm64_init_image;
    validate_elf_rejections(image_size);
    run_init_wave(image_size);

    unsigned long free_after_warmup = allocator->free_frames;
    run_init_wave(image_size);
    KERNEL_ASSERT(allocator->free_frames == free_after_warmup);
    validate_adoption_and_fault(image_size);
    KERNEL_ASSERT(allocator->free_frames == free_after_warmup);
    platform_serial_write(
        "ARM64 process wait passed\nARM64 init reclamation passed\nARM64 musl init passed\n");
}
