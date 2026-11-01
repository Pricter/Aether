#pragma once
#include <stddef.h>
size_t strlen(const char *string);
int strcmp(const char *left, const char *right);
int strncmp(const char *left, const char *right, size_t length);
char *strdup(const char *string);
void *memcpy(void *destination, const void *source, size_t length);
void *memset(void *destination, int value, size_t length);
int memcmp(const void *left, const void *right, size_t length);
void *memmove(void *destination, const void *source, size_t length);
