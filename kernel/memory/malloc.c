#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limine.h>
#include <kernel/mmu.h>
#include <kernel/spinlock.h>
#include <memory.h>

#define SIZEOF_ARRAY(ARRAY) (sizeof(ARRAY) / sizeof(ARRAY[0]))
#define DIV_ROUNDUP(VALUE, DIV) ({ \
    __auto_type DIV_ROUNDUP_value = VALUE; \
    __auto_type DIV_ROUNDUP_div = DIV; \
    (DIV_ROUNDUP_value + (DIV_ROUNDUP_div - 1)) / DIV_ROUNDUP_div; \
})
#define ALIGN_UP(VALUE, ALIGN) ({ \
    __auto_type ALIGN_UP_value = VALUE; \
    __auto_type ALIGN_UP_align = ALIGN; \
    DIV_ROUNDUP(ALIGN_UP_value, ALIGN_UP_align) * ALIGN_UP_align; \
})

struct slab {
    spinlock_t lock;
    void **first_free;
    size_t ent_size;
};

struct slab_header {
    struct slab *slab;
};

struct alloc_metadata {
    size_t pages;
    size_t size;
};

static struct slab slabs[10];

static inline struct slab *slab_for(size_t size) {
    for (size_t i = 0; i < SIZEOF_ARRAY(slabs); i++) {
        struct slab *slab = &slabs[i];
        if (slab->ent_size >= size) {
            return slab;
        }
    }
    return NULL;
}

static void create_slab(struct slab *slab, size_t ent_size) {
    slab->lock = (spinlock_t)SPINLOCK_ZERO;
    slab->first_free = mmu_request_frame() + HHDM_HIGHER_HALF;
    slab->ent_size = ent_size;

    size_t header_offset = ALIGN_UP(sizeof(struct slab_header), ent_size);
    size_t available_size = PAGE_SIZE - header_offset;

    struct slab_header *slab_ptr = (struct slab_header *)slab->first_free;
    slab_ptr->slab = slab;
    slab->first_free = (void **)((void *)slab->first_free + header_offset);

    void **arr = (void **)slab->first_free;
    size_t max = available_size / ent_size - 1;
    size_t fact = ent_size / sizeof(void *);

    for (size_t i = 0; i < max; i++) {
        arr[i * fact] = &arr[(i + 1) * fact];
    }
    arr[max * fact] = NULL;
}

static void *alloc_from_slab(struct slab *slab) {
    spinlock_acquire(&slab->lock);

    if (slab->first_free == NULL) {
        create_slab(slab, slab->ent_size);
    }

    void **old_free = slab->first_free;
    slab->first_free = *old_free;
    memset(old_free, 0, slab->ent_size);

    spinlock_release(&slab->lock);
    return old_free;
}

static void free_in_slab(struct slab *slab, void *addr) {
    spinlock_acquire(&slab->lock);

    if (addr == NULL) {
        goto cleanup;
    }

    void **new_head = addr;
    *new_head = slab->first_free;
    slab->first_free = new_head;

cleanup:
    spinlock_release(&slab->lock);
}

void slab_init(void) {
    create_slab(&slabs[0], 8);
    create_slab(&slabs[1], 16);
    create_slab(&slabs[2], 24);
    create_slab(&slabs[3], 32);
    create_slab(&slabs[4], 48);
    create_slab(&slabs[5], 64);
    create_slab(&slabs[6], 128);
    create_slab(&slabs[7], 256);
    create_slab(&slabs[8], 512);
    create_slab(&slabs[9], 1024);
}

void *malloc(size_t size) {
    struct slab *slab = slab_for(size);
    if (slab != NULL) {
        return alloc_from_slab(slab);
    }

    size_t page_count = DIV_ROUNDUP(size, PAGE_SIZE);
    void *ret = mmu_request_frames(page_count + 1);

    ret += HHDM_HIGHER_HALF;
    struct alloc_metadata *metadata = (struct alloc_metadata *)ret;

    metadata->pages = page_count;
    metadata->size = size;

    return ret + PAGE_SIZE;
}

void *realloc(void *addr, size_t new_size) {
    if (addr == NULL) {
        return malloc(new_size);
    }

    if (((uintptr_t)addr & 0xfff) == 0) {
        struct alloc_metadata *metadata = (struct alloc_metadata *)(addr - PAGE_SIZE);
        if (DIV_ROUNDUP(metadata->size, PAGE_SIZE) == DIV_ROUNDUP(new_size, PAGE_SIZE)) {
            metadata->size = new_size;
            return addr;
        }

        void *new_addr = malloc(new_size);
        if (new_addr == NULL) {
            return NULL;
        }

        if (metadata->size > new_size) {
            memcpy(new_addr, addr, new_size);
        } else {
            memcpy(new_addr, addr, metadata->size);
        }

        free(addr);
        return new_addr;
    }

    struct slab_header *slab_header = (struct slab_header *)((uintptr_t)addr & ~0xfff);
    struct slab *slab = slab_header->slab;

    if (new_size > slab->ent_size) {
        void *new_addr = malloc(new_size);
        if (new_addr == NULL) {
            return NULL;
        }

        memcpy(new_addr, addr, slab->ent_size);
        free_in_slab(slab, addr);
        return new_addr;
    }

    return addr;
}

void free(void *addr) {
    if (addr == NULL) {
        return;
    }

    if (((uintptr_t)addr & 0xfff) == 0) {
        struct alloc_metadata *metadata = (struct alloc_metadata *)(addr - PAGE_SIZE);
        mmu_free_frames((void *)metadata - HHDM_HIGHER_HALF, metadata->pages + 1);
        return;
    }

    struct slab_header *slab_header = (struct slab_header *)((uintptr_t)addr & ~0xfff);
    free_in_slab(slab_header->slab, addr);
}
