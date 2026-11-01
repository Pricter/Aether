/*
 * tmpfs.c - in-memory filesystem (RAM filesystem).
 *
 * tmpfs provides a fully functional read/write filesystem backed entirely
 * by kernel heap memory.  It is the natural choice for "/" before any
 * disk driver exists.
 *
 * Supported operations:
 *   Directories: lookup, create, mkdir, unlink, readdir, stat
 *   Regular files: open (with O_TRUNC), read, write, seek, stat
 *
 * All data is lost on reboot.  No persistence, no journalling.
 *
 * Concurrency: each inode has its own spinlock protecting the private data
 * (directory entries list or file buffer).  Inode metadata (size, nlink)
 * is also protected by the inode lock.
 */

#include <kernel/vfs.h>
#include <kernel/heap.h>
#include <kernel/strings.h>
#include <kernel/panic.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Inode number allocator */

static uint64_t next_tmpfs_inode_number = 1;

/* Allocate an inode identity independently of directory or file locks. */
static uint64_t allocate_inode_number(void) {
    return __atomic_fetch_add(&next_tmpfs_inode_number, 1ULL, __ATOMIC_RELAXED);
}

/* tmpfs private data structures */

/*
 * tmpfs_file_t - private data for a regular-file inode.
 * Protected by the owning inode's lock.
 */
typedef struct {
    uint8_t *buffer; /* heap buffer holding file content */
    size_t size;     /* number of valid bytes in buffer */
    size_t capacity; /* allocated size of buffer */
} tmpfs_file_t;

/*
 * tmpfs_dentry_t - one entry in a directory's child list.
 */
typedef struct {
    char *name;       /* heap-allocated NUL-terminated name */
    inode_t *inode;   /* held ref on the child inode */
    list_node_t node; /* linkage in tmpfs_dir_t.entries */
} tmpfs_dentry_t;

/*
 * tmpfs_dir_t - private data for a directory inode.
 * Protected by the owning inode's lock.
 */
typedef struct {
    list_head_t entries;  /* tmpfs_dentry_t nodes */
    uint32_t entry_count; /* number of entries (not counting . and ..) */
} tmpfs_dir_t;

/* Forward declarations */

static const inode_ops_t tmpfs_dir_inode_ops;
static const inode_ops_t tmpfs_file_inode_ops;
static const file_ops_t tmpfs_file_operations;

/* Inode allocation */

/* Allocate one owned inode and its directory or regular-file private storage. */
static inode_t *tmpfs_alloc_inode(uint32_t mode) {
    inode_t *inode = malloc(sizeof(inode_t));

    if (!inode) {
        return NULL;
    }

    inode->ino = allocate_inode_number();
    inode->mode = mode;
    inode->size = 0;
    inode->uid = 0;
    inode->gid = 0;
    inode->rdev = 0;
    inode->nlink = 1;
    inode->refcount = 1;
    inode->lock = (spinlock_t)SPINLOCK_ZERO;
    inode->private = NULL;

    if (S_ISDIR(mode)) {
        inode->ops = &tmpfs_dir_inode_ops;

        tmpfs_dir_t *directory_data = malloc(sizeof(tmpfs_dir_t));

        if (!directory_data) {
            free(inode);

            return NULL;
        }

        list_head_init(&directory_data->entries);
        directory_data->entry_count = 0;
        inode->private = directory_data;
    } else {
        inode->ops = &tmpfs_file_inode_ops;

        tmpfs_file_t *file_data = malloc(sizeof(tmpfs_file_t));

        if (!file_data) {
            free(inode);

            return NULL;
        }

        file_data->buffer = NULL;
        file_data->size = 0;
        file_data->capacity = 0;
        inode->private = file_data;
    }

    return inode;
}

/* File buffer growth */

/* Grow file storage geometrically; the caller holds the owning inode lock. */
static errno_t tmpfs_file_reserve(tmpfs_file_t *file_data, size_t required_capacity) {
    if (required_capacity <= file_data->capacity) {
        return EOK;
    }

    size_t new_capacity = file_data->capacity ? file_data->capacity : 64;

    while (new_capacity < required_capacity) {
        if (new_capacity > SIZE_MAX / 2) {
            return -EOVERFLOW;
        }

        new_capacity *= 2;
    }

    uint8_t *new_buffer = malloc(new_capacity);

    if (!new_buffer) {
        return -ENOSPC;
    }

    if (file_data->buffer) {
        __builtin_memcpy(new_buffer, file_data->buffer, file_data->size);
        free(file_data->buffer);
    }

    /* zero the new region */
    if (new_capacity > file_data->size) {
        __builtin_memset(new_buffer + file_data->size, 0, new_capacity - file_data->size);
    }

    file_data->buffer = new_buffer;
    file_data->capacity = new_capacity;

    return EOK;
}

/* Directory helpers */

/*
 * find_directory_entry - find a dentry by name; caller must hold inode->lock.
 * Returns the dentry or NULL.
 */
static tmpfs_dentry_t *find_directory_entry(tmpfs_dir_t *directory_data, const char *name) {
    list_node_t *entry_node;
    list_for_each(entry_node, &directory_data->entries) {
        tmpfs_dentry_t *entry = list_entry(entry_node, tmpfs_dentry_t, node);

        if (strcmp(entry->name, name) == 0) {
            return entry;
        }
    }

    return NULL;
}

/*
 * add_directory_entry - add a new dentry pointing to child; caller must hold inode->lock.
 * Takes one additional reference on child (directory entry owns a ref).
 * Returns 0 on success, -ENOSPC on OOM.
 */
static errno_t add_directory_entry(tmpfs_dir_t *directory_data, const char *name, inode_t *child) {
    tmpfs_dentry_t *entry = malloc(sizeof(tmpfs_dentry_t));

    if (!entry) {
        return -ENOSPC;
    }

    entry->name = strdup(name);

    if (!entry->name) {
        free(entry);

        return -ENOSPC;
    }

    inode_ref(child);
    entry->inode = child;
    list_node_init(&entry->node);
    list_push_back(&directory_data->entries, &entry->node);
    directory_data->entry_count++;

    return EOK;
}

/* Directory inode operations */

/* Retain the child inode while the directory lock protects its entry. */
static errno_t tmpfs_dir_lookup(inode_t *directory, const char *name, inode_t **inode_out) {
    tmpfs_dir_t *directory_data = (tmpfs_dir_t *)directory->private;
    bool interrupts_enabled = spinlock_acquire(&directory->lock);
    tmpfs_dentry_t *entry = find_directory_entry(directory_data, name);

    if (entry) {
        inode_ref(entry->inode);
        *inode_out = entry->inode;
    }

    spinlock_release(&directory->lock, interrupts_enabled);

    return entry ? EOK : -ENOENT;
}

/* Allocate a child and publish its directory reference, returning the allocation reference. */
static errno_t tmpfs_dir_create(
    inode_t *directory, const char *name, uint32_t mode, inode_t **inode_out) {
    tmpfs_dir_t *directory_data = (tmpfs_dir_t *)directory->private;

    bool interrupts_enabled = spinlock_acquire(&directory->lock);

    if (find_directory_entry(directory_data, name)) {
        spinlock_release(&directory->lock, interrupts_enabled);

        return -EEXIST;
    }

    spinlock_release(&directory->lock, interrupts_enabled);

    inode_t *child = tmpfs_alloc_inode(mode);

    if (!child) {
        return -ENOSPC;
    }

    interrupts_enabled = spinlock_acquire(&directory->lock);

    errno_t status = find_directory_entry(directory_data, name)
                         ? -EEXIST
                         : add_directory_entry(directory_data, name, child);
    spinlock_release(&directory->lock, interrupts_enabled);

    if (status < 0) {
        inode_unref(child);

        return status;
    }

    /* Return with one ref (the caller's ref; dir entry holds another) */
    *inode_out = child;

    return EOK;
}

/* Create a directory and release the caller reference retained by the create helper. */
static errno_t tmpfs_dir_mkdir(inode_t *directory, const char *name, uint32_t mode) {
    inode_t *child = NULL;
    errno_t status = tmpfs_dir_create(directory, name, (mode & 0777u) | S_IFDIR, &child);

    if (status < 0) {
        return status;
    }

    inode_unref(child); /* caller doesn't need it; dir holds ref */
    return EOK;
}

/* Detach under the directory lock, then release the entry's inode reference outside it. */
static errno_t tmpfs_dir_unlink(inode_t *directory, const char *name) {
    tmpfs_dir_t *directory_data = (tmpfs_dir_t *)directory->private;
    bool interrupts_enabled = spinlock_acquire(&directory->lock);

    tmpfs_dentry_t *entry = find_directory_entry(directory_data, name);

    if (!entry) {
        spinlock_release(&directory->lock, interrupts_enabled);

        return -ENOENT;
    }

    list_remove(&entry->node);
    directory_data->entry_count--;
    spinlock_release(&directory->lock, interrupts_enabled);

    inode_unref(entry->inode);
    free(entry->name);
    free(entry);

    return EOK;
}

/* Return one referenced child by index; index zero names the directory itself. */
static errno_t tmpfs_dir_readdir(inode_t *directory, uint64_t entry_index, char *name_out,
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

    tmpfs_dir_t *directory_data = (tmpfs_dir_t *)directory->private;
    bool interrupts_enabled = spinlock_acquire(&directory->lock);

    uint64_t current_entry_index = 0;
    list_node_t *entry_node;
    list_for_each(entry_node, &directory_data->entries) {
        if (current_entry_index == entry_index) {
            tmpfs_dentry_t *entry = list_entry(entry_node, tmpfs_dentry_t, node);
            size_t name_length = strlen(entry->name);

            if (name_length >= name_capacity) {
                name_length = name_capacity - 1;
            }

            __builtin_memcpy(name_out, entry->name, name_length);
            name_out[name_length] = '\0';
            inode_ref(entry->inode);
            *inode_out = entry->inode;
            spinlock_release(&directory->lock, interrupts_enabled);

            return EOK;
        }

        current_entry_index++;
    }

    spinlock_release(&directory->lock, interrupts_enabled);

    return -ENOENT;
}

/* Create an open description for enumeration without regular-file operations. */
static errno_t tmpfs_dir_open(inode_t *inode, int flags, file_t **file_out) {
    (void)flags;

    /* Opening a directory returns a file_t for readdir use; no file ops needed */
    file_t *file = file_alloc(inode, flags, NULL);

    if (!file) {
        return -ENOSPC;
    }
    *file_out = file;

    return EOK;
}

/* Snapshot directory metadata while its child list is locked. */
static errno_t tmpfs_dir_stat(inode_t *inode, vfs_stat_t *stat_out) {
    bool interrupts_enabled = spinlock_acquire(&inode->lock);
    stat_out->ino = inode->ino;
    stat_out->mode = inode->mode;
    stat_out->nlink = inode->nlink + 2; /* . and .. */
    stat_out->uid = inode->uid;
    stat_out->gid = inode->gid;
    stat_out->size = ((tmpfs_dir_t *)inode->private)->entry_count * 32ULL;
    stat_out->blksize = 4096;
    stat_out->blocks = 0;
    stat_out->rdev = 0;
    spinlock_release(&inode->lock, interrupts_enabled);

    return EOK;
}

/* Release every child-entry reference before freeing the private directory state. */
static void tmpfs_dir_destroy(inode_t *inode) {
    tmpfs_dir_t *directory_data = (tmpfs_dir_t *)inode->private;

    if (!directory_data) {
        return;
    }

    list_node_t *entry_node, *next_node;
    list_for_each_safe(entry_node, next_node, &directory_data->entries) {
        tmpfs_dentry_t *entry = list_entry(entry_node, tmpfs_dentry_t, node);
        inode_unref(entry->inode);
        free(entry->name);
        free(entry);
    }

    free(directory_data);
    inode->private = NULL;
}

/* Move or rename an entry, transferring its existing child reference between directories. */
/* Prepare allocation before locking. Ordering the two inode locks by address
 * makes cross-directory moves atomic to lookups without an ABBA deadlock. */
static errno_t tmpfs_dir_rename(inode_t *source_directory, const char *source_name,
    inode_t *destination_directory, const char *destination_name) {
    char *replacement_name = strdup(destination_name);

    if (!replacement_name) {
        return -ENOSPC;
    }

    inode_t *first_directory = (uintptr_t)source_directory < (uintptr_t)destination_directory
                                   ? source_directory
                                   : destination_directory;
    inode_t *second_directory =
        first_directory == source_directory ? destination_directory : source_directory;
    bool first_interrupts = spinlock_acquire(&first_directory->lock);
    bool second_interrupts = false;

    if (first_directory != second_directory) {
        second_interrupts = spinlock_acquire(&second_directory->lock);
    }

    tmpfs_dir_t *source_data = source_directory->private;
    tmpfs_dir_t *destination_data = destination_directory->private;
    tmpfs_dentry_t *source_entry = find_directory_entry(source_data, source_name);
    tmpfs_dentry_t *destination_entry = find_directory_entry(destination_data, destination_name);
    errno_t status = EOK;
    char *previous_name = NULL;
    tmpfs_dentry_t *displaced_entry = NULL;

    if (!source_entry) {
        status = -ENOENT;
    } else if (source_entry == destination_entry) { /* Existing identical name is a no-op. */
    } else if (S_ISDIR(source_entry->inode->mode)) {
        status = -ENOSYS; /* directory cycle checks not implemented */
    } else if (destination_entry && S_ISDIR(destination_entry->inode->mode)) {
        status = -EISDIR;
    } else {
        if (destination_entry) {
            list_remove(&destination_entry->node);
            --destination_data->entry_count;
            displaced_entry = destination_entry;
        }

        list_remove(&source_entry->node);
        --source_data->entry_count;
        previous_name = source_entry->name;
        source_entry->name = replacement_name;
        replacement_name = NULL;
        list_push_back(&destination_data->entries, &source_entry->node);
        ++destination_data->entry_count;
    }

    if (first_directory != second_directory) {
        spinlock_release(&second_directory->lock, second_interrupts);
    }

    spinlock_release(&first_directory->lock, first_interrupts);
    free(replacement_name);
    free(previous_name);

    if (displaced_entry) {
        inode_unref(displaced_entry->inode);
        free(displaced_entry->name);
        free(displaced_entry);
    }

    return status;
}

static const inode_ops_t tmpfs_dir_inode_ops = {
    .lookup = tmpfs_dir_lookup,
    .create = tmpfs_dir_create,
    .mkdir = tmpfs_dir_mkdir,
    .unlink = tmpfs_dir_unlink,
    .readdir = tmpfs_dir_readdir,
    .open = tmpfs_dir_open,
    .stat = tmpfs_dir_stat,
    .destroy = tmpfs_dir_destroy,
    .rename = tmpfs_dir_rename,
};

/* File inode operations */

/* Apply truncation under the inode lock before allocating an open-file description. */
static errno_t tmpfs_file_open(inode_t *inode, int flags, file_t **file_out) {
    if (flags & O_TRUNC) {
        tmpfs_file_t *file_data = (tmpfs_file_t *)inode->private;
        bool interrupts_enabled = spinlock_acquire(&inode->lock);
        file_data->size = 0;
        inode->size = 0;
        spinlock_release(&inode->lock, interrupts_enabled);
    }

    file_t *file = file_alloc(inode, flags, &tmpfs_file_operations);

    if (!file) {
        return -ENOSPC;
    }
    *file_out = file;

    return EOK;
}

/* Snapshot regular-file metadata under the inode lock. */
static errno_t tmpfs_file_stat(inode_t *inode, vfs_stat_t *stat_out) {
    bool interrupts_enabled = spinlock_acquire(&inode->lock);
    stat_out->ino = inode->ino;
    stat_out->mode = inode->mode;
    stat_out->nlink = inode->nlink;
    stat_out->uid = inode->uid;
    stat_out->gid = inode->gid;
    stat_out->size = inode->size;
    stat_out->blksize = 4096;
    stat_out->blocks = (int64_t)((inode->size + 511) / 512);
    stat_out->rdev = 0;
    spinlock_release(&inode->lock, interrupts_enabled);

    return EOK;
}

/* Free the content buffer when the final inode reference is released. */
static void tmpfs_file_destroy(inode_t *inode) {
    tmpfs_file_t *file_data = (tmpfs_file_t *)inode->private;

    if (!file_data) {
        return;
    }

    if (file_data->buffer) {
        free(file_data->buffer);
    }

    free(file_data);
    inode->private = NULL;
}

/* Resize visible content, zeroing extensions while retaining capacity after shrink. */
static errno_t tmpfs_file_truncate(inode_t *inode, uint64_t new_size) {
    tmpfs_file_t *file_data = (tmpfs_file_t *)inode->private;
    bool interrupts_enabled = spinlock_acquire(&inode->lock);

    if (new_size > file_data->size) {
        /* Extend: reserve and zero the gap */
        errno_t status = tmpfs_file_reserve(file_data, (size_t)new_size);

        if (status < 0) {
            spinlock_release(&inode->lock, interrupts_enabled);

            return status;
        }

        if (new_size > file_data->size) {
            __builtin_memset(
                file_data->buffer + file_data->size, 0, (size_t)(new_size - file_data->size));
        }

        file_data->size = (size_t)new_size;
        inode->size = new_size;
    } else if (new_size < file_data->size) {
        /* Shrink: just update the length; don't free memory */
        file_data->size = (size_t)new_size;
        inode->size = new_size;
    }

    spinlock_release(&inode->lock, interrupts_enabled);

    return EOK;
}

static const inode_ops_t tmpfs_file_inode_ops = {
    .lookup = NULL,
    .create = NULL,
    .mkdir = NULL,
    .unlink = NULL,
    .readdir = NULL,
    .open = tmpfs_file_open,
    .stat = tmpfs_file_stat,
    .destroy = tmpfs_file_destroy,
    .truncate = tmpfs_file_truncate,
};

/* File operations (for open file_t) */

/* Lock the open-file position before inode data, then advance by bytes actually read. */
static errno_t tmpfs_fops_read(
    file_t *file, void *buffer, size_t byte_count, size_t *bytes_transferred) {
    inode_t *inode = file->inode;
    tmpfs_file_t *file_data = (tmpfs_file_t *)inode->private;
    *bytes_transferred = 0;

    bool file_interrupts_enabled = spinlock_acquire(&file->lock);
    bool inode_interrupts_enabled = spinlock_acquire(&inode->lock);

    int64_t position = file->pos;

    if (position < 0 || (uint64_t)position >= file_data->size) {
        spinlock_release(&inode->lock, inode_interrupts_enabled);
        spinlock_release(&file->lock, file_interrupts_enabled);

        return EOK; /* EOF */
    }

    size_t available_bytes = file_data->size - (size_t)position;
    size_t bytes_to_read = byte_count < available_bytes ? byte_count : available_bytes;
    __builtin_memcpy(buffer, file_data->buffer + position, bytes_to_read);
    file->pos += (int64_t)bytes_to_read;
    *bytes_transferred = bytes_to_read;

    spinlock_release(&inode->lock, inode_interrupts_enabled);
    spinlock_release(&file->lock, file_interrupts_enabled);

    return EOK;
}

/* Lock position before inode data; append selects EOF while the inode remains locked. */
static errno_t tmpfs_fops_write(
    file_t *file, const void *buffer, size_t byte_count, size_t *bytes_transferred) {
    inode_t *inode = file->inode;
    tmpfs_file_t *file_data = (tmpfs_file_t *)inode->private;
    *bytes_transferred = 0;

    if (byte_count == 0) {
        return EOK;
    }

    bool file_interrupts_enabled = spinlock_acquire(&file->lock);
    bool inode_interrupts_enabled = spinlock_acquire(&inode->lock);

    int64_t position = file->pos;

    if (file->flags & O_APPEND) {
        position = (int64_t)file_data->size;
    }

    if (position < 0 || byte_count > SIZE_MAX - (uint64_t)position) {
        spinlock_release(&inode->lock, inode_interrupts_enabled);
        spinlock_release(&file->lock, file_interrupts_enabled);

        return -EOVERFLOW;
    }

    size_t write_end = (size_t)position + byte_count;
    errno_t status = tmpfs_file_reserve(file_data, write_end);

    if (status < 0) {
        spinlock_release(&inode->lock, inode_interrupts_enabled);
        spinlock_release(&file->lock, file_interrupts_enabled);

        return status;
    }

    if ((uint64_t)position > file_data->size) {
        __builtin_memset(file_data->buffer + file_data->size, 0, position - file_data->size);
    }

    __builtin_memcpy(file_data->buffer + position, buffer, byte_count);

    if (write_end > file_data->size) {
        file_data->size = write_end;
        inode->size = write_end;
    }

    file->pos = (int64_t)write_end;
    *bytes_transferred = byte_count;

    spinlock_release(&inode->lock, inode_interrupts_enabled);
    spinlock_release(&file->lock, file_interrupts_enabled);

    return EOK;
}

/* Resolve the requested origin under both locks and reject negative resulting positions. */
static errno_t tmpfs_fops_seek(
    file_t *file, int64_t offset, int whence, int64_t *new_position_out) {
    inode_t *inode = file->inode;
    tmpfs_file_t *file_data = (tmpfs_file_t *)inode->private;

    bool file_interrupts_enabled = spinlock_acquire(&file->lock);
    bool inode_interrupts_enabled = spinlock_acquire(&inode->lock);

    int64_t base;

    switch (whence) {
    case SEEK_SET:
        base = 0;
        break;
    case SEEK_CUR:
        base = file->pos;
        break;
    case SEEK_END:
        base = (int64_t)file_data->size;
        break;
    default:
        spinlock_release(&inode->lock, inode_interrupts_enabled);
        spinlock_release(&file->lock, file_interrupts_enabled);

        return -EINVAL;
    }

    if ((offset > 0 && base > INT64_MAX - offset) || (offset < 0 && base < INT64_MIN - offset)) {
        spinlock_release(&inode->lock, inode_interrupts_enabled);
        spinlock_release(&file->lock, file_interrupts_enabled);

        return -EOVERFLOW;
    }

    int64_t new_position = base + offset;

    if (new_position < 0) {
        spinlock_release(&inode->lock, inode_interrupts_enabled);
        spinlock_release(&file->lock, file_interrupts_enabled);

        return -EINVAL;
    }

    file->pos = new_position;
    *new_position_out = new_position;

    spinlock_release(&inode->lock, inode_interrupts_enabled);
    spinlock_release(&file->lock, file_interrupts_enabled);

    return EOK;
}

/* A positioned read holds only the inode lock, leaving file->pos unchanged. */
static errno_t tmpfs_read_at(
    file_t *file, void *buffer, size_t length, int64_t offset, size_t *completed) {
    tmpfs_file_t *data = file->inode->private;
    bool interrupts_enabled = spinlock_acquire(&file->inode->lock);
    size_t available = (uint64_t)offset < data->size ? data->size - offset : 0;
    *completed = length < available ? length : available;

    if (*completed) {
        __builtin_memcpy(buffer, data->buffer + offset, *completed);
    }

    spinlock_release(&file->inode->lock, interrupts_enabled);

    return EOK;
}

/* Snapshot status flags under the file lock, then serialize content growth. */
static errno_t tmpfs_write_at(
    file_t *file, const void *buffer, size_t length, int64_t offset, size_t *completed) {
    bool file_interrupts = spinlock_acquire(&file->lock);
    bool inode_interrupts = spinlock_acquire(&file->inode->lock);
    tmpfs_file_t *data = file->inode->private;

    if (file->flags & O_APPEND) {
        offset = data->size;
    }

    errno_t status = EOK;

    if (length > SIZE_MAX - (uint64_t)offset) {
        status = -EOVERFLOW;
    } else if (length) {
        size_t end = offset + length;
        status = tmpfs_file_reserve(data, end);

        if (!status) {
            /* A prior truncate may leave stale bytes in retained capacity. */
            if ((uint64_t)offset > data->size) {
                __builtin_memset(data->buffer + data->size, 0, offset - data->size);
            }

            __builtin_memcpy(data->buffer + offset, buffer, length);

            if (end > data->size) {
                data->size = end;
                file->inode->size = end;
            }
            *completed = length;
        }
    }

    spinlock_release(&file->inode->lock, inode_interrupts);
    spinlock_release(&file->lock, file_interrupts);

    return status;
}

static const file_ops_t tmpfs_file_operations = {
    .read = tmpfs_fops_read,
    .read_at = tmpfs_read_at,
    .write_at = tmpfs_write_at,
    .write = tmpfs_fops_write,
    .seek = tmpfs_fops_seek,
    .ioctl = NULL,
    .close = NULL, /* no extra cleanup; file_unref handles inode_unref + free */
};

/* Public entry point */

/* Return one owned reference to an empty, writable root directory. */
inode_t *tmpfs_create_root(void) {
    return tmpfs_alloc_inode(S_IFDIR | 0755u);
}
