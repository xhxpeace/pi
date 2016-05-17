/*
 * queue.c
 *
 *  Created on: May 21, 2012
 *      Author: fumin
 */

#include <stdlib.h>
#include <stdio.h>
#include "queue.h"

Queue* queue_new() {
	Queue *queue = (Queue*) malloc(sizeof(Queue));
	queue->first = queue->last = 0;
	queue->elem_num = 0;
	queue->file_num = 0;
	return queue;
}

void queue_init(Queue *queue) {
	queue->first = queue->last = 0;
	queue->elem_num = 0;
	queue->file_num = 0;
}

void queue_empty(Queue *queue, void (*free_data)(void*)) {
	while (queue->elem_num) {
		void *data = queue_pop(queue);
		free_data(data);
	}
}

void queue_free(Queue *queue, void (*free_data)(void*)) {
	queue_empty(queue, free_data);
	free(queue);
}

void queue_push(Queue *queue, void *element) {
	queue_ele_t *item;

	if ((item = (queue_ele_t *) malloc(sizeof(queue_ele_t))) == 0) {
		puts("Not enough memory!");
		return;
	}
	item->data = element;
	item->next = 0;

	/* Add to end of queue */
	if (queue->first == 0) {
		queue->first = item;
	} else {
		queue->last->next = item;
	}
	queue->last = item;

	++queue->elem_num;
}


void* queue_pop(Queue *queue) {
	queue_ele_t *item = 0;
	if (queue->elem_num == 0)
		return NULL;

	item = queue->first;

	queue->first = item->next;
	if (queue->last == item)
		queue->last = NULL;
	--queue->elem_num;

	void *ret = item->data;
	free(item);
	return ret;
}

void sub_queue_push(Queue *queue, Queue *sub) {
	queue->last->next = sub->first;
	queue->last = sub->last;
	queue->elem_num += sub->elem_num;
	sub->first = NULL;
	sub->last = NULL;
	sub->elem_num = 0;
}

//pop a sub queue
int  sub_queue_pop(Queue *queue,Queue *sub) {
	if(queue->elem_num == 0){
		return 0;
	}
	queue_ele_t *first = queue->first;
	struct chunk *c = (struct chunk *)(first->data);
	if(!CHECK_CHUNK(c,CHUNK_FILE_START)){
		printf("Lost file start\n");
		exit(-1);
	}

	int count=1;
	queue_ele_t *p=first->next;
     while(1){
    		if(p==NULL){
	    		printf("Lost file end\n");
	    		exit(-1);
	    	}
	    	count++;
    		c = (struct chunk *)(p->data);
    		if(CHECK_CHUNK(c,CHUNK_FILE_END)){
	    		break;
	    	}
	        	
	    	p=p->next;
    }
    sub->first=first;
    sub->last=p;
    sub->elem_num=count;

    queue->first=p->next;
    if(queue->last == p)
    	queue->last == NULL;
    queue->elem_num-=count;
    return 1;

}

void * queue_top(Queue *queue) {
	if (queue->elem_num == 0)
		return NULL;
	return queue->first->data;
}

int queue_size(Queue *queue) {
	return queue->elem_num;
}

void queue_foreach(Queue *queue, void (*func)(void *data, void *user_data),
		void *user_data) {
	queue_ele_t *item = 0;
	if (queue->elem_num == 0)
		return;
	item = queue->first;
	while (item) {
		func(item->data, user_data);
		item = item->next;
	}
}

/*
 * return the nth elem in queue.
 */
void* queue_get_n(Queue *queue, int n) {
	if (n >= queue_size(queue)) {
		return NULL;
	}
	int i = 0;
	queue_ele_t *item = queue->first;
	while (i < n) {
		item = item->next;
		++i;
	}
	return item->data;

}

/*
 * Iterate the Queue to find an elem which meets the condition ('hit' returns 1).
 */
void* queue_find(Queue* queue, int (*hit)(void*, void*), void* data) {

	queue_ele_t *item = 0;
	if (queue->elem_num == 0)
		return NULL;

	item = queue->first;
	do {
		if (hit(item->data, data) == 1)
			break;
	} while ((item = item->next));

	return item ? item->data : NULL;

}
