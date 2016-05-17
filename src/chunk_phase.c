#include "destor.h"
#include "jcr.h"
#include "utils/rabin_chunking.h"
#include "backup.h"

#define CHUNK_THREAD_NUM 2
static pthread_t chunk_t[CHUNK_THREAD_NUM];
static int64_t chunk_num;

static void* chunk_thread(void *arg) {
	//printf("haha chunk_thread\n");	
	int leftlen = 0;
	int leftoff = 0;
	unsigned char *leftbuf = malloc(DEFAULT_BLOCK_SIZE + destor.chunk_max_size);

	unsigned char *zeros = malloc(destor.chunk_max_size);
	bzero(zeros, destor.chunk_max_size);
	unsigned char *data = malloc(destor.chunk_max_size);
	//unsigned char *filename=NULL;
	struct chunk* c = NULL;
	Queue *read_sub = queue_new();
	Queue *chunk_sub = queue_new();
	int is_end = 1;
	int quality = 90;
	while (1) {

		is_end = sync_subQueue_pop(read_queue,read_sub);

		if (is_end == 0) {
			sync_queue_term(chunk_queue);
			break;
		}

		c = queue_pop(read_sub);//jpg FILE_START
		assert(CHECK_CHUNK(c, CHUNK_FILE_START));
		/*filename=malloc(strlen(c->data)+1);
		filename[strlen(c->data)]='\0';
		strcpy(filename,c->data);*/
		int isJpgFile = c->row;
				
		if(PIC_CHUNK_YES_OR_NO && isJpgFile != 0){
			struct chunk *fc = queue_pop(read_sub);//jpg data
			
			quality = read_quality(fc->data,fc->size);
			quality = set_quality(quality);
			c->data[c->size-2]=(unsigned char)quality;//put quality of jpg to string of filename 
			
			if(!mem_read_chunk_jpeg(fc,c,chunk_sub))
				exit(-1);
			free_chunk(fc);
		}
		else{
			queue_push(chunk_sub, c);//FILE_START
			/* Try to receive normal chunks. */
			c = queue_pop(read_sub);
			if (!CHECK_CHUNK(c, CHUNK_FILE_END)) {
				memcpy(leftbuf, c->data, c->size);
				leftlen += c->size;
				free_chunk(c);
				c = NULL;
			}

			while (1) {
				/* c == NULL indicates more data for this file can be read. */
				if ((leftlen < destor.chunk_max_size) && c == NULL) {
					c = queue_pop(read_sub);
					if (!CHECK_CHUNK(c, CHUNK_FILE_END)) {
						memmove(leftbuf, leftbuf + leftoff, leftlen);
						leftoff = 0;
						memcpy(leftbuf + leftlen, c->data, c->size);
						leftlen += c->size;
						free_chunk(c);
						c = NULL;
					}
				}

				if (leftlen == 0) {
					assert(c);
					break;
				}

				TIMER_DECLARE(1);
				TIMER_BEGIN(1);

				int chunk_size = 0;
				if (destor.chunk_algorithm == CHUNK_RABIN
						|| destor.chunk_algorithm == CHUNK_NORMALIZED_RABIN)
					chunk_size = rabin_chunk_data(leftbuf + leftoff, leftlen);
				else
					chunk_size = destor.chunk_avg_size > leftlen ?
									leftlen : destor.chunk_avg_size;

				TIMER_END(1, jcr.chunk_time);

				struct chunk *nc = new_chunk(chunk_size);
				memcpy(nc->data, leftbuf + leftoff, chunk_size);
				leftlen -= chunk_size;
				leftoff += chunk_size;

				if (memcmp(zeros, nc->data, chunk_size) == 0) {
					VERBOSE("Chunk phase: %ldth chunk  of %d zero bytes",
							chunk_num++, chunk_size);
					jcr.zero_chunk_num++;
					jcr.zero_chunk_size += chunk_size;
				} else
					VERBOSE("Chunk phase: %ldth chunk of %d bytes", chunk_num++,
							chunk_size);

				queue_push(chunk_sub, nc);
			}

			leftoff = 0;
		}

		queue_push(chunk_sub, c);//FILE_END		
		windows_reset();
		c = NULL;
		sync_subQueue_push(chunk_queue,chunk_sub);		
	}

	free(read_sub);
	free(chunk_sub);
	free(leftbuf);
	free(zeros);
	free(data);
	//free(filename);
	return NULL;
}

void start_chunk_phase() {
	//printf("start chunk_phase\n");
	assert(destor.chunk_avg_size > destor.chunk_min_size);
	assert(destor.chunk_avg_size < destor.chunk_max_size);
	chunkAlg_init();
	chunk_queue = sync_queue_new(10000);
	int i;
	for(i = 0; i < CHUNK_THREAD_NUM ; i++){
		pthread_create(&chunk_t[i], NULL, chunk_thread, NULL);
	}
	
	//printf("new chunk_thread success\n");
}

void stop_chunk_phase() {
	for(i = 0; i < CHUNK_THREAD_NUM ; i++){
		pthread_join(chunk_t[i], NULL);
	}	
}
