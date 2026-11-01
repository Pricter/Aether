#pragma once

#include <kernel/boot_memory.h>

#define FRAME_SIZE 4096UL

/* Caller-synchronized allocator. Caller owns state/bitmap for their lifetime.
 * There is no internal locking: use only during single-CPU IRQ-masked boot,
 * or through the runtime backend's IRQ-safe PMM lock. Spans copy usable ranges.
 * One bit tracks each usable frame, so physical holes consume no bitmap. */
typedef struct {
    boot_memory_range_t spans[BOOT_MEMORY_MAX_AVAILABLE];
    unsigned int span_count;
    unsigned char *bitmap;
    size_t bitmap_bytes;
    unsigned long total_frames;
    unsigned long free_frames;
    bool initialized;
} frame_allocator_t;

/* Return required bitmap bytes, or zero for an invalid/empty map. No allocation. */
size_t frame_allocator_metadata_size(const boot_memory_map_t *memory_map);

/* Borrow caller-provided bitmap storage, which MUST already be excluded from
 * the finalized map. Contents are reset; storage must be writable and remain
 * mapped. The map is copied. Rejects invalid spans or insufficient storage.
 * Call only on new/inactive state; reinitializing loses existing ownership. */
bool frame_allocator_initialize(frame_allocator_t *allocator, const boot_memory_map_t *memory_map,
    void *bitmap_storage, size_t storage_bytes);

/* Transfer count contiguous 4 KiB physical frames to the caller. Never crosses
 * a span boundary. Contents are unspecified. Returns false on invalid input
 * or exhaustion and writes zero to result; successful frames are nonzero.
 * Caller must release owned frames or document permanent ownership. */
bool frame_allocator_allocate(frame_allocator_t *allocator, unsigned long count, paddr_t *result);

/* Release caller-owned frames. Rejects reserved, unaligned, unallocated, or
 * out-of-range pages without changing state. A successful release invalidates
 * all caller aliases. It cannot detect freeing another owner's live frames. */
bool frame_allocator_release(frame_allocator_t *allocator, paddr_t base, unsigned long count);
