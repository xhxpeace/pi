#include "destor.h"
#include "jcr.h"
#include "backup.h"

static pthread_t hash_t;
static int64_t chunk_num;

static void* sha1_thread(void* arg) {
	while (1) {
		struct chunk* c = sync_queue_pop(chunk_queue);

		if (c == NULL) {
			sync_queue_term(hash_queue);
			break;
		}

		if (!CHECK_CHUNK(c, CHUNK_FILE_START) && !CHECK_CHUNK(c, CHUNK_FILE_END)) {
			jcr.chunk_num++;
			chunk_num++;
		}
		sync_queue_push(hash_queue, c);
	}
}

void start_hash_phase() {
	hash_queue = sync_queue_new(1000);
	pthread_create(&hash_t, NULL, sha1_thread, NULL);
}

void stop_hash_phase() {
	pthread_join(hash_t, NULL);
}



