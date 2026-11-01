#pragma once

/*
 * list.head - allocation-free intrusive doubly-linked list
 *
 * Design: embed list_node_t directly inside the owning struct.
 * The list never allocates; node memory is part of the object.
 * Use list_entry() to recover the containing struct from a node pointer.
 *
 * Invariants:
 *   - An empty list has head.next == head.prev == &head (circular sentinel).
 *   - A node not on any list should be initialised with LIST_NODE_INIT or
 *     list_node_init() so its pointers are non-garbage.
 *   - Callers are responsible for external synchronisation; these operations
 *     are NOT thread-safe and must be called under the appropriate lock.
 *   - list_entry() is only valid when the node IS on the list (or known to
 *     point to a valid containing struct).
 *
 * Contrast with dlist_t (kernel/lib/dlist.c):
 *   dlist_t is a non-intrusive list that allocates a wrapper node per entry.
 *   It is suitable for use during normal-context initialization paths where allocation overhead is acceptable.
 *   list_t (this header) is for hot paths - scheduler run queues, wait
 *   queues, timer lists - where allocations inside the data structure itself
 *   are a design violation.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* -- Node -------------------------------------------------------------- */

typedef struct list_node {
    struct list_node *next;
    struct list_node *prev;
} list_node_t;

/* Initialise a node that is not yet on any list */
#define LIST_NODE_INIT(node) {.next = &(node), .prev = &(node)}

static inline void list_node_init(list_node_t *node) {
    node->next = node;
    node->prev = node;
}

/* -- Head (alias for node, used to make intent clear) ------------------ */

typedef list_node_t list_head_t;

#define LIST_HEAD_INIT(head) LIST_NODE_INIT(head)

static inline void list_head_init(list_head_t *head) {
    list_node_init(head);
}

/* -- Entry recovery ---------------------------------------------------- */

/*
 * list_entry - recover the pointer to the containing struct.
 *
 *   node_pointer    : pointer to the list_node_t member
 *   type   : type of the containing struct
 *   member : name of the list_node_t member within that struct
 */
#define list_entry(node_pointer, type, member)                                                     \
    ((type *)((uintptr_t)(node_pointer) - offsetof(type, member)))

/* -- Predicates -------------------------------------------------------- */

static inline bool list_empty(const list_head_t *head) {
    return head->next == head;
}

/* -- Insert / remove --------------------------------------------------- */

/* Insert 'node' between 'prev' and 'next' */
static inline void _list_insert(list_node_t *prev, list_node_t *next, list_node_t *node) {
    node->prev = prev;
    node->next = next;
    prev->next = node;
    next->prev = node;
}

/* Add 'node' at the front of the list headed by 'head' (new head->next) */
static inline void list_push_front(list_head_t *head, list_node_t *node) {
    _list_insert(head, head->next, node);
}

/* Add 'node' at the back of the list headed by 'head' (new head->prev) */
static inline void list_push_back(list_head_t *head, list_node_t *node) {
    _list_insert(head->prev, head, node);
}

/* Remove 'node' from whichever list it is on */
static inline void list_remove(list_node_t *node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;
    list_node_init(node); /* leave in a safe, detached state */
}

/* -- Pop --------------------------------------------------------------- */

/*
 * list_pop_front - remove and return the first node, or NULL if empty.
 * The returned node is in detached state (next == prev == self).
 */
static inline list_node_t *list_pop_front(list_head_t *head) {
    if (list_empty(head)) {
        return NULL;
    }

    list_node_t *node = head->next;
    list_remove(node);

    return node;
}

/* -- Iteration --------------------------------------------------------- */

/*
 * list_for_each - iterate over all nodes.
 * 'cursor' is a list_node_t * cursor; do not modify the list inside the loop
 * unless using list_for_each_safe.
 */
#define list_for_each(cursor, head)                                                                \
    for ((cursor) = (head)->next; (cursor) != (head); (cursor) = (cursor)->next)

/*
 * list_for_each_safe - iteration safe against removal of 'cursor' inside the
 * loop body.  'next_cursor' is a list_node_t * scratch variable.
 */
#define list_for_each_safe(cursor, next_cursor, head)                                              \
    for ((cursor) = (head)->next, (next_cursor) = (cursor)->next; (cursor) != (head);              \
        (cursor) = (next_cursor), (next_cursor) = (cursor)->next)
