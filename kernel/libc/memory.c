/* Freestanding byte operations required by the compiler and kernel callers. */
#include <kernel/strings.h>
#include <stdint.h>

/* Copy disjoint byte ranges; overlap requires memmove instead. */
void *memcpy(void *destination, const void *source, size_t length) {
    unsigned char *destination_bytes = destination;
    const unsigned char *source_bytes = source;

    for (size_t byte_index = 0; byte_index < length; ++byte_index) {
        destination_bytes[byte_index] = source_bytes[byte_index];
    }

    return destination;
}

/* Fill exactly length bytes with the low byte of value. */
void *memset(void *destination, int value, size_t length) {
    unsigned char *destination_bytes = destination;

    for (size_t byte_index = 0; byte_index < length; ++byte_index) {
        destination_bytes[byte_index] = (unsigned char)value;
    }

    return destination;
}

/* Choose copy direction by address so overlapping ranges retain source bytes.
 * Integer address comparisons also avoid ordering unrelated C object pointers. */
void *memmove(void *destination, const void *source, size_t length) {
    unsigned char *destination_bytes = destination;
    const unsigned char *source_bytes = source;

    if ((uintptr_t)destination < (uintptr_t)source) {
        for (size_t byte_index = 0; byte_index < length; ++byte_index) {
            destination_bytes[byte_index] = source_bytes[byte_index];
        }
    } else if ((uintptr_t)destination > (uintptr_t)source) {
        for (size_t remaining = length; remaining; --remaining) {
            destination_bytes[remaining - 1] = source_bytes[remaining - 1];
        }
    }

    return destination;
}

/* Compare unsigned bytes without reading beyond either supplied range. */
int memcmp(const void *left, const void *right, size_t length) {
    const unsigned char *left_bytes = left;
    const unsigned char *right_bytes = right;

    for (size_t byte_index = 0; byte_index < length; ++byte_index) {
        if (left_bytes[byte_index] != right_bytes[byte_index]) {
            return left_bytes[byte_index] < right_bytes[byte_index] ? -1 : 1;
        }
    }

    return 0;
}
