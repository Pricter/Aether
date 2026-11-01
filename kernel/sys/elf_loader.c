#include <kernel/userspace.h>
#include <kernel/strings.h>
#include <kernel/heap.h>
#include <kernel/page_memory.h>
#include <elf.h>

/* Bootstrap policy limits, distinct from the ELF wire-format values. */
#define ELF_ARGUMENT_MAXIMUM 63UL
#define ELF_PROGRAM_LIMIT 128U
#define ELF_TLS_LIMIT (1024UL * 1024)
#define ELF_MMAP_GAP (1024UL * 1024)
#define AARCH64_INSTRUCTION_BYTES 4UL

/* ELF64 wire structs use fixed-width fields; all arithmetic is range checked
 * before dereferencing the borrowed image, which may be an untrusted file. */
typedef struct {
    unsigned char identity[16];
    uint16_t type, machine;
    uint32_t version;
    uint64_t entry, program_offset, section_offset;
    uint32_t flags;
    uint16_t header_size, program_size, program_count, section_size, section_count, names;
} elf_header_t;
typedef struct {
    uint32_t type, flags;
    uint64_t offset, address, physical, file_size, memory_size, alignment;
} elf_program_t;
_Static_assert(sizeof(elf_header_t) == 64 && sizeof(elf_program_t) == 56, "ELF64 layouts");

/* ELF auxiliary-vector tags, each followed by its corresponding value. */
typedef enum {
    AT_NULL = 0,
    AT_PHDR = 3,
    AT_PHENT = 4,
    AT_PHNUM = 5,
    AT_PAGESZ = 6,
    AT_BASE = 7,
    AT_ENTRY = 9,
    AT_UID = 11,
    AT_EUID = 12,
    AT_GID = 13,
    AT_EGID = 14,
    AT_HWCAP = 16,
    AT_CLKTCK = 17,
    AT_SECURE = 23,
    AT_RANDOM = 25,
    AT_EXECFN = 31,
} elf_auxiliary_tag_t;

typedef struct {
    uint64_t type;
    uint64_t value;
} elf_auxiliary_entry_t;

#define INITIAL_AUXILIARY_COUNT 16
#define INITIAL_STRINGS_OFFSET 1280
#define INITIAL_NONCE_OFFSET (KERNEL_PAGE_SIZE - 16)
#define BOOTSTRAP_NONCE_MIX 0x9e3779b97f4a7c15UL

/* Check an image slice without forming an overflowing end address. */
static bool image_slice(size_t length, uint64_t offset, uint64_t count) {
    return offset <= length && count <= length - offset;
}

/* Copy program headers instead of dereferencing possibly unaligned image data. */
static elf_program_t program_at(
    const unsigned char *image, const elf_header_t *header, unsigned int index) {
    elf_program_t program;
    memcpy(&program, image + header->program_offset + index * sizeof(program), sizeof(program));

    return program;
}

/* A static executable must have readable, file-backed program headers for libc
 * TLS discovery and an entry inside executable bytes, never writable data. */
process_t *arm64_elf_load_vectors(const void *image_pointer, size_t length, const char *path,
    const char *const *arguments, const char *const *environment, process_t *parent,
    thread_t **thread_out, errno_t *error_out) {
    *thread_out = NULL;
    *error_out = -ENOEXEC;
    const unsigned char *image = image_pointer;
    elf_header_t header;

    if (!image || length < sizeof(header) || !path || !arguments || !environment) {
        return NULL;
    }

    memcpy(&header, image, sizeof(header));

    if (memcmp(header.identity, "\177ELF", 4) || header.identity[EI_CLASS] != ELFCLASS64 ||
        header.identity[EI_DATA] != ELFDATA2LSB || header.identity[EI_VERSION] != EV_CURRENT ||
        header.type != ET_EXEC || header.machine != EM_AARCH64 || header.version != EV_CURRENT ||
        header.header_size != sizeof(header) || header.program_size != sizeof(elf_program_t) ||
        !header.program_count || header.program_count > ELF_PROGRAM_LIMIT ||
        !image_slice(length, header.program_offset, header.program_count * sizeof(elf_program_t)) ||
        (header.entry & (AARCH64_INSTRUCTION_BYTES - 1))) {
        return NULL;
    }

    uintptr_t program_headers = 0, highest_end = 0;
    bool entry_valid = false;

    for (unsigned int index = 0; index < header.program_count; ++index) {
        elf_program_t program = program_at(image, &header, index);

        if (program.type == PT_INTERP || program.type == PT_DYNAMIC ||
            (program.type == PT_GNU_STACK && (program.flags & PF_X))) {
            return NULL;
        }

        if (program.type != PT_LOAD || !program.memory_size) {
            continue;
        }

        if (program.flags & ~(PF_R | PF_W | PF_X) || !(program.flags & PF_R) ||
            (program.flags & (PF_W | PF_X)) == (PF_W | PF_X) ||
            program.file_size > program.memory_size ||
            !image_slice(length, program.offset, program.file_size) ||
            program.address < ARM64_USER_MIN ||
            program.address >= USTACK_TOP - USTACK_SIZE - KERNEL_PAGE_SIZE ||
            program.memory_size > USTACK_TOP - USTACK_SIZE - KERNEL_PAGE_SIZE - program.address ||
            (program.address & (KERNEL_PAGE_SIZE - 1)) !=
                (program.offset & (KERNEL_PAGE_SIZE - 1)) ||
            (program.alignment > 1 &&
                ((program.alignment & (program.alignment - 1)) ||
                    ((program.address - program.offset) & (program.alignment - 1))))) {
            return NULL;
        }

        uintptr_t first = program.address & ~(KERNEL_PAGE_SIZE - 1);
        uintptr_t end = (program.address + program.memory_size + (KERNEL_PAGE_SIZE - 1)) &
                        ~(KERNEL_PAGE_SIZE - 1);
        for (unsigned int previous_index = 0; previous_index < index; ++previous_index) {
            elf_program_t previous = program_at(image, &header, previous_index);

            if (previous.type == PT_LOAD && previous.memory_size &&
                first < ((previous.address + previous.memory_size + (KERNEL_PAGE_SIZE - 1)) &
                            ~(KERNEL_PAGE_SIZE - 1)) &&
                (previous.address & ~(KERNEL_PAGE_SIZE - 1)) < end) {
                return NULL;
            }
        }

        if (end > highest_end) {
            highest_end = end;
        }

        if ((program.flags & PF_X) && header.entry >= program.address &&
            header.entry - program.address < program.file_size) {
            entry_valid = true;
        }

        size_t table_length = header.program_count * sizeof(elf_program_t);

        if (header.program_offset >= program.offset &&
            header.program_offset - program.offset <= program.file_size &&
            table_length <= program.file_size - (header.program_offset - program.offset)) {
            program_headers = program.address + header.program_offset - program.offset;
        }
    }

    /* TLS descriptors are consumed by libc, so validate their slices and
     * require their initialized bytes to belong to a readable loaded segment. */
    for (unsigned int index = 0; index < header.program_count; ++index) {
        elf_program_t tls = program_at(image, &header, index);

        if (tls.type != PT_TLS) {
            continue;
        }

        if (tls.file_size > tls.memory_size || tls.memory_size > ELF_TLS_LIMIT ||
            tls.address < ARM64_USER_MIN || tls.address >= ARM64_USER_LIMIT ||
            tls.memory_size > ARM64_USER_LIMIT - tls.address ||
            !image_slice(length, tls.offset, tls.file_size) || tls.alignment > KERNEL_PAGE_SIZE ||
            (tls.alignment && (tls.alignment & (tls.alignment - 1)))) {
            return NULL;
        }

        /* Pure .tbss need not occupy a PT_LOAD range: libc allocates it. */
        bool contained = !tls.file_size;

        for (unsigned int load_index = 0; load_index < header.program_count; ++load_index) {
            elf_program_t load = program_at(image, &header, load_index);

            if (load.type == PT_LOAD && tls.address >= load.address &&
                tls.address - load.address <= load.memory_size &&
                tls.file_size <= load.memory_size - (tls.address - load.address) &&
                tls.offset >= load.offset &&
                tls.offset - load.offset == tls.address - load.address &&
                tls.offset - load.offset <= load.file_size &&
                tls.file_size <= load.file_size - (tls.offset - load.offset)) {
                contained = true;
            }
        }

        if (!contained) {
            return NULL;
        }
    }

    if (!entry_valid || !program_headers ||
        highest_end >= USTACK_TOP - USTACK_SIZE - ELF_MMAP_GAP) {
        return NULL;
    }

    *error_out = -ENOMEM;
    process_t *process = process_create("/sbin/init");

    if (!process) {
        return NULL;
    }

    for (unsigned int index = 0; index < header.program_count; ++index) {
        elf_program_t program = program_at(image, &header, index);

        if (program.type != PT_LOAD || !program.memory_size) {
            continue;
        }

        unsigned int protection = PROT_READ | ((program.flags & PF_W) ? PROT_WRITE : 0) |
                                  ((program.flags & PF_X) ? PROT_EXEC : 0);
        uintptr_t first = program.address & ~(KERNEL_PAGE_SIZE - 1);
        size_t span = ((program.address + program.memory_size + (KERNEL_PAGE_SIZE - 1)) &
                          ~(KERNEL_PAGE_SIZE - 1)) -
                      first;
        if (!arm64_user_map(process, first, span, protection) ||
            !arm64_user_copy(process, program.address, (void *)(image + program.offset),
                program.file_size, true, true)) {
            goto failed;
        }
    }

    process->brk = process->minimum_break = highest_end;

    if (process->mmap_base < highest_end + ELF_MMAP_GAP) {
        process->mmap_base = highest_end + ELF_MMAP_GAP;
    }

    if (!process_alloc_ustack(process)) {
        goto failed;
    }

    /* argc, argv[], envp[], and auxv share one aligned initial stack frame.
     * AT_RANDOM is a bootstrap nonce, not a cryptographic entropy service. */
    unsigned char stack_page[KERNEL_PAGE_SIZE] = {0};
    uintptr_t stack_base = USTACK_TOP - sizeof(stack_page);
    size_t string_offset = INITIAL_STRINGS_OFFSET;
    uint64_t vectors[INITIAL_STRINGS_OFFSET / sizeof(uint64_t)] = {0};
    size_t vector_count = 1, argument_count = 0;
    const char *const *lists[] = {arguments, environment};
    uintptr_t executable_address = 0;

    for (unsigned int list = 0; list < 2; ++list) {
        for (size_t index = 0; lists[list][index]; ++index) {
            if (index >= EXEC_VECTOR_MAXIMUM || vector_count + 2 + INITIAL_AUXILIARY_COUNT * 2 >=
                                                    INITIAL_STRINGS_OFFSET / sizeof(uint64_t)) {
                *error_out = -E2BIG;
                goto failed;
            }

            size_t bytes = strlen(lists[list][index]) + 1;

            if (bytes > INITIAL_NONCE_OFFSET - string_offset) {
                *error_out = -E2BIG;
                goto failed;
            }

            vectors[vector_count++] = stack_base + string_offset;
            memcpy(stack_page + string_offset, lists[list][index], bytes);
            string_offset += bytes;

            if (!list) {
                ++argument_count;
            }
        }

        vectors[vector_count++] = 0;
    }

    size_t path_bytes = strlen(path) + 1;

    if (path_bytes > INITIAL_NONCE_OFFSET - string_offset) {
        *error_out = -E2BIG;
        goto failed;
    }

    executable_address = stack_base + string_offset;
    memcpy(stack_page + string_offset, path, path_bytes);
    vectors[0] = argument_count;

    /* Supply 16 bootstrap bytes for libc startup. The counter/PID and mix below
     * diversify this placeholder; they do not provide cryptographic entropy. */
    uint64_t nonce;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(nonce));
    nonce ^= (uint64_t)process->pid << 32;
    memcpy(stack_page + INITIAL_NONCE_OFFSET, &nonce, sizeof(nonce));
    nonce ^= BOOTSTRAP_NONCE_MIX;
    memcpy(stack_page + INITIAL_NONCE_OFFSET + sizeof(nonce), &nonce, sizeof(nonce));

    const elf_auxiliary_entry_t auxiliary[] = {
        {AT_PHDR, program_headers},
        {AT_PHENT, sizeof(elf_program_t)},
        {AT_PHNUM, header.program_count},
        {AT_PAGESZ, KERNEL_PAGE_SIZE},
        {AT_BASE, 0},
        {AT_ENTRY, header.entry},
        {AT_UID, 0},
        {AT_EUID, 0},
        {AT_GID, 0},
        {AT_EGID, 0},
        {AT_HWCAP, 0},
        {AT_CLKTCK, 100},
        {AT_SECURE, 0},
        {AT_RANDOM, stack_base + INITIAL_NONCE_OFFSET},
        {AT_EXECFN, executable_address},
        {AT_NULL, 0},
    };

    memcpy(stack_page, vectors, vector_count * sizeof(uint64_t));
    memcpy(stack_page + vector_count * sizeof(uint64_t), auxiliary, sizeof(auxiliary));

    if (!arm64_user_copy(process, stack_base, stack_page, sizeof(stack_page), true, true)) {
        goto failed;
    }

    /* Inherit shared open descriptions; root init instead opens its own stdio. */
    if (parent && parent->address_space) {
        bool interrupts_enabled = spinlock_acquire(&parent->lock);
        memcpy(process->cwd_path, parent->cwd_path, sizeof(process->cwd_path));
        process->umask = parent->umask;
        process->cwd = parent->cwd;

        if (process->cwd) {
            inode_ref(process->cwd);
        }

        for (unsigned int descriptor = 0; descriptor < FD_TABLE_SIZE; ++descriptor) {
            if (parent->fd_table.cloexec[descriptor]) {
                continue;
            }

            process->fd_table.files[descriptor] = parent->fd_table.files[descriptor];
            process->fd_table.cloexec[descriptor] = parent->fd_table.cloexec[descriptor];

            if (process->fd_table.files[descriptor]) {
                file_ref(process->fd_table.files[descriptor]);
            }
        }

        spinlock_release(&parent->lock, interrupts_enabled);
    } else {
        vfs_setup_std_fds(process);

        if (!process->fd_table.files[0] || !process->fd_table.files[1] ||
            !process->fd_table.files[2]) {
            goto failed;
        }
    }
    *thread_out = thread_create_user(process, header.entry, stack_base);

    if (!*thread_out) {
        goto failed;
    }

    *error_out = EOK;

    return process;
failed:
    process_destroy(process);

    return NULL;
}

/* Preserve the boot/test convenience interface while the loader accepts general vectors. */
process_t *arm64_elf_load(const void *image, size_t length, const char *argument, process_t *parent,
    thread_t **thread_out) {
    if (!argument || strlen(argument) > ELF_ARGUMENT_MAXIMUM) {
        *thread_out = NULL;

        return NULL;
    }

    const char *arguments[] = {"/sbin/init", argument, NULL};
    const char *environment[] = {"AETHER_ARCH=aarch64", "PATH=/bin:/sbin", NULL};
    errno_t error;

    return arm64_elf_load_vectors(
        image, length, "/sbin/init", arguments, environment, parent, thread_out, &error);
}

/* Read a bounded executable through normal VFS ownership and checked metadata.
 * The owned file reference never consumes a user descriptor slot. */
errno_t arm64_elf_load_path(process_t *owner, const char *path, const char *const *arguments,
    const char *const *environment, process_t **process_out, thread_t **thread_out) {
    *process_out = NULL;
    *thread_out = NULL;
    file_t *file;
    errno_t status = vfs_open_readonly(owner, path, &file);

    if (status < 0) {
        return status;
    }

    vfs_stat_t metadata;
    status = file->inode->ops->stat ? file->inode->ops->stat(file->inode, &metadata) : -ENOSYS;

    if (status == EOK && (!S_ISREG(metadata.mode) || !(metadata.mode & 0111))) {
        status = -EACCES;
    }

    if (status == EOK && (!metadata.size || metadata.size > 8 * 1024 * 1024)) {
        status = -ENOEXEC;
    }

    void *image = status == EOK ? malloc(metadata.size) : NULL;

    if (status == EOK && !image) {
        status = -ENOMEM;
    }

    size_t completed = 0;

    while (status == EOK && completed < metadata.size) {
        size_t bytes = 0;
        status = file->ops && file->ops->read
                     ? file->ops->read(file, (unsigned char *)image + completed,
                           metadata.size - completed, &bytes)
                     : -ENOSYS;
        if (status == EOK && !bytes) {
            status = -ENOEXEC;
        }

        completed += bytes;
    }

    file_unref(file);

    if (status == EOK) {
        *process_out = arm64_elf_load_vectors(
            image, metadata.size, path, arguments, environment, owner, thread_out, &status);
    }

    free(image);

    return status;
}
