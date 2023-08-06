#pragma once

#include <stdint.h>

enum {
    DARRAY_CAPACITY,
    DARRAY_LENGTH,
    DARRAY_STRIDE,
    DARRAY_FIELD_LENGTH
};

void* _darray_create(uint64_t length, uint64_t stride);
void _darray_destroy(void* array);

uint64_t _darray_field_get(void* array, uint64_t field);
void _darray_field_set(void* array, uint64_t field, uint64_t value);

void* _darray_resize(void* array);

void* _darray_push(void* array, const void* value_ptr);
void _darray_pop(void* array, void* dest);

void* _darray_pop_at(void* array, uint64_t index, void* dest);
void* _darray_push_at(void* array, uint64_t index, void* value_ptr);

#define DARRAY_DEFAULT_CAPACITY 2
#define DARRAY_RESIZE_FACTOR 2

#define darray_create(type) \
    _darray_create(DARRAY_DEFAULT_CAPACITY, sizeof(type))

#define darray_reserve(type, capacity) \
    _darray_create(capacity, sizeof(type))

#define darray_destroy(array) _darray_destroy(array)

#define darray_push(array, value)                \
    do {                                         \
        typeof(value) temp = value;              \
        array = _darray_push(array, &temp);      \
    } while (0)

#define darray_pop(array, value_ptr) \
    _darray_pop(array, value_ptr)

#define darray_push_at(array, index, value)       \
    do {                                               \
        typeof(value) temp = value;                 \
        array = _darray_push_at(array, index, &temp); \
    } while(0)

#define darray_pop_at(array, index, value_ptr) \
    _darray_pop_at(array, index, value_ptr)

#define darray_clear(array) \
    _darray_field_set(array, DARRAY_LENGTH, 0)

#define darray_capacity(array) \
    _darray_field_get(array, DARRAY_CAPACITY)

#define darray_length(array) \
    _darray_field_get(array, DARRAY_LENGTH)

#define darray_stride(array) \
    _darray_field_get(array, DARRAY_STRIDE)

#define darray_length_set(array, value) _darray_field_set(array, DARRAY_LENGTH, value)