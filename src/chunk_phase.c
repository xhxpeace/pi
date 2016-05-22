#include "destor.h"
#include "jcr.h"
#include "utils/rabin_chunking.h"
#include "backup.h"

#define CHUNK_THREAD_NUM 2
static pthread_t chunk_t[CHUNK_THREAD_NUM];
static int64_t chunk_num;

static void get_gray(unsigned char *gray,struct chunk *c){
	int i;
	int j=0;
	for(i=0;i<c->size;i+=3){
		gray[j++]=(c->data[i]*38+c->data[i+1]*75+c->data[i+2]*15)>>7;
	}
	//printf("gray[0]=%d\n",gray[0]);

}

static int avg_gray(unsigned char *gray,int offset,int width,int h_space,int w_space){
	int avg=0;
	int i=0;
	int j;
	while(i<h_space){
		j=0;
		while(j<w_space){
			avg+=gray[offset+j];
			j++;
		}
		offset+=width;
		i++;
	}
	int hw=h_space*w_space;
	float favg=avg*1.0/hw;
	int iavg=avg/hw;
	int less1=(int)((favg-iavg)*100);
	if(less1<=80)  avg=iavg;
	else avg=iavg+1;
	//avg/=(h_space*w_space);// judge similar chunk by this "avg"
	//printf("%d ",avg );
	return (unsigned char)avg;
}

static void avgs_gray(unsigned char *avg,unsigned char *gray,struct chunk *c){
	int h_space=c->row/SQUARE_HASH_SIZE;
	if(h_space<1) h_space=1;
	int w_space=c->column/SQUARE_HASH_SIZE;
	if(w_space<1) w_space=1;

	int w_offset,h_offset;
	int h_space_w=h_space*c->column;
	int h_w=c->row*c->column;

	int k=0;
	h_offset=0;
	while(h_offset<h_w){
		w_offset=0;
		while(w_offset<c->column){
			avg[k++]=avg_gray(gray,h_offset+w_offset,c->column,h_space,w_space);
			w_offset+=w_space;
		}	
		h_offset+=h_space_w;
	}
	//printf("\n\n");
	//printf("h_space=%d w_space=%d avg=%d\n",h_space,w_space,avg[0] );
}
static void accuracy_control(unsigned char *avg){
	int i=1;
	int count=0;
	unsigned char avg0=avg[0];
	unsigned char b,c;

	while(i<HASH_SIZE && count<10){
		if(abs(avg[i]-avg0)>5) count++;
		i++;
	}
	if(count==10){//the chunk which is not near pure
		for(i=0;i<HASH_SIZE;i++){
			c=avg[i]%16;
			if(c<5) c=2;//0~4
			else if(c<10) c=7;//5~9
			else c=12;//10~15
			b=avg[i]>>4;
			avg[i]=(b<<4)+c;
		}
	}
}
static void set_inbuf(unsigned char **inbuf,struct chunk *c){
	int i,j,k;
	int column3=c->column*3;
	unsigned char *pdata=c->data;
	k=0;
	for(i=0;i<c->row;i++){
		memcpy(inbuf[i],pdata,column3);
		pdata += column3;
	}
}	

static int offset_of_mark0xffda(unsigned char *data,int len){
	int m=0;
	while(m<len){
		if(data[m]==255&&data[m+1]==218){
			break;
		}
		m++;
	}
	//printf("hash_phase/offset_of_mark0xffda:m=%d\n",m);
	return m;
}

static void set_hash(Queue *queue){
	char code[41];
	int quality=90;
	int flag=0;

	queue_ele_t *p=queue->first;//FILE_START
	struct chunk *c = (struct chunk *)(p->data);
	if(PIC_CHUNK_YES_OR_NO && c->row!=0){
		quality=(int)c->data[c->size-2];
		flag=1;
	}

	p=p->next;
	unsigned char *avg=(unsigned char *)malloc(HASH_SIZE*sizeof(unsigned char));
	//memset(avg,0,HASH_SIZE); 

	while(p != queue->last){
		c = (struct chunk *)(p->data);
		if(flag){
			unsigned char *outbuf=NULL;
			/*TIMER_DECLARE(1);
			TIMER_BEGIN(1);*/
			int len=write_to_mem(&outbuf,c->data,quality,c->column,c->row);
			//TIMER_END(1,jcr.compre_time);
			if(c->row==PIC_CHUNK_ROW && c->column==PIC_CHUNK_ROW){
				unsigned char *gray=(unsigned char *)malloc(c->row*c->column*sizeof(unsigned char));
				get_gray(gray,c);				
				avgs_gray(avg,gray,c);
				accuracy_control(avg);
				free(gray);
			}
			//update c->data
			int m=609;//int m=offset_of_mark0xffda(outbuf,len);m is always 609
			int rlen=len-m;
			len=m;
			free(c->data);
			c->data=(unsigned char *)malloc((rlen+4)*sizeof(unsigned char));	
			memcpy(c->data,outbuf+len,rlen);
			c->data[rlen]=(unsigned char)c->row;
			c->data[rlen+1]=(unsigned char)c->column;
			c->data[rlen+2]=(unsigned char)quality;
			c->data[rlen+3]='\0';
			c->size=rlen+4;
			free(outbuf);
		}

		/*TIMER_DECLARE(1);
		TIMER_BEGIN(1);*/
		SHA_CTX ctx;
		SHA_Init(&ctx);	

		if(c->row == PIC_CHUNK_ROW && c->column == PIC_CHUNK_ROW) {
			SHA_Update(&ctx, avg, HASH_SIZE);
		}
		else SHA_Update(&ctx, c->data, c->size);
		SHA_Final(c->fp, &ctx);
		//TIMER_END(1, jcr.hash_time);
		
		hash2code(c->fp, code);

		p=p->next;	

	}//end while(p != queue->last)
	free(avg);
}

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
		int isJpgFile = c->row;
				
		if(PIC_CHUNK_YES_OR_NO && isJpgFile != 0){
			struct chunk *fc = queue_pop(read_sub);//jpg data

			quality = read_quality(fc->data,fc->size);
			quality = set_quality(quality);
			c->data[c->size-2]=(unsigned char)quality;//put quality of jpg to string of filename 
			
			if(!mem_read_chunk_jpeg(fc,c,chunk_sub))
				exit(-1);
			free_chunk(fc);

			c = queue_pop(read_sub);//FILE_END
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
		set_hash(chunk_sub);
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
	int i;
	for(i = 0; i < CHUNK_THREAD_NUM ; i++){
		pthread_join(chunk_t[i], NULL);
	}	
}
