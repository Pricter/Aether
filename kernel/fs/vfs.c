/*
 * vfs.c - Virtual Filesystem Switch core.
 *
 * Responsibilities:
 *   - Mount table: up to MAX_MOUNTS entries, resolved by longest prefix.
 *   - Path resolution: find mount root then walk components via lookup.
 *   - fd management: allocate/release fd slots in process_t.fd_table.
 *   - Reference counting: inode_ref/unref, file_ref/unref.
 *   - Public vfs_open/read/write/close/fstat/stat/ioctl/seek/mkdir.
 *
 * This file knows nothing about tmpfs or devfs internals.  Filesystem
 * implementations are selected entirely through the inode_ops / file_ops
 * vtables.
 */

#include <kernel/vfs.h>
#include <kernel/scheduler.h>
#include <kernel/heap.h>
#include <kernel/strings.h>
#include <kernel/panic.h>
#include <kernel/spinlock.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Mount table */

#define MAX_MOUNTS 16
#define VFS_PATH_MAX PATH_MAX
#define VFS_MAX_COMPONENTS 128

typedef struct {
    char path[VFS_PATH_MAX];
    inode_t *root;
    bool active;
} mount_entry_t;

static mount_entry_t mount_table[MAX_MOUNTS];
static spinlock_t mount_lock = SPINLOCK_ZERO;

/* Path helpers */

/*
 * canonicalize_path - canonicalize an absolute path by resolving "." and ".."
 * components via string manipulation (no filesystem access).
 *
 * `input_path` must start with '/'.  Output is written to `output_path` (may alias `input_path`
 * only if they are the same pointer -- we copy `input_path` to a local buffer
 * first).  Returns true on success, false if the result overflows output_capacity.
 */
static bool canonicalize_path(const char *input_path, char *output_path, size_t output_capacity) {
    if (!input_path || input_path[0] != '/') {
        return false;
    }

    size_t input_length = strlen(input_path);

    if (input_length >= VFS_PATH_MAX) {
        return false;
    }

    char buffer[VFS_PATH_MAX];
    __builtin_memcpy(buffer, input_path, input_length + 1);

    const char *components[VFS_MAX_COMPONENTS];
    int component_count = 0;

    char *path_cursor = buffer + 1; /* skip leading '/' */
    while (*path_cursor) {
        while (*path_cursor == '/') {
            path_cursor++;
        }

        if (!*path_cursor) {
            break;
        }

        char *component = path_cursor;

        while (*path_cursor && *path_cursor != '/') {
            path_cursor++;
        }

        char *component_end = path_cursor;

        if (*path_cursor == '/') {
            *path_cursor++ = '\0';
        }

        size_t component_length = (size_t)(component_end - component);

        if (component_length == 1 && component[0] == '.') {
            continue;
        }

        if (component_length == 2 && component[0] == '.' && component[1] == '.') {
            if (component_count > 0) {
                component_count--;
            }

            continue;
        }

        if (component_count >= VFS_MAX_COMPONENTS) {
            return false;
        }

        components[component_count++] = component;
    }

    size_t position = 0;

    if (position + 1 >= output_capacity) {
        return false;
    }

    output_path[position++] = '/';

    for (int entry_index = 0; entry_index < component_count; entry_index++) {
        size_t component_length = strlen(components[entry_index]);

        if (entry_index > 0) {
            if (position + 1 >= output_capacity) {
                return false;
            }

            output_path[position++] = '/';
        }

        if (position + component_length >= output_capacity) {
            return false;
        }

        __builtin_memcpy(output_path + position, components[entry_index], component_length);
        position += component_length;
    }

    output_path[position] = '\0';

    return true;
}

/*
 * make_absolute_path - construct an absolute path from a possibly-relative one.
 *
 * If path starts with '/' it is just canonicalized.
 * Otherwise process->cwd_path is prepended (a snapshot is taken under
 * process->lock to avoid races with concurrent chdir).  process may be NULL,
 * in which case "/" is assumed as the CWD.
 *
 * Returns true on success; false if the result would exceed output_capacity.
 */
static bool make_absolute_path(
    process_t *process, const char *path, char *output_path, size_t output_capacity) {
    if (!path || !output_path || output_capacity < 2) {
        return false;
    }

    if (path[0] == '/') {
        return canonicalize_path(path, output_path, output_capacity);
    }

    /* Relative path: snapshot cwd under lock */
    char working_directory[VFS_PATH_MAX];

    if (process) {
        bool interrupts_enabled = spinlock_acquire(&process->lock);
        size_t working_directory_length = strlen(process->cwd_path);

        if (working_directory_length >= VFS_PATH_MAX) {
            spinlock_release(&process->lock, interrupts_enabled);

            return false;
        }

        __builtin_memcpy(working_directory, process->cwd_path, working_directory_length + 1);
        spinlock_release(&process->lock, interrupts_enabled);
    } else {
        working_directory[0] = '/';
        working_directory[1] = '\0';
    }

    size_t working_directory_length = strlen(working_directory);
    bool needs_separator = (working_directory[working_directory_length - 1] != '/');
    size_t path_length = strlen(path);

    if (working_directory_length + (needs_separator ? 1u : 0u) + path_length >= VFS_PATH_MAX) {
        return false;
    }

    char combined_path[VFS_PATH_MAX];
    __builtin_memcpy(combined_path, working_directory, working_directory_length);

    size_t position = working_directory_length;

    if (needs_separator) {
        combined_path[position++] = '/';
    }

    __builtin_memcpy(combined_path + position, path, path_length + 1);

    return canonicalize_path(combined_path, output_path, output_capacity);
}

/* Reference counting */

/* Retain an already-live inode; acquiring the initial reference is the caller's responsibility. */
void inode_ref(inode_t *inode) {
    KERNEL_ASSERT(inode != NULL);
    __atomic_fetch_add(&inode->refcount, 1u, __ATOMIC_RELAXED);
}

/* Destroy filesystem-private state only when the final inode reference is released. */
void inode_unref(inode_t *inode) {
    if (!inode) {
        return;
    }

    uint32_t previous_reference_count = __atomic_fetch_sub(&inode->refcount, 1u, __ATOMIC_ACQ_REL);
    KERNEL_ASSERT(previous_reference_count > 0);

    if (previous_reference_count == 1) {
        if (inode->ops && inode->ops->destroy) {
            inode->ops->destroy(inode);
        }

        free(inode);
    }
}

/* Retain an open description, sharing its position and status flags between descriptors. */
void file_ref(file_t *file) {
    KERNEL_ASSERT(file != NULL);
    __atomic_fetch_add(&file->refcount, 1u, __ATOMIC_RELAXED);
}

/* On final release, close the endpoint before dropping its owned inode reference. */
void file_unref(file_t *file) {
    if (!file) {
        return;
    }

    uint32_t previous_reference_count = __atomic_fetch_sub(&file->refcount, 1u, __ATOMIC_ACQ_REL);
    KERNEL_ASSERT(previous_reference_count > 0);

    if (previous_reference_count == 1) {
        if (file->ops && file->ops->close) {
            file->ops->close(file);
        }

        inode_unref(file->inode);
        free(file);
    }
}

/* file_alloc helper for filesystem implementations */

/* Return one owned open description, taking its own reference to the supplied inode. */
file_t *file_alloc(inode_t *inode, int flags, const file_ops_t *operations) {
    file_t *file = malloc(sizeof(file_t));

    if (!file) {
        return NULL;
    }

    inode_ref(inode);
    file->inode = inode;
    file->flags = flags;
    file->pos = 0;
    file->refcount = 1;
    file->ops = operations;
    file->lock = (spinlock_t)SPINLOCK_ZERO;
    file->private = NULL;

    return file;
}

/* fd_table helpers */

/* Initialize an unpublished process descriptor table with every slot empty. */
void fd_table_init(fd_table_t *descriptor_table) {
    __builtin_memset(descriptor_table, 0, sizeof(fd_table_t));
}

/* Drop every slot reference after the caller has made the descriptor table unreachable. */
void fd_table_destroy(fd_table_t *descriptor_table) {
    for (int descriptor_index = 0; descriptor_index < FD_TABLE_SIZE; descriptor_index++) {
        if (descriptor_table->files[descriptor_index]) {
            file_unref(descriptor_table->files[descriptor_index]);
            descriptor_table->files[descriptor_index] = NULL;
        }
    }
}

/* Find a free slot >= start; caller must hold process->lock. */
static int allocate_descriptor_from(process_t *process, int first_descriptor) {
    for (int descriptor_index = first_descriptor; descriptor_index < FD_TABLE_SIZE;
        descriptor_index++) {
        if (!process->fd_table.files[descriptor_index]) {
            return descriptor_index;
        }
    }

    return -1;
}

/* Find the lowest free slot; caller must hold process->lock. */
static int allocate_descriptor(process_t *process) {
    return allocate_descriptor_from(process, 0);
}

/*
 * get_referenced_file - look up fd in process, increment the file's refcount, and return it.
 * Caller must NOT hold process->lock.
 * Returns NULL with *status set to -EBADF if fd is invalid or closed.
 */
static file_t *get_referenced_file(process_t *process, int file_descriptor, errno_t *status) {
    if (file_descriptor < 0 || file_descriptor >= FD_TABLE_SIZE) {
        *status = -EBADF;

        return NULL;
    }

    bool interrupts_enabled = spinlock_acquire(&process->lock);
    file_t *file = process->fd_table.files[file_descriptor];

    if (file) {
        file_ref(file);
    }

    spinlock_release(&process->lock, interrupts_enabled);

    if (!file) {
        *status = -EBADF;

        return NULL;
    }

    return file;
}

/* Path helpers */

/*
 * split_parent_path - split an absolute path into parent directory path and basename.
 *
 * Examples:
 *   "/dev/null"  -> parent="/dev",  basename="null"
 *   "/dev"       -> parent="/",     basename="dev"
 *   "/"          -> parent="/",     basename="" (cannot create root)
 *
 * parent_out must be at least VFS_PATH_MAX bytes.
 * Returns true on success, false if path is too long or malformed.
 */
static bool split_parent_path(const char *path, char *parent_out, const char **basename_out) {
    size_t length = strlen(path);

    if (length == 0 || path[0] != '/') {
        return false;
    }

    /* find the last '/' */
    const char *last_slash = path + length - 1;

    while (last_slash > path && *last_slash != '/') {
        last_slash--;
    }

    /* everything before the last '/' is the parent */
    size_t parent_length;

    if (last_slash == path) {
        /* parent is root "/" */
        parent_out[0] = '/';
        parent_out[1] = '\0';
        parent_length = 1;
    } else {
        parent_length = (size_t)(last_slash - path);

        if (parent_length >= VFS_PATH_MAX) {
            return false;
        }

        __builtin_memcpy(parent_out, path, parent_length);
        parent_out[parent_length] = '\0';
    }

    (void)parent_length;

    *basename_out = last_slash + 1;

    return true;
}

/* Path resolution */

/*
 * resolve_inode - resolve an absolute path to an inode.
 *
 * Returns the inode with one reference held, or NULL with *err_out set.
 * Walks the path component-by-component using inode_ops->lookup.
 */
static inode_t *resolve_inode(const char *path, errno_t *err_out) {
    if (!path || path[0] != '/') {
        *err_out = -EINVAL;

        return NULL;
    }

    /* Find the longest-prefix mount point. */
    inode_t *root = NULL;
    size_t best_len = 0;

    bool interrupts_enabled = spinlock_acquire(&mount_lock);

    for (int mount_index = 0; mount_index < MAX_MOUNTS; mount_index++) {
        if (!mount_table[mount_index].active) {
            continue;
        }

        const char *mount_path = mount_table[mount_index].path;
        size_t mount_path_length = strlen(mount_path);

        bool matches;

        if (mount_path_length == 1 && mount_path[0] == '/') {
            /* "/" matches any absolute path */
            matches = true;
        } else {
            matches = (mount_path_length <= strlen(path)) &&
                      (strncmp(path, mount_path, mount_path_length) == 0) &&
                      (path[mount_path_length] == '/' || path[mount_path_length] == '\0');
        }

        if (matches && mount_path_length > best_len) {
            best_len = mount_path_length;
            root = mount_table[mount_index].root;
        }
    }

    if (root) {
        inode_ref(root);
    }

    spinlock_release(&mount_lock, interrupts_enabled);

    if (!root) {
        *err_out = -ENOENT;

        return NULL;
    }

    /* Walk the remaining path components. */
    const char *path_cursor = path + best_len;

    while (*path_cursor == '/') {
        path_cursor++; /* strip leading slashes after mount prefix */
    }

    inode_t *current_inode = root;

    while (*path_cursor) {
        const char *start = path_cursor;

        while (*path_cursor && *path_cursor != '/') {
            path_cursor++;
        }

        size_t component_length = (size_t)(path_cursor - start);

        while (*path_cursor == '/') {
            path_cursor++;
        }

        if (component_length == 0) {
            continue;
        }

        if (component_length >= 256) {
            inode_unref(current_inode);
            *err_out = -ENAMETOOLONG;

            return NULL;
        }

        char name[256];
        __builtin_memcpy(name, start, component_length);
        name[component_length] = '\0';

        if (!S_ISDIR(current_inode->mode)) {
            inode_unref(current_inode);
            *err_out = -ENOTDIR;

            return NULL;
        }

        if (!current_inode->ops || !current_inode->ops->lookup) {
            inode_unref(current_inode);
            *err_out = -ENOSYS;

            return NULL;
        }

        inode_t *next = NULL;
        errno_t status = current_inode->ops->lookup(current_inode, name, &next);
        inode_unref(current_inode);

        if (status < 0) {
            *err_out = status;

            return NULL;
        }

        current_inode = next;
    }

    *err_out = EOK;

    return current_inode;
}

/* VFS init and mount */

/* Initialize the mount table once, before filesystem roots are published. */
void vfs_init(void) {
    __builtin_memset(mount_table, 0, sizeof(mount_table));
    mount_lock = (spinlock_t)SPINLOCK_ZERO;
}

/* Publish a mounted root and retain its inode reference under the mount-table lock. */
errno_t vfs_mount(const char *path, inode_t *root) {
    if (!path || !root || path[0] != '/') {
        return -EINVAL;
    }

    size_t mount_path_length = strlen(path);

    if (mount_path_length >= VFS_PATH_MAX) {
        return -ENAMETOOLONG;
    }

    bool interrupts_enabled = spinlock_acquire(&mount_lock);

    /* reject duplicate mount points */
    for (int mount_index = 0; mount_index < MAX_MOUNTS; mount_index++) {
        if (mount_table[mount_index].active && strcmp(mount_table[mount_index].path, path) == 0) {
            spinlock_release(&mount_lock, interrupts_enabled);

            return -EBUSY;
        }
    }

    /* find a free slot */
    for (int mount_index = 0; mount_index < MAX_MOUNTS; mount_index++) {
        if (!mount_table[mount_index].active) {
            __builtin_memcpy(mount_table[mount_index].path, path, mount_path_length + 1);
            mount_table[mount_index].root = root;
            mount_table[mount_index].active = true;
            inode_ref(root);
            spinlock_release(&mount_lock, interrupts_enabled);

            return EOK;
        }
    }

    spinlock_release(&mount_lock, interrupts_enabled);

    return -ENOMEM;
}

/* vfs_open */

/* Snapshot creation permissions under their process synchronization owner. */
static uint32_t creation_permissions(process_t *process, uint32_t mode) {
    if (!process) {
        return mode & 0777u;
    }

    bool interrupts_enabled = spinlock_acquire(&process->lock);
    uint32_t permissions = (mode & 0777u) & ~process->umask;
    spinlock_release(&process->lock, interrupts_enabled);

    return permissions;
}

/* Resolve or create the inode, open it, then transfer the file reference into a descriptor slot. */
errno_t vfs_open(
    process_t *process, const char *path, int flags, uint32_t mode, int *descriptor_out) {
    if (!process || !path || !descriptor_out) {
        return -EINVAL;
    }

    char absolute_path[VFS_PATH_MAX];

    if (!make_absolute_path(process, path, absolute_path, sizeof(absolute_path))) {
        return -ENAMETOOLONG;
    }

    errno_t status = 0;
    inode_t *inode = resolve_inode(absolute_path, &status);

    if (!inode) {
        if (status != -ENOENT || !(flags & O_CREAT)) {
            return status;
        }

        /* O_CREAT: resolve parent dir and create the file */
        char parent[VFS_PATH_MAX];
        const char *basename = NULL;

        if (!split_parent_path(absolute_path, parent, &basename) || !basename || !*basename) {
            return -EINVAL;
        }

        errno_t parent_status = 0;
        inode_t *directory = resolve_inode(parent, &parent_status);

        if (!directory) {
            return parent_status;
        }

        if (!S_ISDIR(directory->mode) || !directory->ops || !directory->ops->create) {
            inode_unref(directory);

            return -ENOTDIR;
        }

        uint32_t effective_mode = creation_permissions(process, mode) | S_IFREG;
        status = directory->ops->create(directory, basename, effective_mode, &inode);
        inode_unref(directory);

        if (status < 0) {
            return status;
        }

    } else if ((flags & O_CREAT) && (flags & O_EXCL)) {
        inode_unref(inode);

        return -EEXIST;
    }

    /* O_DIRECTORY: caller requires a directory */
    if ((flags & O_DIRECTORY) && !S_ISDIR(inode->mode)) {
        inode_unref(inode);

        return -ENOTDIR;
    }

    if (!inode->ops || !inode->ops->open) {
        inode_unref(inode);

        return -ENOSYS;
    }

    file_t *file = NULL;
    status = inode->ops->open(inode, flags, &file);
    inode_unref(inode); /* open holds its own ref via file_alloc */
    if (status < 0) {
        return status;
    }

    bool interrupts_enabled = spinlock_acquire(&process->lock);
    int file_descriptor = allocate_descriptor(process);

    if (file_descriptor < 0) {
        spinlock_release(&process->lock, interrupts_enabled);
        file_unref(file);

        return -EMFILE;
    }

    process->fd_table.files[file_descriptor] = file;
    process->fd_table.cloexec[file_descriptor] = (flags & O_CLOEXEC) ? 1 : 0;
    spinlock_release(&process->lock, interrupts_enabled);

    *descriptor_out = file_descriptor;

    return EOK;
}

/* vfs_close */

/* Detach the slot under the process lock; close callbacks run after releasing that lock. */
errno_t vfs_close(process_t *process, int file_descriptor) {
    if (!process || file_descriptor < 0 || file_descriptor >= FD_TABLE_SIZE) {
        return -EBADF;
    }

    bool interrupts_enabled = spinlock_acquire(&process->lock);
    file_t *file = process->fd_table.files[file_descriptor];

    if (!file) {
        spinlock_release(&process->lock, interrupts_enabled);

        return -EBADF;
    }

    process->fd_table.files[file_descriptor] = NULL;
    process->fd_table.cloexec[file_descriptor] = 0;
    spinlock_release(&process->lock, interrupts_enabled);

    file_unref(file);

    return EOK;
}

/* vfs_read */

/* Borrow a file reference across driver I/O after validating read access and file type. */
errno_t vfs_read(
    process_t *process, int file_descriptor, void *buffer, size_t count, size_t *bytes_read) {
    if (!process || !buffer || !bytes_read) {
        return -EINVAL;
    }
    *bytes_read = 0;

    errno_t status = 0;
    file_t *file = get_referenced_file(process, file_descriptor, &status);

    if (!file) {
        return status;
    }

    int access_mode = file_status_flags(file) & O_ACCMODE;

    if (access_mode == O_WRONLY) {
        file_unref(file);

        return -EBADF;
    }

    if (S_ISDIR(file->inode->mode)) {
        file_unref(file);

        return -EISDIR;
    }

    if (!file->ops || !file->ops->read) {
        file_unref(file);

        return -ENOSYS;
    }

    status = file->ops->read(file, buffer, count, bytes_read);
    file_unref(file);

    return status;
}

/* vfs_write */

/* Borrow a file reference across driver I/O after validating write access and file type. */
errno_t vfs_write(process_t *process, int file_descriptor, const void *buffer, size_t count,
    size_t *bytes_written) {
    if (!process || !buffer || !bytes_written) {
        return -EINVAL;
    }
    *bytes_written = 0;

    errno_t status = 0;
    file_t *file = get_referenced_file(process, file_descriptor, &status);

    if (!file) {
        return status;
    }

    int access_mode = file_status_flags(file) & O_ACCMODE;

    if (access_mode == O_RDONLY) {
        file_unref(file);

        return -EBADF;
    }

    if (S_ISDIR(file->inode->mode)) {
        file_unref(file);

        return -EISDIR;
    }

    if (!file->ops || !file->ops->write) {
        file_unref(file);

        return -ENOSYS;
    }

    status = file->ops->write(file, buffer, count, bytes_written);
    file_unref(file);

    return status;
}

/* vfs_fstat */

/* Borrow the descriptor's inode while obtaining filesystem-specific metadata. */
errno_t vfs_fstat(process_t *process, int file_descriptor, vfs_stat_t *stat_out) {
    if (!process || !stat_out) {
        return -EINVAL;
    }

    errno_t status = 0;
    file_t *file = get_referenced_file(process, file_descriptor, &status);

    if (!file) {
        return status;
    }

    if (!file->inode->ops || !file->inode->ops->stat) {
        file_unref(file);

        return -ENOSYS;
    }

    status = file->inode->ops->stat(file->inode, stat_out);
    file_unref(file);

    return status;
}

/* vfs_stat */

/* Resolve a path and release its temporary inode reference after collecting metadata. */
errno_t vfs_stat(process_t *process, const char *path, vfs_stat_t *stat_out) {
    if (!path || !stat_out) {
        return -EINVAL;
    }

    char absolute_path[VFS_PATH_MAX];

    if (!make_absolute_path(process, path, absolute_path, sizeof(absolute_path))) {
        return -ENAMETOOLONG;
    }

    errno_t status = 0;
    inode_t *inode = resolve_inode(absolute_path, &status);

    if (!inode) {
        return status;
    }

    if (!inode->ops || !inode->ops->stat) {
        inode_unref(inode);

        return -ENOSYS;
    }

    status = inode->ops->stat(inode, stat_out);
    inode_unref(inode);

    return status;
}

/* Handle empty-path descriptor stats and the supported absolute/CWD-relative path cases. */
errno_t vfs_statat(process_t *process, int directory_descriptor, const char *path, int flags,
    vfs_stat_t *stat_out) {
    if (!path || !stat_out) {
        return -EINVAL;
    }

    /* AT_EMPTY_PATH: stat the fd itself */
    if ((flags & AT_EMPTY_PATH) && path[0] == '\0') {
        return vfs_fstat(process, directory_descriptor, stat_out);
    }

    /* Absolute paths and AT_FDCWD both handled by vfs_stat via make_absolute_path */
    if (path[0] == '/' || directory_descriptor == AT_FDCWD) {
        return vfs_stat(process, path, stat_out);
    }

    return -ENOSYS; /* relative path + non-CWD dirfd not supported */
}

/* vfs_ioctl */

/* Keep the open description alive while invoking its device-specific control callback. */
errno_t vfs_ioctl(
    process_t *process, int file_descriptor, unsigned long request, uintptr_t argument) {
    if (!process) {
        return -EINVAL;
    }

    errno_t status = 0;
    file_t *file = get_referenced_file(process, file_descriptor, &status);

    if (!file) {
        return status;
    }

    if (!file->ops || !file->ops->ioctl) {
        file_unref(file);

        return -ENOTTY;
    }

    status = file->ops->ioctl(file, request, argument);
    file_unref(file);

    return status;
}

/* vfs_seek */

/* Dispatch regular-file seeks or update the enumeration index of a directory description. */
errno_t vfs_seek(process_t *process, int file_descriptor, int64_t offset, int whence,
    int64_t *new_position_out) {
    if (!process || !new_position_out) {
        return -EINVAL;
    }

    errno_t status = 0;
    file_t *file = get_referenced_file(process, file_descriptor, &status);

    if (!file) {
        return status;
    }

    if (!file->ops || !file->ops->seek) {
        /* Directories have no seek op; allow SEEK_SET and SEEK_CUR(0) on
         * the position index so getdents64 can peek and rewind. */
        if (S_ISDIR(file->inode->mode)) {
            bool interrupts_enabled = spinlock_acquire(&file->lock);

            if (whence == SEEK_SET && offset >= 0) {
                file->pos = offset;
                *new_position_out = offset;
                spinlock_release(&file->lock, interrupts_enabled);
                file_unref(file);

                return EOK;
            }

            if (whence == SEEK_CUR && offset == 0) {
                *new_position_out = file->pos;
                spinlock_release(&file->lock, interrupts_enabled);
                file_unref(file);

                return EOK;
            }

            spinlock_release(&file->lock, interrupts_enabled);
        }

        errno_t seek_error = S_ISDIR(file->inode->mode) ? -EINVAL : -ESPIPE;
        file_unref(file);

        return seek_error;
    }

    status = file->ops->seek(file, offset, whence, new_position_out);
    file_unref(file);

    return status;
}

/* vfs_mkdir */

/* Resolve the parent, apply the process creation mask, and request a child directory. */
errno_t vfs_mkdir(process_t *process, const char *path, uint32_t mode) {
    if (!path) {
        return -EINVAL;
    }

    char absolute_path[VFS_PATH_MAX];

    if (!make_absolute_path(process, path, absolute_path, sizeof(absolute_path))) {
        return -ENAMETOOLONG;
    }

    char parent[VFS_PATH_MAX];
    const char *basename = NULL;

    if (!split_parent_path(absolute_path, parent, &basename) || !basename || !*basename) {
        return -EINVAL;
    }

    errno_t status = 0;
    inode_t *directory = resolve_inode(parent, &status);

    if (!directory) {
        return status;
    }

    if (!S_ISDIR(directory->mode) || !directory->ops || !directory->ops->mkdir) {
        inode_unref(directory);

        return -ENOTDIR;
    }

    uint32_t effective_mode = creation_permissions(process, mode);
    status = directory->ops->mkdir(directory, basename, effective_mode);
    inode_unref(directory);

    return status;
}

/* vfs_setup_std_fds */

/* Populate the first three free slots with terminal descriptions, falling back to null. */
void vfs_setup_std_fds(process_t *process) {
    int file_descriptor;

    /* fd 0: stdin  - open /dev/tty read-only */
    if (vfs_open(process, "/dev/tty", O_RDONLY, 0, &file_descriptor) < 0) {
        vfs_open(process, "/dev/null", O_RDONLY, 0, &file_descriptor);
    }

    /* fd 1: stdout - open /dev/tty write-only */
    if (vfs_open(process, "/dev/tty", O_WRONLY, 0, &file_descriptor) < 0) {
        vfs_open(process, "/dev/null", O_WRONLY, 0, &file_descriptor);
    }

    /* fd 2: stderr - open /dev/tty write-only */
    if (vfs_open(process, "/dev/tty", O_WRONLY, 0, &file_descriptor) < 0) {
        vfs_open(process, "/dev/null", O_WRONLY, 0, &file_descriptor);
    }
}

/* vfs_readdir */

/* Borrow a directory description and advance its enumeration index after a successful lookup. */
errno_t vfs_readdir(process_t *process, int file_descriptor, char *name_out, size_t name_capacity,
    uint64_t *ino_out, uint32_t *mode_out) {
    if (!process || !name_out || name_capacity < 2 || !ino_out || !mode_out) {
        return -EINVAL;
    }

    errno_t status = 0;
    file_t *file = get_referenced_file(process, file_descriptor, &status);

    if (!file) {
        return status;
    }

    inode_t *inode = file->inode;

    if (!S_ISDIR(inode->mode)) {
        file_unref(file);

        return -ENOTDIR;
    }

    if (!inode->ops || !inode->ops->readdir) {
        file_unref(file);

        return -ENOSYS;
    }

    /* Directory callbacks are nonblocking. Hold position ownership across the
     * lookup so dup/inherited descriptions cannot consume the same entry twice.
     * Lock order remains file -> directory inode, as for regular I/O. */
    bool interrupts_enabled = spinlock_acquire(&file->lock);
    uint64_t entry_index = (uint64_t)(file->pos < 0 ? 0 : file->pos);
    inode_t *child = NULL;
    status = inode->ops->readdir(inode, entry_index, name_out, name_capacity, &child);

    if (status == EOK) {
        *ino_out = child->ino;
        *mode_out = child->mode;
        file->pos++;
    }

    spinlock_release(&file->lock, interrupts_enabled);

    if (child) {
        inode_unref(child);
    }

    file_unref(file);

    return status;
}

/* vfs_dup / vfs_dup2 */

/* Install a second descriptor referencing the same open description, with close-on-exec cleared. */
errno_t vfs_dup(process_t *process, int source_descriptor, int *destination_descriptor_out) {
    if (!process || !destination_descriptor_out) {
        return -EINVAL;
    }

    if (source_descriptor < 0 || source_descriptor >= FD_TABLE_SIZE) {
        return -EBADF;
    }

    bool interrupts_enabled = spinlock_acquire(&process->lock);
    file_t *file = process->fd_table.files[source_descriptor];

    if (!file) {
        spinlock_release(&process->lock, interrupts_enabled);

        return -EBADF;
    }

    int destination_descriptor = allocate_descriptor(process);

    if (destination_descriptor < 0) {
        spinlock_release(&process->lock, interrupts_enabled);

        return -EMFILE;
    }

    file_ref(file);
    process->fd_table.files[destination_descriptor] = file;
    process->fd_table.cloexec[destination_descriptor] = 0; /* dup never inherits FD_CLOEXEC */
    spinlock_release(&process->lock, interrupts_enabled);

    *destination_descriptor_out = destination_descriptor;

    return EOK;
}

/* Replace a descriptor under the process lock; release the displaced file outside the lock. */
errno_t vfs_dup2(process_t *process, int source_descriptor, int destination_descriptor) {
    if (!process) {
        return -EINVAL;
    }

    if (source_descriptor < 0 || source_descriptor >= FD_TABLE_SIZE) {
        return -EBADF;
    }

    if (destination_descriptor < 0 || destination_descriptor >= FD_TABLE_SIZE) {
        return -EBADF;
    }

    bool interrupts_enabled = spinlock_acquire(&process->lock);
    file_t *source = process->fd_table.files[source_descriptor];

    if (!source) {
        spinlock_release(&process->lock, interrupts_enabled);

        return -EBADF;
    }

    if (source_descriptor == destination_descriptor) {
        spinlock_release(&process->lock, interrupts_enabled);

        return EOK;
    }

    file_t *replaced_file = process->fd_table.files[destination_descriptor];
    file_ref(source);
    process->fd_table.files[destination_descriptor] = source;
    process->fd_table.cloexec[destination_descriptor] = 0; /* dup2 never inherits FD_CLOEXEC */
    spinlock_release(&process->lock, interrupts_enabled);

    if (replaced_file) {
        file_unref(replaced_file); /* close previous occupant outside the lock */
    }

    return EOK;
}

/* vfs_pipe */

/* Allocate both endpoints and publish their descriptor slots together under the process lock. */
errno_t vfs_pipe(process_t *process, int descriptors[2]) {
    if (!process || !descriptors) {
        return -EINVAL;
    }

    file_t *read_file = NULL, *write_file = NULL;
    errno_t status = pipe_alloc_files(&read_file, &write_file);

    if (status < 0) {
        return status;
    }

    bool interrupts_enabled = spinlock_acquire(&process->lock);
    int read_descriptor = allocate_descriptor(process);

    if (read_descriptor < 0) {
        spinlock_release(&process->lock, interrupts_enabled);
        file_unref(read_file);
        file_unref(write_file);

        return -EMFILE;
    }

    process->fd_table.files[read_descriptor] = read_file;
    process->fd_table.cloexec[read_descriptor] = 0;

    int write_descriptor = allocate_descriptor(process);

    if (write_descriptor < 0) {
        process->fd_table.files[read_descriptor] = NULL;
        spinlock_release(&process->lock, interrupts_enabled);
        file_unref(read_file);
        file_unref(write_file);

        return -EMFILE;
    }

    process->fd_table.files[write_descriptor] = write_file;
    process->fd_table.cloexec[write_descriptor] = 0;
    spinlock_release(&process->lock, interrupts_enabled);

    descriptors[0] = read_descriptor;
    descriptors[1] = write_descriptor;

    return EOK;
}

/* vfs_pipe2 */

/* Publish pipe endpoints with the requested supported status and descriptor flags. */
errno_t vfs_pipe2(process_t *process, int descriptors[2], int flags) {
    if (!process || !descriptors) {
        return -EINVAL;
    }

    if (flags & ~(O_CLOEXEC | O_NONBLOCK)) {
        return -EINVAL;
    }

    file_t *read_file = NULL, *write_file = NULL;
    errno_t status = pipe_alloc_files(&read_file, &write_file);

    if (status < 0) {
        return status;
    }

    if (flags & O_NONBLOCK) {
        read_file->flags |= O_NONBLOCK;
        write_file->flags |= O_NONBLOCK;
    }

    bool interrupts_enabled = spinlock_acquire(&process->lock);
    int read_descriptor = allocate_descriptor(process);

    if (read_descriptor < 0) {
        spinlock_release(&process->lock, interrupts_enabled);
        file_unref(read_file);
        file_unref(write_file);

        return -EMFILE;
    }

    process->fd_table.files[read_descriptor] = read_file;
    process->fd_table.cloexec[read_descriptor] = (flags & O_CLOEXEC) ? 1 : 0;

    int write_descriptor = allocate_descriptor(process);

    if (write_descriptor < 0) {
        process->fd_table.files[read_descriptor] = NULL;
        process->fd_table.cloexec[read_descriptor] = 0;
        spinlock_release(&process->lock, interrupts_enabled);
        file_unref(read_file);
        file_unref(write_file);

        return -EMFILE;
    }

    process->fd_table.files[write_descriptor] = write_file;
    process->fd_table.cloexec[write_descriptor] = (flags & O_CLOEXEC) ? 1 : 0;
    spinlock_release(&process->lock, interrupts_enabled);

    descriptors[0] = read_descriptor;
    descriptors[1] = write_descriptor;

    return EOK;
}

/* vfs_dup3 */

/* Replace a distinct destination descriptor with explicitly selected close-on-exec behavior. */
errno_t vfs_dup3(process_t *process, int source_descriptor, int destination_descriptor, int flags) {
    if (!process) {
        return -EINVAL;
    }

    if (source_descriptor < 0 || source_descriptor >= FD_TABLE_SIZE) {
        return -EBADF;
    }

    if (destination_descriptor < 0 || destination_descriptor >= FD_TABLE_SIZE) {
        return -EBADF;
    }

    if (source_descriptor == destination_descriptor) {
        return -EINVAL; /* dup3 requires source_descriptor != destination_descriptor */
    }

    if (flags & ~O_CLOEXEC) {
        return -EINVAL;
    }

    bool interrupts_enabled = spinlock_acquire(&process->lock);
    file_t *source = process->fd_table.files[source_descriptor];

    if (!source) {
        spinlock_release(&process->lock, interrupts_enabled);

        return -EBADF;
    }

    file_t *replaced_file = process->fd_table.files[destination_descriptor];
    file_ref(source);
    process->fd_table.files[destination_descriptor] = source;
    process->fd_table.cloexec[destination_descriptor] = (flags & O_CLOEXEC) ? 1 : 0;
    spinlock_release(&process->lock, interrupts_enabled);

    if (replaced_file) {
        file_unref(replaced_file);
    }

    return EOK;
}

/* vfs_chdir / vfs_getcwd */

/* Transfer the resolved directory reference into the process and release its previous CWD. */
errno_t vfs_chdir(process_t *process, const char *path) {
    if (!process || !path) {
        return -EINVAL;
    }

    char absolute_path[VFS_PATH_MAX];

    if (!make_absolute_path(process, path, absolute_path, sizeof(absolute_path))) {
        return -ENAMETOOLONG;
    }

    errno_t status = 0;
    inode_t *inode = resolve_inode(absolute_path, &status);

    if (!inode) {
        return status;
    }

    if (!S_ISDIR(inode->mode)) {
        inode_unref(inode);

        return -ENOTDIR;
    }

    bool interrupts_enabled = spinlock_acquire(&process->lock);
    inode_t *old_cwd = process->cwd;
    process->cwd = inode;

    size_t length = strlen(absolute_path);

    if (length >= VFS_PATH_MAX) {
        length = VFS_PATH_MAX - 1;
    }

    __builtin_memcpy(process->cwd_path, absolute_path, length + 1);
    spinlock_release(&process->lock, interrupts_enabled);

    if (old_cwd) {
        inode_unref(old_cwd);
    }

    return EOK;
}

/* Copy a consistent snapshot of the process working-directory path. */
errno_t vfs_getcwd(process_t *process, char *buffer, size_t size) {
    if (!process || !buffer || size == 0) {
        return -EINVAL;
    }

    bool interrupts_enabled = spinlock_acquire(&process->lock);
    size_t length = strlen(process->cwd_path);

    if (length + 1 > size) {
        spinlock_release(&process->lock, interrupts_enabled);

        return -ERANGE;
    }

    __builtin_memcpy(buffer, process->cwd_path, length + 1);
    spinlock_release(&process->lock, interrupts_enabled);

    return EOK;
}

/* vfs_unlink */

/* Resolve the parent and reject directory removal before invoking the filesystem unlink callback. */
errno_t vfs_unlink(process_t *process, const char *path) {
    if (!path) {
        return -EINVAL;
    }

    char absolute_path[VFS_PATH_MAX];

    if (!make_absolute_path(process, path, absolute_path, sizeof(absolute_path))) {
        return -ENAMETOOLONG;
    }

    char parent[VFS_PATH_MAX];
    const char *basename = NULL;

    if (!split_parent_path(absolute_path, parent, &basename) || !basename || !*basename) {
        return -EINVAL;
    }

    errno_t status = 0;
    inode_t *directory = resolve_inode(parent, &status);

    if (!directory) {
        return status;
    }

    if (!S_ISDIR(directory->mode) || !directory->ops || !directory->ops->unlink) {
        inode_unref(directory);

        return -ENOTDIR;
    }

    /* Reject attempt to unlink a directory (use rmdir instead) */
    inode_t *target = NULL;
    errno_t lookup_status = directory->ops->lookup(directory, basename, &target);

    if (lookup_status == EOK) {
        bool is_dir = S_ISDIR(target->mode);
        inode_unref(target);

        if (is_dir) {
            inode_unref(directory);

            return -EISDIR;
        }
    }

    status = directory->ops->unlink(directory, basename);
    inode_unref(directory);

    return status;
}

/* vfs_rmdir */

/* Reject a nonempty child directory before removing its parent entry. */
errno_t vfs_rmdir(process_t *process, const char *path) {
    if (!path) {
        return -EINVAL;
    }

    char absolute_path[VFS_PATH_MAX];

    if (!make_absolute_path(process, path, absolute_path, sizeof(absolute_path))) {
        return -ENAMETOOLONG;
    }

    char parent[VFS_PATH_MAX];
    const char *basename = NULL;

    if (!split_parent_path(absolute_path, parent, &basename) || !basename || !*basename) {
        return -EINVAL;
    }

    errno_t status = 0;
    inode_t *directory = resolve_inode(parent, &status);

    if (!directory) {
        return status;
    }

    if (!S_ISDIR(directory->mode) || !directory->ops || !directory->ops->lookup) {
        inode_unref(directory);

        return -ENOTDIR;
    }

    inode_t *target = NULL;
    status = directory->ops->lookup(directory, basename, &target);

    if (status < 0) {
        inode_unref(directory);

        return status;
    }

    if (!S_ISDIR(target->mode)) {
        inode_unref(target);
        inode_unref(directory);

        return -ENOTDIR;
    }

    /* Check empty: entry_index 1 should not exist (entry_index 0 is ".") */
    if (target->ops && target->ops->readdir) {
        char probe_name[2];
        inode_t *probe_child = NULL;
        errno_t empty =
            target->ops->readdir(target, 1, probe_name, sizeof(probe_name), &probe_child);
        if (empty == EOK) {
            inode_unref(probe_child);
            inode_unref(target);
            inode_unref(directory);

            return -ENOTEMPTY;
        }
    }

    inode_unref(target);

    if (!directory->ops->unlink) {
        inode_unref(directory);

        return -ENOSYS;
    }

    status = directory->ops->unlink(directory, basename);
    inode_unref(directory);

    return status;
}

/* vfs_rename */

/* Hold references to both parents while the filesystem transfers the named directory entry. */
errno_t vfs_rename(process_t *process, const char *source_path, const char *destination_path) {
    if (!source_path || !destination_path) {
        return -EINVAL;
    }

    char absolute_source[VFS_PATH_MAX], absolute_destination[VFS_PATH_MAX];

    if (!make_absolute_path(process, source_path, absolute_source, sizeof(absolute_source))) {
        return -ENAMETOOLONG;
    }

    if (!make_absolute_path(
            process, destination_path, absolute_destination, sizeof(absolute_destination))) {
        return -ENAMETOOLONG;
    }

    char source_parent_path[VFS_PATH_MAX], destination_parent_path[VFS_PATH_MAX];
    const char *source_name = NULL, *destination_name = NULL;

    if (!split_parent_path(absolute_source, source_parent_path, &source_name) || !source_name ||
        !*source_name) {
        return -EINVAL;
    }

    if (!split_parent_path(absolute_destination, destination_parent_path, &destination_name) ||
        !destination_name || !*destination_name) {
        return -EINVAL;
    }

    errno_t status = 0;
    inode_t *source_directory = resolve_inode(source_parent_path, &status);

    if (!source_directory) {
        return status;
    }

    inode_t *destination_directory = NULL;

    if (strcmp(source_parent_path, destination_parent_path) == 0) {
        destination_directory = source_directory;
        inode_ref(destination_directory);
    } else {
        destination_directory = resolve_inode(destination_parent_path, &status);

        if (!destination_directory) {
            inode_unref(source_directory);

            return status;
        }
    }

    if (!S_ISDIR(source_directory->mode) || !S_ISDIR(destination_directory->mode)) {
        inode_unref(source_directory);
        inode_unref(destination_directory);

        return -ENOTDIR;
    }

    if (!source_directory->ops || !source_directory->ops->rename) {
        inode_unref(source_directory);
        inode_unref(destination_directory);

        return -ENOSYS;
    }

    if (source_directory->ops != destination_directory->ops) {
        inode_unref(source_directory);
        inode_unref(destination_directory);

        return -EXDEV;
    }

    status = source_directory->ops->rename(
        source_directory, source_name, destination_directory, destination_name);
    inode_unref(source_directory);
    inode_unref(destination_directory);

    return status;
}

/* vfs_truncate / vfs_ftruncate */

/* Resolve a regular-file inode and delegate its content resize operation. */
errno_t vfs_truncate(process_t *process, const char *path, uint64_t size) {
    if (!path) {
        return -EINVAL;
    }

    char absolute_path[VFS_PATH_MAX];

    if (!make_absolute_path(process, path, absolute_path, sizeof(absolute_path))) {
        return -ENAMETOOLONG;
    }

    errno_t status = 0;
    inode_t *inode = resolve_inode(absolute_path, &status);

    if (!inode) {
        return status;
    }

    if (S_ISDIR(inode->mode)) {
        inode_unref(inode);

        return -EISDIR;
    }

    if (!inode->ops || !inode->ops->truncate) {
        inode_unref(inode);

        return -ENOSYS;
    }

    status = inode->ops->truncate(inode, size);
    inode_unref(inode);

    return status;
}

/* Keep the descriptor's inode alive while resizing its regular-file contents. */
errno_t vfs_ftruncate(process_t *process, int file_descriptor, uint64_t size) {
    if (!process) {
        return -EINVAL;
    }

    errno_t status = 0;
    file_t *file = get_referenced_file(process, file_descriptor, &status);

    if (!file) {
        return status;
    }

    inode_t *inode = file->inode;

    if (S_ISDIR(inode->mode)) {
        file_unref(file);

        return -EISDIR;
    }

    if (!inode->ops || !inode->ops->truncate) {
        file_unref(file);

        return -ENOSYS;
    }

    status = inode->ops->truncate(inode, size);
    file_unref(file);

    return status;
}

/* vfs_access */

/* Check path existence; permission enforcement is not implemented by this baseline. */
errno_t vfs_access(process_t *process, const char *path, int access_mode) {
    (void)access_mode; /* no permission model yet; just check existence */
    if (!path) {
        return -EINVAL;
    }

    char absolute_path[VFS_PATH_MAX];

    if (!make_absolute_path(process, path, absolute_path, sizeof(absolute_path))) {
        return -ENAMETOOLONG;
    }

    errno_t status = 0;
    inode_t *inode = resolve_inode(absolute_path, &status);

    if (!inode) {
        return status;
    }

    inode_unref(inode);

    return EOK;
}

/* vfs_fcntl */

/* Separate per-descriptor flags from status flags shared by an open-file description. */
errno_t vfs_fcntl(
    process_t *process, int file_descriptor, int command, uintptr_t argument, int *result) {
    if (!process || !result) {
        return -EINVAL;
    }

    if (file_descriptor < 0 || file_descriptor >= FD_TABLE_SIZE) {
        return -EBADF;
    }

    switch (command) {
    case F_GETFD: {
        bool interrupts_enabled = spinlock_acquire(&process->lock);

        if (!process->fd_table.files[file_descriptor]) {
            spinlock_release(&process->lock, interrupts_enabled);

            return -EBADF;
        }
        *result = process->fd_table.cloexec[file_descriptor] ? FD_CLOEXEC : 0;
        spinlock_release(&process->lock, interrupts_enabled);

        return EOK;
    }

    case F_SETFD: {
        bool interrupts_enabled = spinlock_acquire(&process->lock);

        if (!process->fd_table.files[file_descriptor]) {
            spinlock_release(&process->lock, interrupts_enabled);

            return -EBADF;
        }

        process->fd_table.cloexec[file_descriptor] = ((int)argument & FD_CLOEXEC) ? 1 : 0;
        *result = 0;
        spinlock_release(&process->lock, interrupts_enabled);

        return EOK;
    }

    case F_GETFL: {
        errno_t status = 0;
        file_t *file = get_referenced_file(process, file_descriptor, &status);

        if (!file) {
            return status;
        }
        *result = file_status_flags(file);
        file_unref(file);

        return EOK;
    }

    case F_SETFL: {
        errno_t status = 0;
        file_t *file = get_referenced_file(process, file_descriptor, &status);

        if (!file) {
            return status;
        }

        /* Only O_APPEND and O_NONBLOCK are mutable */
        bool interrupts_enabled = spinlock_acquire(&file->lock);
        __atomic_store_n(&file->flags,
            (file_status_flags(file) & ~(O_APPEND | O_NONBLOCK)) |
                ((int)argument & (O_APPEND | O_NONBLOCK)),
            __ATOMIC_RELEASE);
        spinlock_release(&file->lock, interrupts_enabled);
        *result = 0;
        file_unref(file);

        return EOK;
    }

    case F_DUPFD:
    case F_DUPFD_CLOEXEC: {
        int start = (argument < (uintptr_t)FD_TABLE_SIZE) ? (int)argument : FD_TABLE_SIZE;
        bool interrupts_enabled = spinlock_acquire(&process->lock);

        if (!process->fd_table.files[file_descriptor]) {
            spinlock_release(&process->lock, interrupts_enabled);

            return -EBADF;
        }

        file_t *source = process->fd_table.files[file_descriptor];
        int destination_descriptor = allocate_descriptor_from(process, start);

        if (destination_descriptor < 0) {
            spinlock_release(&process->lock, interrupts_enabled);

            return -EMFILE;
        }

        file_ref(source);
        process->fd_table.files[destination_descriptor] = source;
        process->fd_table.cloexec[destination_descriptor] = (command == F_DUPFD_CLOEXEC) ? 1 : 0;
        spinlock_release(&process->lock, interrupts_enabled);
        *result = destination_descriptor;

        return EOK;
    }

    default:
        return -EINVAL;
    }
}

/* vfs_openat */

/* Support absolute paths and AT_FDCWD; other directory-relative opens remain unsupported. */
errno_t vfs_openat(process_t *process, int directory_descriptor, const char *path, int flags,
    uint32_t mode, int *descriptor_out) {
    if (!process || !path || !descriptor_out) {
        return -EINVAL;
    }

    /* Absolute paths and AT_FDCWD are both handled by vfs_open via make_absolute_path */
    if (path[0] == '/' || directory_descriptor == AT_FDCWD) {
        return vfs_open(process, path, flags, mode, descriptor_out);
    }

    return -ENOSYS; /* relative + non-CWD dirfd not supported */
}

/* Retain the description across positioned I/O; filesystem callbacks serialize
 * inode data without changing the offset shared by dup/inherited descriptors. */
errno_t vfs_read_at(process_t *process, int descriptor, void *buffer, size_t length, int64_t offset,
    size_t *completed) {
    if (!process || !buffer || !completed || offset < 0) {
        return -EINVAL;
    }
    *completed = 0;
    errno_t status;
    file_t *file = get_referenced_file(process, descriptor, &status);

    if (!file) {
        return status;
    }

    if ((file_status_flags(file) & O_ACCMODE) == O_WRONLY) {
        status = -EBADF;
    } else if (S_ISDIR(file->inode->mode)) {
        status = -EISDIR;
    } else if (!file->ops || !file->ops->read_at) {
        status = -ESPIPE;
    } else {
        status = file->ops->read_at(file, buffer, length, offset, completed);
    }

    file_unref(file);

    return status;
}

/* O_APPEND positional writes retain the current baseline append semantics. */
errno_t vfs_write_at(process_t *process, int descriptor, const void *buffer, size_t length,
    int64_t offset, size_t *completed) {
    if (!process || !buffer || !completed || offset < 0) {
        return -EINVAL;
    }
    *completed = 0;
    errno_t status;
    file_t *file = get_referenced_file(process, descriptor, &status);

    if (!file) {
        return status;
    }

    if ((file_status_flags(file) & O_ACCMODE) == O_RDONLY) {
        status = -EBADF;
    } else if (S_ISDIR(file->inode->mode)) {
        status = -EISDIR;
    } else if (!file->ops || !file->ops->write_at) {
        status = -ESPIPE;
    } else {
        status = file->ops->write_at(file, buffer, length, offset, completed);
    }

    file_unref(file);

    return status;
}
