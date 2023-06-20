#pragma once

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) > (b) ? (b) : (a))

static inline uint64_t align_pointer(void* ptr, size_t alignment) {
	uintptr_t address = (uintptr_t)ptr;
	uintptr_t aligned_address = (address + alignment - 1) & ~(alignment - 1);
	return aligned_address;
}