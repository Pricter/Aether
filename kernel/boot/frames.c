#include <kernel/frame_allocator.h>

/* Validate sorted, page-aligned spans and count usable frames without overflow. */
static unsigned long count_frames(const boot_memory_map_t *memory_map) {
    if (!memory_map || !memory_map->available_count ||
        memory_map->available_count > BOOT_MEMORY_MAX_AVAILABLE) {
        return 0;
    }

    unsigned long total_frames = 0;
    paddr_t previous_end = 0;

    for (unsigned int span_index = 0; span_index < memory_map->available_count; ++span_index) {
        boot_memory_range_t span = memory_map->available[span_index];

        if (!span.base || span.base < previous_end || span.base >= span.end ||
            ((span.base | span.end) & (FRAME_SIZE - 1))) {
            return 0;
        }

        unsigned long span_frames = (span.end - span.base) / FRAME_SIZE;

        if (span_frames > ~0UL - total_frames) {
            return 0;
        }

        total_frames += span_frames;
        previous_end = span.end;
    }

    return total_frames;
}

/* Round up one bit per usable frame; do not index across physical holes. */
size_t frame_allocator_metadata_size(const boot_memory_map_t *memory_map) {
    unsigned long total_frames = count_frames(memory_map);

    return total_frames / 8 + (total_frames % 8 != 0);
}

/* Establish caller-owned storage before exposing any frame operations. */
bool frame_allocator_initialize(frame_allocator_t *allocator, const boot_memory_map_t *memory_map,
    void *bitmap_storage, size_t storage_bytes) {
    *allocator = (frame_allocator_t){0};
    unsigned long total_frames = count_frames(memory_map);
    size_t required_bytes = frame_allocator_metadata_size(memory_map);

    if (!total_frames || !bitmap_storage || required_bytes > storage_bytes) {
        return false;
    }

    allocator->bitmap = bitmap_storage;
    allocator->bitmap_bytes = required_bytes;
    allocator->total_frames = total_frames;
    allocator->free_frames = total_frames;
    allocator->span_count = memory_map->available_count;

    for (unsigned int span_index = 0; span_index < allocator->span_count; ++span_index) {
        allocator->spans[span_index] = memory_map->available[span_index];
    }

    for (size_t byte_index = 0; byte_index < required_bytes; ++byte_index) {
        allocator->bitmap[byte_index] = 0;
    }

    allocator->initialized = true;

    return true;
}

/* Read one valid frame bit; callers establish index bounds from copied spans. */
static bool frame_is_owned(const frame_allocator_t *allocator, unsigned long frame_index) {
    return (allocator->bitmap[frame_index / 8] >> (frame_index % 8)) & 1;
}

/* Set or clear a valid frame bit in the boot CPU's private allocator state. */
static void frame_set_owned(frame_allocator_t *allocator, unsigned long frame_index, bool owned) {
    unsigned char bit_mask = (unsigned char)(1U << (frame_index % 8));

    if (owned) {
        allocator->bitmap[frame_index / 8] |= bit_mask;
    } else {
        allocator->bitmap[frame_index / 8] &= (unsigned char)~bit_mask;
    }
}

/* First-fit contiguous allocation stays inside one usable span, even when
 * two adjacent spans have consecutive bitmap indices across a physical hole. */
bool frame_allocator_allocate(frame_allocator_t *allocator, unsigned long count, paddr_t *result) {
    if (!result) {
        return false;
    }
    *result = 0;

    if (!allocator->initialized || !count || count > allocator->free_frames) {
        return false;
    }

    unsigned long bitmap_base = 0;

    for (unsigned int span_index = 0; span_index < allocator->span_count; ++span_index) {
        boot_memory_range_t span = allocator->spans[span_index];
        unsigned long span_frames = (span.end - span.base) / FRAME_SIZE;
        unsigned long run_length = 0;

        for (unsigned long span_frame = 0; span_frame < span_frames; ++span_frame) {
            run_length = frame_is_owned(allocator, bitmap_base + span_frame) ? 0 : run_length + 1;

            if (run_length == count) {
                unsigned long first_frame = span_frame + 1 - count;

                for (unsigned long run_offset = 0; run_offset < count; ++run_offset) {
                    frame_set_owned(allocator, bitmap_base + first_frame + run_offset, true);
                }

                allocator->free_frames -= count;
                *result = span.base + first_frame * FRAME_SIZE;

                return true;
            }
        }

        bitmap_base += span_frames;
    }

    return false;
}

/* Validate the entire release before clearing any bit, so partial invalid
 * releases cannot corrupt allocation accounting or expose live pages. */
bool frame_allocator_release(frame_allocator_t *allocator, paddr_t base, unsigned long count) {
    if (!allocator->initialized || !count || (base & (FRAME_SIZE - 1))) {
        return false;
    }

    unsigned long bitmap_base = 0;

    for (unsigned int span_index = 0; span_index < allocator->span_count; ++span_index) {
        boot_memory_range_t span = allocator->spans[span_index];

        if (base >= span.base && base < span.end) {
            if (count > (span.end - base) / FRAME_SIZE) {
                return false;
            }

            unsigned long first_frame = bitmap_base + (base - span.base) / FRAME_SIZE;

            for (unsigned long run_offset = 0; run_offset < count; ++run_offset) {
                if (!frame_is_owned(allocator, first_frame + run_offset)) {
                    return false;
                }
            }

            for (unsigned long run_offset = 0; run_offset < count; ++run_offset) {
                frame_set_owned(allocator, first_frame + run_offset, false);
            }

            allocator->free_frames += count;

            return true;
        }

        bitmap_base += (span.end - span.base) / FRAME_SIZE;
    }

    return false;
}
