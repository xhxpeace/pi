#include "destor.h"
#include "jcr.h"
#include "backup.h"

static pthread_t hash_t;
static int64_t chunk_num;


void get_gray(unsigned char *gray,struct chunk *c){
	int i;
	int j=0;
	for(i=0;i<c->size;i+=3){
		gray[j++]=(c->data[i]*38+c->data[i+1]*75+c->data[i+2]*15)>>7;
	}
	//printf("gray[0]=%d\n",gray[0]);

}

int avg_gray(unsigned char *gray,int offset,int width,int h_space,int w_space){
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

void avgs_gray(unsigned char *avg,unsigned char *gray,struct chunk *c){
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
void accuracy_control(unsigned char *avg){
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
void set_inbuf(unsigned char **inbuf,struct chunk *c){
	int i,j,k;
	int column3=c->column*3;
	unsigned char *pdata=c->data;
	k=0;
	for(i=0;i<c->row;i++){
		memcpy(inbuf[i],pdata,column3);
		pdata += column3;
	}
}	

int offset_of_mark0xffda(unsigned char *data,int len){
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

static void* sha1_thread(void* arg) {
	char code[41];
	int quality=90;
	while (1) {
		struct chunk* c = sync_queue_pop(chunk_queue);
		if (c == NULL) {
			sync_queue_term(hash_queue);
			break;
		}

		if (CHECK_CHUNK(c, CHUNK_FILE_START) || CHECK_CHUNK(c, CHUNK_FILE_END)) {
			if(CHECK_CHUNK(c, CHUNK_FILE_START)&&PIC_CHUNK_YES_OR_NO&&c->row!=0) {
				quality=(int)c->data[c->size-2];
			}
			sync_queue_push(hash_queue, c);
			continue;
		}	

		unsigned char *avg=(unsigned char *)malloc(HASH_SIZE*sizeof(unsigned char));
		memset(avg,0,HASH_SIZE);

		if(PIC_CHUNK_YES_OR_NO&&c->row!=0){	
			//can't use inbuf=malloc_2_array();
			unsigned char **inbuf=(unsigned char **)malloc(c->row*sizeof(unsigned char *));
			int i;
			for(i=0;i<c->row;i++)
				inbuf[i]=(unsigned char *)malloc(c->column*3*sizeof(unsigned char));
			set_inbuf(inbuf,c);

			//compress bit map chunk
			unsigned char *outbuf=NULL;
			TIMER_DECLARE(1);
			TIMER_BEGIN(1);
			int len=write_to_mem(outbuf,inbuf,quality,c->column,c->row);
			TIMER_END(1,jcr.compre_time);
			if(c->row==PIC_CHUNK_ROW&&c->column==PIC_CHUNK_ROW){
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
			c->data=NULL;
			c->data=(unsigned char *)malloc((rlen+4)*sizeof(unsigned char));	
			for(m=0;m<rlen;m++)
				c->data[m]=outbuf[len+m];
			c->data[m]=(unsigned char)c->row;
			c->data[m+1]=(unsigned char)c->column;
			c->data[m+2]=(unsigned char)quality;
			c->data[m+3]='\0';
			c->size=rlen+4;

			free(outbuf);
			free_2_array(inbuf,c->row);						
		}
		
		TIMER_DECLARE(1);
		TIMER_BEGIN(1);
		SHA_CTX ctx;
		SHA_Init(&ctx);
		
		if(c->row!=PIC_CHUNK_ROW||c->row!=PIC_CHUNK_ROW) 
			SHA_Update(&ctx, c->data, c->size);
		else SHA_Update(&ctx, avg, HASH_SIZE);
		SHA_Final(c->fp, &ctx);
		TIMER_END(1, jcr.hash_time);

		jcr.chunk_num++;
		hash2code(c->fp, code);
		code[40] = 0;
		VERBOSE("Hash phase: %ldth chunk identified by %s", chunk_num++, code);

		sync_queue_push(hash_queue, c);
		free(avg);
	}

	return NULL;
}

void start_hash_phase() {
	hash_queue = sync_queue_new(1000);
	pthread_create(&hash_t, NULL, sha1_thread, NULL);
}

void stop_hash_phase() {
	pthread_join(hash_t, NULL);
}



