#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Normal-context, externally synchronized wrapper list. Creation and pushes
 * allocate; destroy frees wrappers only. Values are borrowed throughout.
 * Pushes return false on allocation failure without modifying the list. */

typedef struct dlist_node {
    struct dlist_node *prev;
    struct dlist_node *next;
    void *value;
} dlist_node_t;

typedef struct dlist {
    dlist_node_t *head;
    dlist_node_t *tail;
    size_t length;
} dlist_t;

dlist_t *dlist_create(void);
void dlist_destroy(dlist_t *list);

bool dlist_push_back(dlist_t *list, void *value);
bool dlist_push_front(dlist_t *list, void *value);
void *dlist_pop_back(dlist_t *list);
void *dlist_pop_front(dlist_t *list);

void *dlist_get(dlist_t *list, size_t index);
size_t dlist_get_length(dlist_t *list);
void dlist_remove(dlist_t *list, void *value);

/* Convenience aliases */
#define dlist_push(list, val) dlist_push_back(list, val)
#define dlist_pop(list) dlist_pop_back(list)
