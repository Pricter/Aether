/* Shared heap: allocation policy is independent of physical RAM addressing.
 * Cache locks may acquire the page backend lock; the reverse is forbidden.
 * Empty slab pages stay owned by their caches for the kernel lifetime. */
#include <kernel/heap.h>
#include <kernel/page_memory.h>
#include <kernel/spinlock.h>
#include <kernel/irq_control.h>
#include <kernel/panic.h>
#include <stdint.h>
#include <stdbool.h>

#define SLAB_MAGIC 0x514b3a00U
#define LARGE_MAGIC 0xb16a7e00U
#define SLAB_MAX_SIZE 2048UL

struct slab_cache;
struct slab_page {
    uint32_t magic;
    uint16_t inuse;
    uint16_t capacity;
    struct slab_cache *cache;
    struct slab_page *next;
    void **freelist;
    /* At most 500 eight-byte slots fit; these bits detect invalid/double frees. */
    uint64_t allocated_slots[8];
};

struct slab_cache {
    spinlock_t lock;
    struct slab_page *pages;
    size_t object_size;
};

struct large_header {
    uint32_t magic;
    uint32_t padding;
    size_t pages;
    size_t size;
};

static struct slab_cache caches[] = {
    {SPINLOCK_ZERO, NULL, 8},
    {SPINLOCK_ZERO, NULL, 16},
    {SPINLOCK_ZERO, NULL, 32},
    {SPINLOCK_ZERO, NULL, 64},
    {SPINLOCK_ZERO, NULL, 128},
    {SPINLOCK_ZERO, NULL, 256},
    {SPINLOCK_ZERO, NULL, 512},
    {SPINLOCK_ZERO, NULL, 1024},
    {SPINLOCK_ZERO, NULL, 2048},
};

static bool heap_ready;

/* Reject heap work in IRQ context before taking locks or touching freelists. */
static void require_heap_context(void) {
    if (kernel_interrupt_depth()) {
        SUBSYS_PANIC("slab", "allocation in interrupt context");
    }

    if (!heap_ready) {
        SUBSYS_PANIC("slab", "heap used before initialization");
    }
}

/* Choose the smallest class that can contain the requested payload. */
static struct slab_cache *cache_for(size_t size) {
    for (size_t cache_index = 0; cache_index < sizeof(caches) / sizeof(caches[0]); ++cache_index) {
        if (caches[cache_index].object_size >= size) {
            return &caches[cache_index];
        }
    }

    return NULL;
}

/* Borrow a page from the backend and transfer ownership to the locked cache.
 * Allocation failure returns NULL without changing the cache's page chain. */
static struct slab_page *slab_new_page(struct slab_cache *cache) {
    KERNEL_ASSERT(spinlock_is_held(&cache->lock));

    paddr_t physical_address;

    if (!page_memory_allocate(1, &physical_address)) {
        return NULL;
    }

    struct slab_page *page = page_memory_pointer(physical_address);
    __builtin_memset(page, 0, sizeof(*page));
    page->magic = SLAB_MAGIC;
    page->capacity = (KERNEL_PAGE_SIZE - sizeof(*page)) / cache->object_size;
    page->cache = cache;
    page->next = cache->pages;

    uintptr_t object_base = (uintptr_t)page + sizeof(*page);
    page->freelist = (void **)object_base;

    for (size_t slot_index = 0; slot_index < page->capacity; ++slot_index) {
        void **slot = (void **)(object_base + slot_index * cache->object_size);
        *slot = slot_index + 1 == page->capacity
                    ? NULL
                    : (void *)(object_base + (slot_index + 1) * cache->object_size);
    }

    cache->pages = page;

    return page;
}

/* Validate exact object boundaries before accessing ownership bits. */
static size_t slab_slot_index(const struct slab_page *page, const void *address) {
    uintptr_t object_base = (uintptr_t)page + sizeof(*page);
    uintptr_t object_address = (uintptr_t)address;

    if (object_address < object_base || (object_address - object_base) % page->cache->object_size) {
        SUBSYS_PANIC("slab", "invalid slab object address");
    }

    size_t slot_index = (object_address - object_base) / page->cache->object_size;

    if (slot_index >= page->capacity) {
        SUBSYS_PANIC("slab", "slab object outside capacity");
    }

    return slot_index;
}

/* Find a free slot, update ownership while locked, then zero the private object. */
static void *slab_allocate(struct slab_cache *cache) {
    bool interrupts_enabled = spinlock_acquire(&cache->lock);
    struct slab_page *page = cache->pages;

    while (page && !page->freelist) {
        page = page->next;
    }

    if (!page) {
        page = slab_new_page(cache);
    }

    if (!page) {
        spinlock_release(&cache->lock, interrupts_enabled);

        return NULL;
    }

    void **object = page->freelist;

    size_t slot_index = slab_slot_index(page, object);
    uint64_t slot_mask = 1ULL << (slot_index % 64);
    KERNEL_ASSERT(!(page->allocated_slots[slot_index / 64] & slot_mask));
    page->allocated_slots[slot_index / 64] |= slot_mask;
    page->freelist = *object;
    ++page->inuse;
    spinlock_release(&cache->lock, interrupts_enabled);
    __builtin_memset(object, 0, cache->object_size);

    return object;
}

/* Reject a second release before linking the object back into the free list. */
static void slab_release(struct slab_page *page, void *address) {
    struct slab_cache *cache = page->cache;
    bool interrupts_enabled = spinlock_acquire(&cache->lock);
    size_t slot_index = slab_slot_index(page, address);
    uint64_t slot_mask = 1ULL << (slot_index % 64);

    if (!(page->allocated_slots[slot_index / 64] & slot_mask)) {
        SUBSYS_PANIC("slab", "double free of slab object");
    }

    page->allocated_slots[slot_index / 64] &= ~slot_mask;
    void **object = address;
    *object = page->freelist;
    page->freelist = object;
    --page->inuse;
    spinlock_release(&cache->lock, interrupts_enabled);
}

/* Check arithmetic before rounding up the header and payload to whole pages. */
static size_t large_page_count(size_t size) {
    if (size > (size_t)-1 - sizeof(struct large_header) - (KERNEL_PAGE_SIZE - 1)) {
        return 0;
    }

    return (sizeof(struct large_header) + size + KERNEL_PAGE_SIZE - 1) / KERNEL_PAGE_SIZE;
}

/* Large payloads own contiguous frames until free or a successful moving realloc. */
static void *large_allocate(size_t size) {
    size_t page_count = large_page_count(size);
    paddr_t physical_address;

    if (!page_count || !page_memory_allocate(page_count, &physical_address)) {
        return NULL;
    }

    struct large_header *header = page_memory_pointer(physical_address);
    *header = (struct large_header){LARGE_MAGIC, 0, page_count, size};
    void *payload = header + 1;
    __builtin_memset(payload, 0, size);

    return payload;
}

/* Transfer a zeroed object to the caller, preserving minimum-size semantics. */
void *malloc(size_t size) {
    require_heap_context();

    if (!size) {
        size = 1;
    }

    struct slab_cache *cache = cache_for(size);

    return cache ? slab_allocate(cache) : large_allocate(size);
}

/* Invalidate large-allocation metadata before returning its pages to the backend. */
static void large_release(struct large_header *header, void *address) {
    KERNEL_ASSERT(address == (void *)(header + 1));

    size_t page_count = header->pages;
    header->magic = 0;

    if (!page_memory_release(page_memory_physical(header), page_count)) {
        SUBSYS_PANIC("slab", "large allocation frame release failed");
    }
}

/* Free only the exact caller-owned object; a null pointer is always harmless. */
void free(void *address) {
    if (!address) {
        return;
    }

    require_heap_context();

    uintptr_t page_base = (uintptr_t)address & ~(KERNEL_PAGE_SIZE - 1);
    uint32_t magic = *(uint32_t *)page_base;

    if (magic == SLAB_MAGIC) {
        slab_release((struct slab_page *)page_base, address);
    } else if (magic == LARGE_MAGIC) {
        large_release((struct large_header *)page_base, address);
    } else {
        SUBSYS_PANIC("slab", "free: corrupt page magic - double-free or bad pointer");
    }
}

/* Moving realloc transfers ownership only after replacement allocation succeeds. */
void *realloc(void *address, size_t new_size) {
    require_heap_context();

    if (!address) {
        return malloc(new_size);
    }

    if (!new_size) {
        free(address);

        return NULL;
    }

    uintptr_t page_base = (uintptr_t)address & ~(KERNEL_PAGE_SIZE - 1);
    uint32_t magic = *(uint32_t *)page_base;
    size_t old_size;

    if (magic == SLAB_MAGIC) {
        struct slab_page *page = (struct slab_page *)page_base;
        old_size = page->cache->object_size;

        if (new_size <= old_size) {
            return address;
        }
    } else if (magic == LARGE_MAGIC) {
        struct large_header *header = (struct large_header *)page_base;
        KERNEL_ASSERT(address == (void *)(header + 1));

        size_t new_pages = large_page_count(new_size);

        if (!new_pages) {
            return NULL;
        }

        old_size = header->size;

        if (new_pages == header->pages) {
            header->size = new_size;

            return address;
        }
    } else {
        SUBSYS_PANIC("slab", "realloc: corrupt page magic - double-free or bad pointer");
    }

    void *replacement = malloc(new_size);

    if (!replacement) {
        return NULL;
    }

    __builtin_memcpy(replacement, address, old_size < new_size ? old_size : new_size);
    free(address);

    return replacement;
}

/* Warm every cache before publishing the heap; these pages remain cache-owned. */
void slab_init(void) {
    KERNEL_ASSERT(!heap_ready && !kernel_interrupt_depth());

    for (size_t cache_index = 0; cache_index < sizeof(caches) / sizeof(caches[0]); ++cache_index) {
        bool interrupts_enabled = spinlock_acquire(&caches[cache_index].lock);
        struct slab_page *page = slab_new_page(&caches[cache_index]);
        spinlock_release(&caches[cache_index].lock, interrupts_enabled);
        KERNEL_ASSERT(page != NULL);
    }

    heap_ready = true;
}

void heap_snapshot(heap_snapshot_t *snapshot) {
    KERNEL_ASSERT(heap_ready && !kernel_interrupt_depth() && !kernel_spinlock_depth());
    _Static_assert(sizeof(caches) / sizeof(caches[0]) == HEAP_CACHE_COUNT, "heap cache count");
    __builtin_memset(snapshot, 0, sizeof(*snapshot));

    for (size_t index = 0; index < HEAP_CACHE_COUNT; ++index) {
        bool interrupts = spinlock_acquire(&caches[index].lock);
        for (struct slab_page *page = caches[index].pages; page; page = page->next) {
            ++snapshot->retained_pages;
            snapshot->live_objects[index] += page->inuse;
        }
        spinlock_release(&caches[index].lock, interrupts);
    }
}
