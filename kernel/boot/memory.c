#include <kernel/boot_memory.h>

#define FDT_BEGIN_NODE 1U
#define FDT_END_NODE 2U
#define FDT_PROPERTY 3U
#define FDT_NOP 4U
#define FDT_END 9U
#define FDT_MAX_DEPTH 16U
#define BOOT_PAGE_SIZE 4096UL

/* Offsets in the version-17 flattened-device-tree header, in bytes. */
enum {
    FDT_HEADER_TOTAL_SIZE = 4,
    FDT_HEADER_STRUCTURE = 8,
    FDT_HEADER_STRINGS = 12,
    FDT_HEADER_RESERVATIONS = 16,
    FDT_HEADER_VERSION = 20,
    FDT_HEADER_COMPATIBLE_VERSION = 24,
    FDT_HEADER_STRINGS_SIZE = 32,
    FDT_HEADER_STRUCTURE_SIZE = 36,
    FDT_HEADER_SIZE = 40,
    FDT_SUPPORTED_VERSION = 17,
};
#define FDT_MAGIC 0xd00dfeedU
#define FDT_MAXIMUM_SIZE (2UL * 1024 * 1024)

/* Temporary parser state lives on the boot stack; no DTB pointers escape. */
typedef struct {
    unsigned int address_cells;
    unsigned int size_cells;
    const unsigned char *register_data;
    unsigned int register_bytes;
    bool memory_node;
    bool reserved_bus;
    bool reserved_child;
    bool enabled;
    bool has_children;
    bool dynamic_reservation;
    bool empty_ranges;
    bool cpu_bus;
    bool cpu_node;
    bool psci_node;
    bool psci_compatible;
    unsigned int psci_conduit;
} device_tree_node_t;

/* DTB integers are big endian and may be unaligned in the source blob. */
static unsigned int read_big_endian_32(const unsigned char *bytes) {
    return ((unsigned int)bytes[0] << 24) | ((unsigned int)bytes[1] << 16) |
           ((unsigned int)bytes[2] << 8) | bytes[3];
}

/* Read one or two cells without assuming native integer alignment. */
static unsigned long read_cells(const unsigned char *bytes, unsigned int cell_count) {
    unsigned long value = read_big_endian_32(bytes);

    if (cell_count == 2) {
        value = (value << 32) | read_big_endian_32(bytes + 4);
    }

    return value;
}

/* Compare only bounded, terminated strings supplied by the validated blob. */
static bool string_equals(const unsigned char *bytes, size_t byte_count, const char *expected) {
    size_t character_index = 0;

    while (expected[character_index]) {
        if (character_index >= byte_count || bytes[character_index] != expected[character_index]) {
            return false;
        }

        ++character_index;
    }

    return character_index < byte_count && bytes[character_index] == 0;
}

/* Validate a sub-block with subtraction, avoiding offset-plus-size overflow. */
static bool block_fits(unsigned int offset, unsigned int size, unsigned int total) {
    return offset <= total && size <= total - offset;
}

/* Validate a terminated compatible string list without reading beyond its property. */
static bool compatible_psci(const unsigned char *bytes, unsigned int byte_count) {
    unsigned int cursor = 0;
    bool recognized = false;

    while (cursor < byte_count) {
        unsigned int start = cursor;

        while (cursor < byte_count && bytes[cursor]) {
            ++cursor;
        }

        if (cursor == byte_count) {
            return false;
        }

        recognized |= string_equals(bytes + start, cursor - start + 1, "arm,psci-0.2") ||
                      string_equals(bytes + start, cursor - start + 1, "arm,psci-1.0");
        ++cursor;
    }

    return recognized;
}

/* CPU reg describes MPIDR affinity, never a logical array index. */
static bool collect_cpu(const device_tree_node_t *node, const device_tree_node_t *parent,
    boot_memory_map_t *memory_map) {
    if (!node->enabled) {
        return true;
    }

    if (!parent->cpu_bus || parent->size_cells != 0 || parent->address_cells < 1 ||
        parent->address_cells > 2 || !node->register_data ||
        node->register_bytes != parent->address_cells * 4 ||
        memory_map->cpu_count == BOOT_MAX_CPUS) {
        return false;
    }

    uint64_t hardware_id = read_cells(node->register_data, parent->address_cells);

    if (hardware_id & ~0xff00ffffffUL) {
        return false;
    }

    for (unsigned int cpu_index = 0; cpu_index < memory_map->cpu_count; ++cpu_index) {
        if (memory_map->cpu_hardware_ids[cpu_index] == hardware_id) {
            return false;
        }
    }

    memory_map->cpu_hardware_ids[memory_map->cpu_count++] = hardware_id;

    return true;
}

/* Append a copied physical interval, rejecting wrapped or empty ranges. */
static bool append_range(boot_memory_range_t *ranges, unsigned int *range_count,
    unsigned int capacity, paddr_t base, paddr_t end, const char *description) {
    if (base >= end || *range_count == capacity) {
        return false;
    }

    ranges[*range_count] = (boot_memory_range_t){base, end, description};
    ++*range_count;

    return true;
}

/* Reservations may overlap; finalize subtracts their union from RAM. */
bool boot_memory_reserve(
    boot_memory_map_t *memory_map, paddr_t base, paddr_t end, const char *description) {
    return append_range(memory_map->reserved, &memory_map->reserved_count, BOOT_MEMORY_MAX_RESERVED,
        base, end, description);
}

/* Decode root memory or identity-addressed reserved-memory reg tuples. */
static bool collect_registers(const device_tree_node_t *node, const device_tree_node_t *parent,
    boot_memory_map_t *memory_map) {
    if (!node->enabled || (!node->memory_node && !node->reserved_child)) {
        return true;
    }

    if (!node->register_data || node->dynamic_reservation ||
        (node->memory_node && node->reserved_child) || parent->address_cells < 1 ||
        parent->address_cells > 2 || parent->size_cells < 1 || parent->size_cells > 2) {
        return false;
    }

    unsigned int tuple_bytes = (parent->address_cells + parent->size_cells) * 4;

    if (!node->register_bytes || node->register_bytes % tuple_bytes) {
        return false;
    }

    for (unsigned int byte_offset = 0; byte_offset < node->register_bytes;
        byte_offset += tuple_bytes) {
        const unsigned char *tuple = node->register_data + byte_offset;
        paddr_t base = read_cells(tuple, parent->address_cells);
        unsigned long length = read_cells(tuple + parent->address_cells * 4, parent->size_cells);

        if (!length || length > ~0UL - base) {
            return false;
        }

        bool accepted = node->memory_node ? append_range(memory_map->ram, &memory_map->ram_count,
                                                BOOT_MEMORY_MAX_RAM, base, base + length, "RAM")
                                          : boot_memory_reserve(memory_map, base, base + length,
                                                "DT reserved-memory");
        if (!accepted) {
            return false;
        }
    }

    return true;
}

/* Apply recognized properties. Unsupported reservation semantics fail closed. */
static bool apply_property(device_tree_node_t *node, const unsigned char *property_name,
    size_t name_capacity, const unsigned char *property_data, unsigned int property_size) {
    if (string_equals(property_name, name_capacity, "#address-cells") ||
        string_equals(property_name, name_capacity, "#size-cells")) {
        if (property_size != 4) {
            return false;
        }

        unsigned int cell_count = read_big_endian_32(property_data);

        if (string_equals(property_name, name_capacity, "#address-cells")) {
            node->address_cells = cell_count;
        } else {
            node->size_cells = cell_count;
        }
    } else if (string_equals(property_name, name_capacity, "device_type")) {
        node->memory_node = string_equals(property_data, property_size, "memory");
        node->cpu_node = string_equals(property_data, property_size, "cpu");
    } else if (node->psci_node && string_equals(property_name, name_capacity, "compatible")) {
        node->psci_compatible = compatible_psci(property_data, property_size);
    } else if (node->psci_node && string_equals(property_name, name_capacity, "method")) {
        if (string_equals(property_data, property_size, "hvc")) {
            node->psci_conduit = 1;
        } else if (string_equals(property_data, property_size, "smc")) {
            node->psci_conduit = 2;
        } else {
            return false;
        }
    } else if (string_equals(property_name, name_capacity, "reg")) {
        if (node->register_data) {
            return false;
        }

        node->register_data = property_data;
        node->register_bytes = property_size;
    } else if (string_equals(property_name, name_capacity, "status")) {
        node->enabled = node->enabled && (string_equals(property_data, property_size, "okay") ||
                                             string_equals(property_data, property_size, "ok"));
    } else if (node->reserved_bus && string_equals(property_name, name_capacity, "ranges")) {
        node->empty_ranges = property_size == 0;
    } else if (node->reserved_child && string_equals(property_name, name_capacity, "size")) {
        node->dynamic_reservation = true;
    }

    return true;
}

/* Walk bounded structure tokens; unsupported nested memory buses are rejected
 * instead of treating bus-relative addresses as physical RAM. */
static bool parse_structure(const unsigned char *structure, unsigned int structure_size,
    const unsigned char *strings, unsigned int strings_size, boot_memory_map_t *memory_map) {
    device_tree_node_t nodes[FDT_MAX_DEPTH];
    unsigned int depth = 0;
    unsigned int cursor = 0;
    bool root_seen = false;

    while (block_fits(cursor, 4, structure_size)) {
        unsigned int token = read_big_endian_32(structure + cursor);
        cursor += 4;

        if (token == FDT_BEGIN_NODE) {
            if (depth == FDT_MAX_DEPTH || (!depth && root_seen)) {
                return false;
            }

            unsigned int name_start = cursor;

            while (cursor < structure_size && structure[cursor]) {
                ++cursor;
            }

            if (cursor == structure_size || (!depth && cursor != name_start)) {
                return false;
            }

            unsigned int name_size = cursor - name_start + 1;
            cursor = (cursor + 4) & ~3U;

            device_tree_node_t *node = &nodes[depth];
            *node = (device_tree_node_t){.address_cells = 2, .size_cells = 1, .enabled = true};

            if (depth) {
                device_tree_node_t *parent = &nodes[depth - 1];
                parent->has_children = true;
                node->enabled = parent->enabled;
                node->reserved_child = parent->reserved_bus;

                if (parent->reserved_child) {
                    return false;
                }
            } else {
                root_seen = true;
            }

            node->reserved_bus =
                depth == 1 && string_equals(structure + name_start, name_size, "reserved-memory");
            node->cpu_bus = depth == 1 && string_equals(structure + name_start, name_size, "cpus");
            node->psci_node =
                depth == 1 && string_equals(structure + name_start, name_size, "psci");
            ++depth;
        } else if (token == FDT_PROPERTY) {
            if (!depth || nodes[depth - 1].has_children || !block_fits(cursor, 8, structure_size)) {
                return false;
            }

            unsigned int property_size = read_big_endian_32(structure + cursor);
            unsigned int name_offset = read_big_endian_32(structure + cursor + 4);
            cursor += 8;

            if (!block_fits(cursor, property_size, structure_size) || name_offset >= strings_size) {
                return false;
            }

            unsigned int name_end = name_offset;

            while (name_end < strings_size && strings[name_end]) {
                ++name_end;
            }

            if (name_end == strings_size ||
                !apply_property(&nodes[depth - 1], strings + name_offset,
                    strings_size - name_offset, structure + cursor, property_size)) {
                return false;
            }

            /* Validated blocks are small enough that 3-byte rounding cannot wrap. */
            cursor = (cursor + property_size + 3) & ~3U;
        } else if (token == FDT_END_NODE) {
            if (!depth) {
                return false;
            }

            device_tree_node_t *node = &nodes[depth - 1];

            if (node->cpu_node &&
                (depth != 3 || !collect_cpu(node, &nodes[depth - 2], memory_map))) {
                return false;
            }

            if (node->psci_node && node->enabled) {
                if (!node->psci_compatible || !node->psci_conduit || memory_map->psci_conduit) {
                    return false;
                }

                memory_map->psci_conduit = node->psci_conduit;
            }

            if (node->memory_node && depth != 2) {
                return false;
            }

            if (node->reserved_bus && node->enabled &&
                (!node->empty_ranges || node->address_cells != nodes[0].address_cells ||
                    node->size_cells != nodes[0].size_cells)) {
                return false;
            }

            if (depth > 1 && !collect_registers(node, &nodes[depth - 2], memory_map)) {
                return false;
            }

            --depth;
        } else if (token == FDT_END) {
            return root_seen && !depth && memory_map->ram_count != 0;
        } else if (token != FDT_NOP) {
            return false;
        }
    }

    return false;
}

/* Validate the blob before reading any variable-size block. Current support
 * requires v17-compatible, conventionally ordered non-overlapping blocks. */
bool boot_memory_discover(const void *device_tree, size_t readable_bytes,
    paddr_t device_tree_address, boot_memory_map_t *memory_map) {
    *memory_map = (boot_memory_map_t){0};

    if (!device_tree || readable_bytes < FDT_HEADER_SIZE) {
        return false;
    }

    const unsigned char *header = device_tree;
    unsigned int total = read_big_endian_32(header + FDT_HEADER_TOTAL_SIZE);
    unsigned int structure_offset = read_big_endian_32(header + FDT_HEADER_STRUCTURE);
    unsigned int strings_offset = read_big_endian_32(header + FDT_HEADER_STRINGS);
    unsigned int reservation_offset = read_big_endian_32(header + FDT_HEADER_RESERVATIONS);
    unsigned int strings_size = read_big_endian_32(header + FDT_HEADER_STRINGS_SIZE);
    unsigned int structure_size = read_big_endian_32(header + FDT_HEADER_STRUCTURE_SIZE);

    if (read_big_endian_32(header) != FDT_MAGIC || total < FDT_HEADER_SIZE ||
        total > readable_bytes || total > FDT_MAXIMUM_SIZE ||
        read_big_endian_32(header + FDT_HEADER_VERSION) < FDT_SUPPORTED_VERSION ||
        read_big_endian_32(header + FDT_HEADER_COMPATIBLE_VERSION) > FDT_SUPPORTED_VERSION ||
        reservation_offset < FDT_HEADER_SIZE || (reservation_offset & 7) ||
        (structure_offset & 3) || reservation_offset >= structure_offset ||
        !block_fits(structure_offset, structure_size, total) ||
        !block_fits(strings_offset, strings_size, total) ||
        structure_offset + structure_size > strings_offset || total > ~0UL - device_tree_address) {
        return false;
    }

    memory_map->device_tree_size = total;

    unsigned int cursor = reservation_offset;

    for (;;) {
        if (!block_fits(cursor, 16, structure_offset)) {
            return false;
        }

        paddr_t base = read_cells(header + cursor, 2);
        unsigned long length = read_cells(header + cursor + 8, 2);
        cursor += 16;

        if (!base && !length) {
            break;
        }

        if (!length || length > ~0UL - base ||
            !boot_memory_reserve(memory_map, base, base + length, "DT memreserve")) {
            return false;
        }
    }

    return parse_structure(header + structure_offset, structure_size, header + strings_offset,
               strings_size, memory_map) &&
           boot_memory_reserve(
               memory_map, device_tree_address, device_tree_address + total, "device tree");
}

/* Sort and merge overlapping RAM reports so free bytes are counted once. */
static void normalize_ram(boot_memory_map_t *memory_map) {
    for (unsigned int range_index = 1; range_index < memory_map->ram_count; ++range_index) {
        boot_memory_range_t pending = memory_map->ram[range_index];
        unsigned int insertion_index = range_index;

        while (insertion_index && memory_map->ram[insertion_index - 1].base > pending.base) {
            memory_map->ram[insertion_index] = memory_map->ram[insertion_index - 1];
            --insertion_index;
        }

        memory_map->ram[insertion_index] = pending;
    }

    unsigned int merged_count = 0;

    for (unsigned int range_index = 0; range_index < memory_map->ram_count; ++range_index) {
        boot_memory_range_t current = memory_map->ram[range_index];

        if (merged_count && current.base <= memory_map->ram[merged_count - 1].end) {
            if (current.end > memory_map->ram[merged_count - 1].end) {
                memory_map->ram[merged_count - 1].end = current.end;
            }
        } else {
            memory_map->ram[merged_count++] = current;
        }
    }

    memory_map->ram_count = merged_count;
}

/* Emit only whole pages: even a one-byte reservation protects its entire page. */
static bool append_available(boot_memory_map_t *memory_map, paddr_t base, paddr_t end) {
    if (base > ~0UL - (BOOT_PAGE_SIZE - 1)) {
        return true;
    }

    base = (base + BOOT_PAGE_SIZE - 1) & ~(BOOT_PAGE_SIZE - 1);
    end &= ~(BOOT_PAGE_SIZE - 1);

    return base >= end || append_range(memory_map->available, &memory_map->available_count,
                              BOOT_MEMORY_MAX_AVAILABLE, base, end, "available");
}

/* Sweep each RAM interval against all reservations. A covering reservation
 * advances the cursor; otherwise emit RAM up to the nearest reservation. */
bool boot_memory_finalize(boot_memory_map_t *memory_map) {
    normalize_ram(memory_map);
    memory_map->available_count = 0;

    for (unsigned int range_index = 0; range_index < memory_map->ram_count; ++range_index) {
        paddr_t cursor = memory_map->ram[range_index].base;
        paddr_t ram_end = memory_map->ram[range_index].end;

        while (cursor < ram_end) {
            paddr_t covered_end = cursor;
            paddr_t next_reserved = ram_end;

            for (unsigned int reserved_index = 0; reserved_index < memory_map->reserved_count;
                ++reserved_index) {
                boot_memory_range_t reserved = memory_map->reserved[reserved_index];

                if (reserved.base <= cursor && reserved.end > covered_end) {
                    covered_end = reserved.end;
                } else if (reserved.base > cursor && reserved.base < next_reserved) {
                    next_reserved = reserved.base;
                }
            }

            if (covered_end > cursor) {
                cursor = covered_end;
            } else {
                if (!append_available(memory_map, cursor, next_reserved)) {
                    return false;
                }

                cursor = next_reserved;
            }
        }
    }

    return memory_map->available_count != 0;
}
