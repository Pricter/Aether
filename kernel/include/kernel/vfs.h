#pragma once

/*
 * vfs.h - Virtual Filesystem Switch public interface.
 *
 * The VFS is the layer between syscalls (open/read/write/close/stat) and
 * filesystem implementations (tmpfs, devfs, ext2, ...).
 *
 * Layering: vfs.h sits above scheduler.h (needs process_t / fd_table_t).
 *           Filesystem implementations include vfs.h and nothing else from
 *           the VFS layer.
 *
 * -----------------------------------------------------------------------
 * Core abstractions
 * -----------------------------------------------------------------------
 *
 *  inode_t   - A filesystem object (file, directory, device node).
 *              Identified by a filesystem-unique ino number.
 *              Reference-counted; freed when refcount reaches 0.
 *              Operations: lookup, create, mkdir, unlink, open, stat, destroy.
 *
 *  file_t    - An open file description (NOT a file descriptor).
 *              Points to an inode; holds one ref on it.
 *              Contains the current file position and open flags.
 *              Reference-counted; freed when refcount reaches 0.
 *              Operations: read, write, seek, ioctl, close.
 *
 *  fd_table_t - Per-process array mapping fd numbers -> file_t*.
 *               Embedded in process_t. Defined in scheduler.h so that
 *               process_t can embed it without including vfs.h.
 *
 * -----------------------------------------------------------------------
 * Ownership rules
 * -----------------------------------------------------------------------
 *
 *  1. inode_ref / inode_unref manage inode lifetime. Never free an inode
 *     directly. inode_unref calls ops->destroy when refcount hits 0.
 *
 *  2. file_ref / file_unref manage file lifetime. Never free a file_t
 *     directly. file_unref calls ops->close when refcount hits 0, then
 *     calls inode_unref on the file's inode.
 *
 *  3. inode_ops->lookup returns an inode with one reference held (caller
 *     must inode_unref when done).
 *
 *  4. inode_ops->open returns a file_t with one reference held (caller
 *     must file_unref when done).
 *
 *  5. The fd_table slot holds one reference on the file_t. vfs_close
 *     clears the slot and calls file_unref.
 *
 *  6. The mount table holds one reference on each mounted root inode.
 *
 * -----------------------------------------------------------------------
 * Concurrency
 * -----------------------------------------------------------------------
 *
 *  process_t.lock protects fd_table slot allocation and deallocation.
 *  Long-running I/O: increment file refcount under lock, release lock,
 *  do I/O, decrement refcount. No spinlock held during I/O.
 *
 *  inode_t.lock protects inode metadata (size) and private data mutations.
 *  file_t.lock protects the file position (pos).
 *
 *  Mount table: spinlock-protected. Lock held only to copy root pointer and
 *  increment its refcount, then released. Path walk proceeds lock-free.
 *
 * Lock ordering (outermost -> innermost):
 *   process_t.lock -> [release before I/O] -> inode_t.lock -> file_t.lock
 *   mount_lock is independent (never held with any of the above).
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <kernel/types.h>
#include <kernel/spinlock.h>
#include <kernel/scheduler.h> /* process_t, fd_table_t */

/* Mode / type bits (Linux-compatible octal values) */

#define S_IFMT 0170000u  /* type mask */
#define S_IFREG 0100000u /* regular file */
#define S_IFDIR 0040000u /* directory */
#define S_IFCHR 0020000u /* character device */
#define S_IFIFO 0010000u /* named pipe */
#define S_IFLNK 0120000u /* symbolic link */

#define S_ISREG(mode) (((mode) & S_IFMT) == S_IFREG)
#define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
#define S_ISCHR(mode) (((mode) & S_IFMT) == S_IFCHR)
#define S_ISFIFO(mode) (((mode) & S_IFMT) == S_IFIFO)

#define S_IRUSR 00400u
#define S_IWUSR 00200u
#define S_IXUSR 00100u
#define S_IRGRP 00040u
#define S_IWGRP 00020u
#define S_IXGRP 00010u
#define S_IROTH 00004u
#define S_IWOTH 00002u
#define S_IXOTH 00001u

/* open(2) flags (Linux AArch64 values) */

#define O_RDONLY 0x000
#define O_WRONLY 0x001
#define O_RDWR 0x002
#define O_ACCMODE 0x003
#define O_CREAT 0x040
#define O_EXCL 0x080
#define O_TRUNC 0x200
#define O_APPEND 0x400
#define O_NONBLOCK 0x800
#define O_DIRECTORY 0x10000
#define O_NOFOLLOW 0x20000
#define O_CLOEXEC 0x80000

/* lseek(2) whence values */

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* fcntl(2) commands and fd flags */

#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define F_DUPFD_CLOEXEC 1030

#define FD_CLOEXEC 1

/* openat / *at constants */

#define AT_FDCWD (-100)
#define AT_EMPTY_PATH 0x1000
#define AT_SYMLINK_NOFOLLOW 0x100

/* access(2) mode bits */

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

/* Forward declarations */

typedef struct inode inode_t;
typedef struct file file_t;

/* vfs_stat_t */

typedef struct {
    uint64_t ino;
    uint32_t mode;
    uint32_t nlink;
    uint32_t uid;
    uint32_t gid;
    uint64_t size;
    int64_t blksize;
    int64_t blocks;
    uint64_t rdev; /* device number for char/block devices */
} vfs_stat_t;

/* inode_ops_t - vtable for inode (filesystem object) operations */

/*
 * All ops return 0 (EOK) on success, -errno on failure.
 * NULL function pointers mean "not supported"; the VFS returns -ENOSYS.
 *
 * lookup:  Find a directory entry by name. Returns inode with one ref held.
 * create:  Create a regular file in a directory. Returns inode with one ref.
 * mkdir:   Create a subdirectory. Does not return the new inode.
 * unlink:  Remove a directory entry.
 * readdir: Nonblocking; called under the file-position lock (file -> inode).
 *          Enumerate directory entries. Index is 0-based; returns -ENOENT
 *          past the end. Sets name_out (NUL-terminated) and *inode_out (one
 *          ref held). Caller must inode_unref(*inode_out) when done.
 * open:    Create a file_t for this inode. Returns file_t with one ref held.
 *          May honour O_TRUNC for regular files.
 * stat:    Fill in a vfs_stat_t from this inode.
 * destroy: Called when refcount reaches 0. Frees inode->private.
 *          Must NOT free the inode_t itself; inode_unref does that.
 */
typedef struct {
    errno_t (*lookup)(inode_t *directory, const char *name, inode_t **inode_out);
    errno_t (*create)(inode_t *directory, const char *name, uint32_t mode, inode_t **inode_out);
    errno_t (*mkdir)(inode_t *directory, const char *name, uint32_t mode);
    errno_t (*unlink)(inode_t *directory, const char *name);
    errno_t (*readdir)(inode_t *directory, uint64_t entry_index, char *name_out,
        size_t name_capacity, inode_t **inode_out);
    errno_t (*open)(inode_t *inode, int flags, file_t **file_out);
    errno_t (*stat)(inode_t *inode, vfs_stat_t *stat_out);
    void (*destroy)(inode_t *inode);
    errno_t (*truncate)(inode_t *inode, uint64_t new_size);
    errno_t (*rename)(inode_t *source_directory, const char *source_name,
        inode_t *destination_directory, const char *destination_name);
} inode_ops_t;

/* file_ops_t - vtable for open-file operations */

/*
 * read/write: transfer data, update *bytes_transferred with bytes transferred.
 * seek:       compute and return the new file position in *new_position_out.
 * ioctl:      device-specific control.
 * close:      called when file refcount drops to 0 (before inode_unref).
 *             Must NOT free the file_t; file_unref does that.
 *
 * NULL function pointers mean "not supported"; the VFS returns -ENOSYS.
 */
typedef struct {
    errno_t (*read)(file_t *file, void *buffer, size_t byte_count, size_t *bytes_transferred);
    errno_t (*write)(
        file_t *file, const void *buffer, size_t byte_count, size_t *bytes_transferred);
    errno_t (*seek)(file_t *file, int64_t offset, int whence, int64_t *new_position_out);
    errno_t (*ioctl)(file_t *file, unsigned long request, uintptr_t argument);
    void (*close)(file_t *file);
    errno_t (*read_at)(
        file_t *file, void *buffer, size_t length, int64_t offset, size_t *completed);
    errno_t (*write_at)(
        file_t *file, const void *buffer, size_t length, int64_t offset, size_t *completed);
} file_ops_t;

/* inode_t */

/*
 * inode_t - a filesystem object.
 *
 * Lifetime: allocated by the filesystem; freed by inode_unref when
 * refcount reaches 0 (which calls ops->destroy then free(inode)).
 *
 * lock:    protects mutable metadata fields (size, uid, gid, mode) and
 *          any mutable data in inode->private.
 * private: filesystem-defined; freed by ops->destroy.
 */
struct inode {
    uint64_t ino;
    uint32_t mode; /* S_IF* | permission bits */
    uint64_t size; /* for S_IFREG: byte length of content */
    uint32_t uid;
    uint32_t gid;
    uint64_t rdev;     /* for S_IFCHR: encoded major/minor */
    uint32_t nlink;    /* hard link count */
    uint32_t refcount; /* atomic */
    const inode_ops_t *ops;
    spinlock_t lock;
    void *private;
};

/* file_t */

/*
 * file_t - an open file description.
 *
 * Holds one reference on inode. Freed by file_unref when refcount drops
 * to 0 (calls ops->close then inode_unref(inode) then free(file)).
 *
 * lock: protects the position and mutable status flags (O_APPEND/O_NONBLOCK).
 */
struct file {
    inode_t *inode;
    int flags;         /* O_* flags */
    int64_t pos;       /* current file offset */
    uint32_t refcount; /* atomic */
    const file_ops_t *ops;
    spinlock_t lock; /* protects pos */
    void *private;   /* filesystem-private; owned by filesystem */
};

/* Status flags can change through F_SETFL while another CPU is doing I/O.
 * Access mode is immutable, but the containing integer still needs an atomic
 * snapshot so flag updates do not race readers in the C memory model. */
static inline int file_status_flags(const file_t *file) {
    return __atomic_load_n(&file->flags, __ATOMIC_ACQUIRE);
}

/* Reference counting */

void inode_ref(inode_t *inode);
void inode_unref(inode_t *inode);

void file_ref(file_t *file);
void file_unref(file_t *file);

/* Filesystem implementation helpers */

/*
 * file_alloc - allocate and initialise a file_t for a given inode.
 *
 * Calls inode_ref(inode). Returns file_t with refcount=1, or NULL on OOM.
 * Filesystem implementations call this from their inode_ops->open function.
 * The caller (open) must NOT additionally call inode_ref.
 */
file_t *file_alloc(inode_t *inode, int flags, const file_ops_t *operations);

/* VFS core API */

/*
 * vfs_init - one-time VFS initialisation. Must be called after slab_init()
 * and before any other vfs_* function.
 */
void vfs_init(void);

/*
 * vfs_mount - mount a filesystem root inode at path.
 *
 * path: absolute path ("/" for the root filesystem, "/dev" for devfs, etc.).
 * root: root inode of the filesystem; vfs_mount calls inode_ref(root).
 *
 * Mounts are resolved by longest-prefix match. Up to MAX_MOUNTS (16) mounts
 * are supported. The "/" mount must be installed before any others.
 *
 * Returns 0 on success, -EBUSY if path is already mounted, -ENOMEM on OOM.
 */
errno_t vfs_mount(const char *path, inode_t *root);

/*
 * vfs_open - open a path and install a file descriptor in process's fd table.
 *
 * path:   absolute path; must be accessible from kernel context.
 * flags:  O_RDONLY / O_WRONLY / O_RDWR | O_CREAT | O_EXCL | O_TRUNC | O_APPEND
 * mode:   creation permissions (only used with O_CREAT; masked to 0777).
 * descriptor_out: receives the new fd number on success.
 *
 * Returns 0 on success, -errno on failure.
 */
errno_t vfs_open(
    process_t *process, const char *path, int flags, uint32_t mode, int *descriptor_out);

/*
 * vfs_close - close file descriptor fd in process.
 * Returns 0 on success, -EBADF if fd is out of range or not open.
 */
errno_t vfs_close(process_t *process, int file_descriptor);

/*
 * vfs_read - read up to count bytes from fd into buffer.
 * Returns 0 on success, -errno on failure. *bytes_read set to bytes transferred.
 */
errno_t vfs_read(
    process_t *process, int file_descriptor, void *buffer, size_t count, size_t *bytes_read);

/*
 * vfs_write - write count bytes from buffer to fd.
 * Returns 0 on success, -errno on failure. *bytes_written set to bytes transferred.
 */
errno_t vfs_write(process_t *process, int file_descriptor, const void *buffer, size_t count,
    size_t *bytes_written);

/*
 * vfs_fstat - stat an open file descriptor.
 */
errno_t vfs_fstat(process_t *process, int file_descriptor, vfs_stat_t *stat_out);

/*
 * vfs_stat - stat a path. Relative paths use process->cwd_path (process may be NULL
 * for absolute-only callers).
 */
errno_t vfs_stat(process_t *process, const char *path, vfs_stat_t *stat_out);

/*
 * vfs_statat - stat a path relative to a directory fd (or AT_FDCWD).
 */
errno_t vfs_statat(process_t *process, int directory_descriptor, const char *path, int flags,
    vfs_stat_t *stat_out);

/*
 * vfs_ioctl - device control on an open fd.
 */
errno_t vfs_ioctl(
    process_t *process, int file_descriptor, unsigned long request, uintptr_t argument);

/*
 * vfs_seek - reposition an open fd.
 * *new_position_out receives the resulting file offset.
 */
errno_t vfs_seek(
    process_t *process, int file_descriptor, int64_t offset, int whence, int64_t *new_position_out);

/*
 * vfs_mkdir - create a directory. Relative paths use process->cwd_path.
 */
errno_t vfs_mkdir(process_t *process, const char *path, uint32_t mode);

/*
 * vfs_unlink - remove a file (not a directory).
 */
errno_t vfs_unlink(process_t *process, const char *path);

/*
 * vfs_rmdir - remove an empty directory.
 */
errno_t vfs_rmdir(process_t *process, const char *path);

/*
 * vfs_rename - rename/move a path.
 */
errno_t vfs_rename(process_t *process, const char *source_path, const char *destination_path);

/*
 * vfs_truncate - set file size via path.
 */
errno_t vfs_truncate(process_t *process, const char *path, uint64_t size);

/*
 * vfs_ftruncate - set file size via fd.
 */
errno_t vfs_ftruncate(process_t *process, int file_descriptor, uint64_t size);

/*
 * vfs_access - check accessibility of a path (F_OK/R_OK/W_OK/X_OK).
 * Currently only tests existence; no permission model yet.
 */
errno_t vfs_access(process_t *process, const char *path, int access_mode);

/*
 * vfs_fcntl - file control operations (F_DUPFD, F_GETFD, F_SETFD, etc.).
 * *result receives the integer return value on success.
 */
errno_t vfs_fcntl(
    process_t *process, int file_descriptor, int command, uintptr_t argument, int *result);

/*
 * vfs_openat - open a path relative to a directory fd (or AT_FDCWD).
 */
errno_t vfs_openat(process_t *process, int directory_descriptor, const char *path, int flags,
    uint32_t mode, int *descriptor_out);

/*
 * vfs_chdir - change working directory.
 */
errno_t vfs_chdir(process_t *process, const char *path);

/*
 * vfs_getcwd - get the working directory path into buffer (size bytes).
 * Returns -ERANGE if buffer is too small.
 */
errno_t vfs_getcwd(process_t *process, char *buffer, size_t size);

/*
 * vfs_pipe2 - create a pipe with flags (O_CLOEXEC, O_NONBLOCK).
 */
errno_t vfs_pipe2(process_t *process, int descriptors[2], int flags);

/*
 * vfs_dup3 - duplicate source_descriptor to destination_descriptor with flags. source_descriptor must != destination_descriptor.
 */
errno_t vfs_dup3(process_t *process, int source_descriptor, int destination_descriptor, int flags);

/* fd_table lifecycle (called from process.c) */

/*
 * fd_table_init - zero all fd slots. Called from process_create().
 * Must be called with NO lock held.
 */
void fd_table_init(fd_table_t *descriptor_table);

/*
 * fd_table_destroy - close all open fds in the table.
 * Called from process_destroy() after thread_count reaches 0.
 * Must be called with NO lock held (calls file_unref internally).
 */
void fd_table_destroy(fd_table_t *descriptor_table);

/* Filesystem-specific init */

/*
 * tmpfs_create_root - allocate a new tmpfs root directory inode.
 * Returns the inode with refcount=1 (caller must inode_unref when done,
 * unless ownership is transferred to vfs_mount).
 * Returns NULL on OOM.
 */
inode_t *tmpfs_create_root(void);

/*
 * devfs_init - create the /dev filesystem with null, zero, tty nodes.
 * Calls vfs_mount("/dev", ...) internally.
 * Must be called after vfs_init() and after vfs_mount("/", ...).
 */
void devfs_init(void);

/*
 * vfs_setup_std_fds - install fds 0 (stdin), 1 (stdout), 2 (stderr) in process.
 * Opens /dev/tty for each. Must be called after devfs_init().
 * Called from elf_load_user() before the process is scheduled.
 */
void vfs_setup_std_fds(process_t *process);

/*
 * vfs_readdir - read one directory entry from an open directory fd.
 * name_capacity must be at least two bytes (including the terminator).
 *
 * Uses file->pos as the 0-based entry index; advances it on success.
 * name_out receives the NUL-terminated entry name (truncated to name_max-1).
 * ino_out and mode_out receive the inode number and mode of the entry.
 *
 * Returns 0 on success, -ENOENT when all entries have been read,
 * -ENOTDIR if fd is not a directory.
 */
errno_t vfs_readdir(process_t *process, int file_descriptor, char *name_out, size_t name_capacity,
    uint64_t *ino_out, uint32_t *mode_out);

/*
 * vfs_dup - duplicate source_descriptor to the lowest available file descriptor.
 * The new fd shares the same open file description (file_t) as source_descriptor.
 */
errno_t vfs_dup(process_t *process, int source_descriptor, int *destination_descriptor_out);

/*
 * vfs_dup2 - duplicate source_descriptor to exactly destination_descriptor, closing destination_descriptor first if open.
 * If source_descriptor == destination_descriptor, returns 0 without doing anything.
 */
errno_t vfs_dup2(process_t *process, int source_descriptor, int destination_descriptor);

/*
 * vfs_pipe - create an anonymous pipe.
 * fds[0] = read end (O_RDONLY), fds[1] = write end (O_WRONLY).
 * Returns 0 on success, -EMFILE if the fd table is full, -ENOMEM on OOM.
 */
errno_t vfs_pipe(process_t *process, int descriptors[2]);

/*
 * pipe_alloc_files - allocate a paired (read, write) file_t for a new pipe.
 * Defined in fs/pipe.c; called only by vfs_pipe in fs/vfs.c.
 */
errno_t pipe_alloc_files(file_t **read_file_out, file_t **write_file_out);

/* Positioned regular-file I/O does not alter the shared open-description offset. */
errno_t vfs_read_at(process_t *process, int descriptor, void *buffer, size_t length, int64_t offset,
    size_t *completed);
errno_t vfs_write_at(process_t *process, int descriptor, const void *buffer, size_t length,
    int64_t offset, size_t *completed);
