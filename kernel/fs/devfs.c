/*
 * devfs.c - device filesystem.
 *
 * Provides a minimal /dev directory with three character devices:
 *
 *   /dev/null  (major=1, minor=3) - reads return 0 bytes (EOF); writes discard.
 *   /dev/zero  (major=1, minor=5) - reads return zeroed bytes; writes discard.
 *   /dev/tty   (major=5, minor=0) - writes go to the PL011 console;
 *                                   reads consume buffered UART input;
 *                                   ioctl handles TCGETS and TIOCGWINSZ.
 *
 * The devfs root directory is a synthetic read-only directory backed by a
 * static table of (name, inode) pairs.  No files can be created or deleted
 * in the root.  Subdirectories are not supported by devfs.
 *
 * Device inodes are heap-allocated at devfs_init() and held alive by a
 * reference from the devfs directory table.  They are never freed in normal
 * operation (no umount support).
 */

#include <kernel/vfs.h>
#include <kernel/heap.h>
#include <kernel/panic.h>
#include <platform/serial.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <kernel/strings.h>
#include <kernel/syscall_abi.h>
#include <kernel/page_memory.h>

/* Device number encoding (ABI: (major<<8)|minor for small numbers) */

/* Pack the small major/minor device numbers used by the built-in devices. */
static inline uint64_t encode_device_number(uint32_t major_number, uint32_t minor_number) {
    return ((uint64_t)(major_number & 0xfff) << 8) | (minor_number & 0xff);
}

/* Devfs directory table */

#define DEVFS_MAX_ENTRIES 16

typedef struct {
    const char *name;
    inode_t *inode;
} devfs_entry_t;

static devfs_entry_t device_entries[DEVFS_MAX_ENTRIES];
static int device_entry_count = 0;

/* Forward declarations */

static const inode_ops_t devfs_dir_inode_ops;

static const file_ops_t devfs_null_file_ops;
static const file_ops_t devfs_zero_file_ops;
static const file_ops_t devfs_tty_file_ops;

/* Devfs root directory inode operations */

/* Look up a boot-registered device and return one owned inode reference. */
static errno_t devfs_dir_lookup(inode_t *directory, const char *name, inode_t **inode_out) {
    (void)directory;

    for (int entry_index = 0; entry_index < device_entry_count; entry_index++) {
        if (strcmp(device_entries[entry_index].name, name) == 0) {
            inode_ref(device_entries[entry_index].inode);
            *inode_out = device_entries[entry_index].inode;

            return EOK;
        }
    }

    return -ENOENT;
}

/* Enumerate the synthetic dot entry followed by the permanent device table. */
static errno_t devfs_dir_readdir(inode_t *directory, uint64_t entry_index, char *name_out,
    size_t name_capacity, inode_t **inode_out) {
    /* entry_index 0 is always "." (self) */
    if (entry_index == 0) {
        if (name_capacity < 2) {
            return -EINVAL;
        }

        name_out[0] = '.';
        name_out[1] = '\0';
        inode_ref(directory);
        *inode_out = directory;

        return EOK;
    }

    entry_index--; /* actual entry index */

    if (entry_index >= (uint64_t)device_entry_count) {
        return -ENOENT;
    }

    const char *name = device_entries[entry_index].name;
    size_t name_length = strlen(name);

    if (name_length >= name_capacity) {
        name_length = name_capacity - 1;
    }

    __builtin_memcpy(name_out, name, name_length);
    name_out[name_length] = '\0';
    inode_ref(device_entries[entry_index].inode);
    *inode_out = device_entries[entry_index].inode;

    return EOK;
}

/* Create an open directory description; enumeration uses inode operations. */
static errno_t devfs_dir_open(inode_t *inode, int flags, file_t **file_out) {
    (void)flags;

    file_t *file = file_alloc(inode, flags, NULL);

    if (!file) {
        return -ENOSPC;
    }
    *file_out = file;

    return EOK;
}

/* Report metadata for the synthetic read-only device directory. */
static errno_t devfs_dir_stat(inode_t *inode, vfs_stat_t *stat_out) {
    stat_out->ino = inode->ino;
    stat_out->mode = inode->mode;
    stat_out->nlink = 2;
    stat_out->uid = 0;
    stat_out->gid = 0;
    stat_out->size = 0;
    stat_out->blksize = KERNEL_PAGE_SIZE;
    stat_out->blocks = 0;
    stat_out->rdev = 0;

    return EOK;
}

static const inode_ops_t devfs_dir_inode_ops = {
    .lookup = devfs_dir_lookup,
    .create = NULL, /* /dev is read-only for directory operations */
    .mkdir = NULL,
    .unlink = NULL,
    .readdir = devfs_dir_readdir,
    .open = devfs_dir_open,
    .stat = devfs_dir_stat,
    .destroy = NULL,
};

/* Device inode operations (shared by all char devices) */

/* Create an open description bound to null-device operations. */
static errno_t devfs_dev_open_null(inode_t *inode, int flags, file_t **file_out) {
    file_t *file = file_alloc(inode, flags, &devfs_null_file_ops);

    if (!file) {
        return -ENOSPC;
    }
    *file_out = file;

    return EOK;
}

/* Create an open description bound to zero-device operations. */
static errno_t devfs_dev_open_zero(inode_t *inode, int flags, file_t **file_out) {
    file_t *file = file_alloc(inode, flags, &devfs_zero_file_ops);

    if (!file) {
        return -ENOSPC;
    }
    *file_out = file;

    return EOK;
}

/* Create an open description bound to the kernel terminal operations. */
static errno_t devfs_dev_open_tty(inode_t *inode, int flags, file_t **file_out) {
    file_t *file = file_alloc(inode, flags, &devfs_tty_file_ops);

    if (!file) {
        return -ENOSPC;
    }
    *file_out = file;

    return EOK;
}

/* Report character-device metadata, including its encoded device number. */
static errno_t devfs_dev_stat(inode_t *inode, vfs_stat_t *stat_out) {
    stat_out->ino = inode->ino;
    stat_out->mode = inode->mode;
    stat_out->nlink = 1;
    stat_out->uid = 0;
    stat_out->gid = 0;
    stat_out->size = 0;
    stat_out->blksize = KERNEL_PAGE_SIZE;
    stat_out->blocks = 0;
    stat_out->rdev = inode->rdev;

    return EOK;
}

/* Each device has its own inode_ops because open differs per device. */

static const inode_ops_t devfs_null_inode_ops = {
    .open = devfs_dev_open_null,
    .stat = devfs_dev_stat,
    .destroy = NULL,
};

static const inode_ops_t devfs_zero_inode_ops = {
    .open = devfs_dev_open_zero,
    .stat = devfs_dev_stat,
    .destroy = NULL,
};

static const inode_ops_t devfs_tty_inode_ops = {
    .open = devfs_dev_open_tty,
    .stat = devfs_dev_stat,
    .destroy = NULL,
};

/* /dev/null file operations */

/* Report immediate EOF without touching the caller's buffer. */
static errno_t null_read(file_t *file, void *buffer, size_t byte_count, size_t *bytes_transferred) {
    (void)file;
    (void)buffer;
    (void)byte_count;
    *bytes_transferred = 0; /* EOF */
    return EOK;
}

/* Accept and discard every byte supplied by the caller. */
static errno_t null_write(
    file_t *file, const void *buffer, size_t byte_count, size_t *bytes_transferred) {
    (void)file;
    (void)buffer;
    *bytes_transferred = byte_count; /* discard; pretend success */
    return EOK;
}

static const file_ops_t devfs_null_file_ops = {
    .read = null_read,
    .write = null_write,
    .seek = NULL,
    .ioctl = NULL,
    .close = NULL,
};

/* /dev/zero file operations */

/* Fill the requested buffer with zero bytes. */
static errno_t zero_read(file_t *file, void *buffer, size_t byte_count, size_t *bytes_transferred) {
    (void)file;
    __builtin_memset(buffer, 0, byte_count);
    *bytes_transferred = byte_count;

    return EOK;
}

/* Accept writes without retaining any data. */
static errno_t zero_write(
    file_t *file, const void *buffer, size_t byte_count, size_t *bytes_transferred) {
    (void)file;
    (void)buffer;
    *bytes_transferred = byte_count;

    return EOK;
}

static const file_ops_t devfs_zero_file_ops = {
    .read = zero_read,
    .write = zero_write,
    .seek = NULL,
    .ioctl = NULL,
    .close = NULL,
};

/* /dev/tty file operations */

/* Userspace termios layout. This first line discipline supports
 * ICANON/ECHO/ECHOE/ECHOK, ICRNL, erase/kill/EOF, and raw VMIN=1/VTIME=0. */
struct terminal_settings {
    uint32_t input_flags, output_flags, control_flags, local_flags;
    unsigned char line, characters[19];
};

_Static_assert(sizeof(struct terminal_settings) == ABI_TERMIOS_SIZE, "termios layout");
#define TERMINAL_ICRNL 0x100U
#define TERMINAL_ICANON 2U
#define TERMINAL_ECHO 8U
#define TERMINAL_ECHOE 16U
#define TERMINAL_ECHOK 32U
#define TERMINAL_ERASE 2U
#define TERMINAL_KILL 3U
#define TERMINAL_EOF 4U
#define TERMINAL_TIME 5U
#define TERMINAL_MINIMUM 6U
static spinlock_t terminal_state_lock = SPINLOCK_ZERO;
static struct terminal_settings terminal_settings = {
    .input_flags = TERMINAL_ICRNL,
    .output_flags = 5,       /* OPOST | ONLCR */
    .control_flags = 0x18b2, /* B115200 | CS8 | CREAD | CLOCAL */
    .local_flags = TERMINAL_ICANON | TERMINAL_ECHO | TERMINAL_ECHOE | TERMINAL_ECHOK,
    .characters = {[TERMINAL_ERASE] = 127,
        [TERMINAL_KILL] = 21,
        [TERMINAL_EOF] = 4,
        [TERMINAL_MINIMUM] = 1},
};

static unsigned char terminal_line[256];
static size_t terminal_used, terminal_offset;
static bool terminal_complete;

/* Caller holds terminal_state_lock; UART output never calls back into devfs. */
static void terminal_echo(const void *bytes, size_t length) {
    if ((terminal_settings.output_flags & 5U) == 5U) {
        platform_serial_write_bytes(bytes, length);
    } else {
        platform_serial_write_raw_bytes(bytes, length);
    }
}

static wait_queue_t terminal_readers;

/* The driver publishes buffered bytes before calling this IRQ-safe callback. */
static void terminal_input_notify(void) {
    wait_queue_wake_all(&terminal_readers);
}

static bool terminal_input_ready(void *context) {
    (void)context;

    return platform_serial_input_ready() || __atomic_load_n(&terminal_complete, __ATOMIC_ACQUIRE);
}

/* Assemble canonical lines under a short state lock. Never hold it over a
 * scheduler wait. All descriptors consume the same terminal input stream. */
static errno_t tty_read(file_t *file, void *buffer, size_t byte_count, size_t *bytes_transferred) {
    *bytes_transferred = 0;

    if (!byte_count) {
        return EOK;
    }

    for (;;) {
        bool interrupts = spinlock_acquire(&terminal_state_lock);
        bool canonical = terminal_settings.local_flags & TERMINAL_ICANON;

        if (!canonical) {
            size_t retained = terminal_used - terminal_offset;

            if (retained) {
                *bytes_transferred = retained < byte_count ? retained : byte_count;
                memcpy(buffer, terminal_line + terminal_offset, *bytes_transferred);
                terminal_offset += *bytes_transferred;

                if (terminal_offset == terminal_used) {
                    terminal_used = terminal_offset = 0;
                    __atomic_store_n(&terminal_complete, false, __ATOMIC_RELEASE);
                }
            } else {
                *bytes_transferred = platform_serial_read_bytes(buffer, byte_count);
                __atomic_store_n(&terminal_complete, false, __ATOMIC_RELEASE);
            }

            spinlock_release(&terminal_state_lock, interrupts);

            if (*bytes_transferred) {
                return EOK;
            }
        } else {
            unsigned char character;

            while (!terminal_complete && platform_serial_read_bytes(&character, 1)) {
                if (character == '\r' && (terminal_settings.input_flags & TERMINAL_ICRNL)) {
                    character = '\n';
                }

                if (character && (character == terminal_settings.characters[TERMINAL_ERASE] ||
                                     character == '\b')) {
                    if (terminal_used) {
                        --terminal_used;

                        if (terminal_settings.local_flags & TERMINAL_ECHO) {
                            if (terminal_settings.local_flags & TERMINAL_ECHOE) {
                                terminal_echo("\b \b", 3);
                            } else {
                                terminal_echo(&character, 1);
                            }
                        }
                    }

                    continue;
                }

                if (character && character == terminal_settings.characters[TERMINAL_KILL]) {
                    terminal_used = 0;

                    if (terminal_settings.local_flags & TERMINAL_ECHO) {
                        if (terminal_settings.local_flags & TERMINAL_ECHOK) {
                            terminal_echo("\n", 1);
                        } else {
                            terminal_echo(&character, 1);
                        }
                    }

                    continue;
                }

                if (character && character == terminal_settings.characters[TERMINAL_EOF]) {
                    __atomic_store_n(&terminal_complete, true, __ATOMIC_RELEASE);
                    break;
                }

                if (terminal_used < sizeof(terminal_line) - 1 || character == '\n') {
                    terminal_line[terminal_used++] = character;

                    if (terminal_settings.local_flags & TERMINAL_ECHO) {
                        terminal_echo(&character, 1);
                    }
                }

                if (character == '\n') {
                    __atomic_store_n(&terminal_complete, true, __ATOMIC_RELEASE);
                }
            }

            if (terminal_complete) {
                size_t count = terminal_used - terminal_offset;

                if (count > byte_count) {
                    count = byte_count;
                }

                memcpy(buffer, terminal_line + terminal_offset, count);
                terminal_offset += count;

                if (terminal_offset == terminal_used) {
                    terminal_used = terminal_offset = 0;
                    __atomic_store_n(&terminal_complete, false, __ATOMIC_RELEASE);
                }
                *bytes_transferred = count;
                spinlock_release(&terminal_state_lock, interrupts);

                return EOK;
            }

            spinlock_release(&terminal_state_lock, interrupts);
        }

        if (file_status_flags(file) & O_NONBLOCK) {
            return -EAGAIN;
        }

        thread_wait_until(&terminal_readers, terminal_input_ready, NULL);
    }
}

/* Forward bytes to the kernel terminal and report the full transfer. */
static errno_t tty_write(
    file_t *file, const void *buffer, size_t byte_count, size_t *bytes_transferred) {
    (void)file;
    bool interrupts = spinlock_acquire(&terminal_state_lock);
    terminal_echo(buffer, byte_count);
    spinlock_release(&terminal_state_lock, interrupts);
    *bytes_transferred = byte_count;

    return EOK;
}

/* The syscall layer supplies checked kernel buffers of the ABI wire size.
 * Settings are shared by terminal descriptors;
 * setting attributes must not overwrite the caller's input buffer. */
static errno_t tty_ioctl(file_t *file, unsigned long request, uintptr_t argument) {
    (void)file;

    if (!argument && (request == ABI_TCGETS || request == ABI_TCSETS || request == ABI_TCSETSW ||
                         request == ABI_TCSETSF || request == ABI_TIOCGWINSZ)) {
        return -EFAULT;
    }

    switch (request) {
    case ABI_TCGETS: {
        bool interrupts = spinlock_acquire(&terminal_state_lock);
        memcpy((void *)argument, &terminal_settings, sizeof(terminal_settings));
        spinlock_release(&terminal_state_lock, interrupts);

        return EOK;
    }

    case ABI_TCSETS:
    case ABI_TCSETSW:
    case ABI_TCSETSF: {
        struct terminal_settings settings;
        memcpy(&settings, (void *)argument, sizeof(settings));

        if (!(settings.local_flags & TERMINAL_ICANON) &&
            (settings.characters[TERMINAL_MINIMUM] != 1 || settings.characters[TERMINAL_TIME])) {
            return -EINVAL;
        }

        bool interrupts = spinlock_acquire(&terminal_state_lock);
        terminal_settings = settings;

        if (request == ABI_TCSETSF) {
            terminal_used = terminal_offset = 0;
            __atomic_store_n(&terminal_complete, false, __ATOMIC_RELEASE);
            unsigned char discard[32];

            while (platform_serial_read_bytes(discard, sizeof(discard))) {
            }
        }

        spinlock_release(&terminal_state_lock, interrupts);
        wait_queue_wake_all(&terminal_readers);

        return EOK;
    }

    case ABI_TIOCGWINSZ: {
        /* Report the default make run console; pixel dimensions are unavailable. */
        enum { DEFAULT_TERMINAL_ROWS = 40, DEFAULT_TERMINAL_COLUMNS = 132 };

        const uint16_t dimensions[] = {DEFAULT_TERMINAL_ROWS, DEFAULT_TERMINAL_COLUMNS, 0, 0};
        _Static_assert(sizeof(dimensions) == ABI_WINSIZE_SIZE, "winsize layout");
        memcpy((void *)argument, dimensions, sizeof(dimensions));

        return EOK;
    }

    default:
        return -ENOTTY;
    }
}

static const file_ops_t devfs_tty_file_ops = {
    .read = tty_read,
    .write = tty_write,
    .seek = NULL,
    .ioctl = tty_ioctl,
    .close = NULL,
};

/* devfs_init */

static uint64_t next_device_inode_number = 0x10000; /* separate from tmpfs inos */

/* Allocate one owned character-device inode with the selected operation table. */
static inode_t *devfs_alloc_dev_inode(
    const inode_ops_t *operations, uint32_t major_number, uint32_t minor_number) {
    inode_t *inode = malloc(sizeof(inode_t));

    if (!inode) {
        return NULL;
    }

    inode->ino = __atomic_fetch_add(&next_device_inode_number, 1ULL, __ATOMIC_RELAXED);
    inode->mode = S_IFCHR | 0666u;
    inode->size = 0;
    inode->uid = 0;
    inode->gid = 0;
    inode->rdev = encode_device_number(major_number, minor_number);
    inode->nlink = 1;
    inode->refcount = 1;
    inode->ops = operations;
    inode->lock = (spinlock_t)SPINLOCK_ZERO;
    inode->private = NULL;

    return inode;
}

/* Retain a directory-table reference; registration happens only during boot. */
static void devfs_register(const char *name, inode_t *inode) {
    KERNEL_ASSERT(device_entry_count < DEVFS_MAX_ENTRIES);
    device_entries[device_entry_count].name = name;
    device_entries[device_entry_count].inode = inode;
    inode_ref(inode); /* directory entry holds one ref */
    device_entry_count++;
}

/* Create the permanent device directory and transfer its root reference to the mount. */
void devfs_init(void) {
    wait_queue_init(&terminal_readers);
    KERNEL_ASSERT(platform_serial_start_input(terminal_input_notify));

    /* Root directory inode for /dev */
    inode_t *device_root = malloc(sizeof(inode_t));
    KERNEL_ASSERT(device_root != NULL);
    device_root->ino = __atomic_fetch_add(&next_device_inode_number, 1ULL, __ATOMIC_RELAXED);
    device_root->mode = S_IFDIR | 0755u;
    device_root->size = 0;
    device_root->uid = 0;
    device_root->gid = 0;
    device_root->rdev = 0;
    device_root->nlink = 2;
    device_root->refcount = 1;
    device_root->ops = &devfs_dir_inode_ops;
    device_root->lock = (spinlock_t)SPINLOCK_ZERO;
    device_root->private = NULL;

    /* Create device inodes */
    inode_t *null_inode = devfs_alloc_dev_inode(&devfs_null_inode_ops, 1, 3);
    inode_t *zero_inode = devfs_alloc_dev_inode(&devfs_zero_inode_ops, 1, 5);
    inode_t *tty_inode = devfs_alloc_dev_inode(&devfs_tty_inode_ops, 5, 0);
    KERNEL_ASSERT(null_inode && zero_inode && tty_inode);

    /* Register in the directory table (adds one ref each) */
    devfs_register("null", null_inode);
    devfs_register("zero", zero_inode);
    devfs_register("tty", tty_inode);

    /* Release our allocation refs; dir table holds the live refs */
    inode_unref(null_inode);
    inode_unref(zero_inode);
    inode_unref(tty_inode);

    /* Mount /dev; vfs_mount holds a ref on dev_root */
    errno_t status = vfs_mount("/dev", device_root);
    KERNEL_ASSERT(status == EOK);
    inode_unref(device_root); /* mount table holds the ref now */
}
