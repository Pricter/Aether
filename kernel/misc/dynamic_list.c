#include <stdint.h>
#include <stddef.h>
#include <kernel/mmu.h>
#include <kernel/dlist.h>
#include <kernel/kprintf.h>

dlist_node_t* dlist_create_empty() {
	dlist_node_t* head = malloc(sizeof(dlist_node_t));
	head->previous = NULL;
	head->next = NULL;
	head->item = NULL;

	return head;
}

void dlist_push_back(dlist_node_t* head, void* item) {
	dlist_node_t* end = head;
	while(end->next != NULL) end = end->next;

	dlist_node_t* new = malloc(sizeof(dlist_node_t));

	new->item = item;
	new->next = NULL;
	new->previous = end;

	end->next = new;
}

void* dlist_get_item(dlist_node_t* head, uint64_t index) {
	uint64_t _index = 0;
	dlist_node_t* current = head;
	while((_index <= index) && current->next != NULL) {
	 	current = current->next;
	 	_index++;
	}
	return current->item;
}

void dlist_push_front(dlist_node_t* head, void* item) {
	dlist_node_t* new = malloc(sizeof(dlist_node_t));
	new->next = head->next;
	new->previous = head;
	new->item = item;

	if(head->next != NULL) {
		head->next->previous = new;
	}
	head->next = new;
}