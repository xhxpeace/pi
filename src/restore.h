/*
 * restore.h
 *
 *  Created on: Nov 27, 2013
 *      Author: fumin
 */

#ifndef RESTORE_H_
#define RESTORE_H_
#define THREAD_NUM 1 
#include "utils/sync_queue.h"

SyncQueue *restore_chunk_queue;
SyncQueue *restore_recipe_queue;

static pthread_t restore_data_t[THREAD_NUM];

void* assembly_restore_thread(void *arg);
void* optimal_restore_thread(void *arg);

#endif /* RESTORE_H_ */
