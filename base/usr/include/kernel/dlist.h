#pragma once

typedef struct dlist_node {
	struct dlist_node* previous;
	void* item;
	struct dlist_node* next;
} dlist_node_t;

dlist_node_t* dlist_create_empty();
void dlist_push_back(dlist_node_t* head, void* item);
void dlist_push_front(dlist_node_t* head, void* item);
void* dlist_get_item(dlist_node_t* head, uint64_t index);