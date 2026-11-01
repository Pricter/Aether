/*
 * pipe.c - anonymous pipe filesystem object.
 *
 * A pipe is a unidirectional byte stream shared between a read-end file_t
 * (O_RDONLY) and a write-end file_t (O_WRONLY) that point to the same
 * underlying inode.  The inode's private pointer holds a pipe_t which
 * contains the ring buffer, open-end counters, and the pipe lock.
 *
 * Blocking behaviour:
 *   pipe_read  - if the buffer is empty and write_endpoint_count > 0, yields via
 *                schedule() until data arrives (cooperative spin-yield).
 *   pipe_write - if the buffer is full and read_endpoint_count > 0, yields similarly.
 *   Both return immediately on EOF / EPIPE when the opposite end is closed.
 *
 * Concurrency: pipe_t.lock protects ring state and endpoint counts. Counts
 * are only decremented
 * by pipe_close which is called exactly once per file_t (when refcount
 * reaches zero).
 */

#include <kernel/vfs.h>
#include <kernel/scheduler.h>
#include <kernel/heap.h>
#include <kernel/panic.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define PIPE_CAPACITY 4096

typedef struct {
    uint8_t buffer[PIPE_CAPACITY];
    size_t read_offset;    /* index of next byte to consume          */
    size_t write_offset;   /* index of next byte to produce          */
    size_t buffered_bytes; /* bytes currently in the buffer          */
    spinlock_t lock;
    uint32_t read_endpoint_count;  /* lock-owned live read descriptions */
    uint32_t write_endpoint_count; /* lock-owned live write descriptions */
} pipe_t;

static uint64_t next_pipe_inode_number = 0x80000000ULL;

/* file_ops */

/* Consume a contiguous or wrapped buffer segment; yield on empty until EOF. */
static errno_t pipe_read(file_t *file, void *buffer, size_t byte_count, size_t *bytes_transferred) {
    pipe_t *pipe = (pipe_t *)file->inode->private;
    *bytes_transferred = 0;

    if (!byte_count) {
        return EOK;
    }

    for (;;) {
        bool interrupts_enabled = spinlock_acquire(&pipe->lock);

        if (pipe->buffered_bytes > 0) {
            size_t bytes_to_read =
                pipe->buffered_bytes < byte_count ? pipe->buffered_bytes : byte_count;
            size_t contiguous_bytes = PIPE_CAPACITY - pipe->read_offset;

            if (contiguous_bytes > bytes_to_read) {
                contiguous_bytes = bytes_to_read;
            }

            __builtin_memcpy(buffer, pipe->buffer + pipe->read_offset, contiguous_bytes);

            if (contiguous_bytes < bytes_to_read) {
                __builtin_memcpy((uint8_t *)buffer + contiguous_bytes, pipe->buffer,
                    bytes_to_read - contiguous_bytes);
            }

            pipe->read_offset = (pipe->read_offset + bytes_to_read) % PIPE_CAPACITY;
            pipe->buffered_bytes -= bytes_to_read;
            spinlock_release(&pipe->lock, interrupts_enabled);
            *bytes_transferred = bytes_to_read;

            return EOK;
        }

        bool writers_closed = pipe->write_endpoint_count == 0;
        spinlock_release(&pipe->lock, interrupts_enabled);

        /* Empty and closed must be observed together: a writer can otherwise
         * publish its final bytes and close between two independent checks. */
        if (writers_closed) {
            return EOK;
        }

        if (file_status_flags(file) & O_NONBLOCK) {
            return -EAGAIN;
        }

        /* Buffer empty but write end still open; yield and retry. */
        schedule();
    }
}

/* Append across wraparound, yielding when full and reporting a closed reader as EPIPE. */
static errno_t pipe_write(
    file_t *file, const void *buffer, size_t byte_count, size_t *bytes_transferred) {
    pipe_t *pipe = (pipe_t *)file->inode->private;
    *bytes_transferred = 0;

    if (!byte_count) {
        return EOK;
    }

    size_t transferred_bytes = 0;

    while (transferred_bytes < byte_count) {
        bool interrupts_enabled = spinlock_acquire(&pipe->lock);

        if (!pipe->read_endpoint_count) {
            spinlock_release(&pipe->lock, interrupts_enabled);
            *bytes_transferred = transferred_bytes;

            return transferred_bytes ? EOK : -EPIPE;
        }

        size_t available_bytes = PIPE_CAPACITY - pipe->buffered_bytes;

        if (available_bytes > 0) {
            size_t bytes_to_write = (byte_count - transferred_bytes) < available_bytes
                                        ? (byte_count - transferred_bytes)
                                        : available_bytes;
            size_t contiguous_bytes = PIPE_CAPACITY - pipe->write_offset;

            if (contiguous_bytes > bytes_to_write) {
                contiguous_bytes = bytes_to_write;
            }

            __builtin_memcpy(pipe->buffer + pipe->write_offset,
                (const uint8_t *)buffer + transferred_bytes, contiguous_bytes);
            if (contiguous_bytes < bytes_to_write) {
                __builtin_memcpy(pipe->buffer,
                    (const uint8_t *)buffer + transferred_bytes + contiguous_bytes,
                    bytes_to_write - contiguous_bytes);
            }

            pipe->write_offset = (pipe->write_offset + bytes_to_write) % PIPE_CAPACITY;
            pipe->buffered_bytes += bytes_to_write;
            transferred_bytes += bytes_to_write;
            spinlock_release(&pipe->lock, interrupts_enabled);
            continue;
        }

        spinlock_release(&pipe->lock, interrupts_enabled);

        if (file_status_flags(file) & O_NONBLOCK) {
            *bytes_transferred = transferred_bytes;

            return transferred_bytes ? EOK : -EAGAIN;
        }

        /* Buffer full but read end still open; yield and retry. */
        schedule();
    }
    *bytes_transferred = transferred_bytes;

    return EOK;
}

/* Drop this open description's endpoint count; duplicated descriptors share that count. */
static void pipe_close(file_t *file) {
    pipe_t *pipe = (pipe_t *)file->inode->private;
    int access_mode = file_status_flags(file) & O_ACCMODE;
    bool interrupts_enabled = spinlock_acquire(&pipe->lock);

    if (access_mode == O_RDONLY) {
        KERNEL_ASSERT(pipe->read_endpoint_count);
        --pipe->read_endpoint_count;
    } else {
        KERNEL_ASSERT(pipe->write_endpoint_count);
        --pipe->write_endpoint_count;
    }

    spinlock_release(&pipe->lock, interrupts_enabled);

    /* Readers / writers polling via schedule() detect the change on their
     * next iteration without any explicit wakeup. */
}

static const file_ops_t pipe_file_ops = {
    .read = pipe_read,
    .write = pipe_write,
    .seek = NULL, /* pipes are not seekable */
    .ioctl = NULL,
    .close = pipe_close,
};

/* inode_ops */

/* Return an owned open-file reference for this shared pipe inode. */
static errno_t pipe_inode_open(inode_t *inode, int flags, file_t **file_out) {
    file_t *file = file_alloc(inode, flags, &pipe_file_ops);

    if (!file) {
        return -ENOMEM;
    }
    *file_out = file;

    return EOK;
}

/* Report FIFO metadata without exposing the internal ring-buffer size as file length. */
static errno_t pipe_inode_stat(inode_t *inode, vfs_stat_t *stat_out) {
    stat_out->ino = inode->ino;
    stat_out->mode = inode->mode;
    stat_out->nlink = 1;
    stat_out->uid = 0;
    stat_out->gid = 0;
    stat_out->size = 0;
    stat_out->blksize = PIPE_CAPACITY;
    stat_out->blocks = 0;
    stat_out->rdev = 0;

    return EOK;
}

/* Release the shared ring buffer after the last inode reference disappears. */
static void pipe_inode_destroy(inode_t *inode) {
    free(inode->private); /* free pipe_t */
}

static const inode_ops_t pipe_inode_ops = {
    .lookup = NULL,
    .create = NULL,
    .mkdir = NULL,
    .unlink = NULL,
    .readdir = NULL,
    .open = pipe_inode_open,
    .stat = pipe_inode_stat,
    .destroy = pipe_inode_destroy,
};

/* pipe_alloc_files */

/*
 * pipe_alloc_files - allocate a (read, write) file_t pair for a new pipe.
 *
 * Allocates the pipe_t ring buffer and a shared inode.  Calls file_alloc
 * twice (which calls inode_ref each time), then drops the initial inode
 * ref so the inode's lifetime is exactly tied to the two file_t objects.
 *
 * On success, both *read_file_out and *write_file_out have refcount=1.
 * The caller (vfs_pipe) installs them into the fd table.
 *
 * On any allocation failure the routine cleans up completely and returns
 * -ENOMEM; no partial state is left behind.
 */
errno_t pipe_alloc_files(file_t **read_file_out, file_t **write_file_out) {
    pipe_t *pipe = malloc(sizeof(pipe_t));

    if (!pipe) {
        return -ENOMEM;
    }

    __builtin_memset(pipe, 0, sizeof(pipe_t));
    pipe->lock = (spinlock_t)SPINLOCK_ZERO;
    pipe->read_endpoint_count = 1;
    pipe->write_endpoint_count = 1;

    inode_t *inode = malloc(sizeof(inode_t));

    if (!inode) {
        free(pipe);

        return -ENOMEM;
    }

    inode->ino = __atomic_fetch_add(&next_pipe_inode_number, 1ULL, __ATOMIC_RELAXED);
    inode->mode = S_IFIFO | 0600u;
    inode->size = 0;
    inode->uid = 0;
    inode->gid = 0;
    inode->rdev = 0;
    inode->nlink = 1;
    inode->refcount = 1; /* initial ref; dropped below after two file_allocs */
    inode->ops = &pipe_inode_ops;
    inode->lock = (spinlock_t)SPINLOCK_ZERO;
    inode->private = pipe;

    /* file_alloc calls inode_ref, so after each call refcount increments. */
    file_t *read_file = file_alloc(inode, O_RDONLY, &pipe_file_ops);

    if (!read_file) {
        free(inode);
        free(pipe);

        return -ENOMEM;
    }

    /* inode refcount == 2 */

    file_t *write_file = file_alloc(inode, O_WRONLY, &pipe_file_ops);

    if (!write_file) {
        file_unref(read_file); /* refcount 1->0: close cb, inode_unref (2->1) */
        inode_unref(inode);    /* refcount 1->0: destroy (frees pipe), free(inode) */
        return -ENOMEM;
    }

    /* inode refcount == 3 */

    inode_unref(inode); /* drop initial ref; steady state refcount == 2 */

    *read_file_out = read_file;
    *write_file_out = write_file;

    return EOK;
}
