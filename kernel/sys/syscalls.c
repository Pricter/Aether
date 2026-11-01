/* Linux AArch64 syscall boundary. User pointers are never passed to the VFS:
 * paths and I/O travel through checked copies and bounded kernel buffers. */
#include <kernel/userspace.h>
#include <kernel/cpu.h>
#include <kernel/strings.h>
#include <kernel/time.h>
#include <kernel/heap.h>
#include <kernel/linux_abi.h>
#include <kernel/page_memory.h>

#define USER_IO_CHUNK 4096UL
#define MAP_PRIVATE 2UL
#define MAP_FIXED 0x10UL
#define MAP_ANONYMOUS 0x20UL
#define MAP_FIXED_NOREPLACE 0x100000UL

/* Linux AArch64 uses the 128-byte asm-generic stat wire layout. */
struct linux_arm64_stat {
    uint64_t device, inode_number;
    uint32_t mode, link_count, owner, group;
    uint64_t special_device, reserved;
    int64_t size;
    int32_t block_size, padding;
    int64_t blocks;
    int64_t access_seconds;
    uint64_t access_nanoseconds;
    int64_t modify_seconds;
    uint64_t modify_nanoseconds;
    int64_t change_seconds;
    uint64_t change_nanoseconds;
    uint32_t unused[2];
};

struct linux_vector {
    uintptr_t base;
    size_t length;
};

struct linux_time {
    int64_t seconds, nanoseconds;
};

_Static_assert(sizeof(struct linux_arm64_stat) == 128, "AArch64 stat wire size");
_Static_assert(__builtin_offsetof(struct linux_arm64_stat, size) == 48, "stat size offset");

/* Scan one byte at a time to stop at NUL before inspecting the next page. */
static errno_t copy_user_path(process_t *process, uintptr_t address, char *path) {
    for (size_t index = 0; index < PATH_MAX; ++index) {
        if (address > ARM64_USER_LIMIT - index ||
            !arm64_user_copy(process, address + index, &path[index], 1, false, false)) {
            return -EFAULT;
        }

        if (!path[index]) {
            return EOK;
        }
    }

    return -ENAMETOOLONG;
}

/* Writes cannot expose privileged aliases; reads preflight before consuming
 * file/pipe input, then copy out again under the process mapping lock. */
static long transfer_io(process_t *process, int descriptor, uintptr_t address, size_t length,
    bool write_file, bool positioned, int64_t offset) {
    if (positioned && offset < 0) {
        return -EINVAL;
    }

    if (!arm64_user_range(process, address, length, write_file ? PROT_READ : PROT_WRITE)) {
        return -EFAULT;
    }

    unsigned char buffer[USER_IO_CHUNK];
    size_t transferred = 0;

    do {
        size_t chunk = length - transferred;

        if (chunk > sizeof(buffer)) {
            chunk = sizeof(buffer);
        }

        if (write_file && chunk &&
            !arm64_user_copy(process, address + transferred, buffer, chunk, false, false)) {
            return transferred ? (long)transferred : -EFAULT;
        }

        size_t completed = 0;
        errno_t status;

        if (positioned) {
            if ((uint64_t)offset > INT64_MAX - transferred) {
                return transferred ? (long)transferred : -EOVERFLOW;
            }

            status = write_file ? vfs_write_at(process, descriptor, buffer, chunk,
                                      offset + transferred, &completed)
                                : vfs_read_at(process, descriptor, buffer, chunk,
                                      offset + transferred, &completed);
        } else {
            status = write_file ? vfs_write(process, descriptor, buffer, chunk, &completed)
                                : vfs_read(process, descriptor, buffer, chunk, &completed);
        }

        if (status < 0) {
            return transferred ? (long)transferred : status;
        }

        if (!write_file && completed &&
            !arm64_user_copy(process, address + transferred, buffer, completed, true, false)) {
            return transferred ? (long)transferred : -EFAULT;
        }

        transferred += completed;

        /* A pipe read returns available data instead of waiting to fill the
         * entire user request. Regular file reads may continue at page chunks. */
        if (completed < chunk || (!write_file && chunk == sizeof(buffer))) {
            break;
        }
    } while (transferred < length);

    return (long)transferred;
}

/* Keep vector metadata in kernel memory across each individual transfer. */
static long transfer_vectors(
    process_t *process, int descriptor, uintptr_t address, long vector_count, bool write_file) {
    if (vector_count < 0 || vector_count > LINUX_IOV_MAX) {
        return -EINVAL;
    }

    if (!arm64_user_range(
            process, address, vector_count * sizeof(struct linux_vector), PROT_READ)) {
        return -EFAULT;
    }

    long total_bytes = 0;

    for (long vector_index = 0; vector_index < vector_count; ++vector_index) {
        struct linux_vector vector;

        if (!arm64_user_copy(process, address + vector_index * sizeof(vector), &vector,
                sizeof(vector), false, false)) {
            return total_bytes ? total_bytes : -EFAULT;
        }

        if (vector.length > INT64_MAX - (uint64_t)total_bytes) {
            return -EINVAL;
        }

        long result =
            transfer_io(process, descriptor, vector.base, vector.length, write_file, false, 0);
        if (result < 0) {
            return total_bytes ? total_bytes : result;
        }

        total_bytes += result;

        if ((size_t)result < vector.length) {
            break;
        }
    }

    return total_bytes;
}

/* Explicit conversion leaves timestamps/reserved fields initialized to zero. */
static long copy_stat_result(process_t *process, uintptr_t address, const vfs_stat_t *metadata) {
    struct linux_arm64_stat result = {0};
    result.inode_number = metadata->ino;
    result.mode = metadata->mode;
    result.link_count = metadata->nlink;
    result.owner = metadata->uid;
    result.group = metadata->gid;
    result.special_device = metadata->rdev;
    result.size = metadata->size;
    result.block_size = metadata->blksize;
    result.blocks = metadata->blocks;

    return arm64_user_copy(process, address, &result, sizeof(result), true, false) ? 0 : -EFAULT;
}

/* linux_dirent64 has a 19-byte fixed prefix and 8-byte record alignment. */
enum {
    DIRENT_INODE_OFFSET = 0,
    DIRENT_POSITION_OFFSET = 8,
    DIRENT_LENGTH_OFFSET = 16,
    DIRENT_TYPE_OFFSET = 18,
    DIRENT_NAME_OFFSET = 19,
    DIRENT_ALIGNMENT = 8,
    DIRENT_MINIMUM_SIZE = 24,
    DIRENT_NAME_CAPACITY = 256,
    DIRENT_BUFFER_SIZE = 280,
    MODE_TYPE_SHIFT = 12,
};

/* Pack Linux dirents and retain the VFS enumeration position if one won't fit. */
static long read_directory(process_t *process, int descriptor, uintptr_t address, size_t length) {
    if (!arm64_user_range(process, address, length, PROT_WRITE)) {
        return -EFAULT;
    }

    size_t used = 0;

    while (length - used >= DIRENT_MINIMUM_SIZE) {
        char name[DIRENT_NAME_CAPACITY];
        uint64_t inode_number;
        uint32_t mode;
        int64_t saved_position, next_position;

        errno_t status = vfs_seek(process, descriptor, 0, SEEK_CUR, &saved_position);

        if (status < 0) {
            return used ? (long)used : status;
        }

        status = vfs_readdir(process, descriptor, name, sizeof(name), &inode_number, &mode);

        if (status == -ENOENT) {
            break;
        }

        if (status < 0) {
            return used ? (long)used : status;
        }

        size_t name_length = strlen(name) + 1;
        uint16_t record_length =
            (DIRENT_NAME_OFFSET + name_length + DIRENT_ALIGNMENT - 1) & ~(DIRENT_ALIGNMENT - 1UL);
        if (record_length > length - used) {
            vfs_seek(process, descriptor, saved_position, SEEK_SET, &next_position);

            return used ? (long)used : -EINVAL;
        }

        vfs_seek(process, descriptor, 0, SEEK_CUR, &next_position);

        unsigned char record[DIRENT_BUFFER_SIZE] = {0};
        memcpy(record + DIRENT_INODE_OFFSET, &inode_number, sizeof(inode_number));
        memcpy(record + DIRENT_POSITION_OFFSET, &next_position, sizeof(next_position));
        memcpy(record + DIRENT_LENGTH_OFFSET, &record_length, sizeof(record_length));
        record[DIRENT_TYPE_OFFSET] = (mode & S_IFMT) >> MODE_TYPE_SHIFT;
        memcpy(record + DIRENT_NAME_OFFSET, name, name_length);

        if (!arm64_user_copy(process, address + used, record, record_length, true, false)) {
            return used ? (long)used : -EFAULT;
        }

        used += record_length;
    }

    return used ? (long)used : length < DIRENT_MINIMUM_SIZE ? -EINVAL : 0;
}

/* Linux distinguishes exact fixed mappings from advisory placement hints. */
static long map_anonymous(process_t *process, const unsigned long *arguments) {
    uintptr_t address = arguments[0];
    size_t length = arguments[1];
    unsigned int protection = arguments[2];
    unsigned long flags = arguments[3];

    if (!length || length > ARM64_USER_LIMIT - (KERNEL_PAGE_SIZE - 1)) {
        return -EINVAL;
    }

    if (!(flags & MAP_ANONYMOUS) || !(flags & MAP_PRIVATE)) {
        return -ENOSYS;
    }

    if (flags & ~(MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_FIXED_NOREPLACE | LINUX_MAP_STACK |
                    LINUX_MAP_UNINITIALIZED)) {
        return -EINVAL;
    }

    if (protection & ~(PROT_READ | PROT_WRITE | PROT_EXEC) ||
        (protection & (PROT_WRITE | PROT_EXEC)) == (PROT_WRITE | PROT_EXEC)) {
        return -EINVAL;
    }

    length = (length + (KERNEL_PAGE_SIZE - 1)) & ~(KERNEL_PAGE_SIZE - 1);

    if ((flags & (MAP_FIXED | MAP_FIXED_NOREPLACE)) &&
        (address < ARM64_USER_MIN || address >= ARM64_USER_LIMIT ||
            length > ARM64_USER_LIMIT - address || (address & (KERNEL_PAGE_SIZE - 1)))) {
        return -EINVAL;
    }

    if ((flags & MAP_FIXED) && !(flags & MAP_FIXED_NOREPLACE)) {
        arm64_user_unmap(process, address, length);
    }

    uintptr_t result = process_mmap(process, address & ~(KERNEL_PAGE_SIZE - 1), length, protection);

    if (!result && !(flags & (MAP_FIXED | MAP_FIXED_NOREPLACE))) {
        result = process_mmap(process, 0, length, protection);
    }

    if (!result) {
        return flags & MAP_FIXED_NOREPLACE ? -EEXIST : -ENOMEM;
    }

    return result;
}

/* Brk failure returns the current break, rather than a negative errno. */
static long change_break(process_t *process, uintptr_t requested) {
    uintptr_t previous = process->brk;

    if (!requested || requested < process->minimum_break || requested >= ARM64_USER_LIMIT) {
        return previous;
    }

    uintptr_t previous_end = (previous + (KERNEL_PAGE_SIZE - 1)) & ~(KERNEL_PAGE_SIZE - 1);
    uintptr_t requested_end = (requested + (KERNEL_PAGE_SIZE - 1)) & ~(KERNEL_PAGE_SIZE - 1);

    if (requested_end > previous_end && !arm64_user_map(process, previous_end,
                                            requested_end - previous_end, PROT_READ | PROT_WRITE)) {
        return previous;
    }

    if (requested_end < previous_end) {
        arm64_user_unmap(process, requested_end, previous_end - requested_end);
    }

    process->brk = requested;

    return requested;
}

/* Only known ioctl layouts cross the boundary. The device sees a kernel buffer. */
static long terminal_control(
    process_t *process, int descriptor, unsigned long request, uintptr_t address) {
    unsigned char buffer[LINUX_TERMIOS_SIZE] = {0};
    bool output = request == LINUX_TCGETS || request == LINUX_TIOCGWINSZ;
    size_t length = request == LINUX_TIOCGWINSZ ? LINUX_WINSIZE_SIZE : LINUX_TERMIOS_SIZE;

    if (request != LINUX_TCGETS && request != LINUX_TIOCGWINSZ && request != LINUX_TCSETSW &&
        request != LINUX_TCSETSF) {
        return -ENOTTY;
    }

    if (!arm64_user_range(process, address, length, output ? PROT_WRITE : PROT_READ)) {
        return -EFAULT;
    }

    if (!output && !arm64_user_copy(process, address, buffer, length, false, false)) {
        return -EFAULT;
    }

    errno_t status = vfs_ioctl(process, descriptor, request, (uintptr_t)buffer);

    if (status < 0) {
        return status;
    }

    return output && !arm64_user_copy(process, address, buffer, length, true, false) ? -EFAULT : 0;
}

/* Decode x0..x5 and x8 directly; register field names remain architectural. */
long arm64_syscall_dispatch(arm64_exception_frame_t *frame) {
    unsigned long *arguments = frame->general_registers;
    process_t *process = this_cpu()->current_thread->parent;
    int descriptor = (int)arguments[0];
    char path[PATH_MAX];
    errno_t status;

    switch (arguments[8]) {
    case LINUX_SYS_GETCWD: /* getcwd returns bytes including NUL on Linux. */
        status = vfs_getcwd(process, path, sizeof(path));

        if (status < 0) {
            return status;
        }

        if (strlen(path) + 1 > arguments[1]) {
            return -ERANGE;
        }

        return arm64_user_copy(process, arguments[0], path, strlen(path) + 1, true, false)
                   ? (long)strlen(path) + 1
                   : -EFAULT;
    case LINUX_SYS_DUP: {
        int result;
        status = vfs_dup(process, descriptor, &result);

        return status < 0 ? status : result;
    }

    case LINUX_SYS_DUP3:
        status = vfs_dup3(process, descriptor, (int)arguments[1], arguments[2]);

        return status < 0 ? status : (int)arguments[1];
    case LINUX_SYS_FCNTL: {
        int result;
        status = vfs_fcntl(process, descriptor, arguments[1], arguments[2], &result);

        return status < 0 ? status : result;
    }

    case LINUX_SYS_IOCTL:
        return terminal_control(process, descriptor, arguments[1], arguments[2]);
    case LINUX_SYS_MKDIRAT:
    case LINUX_SYS_UNLINKAT:
    case LINUX_SYS_FACCESSAT:
    case LINUX_SYS_OPENAT: { /* *at: support absolute paths and AT_FDCWD. */
        status = copy_user_path(process, arguments[1], path);

        if (status < 0) {
            return status;
        }

        if (arguments[8] == LINUX_SYS_OPENAT) {
            int result;
            status = vfs_openat(process, descriptor, path, arguments[2], arguments[3], &result);

            return status < 0 ? status : result;
        }

        if (path[0] != '/' && descriptor != AT_FDCWD) {
            return -ENOSYS;
        }

        if (arguments[8] == LINUX_SYS_MKDIRAT) {
            return vfs_mkdir(process, path, arguments[2]);
        }

        if (arguments[8] == LINUX_SYS_FACCESSAT) {
            return vfs_access(process, path, arguments[2]);
        }

        if (arguments[2] & ~LINUX_AT_REMOVEDIR) {
            return -EINVAL;
        }

        return arguments[2] & LINUX_AT_REMOVEDIR ? vfs_rmdir(process, path)
                                                 : vfs_unlink(process, path);
    }

    case LINUX_SYS_RENAMEAT: { /* renameat */
        char destination[PATH_MAX];
        status = copy_user_path(process, arguments[1], path);

        if (status < 0) {
            return status;
        }

        status = copy_user_path(process, arguments[3], destination);

        if (status < 0) {
            return status;
        }

        if ((path[0] != '/' && descriptor != AT_FDCWD) ||
            (destination[0] != '/' && (int)arguments[2] != AT_FDCWD)) {
            return -ENOSYS;
        }

        return vfs_rename(process, path, destination);
    }

    case LINUX_SYS_TRUNCATE:
    case LINUX_SYS_CHDIR:
        status = copy_user_path(process, arguments[0], path);

        if (status < 0) {
            return status;
        }

        if (arguments[8] == LINUX_SYS_CHDIR) {
            return vfs_chdir(process, path);
        }

        return (long)arguments[1] < 0 ? -EINVAL : vfs_truncate(process, path, arguments[1]);
    case LINUX_SYS_FTRUNCATE:
        return (long)arguments[1] < 0 ? -EINVAL : vfs_ftruncate(process, descriptor, arguments[1]);
    case LINUX_SYS_CLOSE:
        return vfs_close(process, descriptor);
    case LINUX_SYS_PIPE2: { /* Roll back both descriptors if result publication fails. */
        if (!arm64_user_range(process, arguments[0], 2 * sizeof(int), PROT_WRITE)) {
            return -EFAULT;
        }

        int descriptors[2];
        status = vfs_pipe2(process, descriptors, arguments[1]);

        if (status < 0) {
            return status;
        }

        if (arm64_user_copy(process, arguments[0], descriptors, sizeof(descriptors), true, false)) {
            return 0;
        }

        vfs_close(process, descriptors[0]);
        vfs_close(process, descriptors[1]);

        return -EFAULT;
    }

    case LINUX_SYS_GETDENTS64:
        return read_directory(process, descriptor, arguments[1], arguments[2]);
    case LINUX_SYS_LSEEK: {
        int64_t position;
        status = vfs_seek(process, descriptor, arguments[1], arguments[2], &position);

        return status < 0 ? status : position;
    }

    case LINUX_SYS_READ:
    case LINUX_SYS_WRITE:
        return transfer_io(process, descriptor, arguments[1], arguments[2],
            arguments[8] == LINUX_SYS_WRITE, false, 0);
    case LINUX_SYS_READV:
    case LINUX_SYS_WRITEV:
        return transfer_vectors(
            process, descriptor, arguments[1], arguments[2], arguments[8] == LINUX_SYS_WRITEV);
    case LINUX_SYS_PREAD64:
    case LINUX_SYS_PWRITE64:
        return transfer_io(process, descriptor, arguments[1], arguments[2],
            arguments[8] == LINUX_SYS_PWRITE64, true, arguments[3]);
    case LINUX_SYS_NEWFSTATAT:
    case LINUX_SYS_FSTAT: {
        vfs_stat_t metadata;
        uintptr_t output_address = arguments[8] == LINUX_SYS_FSTAT ? arguments[1] : arguments[2];

        if (!arm64_user_range(
                process, output_address, sizeof(struct linux_arm64_stat), PROT_WRITE)) {
            return -EFAULT;
        }

        if (arguments[8] == LINUX_SYS_FSTAT) {
            status = vfs_fstat(process, descriptor, &metadata);
        } else {
            if (arguments[3] & ~(AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW)) {
                return -EINVAL;
            }

            status = copy_user_path(process, arguments[1], path);

            if (status < 0) {
                return status;
            }

            status = vfs_statat(process, descriptor, path, arguments[3], &metadata);
        }

        return status < 0 ? status : copy_stat_result(process, output_address, &metadata);
    }

    case LINUX_SYS_SYNC:
        return 0; /* sync: current filesystems are memory-backed. */
    case LINUX_SYS_FSYNC:
    case LINUX_SYS_FDATASYNC: {
        int result;

        return vfs_fcntl(process, descriptor, F_GETFL, 0, &result);
    }

    case LINUX_SYS_EXIT:
    case LINUX_SYS_EXIT_GROUP:
        arm64_process_exit(arguments[0], false);
    case LINUX_SYS_SET_TID_ADDRESS:
        return this_cpu()->current_thread->tid; /* no clone/clear-child-TID yet */
    case LINUX_SYS_SET_ROBUST_LIST:
        return -ENOSYS; /* robust-list ownership requires user threads */
    case LINUX_SYS_NANOSLEEP:
    case LINUX_SYS_CLOCK_NANOSLEEP: { /* nanosleep / clock_nanosleep */
        unsigned int clock_id =
            arguments[8] == LINUX_SYS_NANOSLEEP ? LINUX_CLOCK_MONOTONIC : arguments[0];
        unsigned long flags = arguments[8] == LINUX_SYS_NANOSLEEP ? 0 : arguments[1];
        uintptr_t request = arguments[8] == LINUX_SYS_NANOSLEEP ? arguments[0] : arguments[2];
        struct linux_time duration;

        if (clock_id > LINUX_CLOCK_MONOTONIC || flags & ~LINUX_TIMER_ABSTIME) {
            return -EINVAL;
        }

        if (!arm64_user_copy(process, request, &duration, sizeof(duration), false, false)) {
            return -EFAULT;
        }

        if (duration.seconds < 0 || duration.nanoseconds < 0 ||
            (uint64_t)duration.nanoseconds >= NANOSECONDS_PER_SECOND ||
            (uint64_t)duration.seconds >
                (UINT64_MAX - duration.nanoseconds) / NANOSECONDS_PER_SECOND) {
            return -EINVAL;
        }

        uint64_t deadline = duration.seconds * NANOSECONDS_PER_SECOND + duration.nanoseconds;

        if (!(flags & LINUX_TIMER_ABSTIME)) {
            uint64_t now = ktime_get_ns();

            if (deadline > UINT64_MAX - now) {
                return -EINVAL;
            }

            deadline += now;
        }
        while (ktime_get_ns() < deadline) {
            schedule();
        }

        return 0;
    }

    case LINUX_SYS_CLOCK_GETTIME: { /* clock_gettime: monotonic epoch for realtime until an RTC exists. */
        if (arguments[0] != LINUX_CLOCK_REALTIME && arguments[0] != LINUX_CLOCK_MONOTONIC &&
            arguments[0] != LINUX_CLOCK_MONOTONIC_RAW &&
            arguments[0] != LINUX_CLOCK_MONOTONIC_COARSE) {
            return -EINVAL;
        }

        uint64_t now = ktime_get_ns();
        struct linux_time result = {now / NANOSECONDS_PER_SECOND, now % NANOSECONDS_PER_SECOND};

        return arm64_user_copy(process, arguments[1], &result, sizeof(result), true, false)
                   ? 0
                   : -EFAULT;
    }

    case LINUX_SYS_SCHED_YIELD:
        schedule();

        return 0;
    case LINUX_SYS_RT_SIGACTION:
    case LINUX_SYS_RT_SIGPROCMASK:
        return -ENOSYS;     /* no pretend-success signal implementation */
    case LINUX_SYS_UNAME: { /* uname: six Linux fields, 65 bytes each. */
        char identity[LINUX_UNAME_FIELD_COUNT][LINUX_UNAME_FIELD_SIZE] = {
            "Aether", "aether", "0.1", "ARM userspace", "aarch64", "(none)"};
        return arm64_user_copy(process, arguments[0], identity, sizeof(identity), true, false)
                   ? 0
                   : -EFAULT;
    }

    case LINUX_SYS_UMASK: {
        bool interrupts_enabled = spinlock_acquire(&process->lock);
        unsigned int previous = process->umask;
        process->umask = arguments[0] & 0777;
        spinlock_release(&process->lock, interrupts_enabled);

        return previous;
    }

    case LINUX_SYS_GETPID:
        return process->pid;
    case LINUX_SYS_GETPPID:
        return arm64_process_parent_pid(process);
    case LINUX_SYS_GETUID:
    case LINUX_SYS_GETEUID:
    case LINUX_SYS_GETGID:
    case LINUX_SYS_GETEGID:
        return 0;
    case LINUX_SYS_GETTID:
        return this_cpu()->current_thread->tid;
    case LINUX_SYS_BRK:
        return change_break(process, arguments[0]);
    case LINUX_SYS_MUNMAP:
        if ((arguments[0] & (KERNEL_PAGE_SIZE - 1)) || !arguments[1] ||
            arguments[1] > ARM64_USER_LIMIT - (KERNEL_PAGE_SIZE - 1)) {
            return -EINVAL;
        }

        return arm64_user_unmap(process, arguments[0],
                   (arguments[1] + (KERNEL_PAGE_SIZE - 1)) & ~(KERNEL_PAGE_SIZE - 1))
                   ? 0
                   : -EINVAL;
    case LINUX_SYS_MMAP:
        return map_anonymous(process, arguments);
    case LINUX_SYS_MPROTECT:
        return process_mprotect(process, arguments[0], arguments[1], arguments[2]);
    case LINUX_SYS_WAIT4: {
        if (arguments[1] && !arm64_user_range(process, arguments[1], sizeof(int), PROT_WRITE)) {
            return -EFAULT;
        }

        if (arguments[3]) {
            return -ENOSYS; /* rusage is not tracked yet */
        }

        int wait_status;
        pid_t child = arm64_process_wait(process, (pid_t)arguments[0], &wait_status, arguments[2]);

        if (child <= 0) {
            return child;
        }

        if (arguments[1] && !arm64_user_copy(process, arguments[1], &wait_status,
                                sizeof(wait_status), true, false)) {
            return -EFAULT;
        }

        return child;
    }

    case LINUX_SYS_PRLIMIT64: { /* prlimit64: read-only reporting, reject unsupported mutations. */
        if (arguments[0] && arguments[0] != (unsigned long)process->pid) {
            return -ESRCH;
        }

        if (arguments[2]) {
            return -EPERM;
        }

        uint64_t limits[2];

        if (arguments[1] == LINUX_RLIMIT_STACK) {
            limits[0] = limits[1] = USTACK_SIZE;
        } else if (arguments[1] == LINUX_RLIMIT_NOFILE) {
            limits[0] = limits[1] = FD_TABLE_SIZE;
        } else {
            return -EINVAL;
        }

        return !arguments[3] ||
                       arm64_user_copy(process, arguments[3], limits, sizeof(limits), true, false)
                   ? 0
                   : -EFAULT;
    }

    default:
        return -ENOSYS;
    }
}
