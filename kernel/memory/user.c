#include <kernel/user.h>
#include <kernel/arm64_mmu.h>
#include <kernel/heap.h>
#include <kernel/page_memory.h>
#include <kernel/panic.h>
#include <kernel/strings.h>

#define PAGE_ADDRESS ARM64_DESCRIPTOR_ADDRESS
#define PAGE_VALID ARM64_DESCRIPTOR_TABLE_OR_PAGE
#define USER_LEAF_TABLE_COUNT (ARM64_USER_LIMIT >> ARM64_LEVEL_TWO_SHIFT)

/* Each reservation owns one page, even while nonresident or PROT_NONE. Keeping
 * ownership separate from the PTE prevents permission changes losing frames. */
typedef struct user_page {
    uintptr_t address;
    paddr_t physical;
    unsigned int protection;
    struct user_page *next;
} user_page_t;
struct user_address_space {
    uint64_t *root, *level_one, *level_two;
    user_page_t *pages;
};

static uint64_t *kernel_root;

/* Validate before adding lengths, so wrapping or privileged ranges never pass. */
static bool valid_range(uintptr_t address, size_t length) {
    return address >= ARM64_USER_MIN && address < ARM64_USER_LIMIT && length &&
           !(address & (KERNEL_PAGE_SIZE - 1)) && !(length & (KERNEL_PAGE_SIZE - 1)) &&
           length <= ARM64_USER_LIMIT - address;
}

/* This baseline supports readable, writable and executable anonymous memory,
 * but never a writable executable page. Read is implicit for write/execute. */
static bool valid_protection(unsigned int protection) {
    return !(protection & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) &&
           (protection & (PROT_WRITE | PROT_EXEC)) != (PROT_WRITE | PROT_EXEC);
}

/* Caller holds the process lock for every page metadata walk. */
static user_page_t *find_page(process_t *process, uintptr_t address) {
    address &= ~(KERNEL_PAGE_SIZE - 1);

    for (user_page_t *page = process->address_space->pages; page; page = page->next) {
        if (page->address == address) {
            return page;
        }
    }

    return NULL;
}

/* All allocated tables are zeroed before a descriptor can expose them. */
static uint64_t *allocate_table(void) {
    paddr_t physical;

    if (!page_memory_allocate(1, &physical)) {
        return NULL;
    }

    uint64_t *table = page_memory_pointer(physical);
    memset(table, 0, KERNEL_PAGE_SIZE);

    return table;
}

/* User virtual addresses occupy only the previously empty first 128 MiB.
 * The rest of the identity map remains privileged and shared, immutable RAM. */
static uint64_t *page_slot(process_t *process, uintptr_t address, bool create) {
    uint64_t *descriptor = &process->address_space->level_two[address >> ARM64_LEVEL_TWO_SHIFT];

    if (!*descriptor && create) {
        uint64_t *table = allocate_table();

        if (!table) {
            return NULL;
        }

        __asm__ volatile("dsb ishst" ::: "memory");
        *descriptor = page_memory_physical(table) | PAGE_VALID;
        __asm__ volatile("dsb ishst" ::: "memory");
    }

    if (!*descriptor) {
        return NULL;
    }

    uint64_t *table = page_memory_pointer(*descriptor & PAGE_ADDRESS);

    return &table[(address >> ARM64_PAGE_SHIFT) & ARM64_TABLE_INDEX_MASK];
}

/* Broadcast to all ASIDs on all inner-shareable CPUs. Completion precedes page
 * reuse; break-before-make also handles concurrent permission revocation. */
static void invalidate_page(uintptr_t address) {
    __asm__ volatile("dsb ishst; tlbi vaae1is, %0; dsb ish; isb" ::"r"(address >> ARM64_PAGE_SHIFT)
        : "memory");
}

/* Synchronize instruction fetch through every virtual alias, even if the page
 * was populated while writable and is becoming executable only now. */
static void synchronize_code(paddr_t physical) {
    uintptr_t cache_type;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(cache_type));

    size_t line_size = 4UL << ((cache_type >> 16) & 15);
    uintptr_t physical_pointer = (uintptr_t)page_memory_pointer(physical);

    for (size_t offset = 0; offset < KERNEL_PAGE_SIZE; offset += line_size) {
        __asm__ volatile("dc cvau, %0" ::"r"(physical_pointer + offset) : "memory");
    }

    __asm__ volatile("dsb ish; ic ialluis; dsb ish; isb" ::: "memory");
}

/* PXN always prevents kernel execution of user code. nG keeps kernel global
 * mappings distinct; ASID zero is flushed locally on address-space switches. */
static void publish_page(process_t *process, user_page_t *page) {
    uint64_t *slot = page_slot(process, page->address, false);

    if (!slot) {
        return;
    }

    if (*slot) {
        *slot = 0;
        invalidate_page(page->address);
    }

    if (page->physical && page->protection) {
        if (page->protection & PROT_EXEC) {
            synchronize_code(page->physical);
        }

        uint64_t descriptor = page->physical | ARM64_DESCRIPTOR_TABLE_OR_PAGE |
                              ARM64_PAGE_NORMAL_MEMORY | ARM64_PAGE_USER_ACCESS |
                              ARM64_PAGE_INNER_SHAREABLE | ARM64_PAGE_ACCESS_FLAG |
                              ARM64_PAGE_NOT_GLOBAL | ARM64_PAGE_PRIVILEGED_NO_EXECUTE;
        if (!(page->protection & PROT_WRITE)) {
            descriptor |= ARM64_PAGE_READ_ONLY;
        }

        if (!(page->protection & PROT_EXEC)) {
            descriptor |= ARM64_PAGE_USER_NO_EXECUTE;
        }
        *slot = descriptor;
        invalidate_page(page->address);
    }
}

/* Allocate on the first permitted access. No user pointer is dereferenced. */
static bool ensure_page(process_t *process, user_page_t *page, unsigned int access) {
    if (!page || !(page->protection & access)) {
        return false;
    }

    if (!page->physical) {
        if (!page_slot(process, page->address, true) || !page_memory_allocate(1, &page->physical)) {
            return false;
        }

        memset(page_memory_pointer(page->physical), 0, KERNEL_PAGE_SIZE);
        publish_page(process, page);
        ++process->demand_faults;
    }

    return true;
}

/* Scheduler initialization captures the permanent root before SMP publication. */
void arm64_user_initialize(void) {
    KERNEL_ASSERT(!kernel_root);

    uintptr_t root_address;
    __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(root_address));
    kernel_root = page_memory_pointer(root_address & PAGE_ADDRESS);
}

/* Clone the private ancestors of the user window; shared kernel branches
 * remain borrowed and immutable for the lifetime of the kernel. */
struct user_address_space *arm64_user_space_create(void) {
    KERNEL_ASSERT(kernel_root);
    struct user_address_space *space = malloc(sizeof(*space));

    if (!space) {
        return NULL;
    }

    space->root = allocate_table();
    space->level_one = allocate_table();
    space->level_two = allocate_table();

    if (!space->root || !space->level_one || !space->level_two) {
        arm64_user_space_destroy(space);

        return NULL;
    }

    memcpy(space->root, kernel_root, KERNEL_PAGE_SIZE);
    uint64_t *kernel_level_one = page_memory_pointer(kernel_root[0] & PAGE_ADDRESS);
    memcpy(space->level_one, kernel_level_one, KERNEL_PAGE_SIZE);
    uint64_t *kernel_level_two = page_memory_pointer(kernel_level_one[0] & PAGE_ADDRESS);
    memcpy(space->level_two, kernel_level_two, KERNEL_PAGE_SIZE);

    for (unsigned int table_index = 0; table_index < USER_LEAF_TABLE_COUNT; ++table_index) {
        KERNEL_ASSERT(!space->level_two[table_index]);
    }

    space->root[0] = page_memory_physical(space->level_one) | PAGE_VALID;
    space->level_one[0] = page_memory_physical(space->level_two) | PAGE_VALID;

    return space;
}

/* Free only owned user frames/tables. Caller guarantees no live translations
 * or threads can still reference this unpublished or fully reaped space. */
void arm64_user_space_destroy(struct user_address_space *space) {
    if (!space) {
        return;
    }
    while (space->pages) {
        user_page_t *page = space->pages;
        space->pages = page->next;

        if (page->physical) {
            page_memory_release(page->physical, 1);
        }

        free(page);
    }

    if (space->level_two) {
        for (unsigned int table_index = 0; table_index < USER_LEAF_TABLE_COUNT; ++table_index) {
            if (space->level_two[table_index]) {
                page_memory_release(space->level_two[table_index] & PAGE_ADDRESS, 1);
            }
        }
    }

    if (space->root) {
        page_memory_release(page_memory_physical(space->root), 1);
    }

    if (space->level_one) {
        page_memory_release(page_memory_physical(space->level_one), 1);
    }

    if (space->level_two) {
        page_memory_release(page_memory_physical(space->level_two), 1);
    }

    free(space);
}

/* A NULL space selects the permanent kernel root for kernel-only threads. */
uint64_t *arm64_user_root(const struct user_address_space *space) {
    return space ? space->root : kernel_root;
}

/* Prepare all metadata first: overlap/OOM leaves the existing map unchanged. */
bool arm64_user_map(process_t *process, uintptr_t address, size_t length, unsigned int protection) {
    if (!process || !process->address_space || !valid_range(address, length) ||
        !valid_protection(protection)) {
        return false;
    }

    user_page_t *prepared = NULL;

    for (size_t offset = 0; offset < length; offset += KERNEL_PAGE_SIZE) {
        user_page_t *page = malloc(sizeof(*page));

        if (!page) {
            while (prepared) {
                page = prepared;
                prepared = page->next;
                free(page);
            }

            return false;
        }

        page->address = address + offset;
        page->protection = protection ? protection | PROT_READ : 0;
        page->next = prepared;
        prepared = page;
    }

    bool saved_interrupts = spinlock_acquire(&process->lock);
    bool success = true;

    for (user_page_t *page = prepared; page; page = page->next) {
        if (find_page(process, page->address)) {
            success = false;
            break;
        }
    }
    while (prepared) {
        user_page_t *page = prepared;
        prepared = page->next;

        if (success) {
            page->next = process->address_space->pages;
            process->address_space->pages = page;
        } else {
            free(page);
        }
    }

    spinlock_release(&process->lock, saved_interrupts);

    return success;
}

/* A whole-range preflight makes partial protection failures atomic to callers. */
bool arm64_user_protect(
    process_t *process, uintptr_t address, size_t length, unsigned int protection) {
    if (!process || !process->address_space || !valid_range(address, length) ||
        !valid_protection(protection)) {
        return false;
    }

    bool saved_interrupts = spinlock_acquire(&process->lock);

    for (size_t offset = 0; offset < length; offset += KERNEL_PAGE_SIZE) {
        if (!find_page(process, address + offset)) {
            spinlock_release(&process->lock, saved_interrupts);

            return false;
        }
    }

    for (size_t offset = 0; offset < length; offset += KERNEL_PAGE_SIZE) {
        user_page_t *page = find_page(process, address + offset);
        page->protection = protection ? protection | PROT_READ : 0;
        publish_page(process, page);
    }

    spinlock_release(&process->lock, saved_interrupts);

    return true;
}

/* Revoke translations synchronously before returning physical pages to PMM. */
bool arm64_user_unmap(process_t *process, uintptr_t address, size_t length) {
    if (!process || !process->address_space || !valid_range(address, length)) {
        return false;
    }

    bool saved_interrupts = spinlock_acquire(&process->lock);
    user_page_t **previous = &process->address_space->pages;

    while (*previous) {
        user_page_t *page = *previous;

        if (page->address >= address && page->address - address < length) {
            page->protection = 0;
            publish_page(process, page);

            if (page->physical) {
                page_memory_release(page->physical, 1);
            }
            *previous = page->next;
            free(page);
        } else {
            previous = &page->next;
        }
    }

    spinlock_release(&process->lock, saved_interrupts);

    return true;
}

/* Address checks share the same lock as revocation and demand resolution. */
bool arm64_user_access(process_t *process, uintptr_t address, unsigned int access) {
    if (!process || !process->address_space || address >= ARM64_USER_LIMIT) {
        return false;
    }

    bool saved_interrupts = spinlock_acquire(&process->lock);
    bool success = ensure_page(process, find_page(process, address), access);
    spinlock_release(&process->lock, saved_interrupts);

    return success;
}

/* Copy through privileged physical aliases, including when this process is not
 * active on the calling CPU. Loader access never bypasses live permissions. */
bool arm64_user_copy(process_t *process, uintptr_t address, void *buffer, size_t length,
    bool write_user, bool loader) {
    if (!process || !process->address_space || address < ARM64_USER_MIN ||
        address >= ARM64_USER_LIMIT || length > ARM64_USER_LIMIT - address) {
        return false;
    }

    bool saved_interrupts = spinlock_acquire(&process->lock);

    if (loader && process->thread_count) {
        spinlock_release(&process->lock, saved_interrupts);

        return false;
    }

    size_t copied = 0;

    while (copied < length) {
        user_page_t *page = find_page(process, address + copied);
        unsigned int access = loader ? PROT_READ : write_user ? PROT_WRITE : PROT_READ;

        if (!ensure_page(process, page, access)) {
            break;
        }

        size_t page_offset = (address + copied) & (KERNEL_PAGE_SIZE - 1);
        size_t chunk = KERNEL_PAGE_SIZE - page_offset;

        if (chunk > length - copied) {
            chunk = length - copied;
        }

        void *physical_pointer = (char *)page_memory_pointer(page->physical) + page_offset;

        if (write_user) {
            memcpy(physical_pointer, (char *)buffer + copied, chunk);
        } else {
            memcpy((char *)buffer + copied, physical_pointer, chunk);
        }

        if (write_user && (page->protection & PROT_EXEC)) {
            synchronize_code(page->physical);
        }

        copied += chunk;
    }

    spinlock_release(&process->lock, saved_interrupts);

    return copied == length;
}

/* Reserve a page-rounded region; automatic placement consumes a monotonic
 * watermark. Explicit hints are exact and fail on overlap in this baseline. */
uintptr_t process_mmap(process_t *process, uintptr_t hint, size_t length, uint32_t protection) {
    if (!process || !length || length > ARM64_USER_LIMIT - (KERNEL_PAGE_SIZE - 1)) {
        return 0;
    }

    length = (length + (KERNEL_PAGE_SIZE - 1)) & ~(KERNEL_PAGE_SIZE - 1);

    if (!hint) {
        bool saved_interrupts = spinlock_acquire(&process->lock);
        hint = process->mmap_base;

        if (!valid_range(hint, length)) {
            hint = 0;
        } else {
            process->mmap_base += length;
        }

        spinlock_release(&process->lock, saved_interrupts);
    }

    return arm64_user_map(process, hint, length, protection) ? hint : 0;
}

/* Arbitrary aligned subranges can be removed without splitting allocations. */
void process_munmap(process_t *process, uintptr_t address, size_t length) {
    if (length && length <= ARM64_USER_LIMIT - (KERNEL_PAGE_SIZE - 1)) {
        arm64_user_unmap(
            process, address, (length + (KERNEL_PAGE_SIZE - 1)) & ~(KERNEL_PAGE_SIZE - 1));
    }
}

/* A missing reservation at the page below this range forms the stack guard. */
uintptr_t process_alloc_ustack(process_t *process) {
    return arm64_user_map(process, USTACK_TOP - USTACK_SIZE, USTACK_SIZE, PROT_READ | PROT_WRITE)
               ? USTACK_TOP
               : 0;
}

/* Return physical ownership only after resolving a readable anonymous page. */
uintptr_t process_ensure_page(process_t *process, uintptr_t address) {
    if (!process || !process->address_space) {
        return 0;
    }

    bool saved_interrupts = spinlock_acquire(&process->lock);
    user_page_t *page = find_page(process, address);
    uintptr_t physical = ensure_page(process, page, PROT_READ) ? page->physical : 0;
    spinlock_release(&process->lock, saved_interrupts);

    return physical;
}

/* Preserve the common negative-errno convention; ARM additionally rejects W+X. */
errno_t process_mprotect(
    process_t *process, uintptr_t address, size_t length, uint32_t protection) {
    if (!length || length > ARM64_USER_LIMIT - (KERNEL_PAGE_SIZE - 1)) {
        return -EINVAL;
    }

    return arm64_user_protect(process, address,
               (length + (KERNEL_PAGE_SIZE - 1)) & ~(KERNEL_PAGE_SIZE - 1), protection)
               ? 0
               : -EINVAL;
}

/* Preflight an entire copy range before a syscall can consume input or modify
 * filesystem state. Subsequent copies still recheck under the mapping lock. */
bool arm64_user_range(process_t *process, uintptr_t address, size_t length, unsigned int access) {
    if (!length) {
        return true;
    }

    if (!process || !process->address_space || address < ARM64_USER_MIN ||
        address >= ARM64_USER_LIMIT || length > ARM64_USER_LIMIT - address) {
        return false;
    }

    bool interrupts_enabled = spinlock_acquire(&process->lock);
    bool success = true;
    uintptr_t end = address + length;

    for (uintptr_t cursor = address & ~(KERNEL_PAGE_SIZE - 1); cursor < end;
        cursor += KERNEL_PAGE_SIZE) {
        if (!ensure_page(process, find_page(process, cursor), access)) {
            success = false;
            break;
        }
    }

    spinlock_release(&process->lock, interrupts_enabled);

    return success;
}

/* Eager fork of a single-threaded owner. The caller is its only user thread;
 * its process lock excludes mapping/copy operations during this snapshot.
 * Child remains unpublished, and failure leaves all storage child-owned. */
bool arm64_user_clone(process_t *source, process_t *destination) {
    bool saved_interrupts = spinlock_acquire(&source->lock);
    bool success = true;

    for (user_page_t *page = source->address_space->pages; page; page = page->next) {
        if (!arm64_user_map(destination, page->address, KERNEL_PAGE_SIZE, page->protection)) {
            success = false;
            break;
        }

        user_page_t *copy = find_page(destination, page->address);

        if (page->physical) {
            if (!page_slot(destination, copy->address, true) ||
                !page_memory_allocate(1, &copy->physical)) {
                success = false;
                break;
            }

            memcpy(page_memory_pointer(copy->physical), page_memory_pointer(page->physical),
                KERNEL_PAGE_SIZE);
            publish_page(destination, copy);
        }
    }

    spinlock_release(&source->lock, saved_interrupts);

    return success;
}
