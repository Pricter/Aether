#pragma once

#include <stddef.h>
#include <memory.h>

/* Freestanding string subset implemented by kernel/libc/strings.c.
 * strdup allocates caller-owned kernel heap storage and returns NULL on OOM;
 * the other operations borrow their input and never allocate or lock. */
size_t strlen(const char *string);
int strcmp(const char *left, const char *right);
int strncmp(const char *left, const char *right, size_t length);
char *strdup(const char *string);
