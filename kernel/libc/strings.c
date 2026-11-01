#include <kernel/strings.h>
#include <kernel/heap.h>
/* Bounded byte accesses avoid crossing an unmapped page while scanning strings. */
size_t strlen(const char *string) {
    size_t length = 0;

    while (string[length]) {
        ++length;
    }

    return length;
}

/* Compare unsigned bytes, matching the C library ordering convention. */
int strcmp(const char *left, const char *right) {
    while (*left && *left == *right) {
        ++left;
        ++right;
    }

    return (unsigned char)*left - (unsigned char)*right;
}

/* Never inspect bytes past the requested prefix. */
int strncmp(const char *left, const char *right, size_t length) {
    while (length && *left && *left == *right) {
        ++left;
        ++right;
        --length;
    }

    return length ? (unsigned char)*left - (unsigned char)*right : 0;
}

/* Return owned, terminated storage; preserve fallible ARM allocation semantics. */
char *strdup(const char *string) {
    size_t length = strlen(string) + 1;
    char *copy = malloc(length);

    if (copy) {
        memcpy(copy, string, length);
    }

    return copy;
}
