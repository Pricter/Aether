#include <kernel/dlist.h>
#include <stddef.h>
#include <memory.h>
#include <kernel/mmu.h>
#include <kernel/kprintf.h>

dlist_node_t* dlist_create_empty(void) {
	dlist_node_t* node = malloc(sizeof(dlist_node_t));
	memset((void*)node, 0, sizeof(dlist_node_t));
	return node;
}

void dlist_push_back(dlist_node_t* head, void* item) {
	dlist_node_t* node = malloc(sizeof(dlist_node_t));
	memset((void*)node, 0, sizeof(dlist_node_t));
	node->item = item;

	dlist_node_t* end = head;

	while(end->next != NULL) end = end->next;
	end->next = node;
	node->previous = end;
}

void dlist_push_front(dlist_node_t* head, void* item) {
	dlist_node_t* node = malloc(sizeof(dlist_node_t));
	memset((void*)node, 0, sizeof(dlist_node_t));
	node->item = item;

	head->next->previous = node;
	node->previous = head;
	node->next = head->next;
	head->next = node;
}

void* dlist_get_item(dlist_node_t* head, uint64_t index) {
	if(head->next == NULL) return NULL;
	dlist_node_t* toGet = head->next;
	if(index == 0) return toGet->item;
	uint64_t count = 0;

	uint64_t length = DLIST_LENGTH(head);

	while(count < length) {
		if(count == index) return toGet->item;
		toGet = toGet->next;
		count++;
	}

	return NULL;
}

void dlist_destroy_item(dlist_node_t* head, uint64_t index) {
	dlist_node_t* node = dlist_remove_item(head, index);
	if(node != NULL) free(node);
}

dlist_node_t* dlist_remove_item(dlist_node_t* head, uint64_t index) {
	if(head->next == NULL) return NULL;
	dlist_node_t* toGet = head->next;
	uint64_t count = 0;

	while(toGet->next != NULL) {
		if(count == index) {
			toGet->previous->next = toGet->next;
			if(toGet->next != NULL) toGet->next->previous = toGet->previous;
			return toGet;
		}
		toGet = toGet->next;
		count++;
	}

	return NULL;
}

uint64_t dlist_length(dlist_node_t* head) {
	uint64_t count = 0;
	dlist_node_t* current = head->next;
	dlist_node_t* end = head->next;
	if(end == NULL) return 0;

	while(end->next != NULL) {
		end = end->next;
		count++;
	}

	return ++count;
}