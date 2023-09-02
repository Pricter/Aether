#include <kernel/dlist.h>
#include <memory.h>
#include <kernel/mmu.h>
#include <kernel/kprintf.h>

node_t* dlist_create_empty() {
    node_t* head = (node_t*)malloc(sizeof(node_t));
    if (head) {
        head->previous = NULL;
        head->value = NULL;
        head->next = NULL;
    }
    return head;
}

uint64_t dlist_get_length(node_t* head) {
    uint64_t length = 0;
    node_t* current = head->next;
    while (current != NULL) {
        length++;
        current = current->next;
    }
    return length;
}

void dlist_push(node_t* head, void* value) {
    node_t* new_node = (node_t*)malloc(sizeof(node_t));
    if (new_node) {
        new_node->previous = head;
        new_node->value = value;
        new_node->next = NULL;

        // Find the last node in the list
        node_t* current = head;
        while (current->next != NULL) {
            current = current->next;
        }

        current->next = new_node;
    }
}

node_t* dlist_pop(node_t* head) {
    node_t* current = head;
    while (current->next != NULL) {
        current = current->next;
    }

    if (current->previous != NULL) {
        current->previous->next = NULL;
    }

    return current;
}

void dlist_push_at(node_t* head, void* value, uint64_t index) {
    uint64_t length = dlist_get_length(head);

    if (index > length) {
        kprintf("Index out of bounds\n");
        return;
    }

    node_t* new_node = (node_t*)malloc(sizeof(node_t));
    if (new_node) {
        new_node->value = value;

        if (index == 0) {
            new_node->previous = head;
            new_node->next = head->next;
            if (head->next != NULL) {
                head->next->previous = new_node;
            }
            head->next = new_node;
        } else {
            node_t* current = head->next;
            for (uint64_t i = 0; i < index - 1; i++) {
                current = current->next;
            }

            new_node->previous = current;
            new_node->next = current->next;
            if (current->next != NULL) {
                current->next->previous = new_node;
            }
            current->next = new_node;
        }
    }
}

node_t* dlist_pop_at(node_t* head, uint64_t index) {
    uint64_t length = dlist_get_length(head);

    if (index >= length) {
        kprintf("Index out of bounds\n");
        return NULL;
    }

    if (index == 0) {
        return dlist_pop(head);
    } else {
        node_t* current = head->next;
        for (uint64_t i = 0; i < index - 1; i++) {
            current = current->next;
        }

        node_t* popped_node = current->next;
        current->next = popped_node->next;
        if (popped_node->next != NULL) {
            popped_node->next->previous = current;
        }

        return popped_node;
    }
}

void dlist_destroy_item(node_t* head, uint64_t index) {
    node_t* item = dlist_pop_at(head, index);
    if (item) {
        free(item->value);
        free(item);
    }
}

void dlist_destroy_array(node_t* head) {
    while (head->next != NULL) {
        dlist_destroy_item(head, 0);
    }
    free(head);
}

void* dlist_get(node_t* head, uint64_t index) {
    uint64_t length = dlist_get_length(head);

    if (index >= length) {
        kprintf("Index out of bounds\n");
        return NULL;
    }

    node_t* current = head->next;
    for (uint64_t i = 0; i < index; i++) {
        current = current->next;
    }

    return current->value;
}