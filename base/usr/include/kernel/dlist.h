#pragma once

#include <stdint.h>

typedef struct node {
	struct node* previous;
	void* value;
	struct node* next;
} node_t;

node_t* dlist_create_empty();
uint64_t dlist_get_length(node_t* head);
void dlist_push(node_t* head, void* value); // Push from last
node_t* dlist_pop(node_t* head); // Pop from last
void dlist_push_at(node_t* head, void* value, uint64_t index);
node_t* dlist_pop_at(node_t* head, uint64_t index);
void dlist_destroy_item(node_t* head, uint64_t index);
void dlist_destroy_array(node_t* head);
void* dlist_get(node_t* head, uint64_t index);
void dlist_remove_item(node_t* head, void* item);