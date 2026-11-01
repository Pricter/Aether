#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Freestanding byte operations implemented by kernel/libc/memory.c. */
void *memcpy(void *destination, const void *source, size_t length);
void *memset(void *destination, int value, size_t length);
void *memmove(void *destination, const void *source, size_t length);
int memcmp(const void *left, const void *right, size_t length);

#ifdef __cplusplus
}
#endif
