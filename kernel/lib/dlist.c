#include <kernel/dlist.h>
#include <kernel/heap.h>

/* Allocate an empty list; the caller owns it and must destroy it. */
dlist_t *dlist_create(void) {
    dlist_t *list = malloc(sizeof(dlist_t));

    if (list) {
        list->head = NULL;
        list->tail = NULL;
        list->length = 0;
    }

    return list;
}

/* Free wrapper nodes and the list; stored values remain caller-owned. */
void dlist_destroy(dlist_t *list) {
    dlist_node_t *current_node = list->head;

    while (current_node) {
        dlist_node_t *next = current_node->next;
        free(current_node);
        current_node = next;
    }

    free(list);
}

/* Append a borrowed value; return false without changing the list on exhaustion. */
bool dlist_push_back(dlist_t *list, void *value) {
    dlist_node_t *node = malloc(sizeof(dlist_node_t));

    if (!node) {
        return false;
    }

    node->value = value;
    node->next = NULL;
    node->prev = list->tail;

    if (list->tail) {
        list->tail->next = node;
    } else {
        list->head = node;
    }

    list->tail = node;
    list->length++;

    return true;
}

/* Prepend a borrowed value; return false without changing the list on exhaustion. */
bool dlist_push_front(dlist_t *list, void *value) {
    dlist_node_t *node = malloc(sizeof(dlist_node_t));

    if (!node) {
        return false;
    }

    node->value = value;
    node->prev = NULL;
    node->next = list->head;

    if (list->head) {
        list->head->prev = node;
    } else {
        list->tail = node;
    }

    list->head = node;
    list->length++;

    return true;
}

/* Remove the last wrapper and return its borrowed value, or NULL if empty. */
void *dlist_pop_back(dlist_t *list) {
    if (!list->tail) {
        return NULL;
    }

    dlist_node_t *node = list->tail;
    void *value = node->value;
    list->tail = node->prev;

    if (list->tail) {
        list->tail->next = NULL;
    } else {
        list->head = NULL;
    }

    list->length--;
    free(node);

    return value;
}

/* Remove the first wrapper and return its borrowed value, or NULL if empty. */
void *dlist_pop_front(dlist_t *list) {
    if (!list->head) {
        return NULL;
    }

    dlist_node_t *node = list->head;
    void *value = node->value;
    list->head = node->next;

    if (list->head) {
        list->head->prev = NULL;
    } else {
        list->tail = NULL;
    }

    list->length--;
    free(node);

    return value;
}

/* Return the entry count; callers provide external synchronization. */
size_t dlist_get_length(dlist_t *list) {
    return list->length;
}

/* Return a borrowed indexed value, or NULL when the index is out of range. */
void *dlist_get(dlist_t *list, size_t index) {
    if (index >= list->length) {
        return NULL;
    }

    dlist_node_t *current_node = list->head;

    for (size_t entry_index = 0; entry_index < index; entry_index++) {
        current_node = current_node->next;
    }

    return current_node->value;
}

/* Remove the first matching wrapper without freeing its borrowed value. */
void dlist_remove(dlist_t *list, void *value) {
    for (dlist_node_t *current_node = list->head; current_node; current_node = current_node->next) {
        if (current_node->value != value) {
            continue;
        }

        if (current_node->prev) {
            current_node->prev->next = current_node->next;
        } else {
            list->head = current_node->next;
        }

        if (current_node->next) {
            current_node->next->prev = current_node->prev;
        } else {
            list->tail = current_node->prev;
        }

        list->length--;
        free(current_node);

        return;
    }
}
