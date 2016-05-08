#include "destor.h"
#include "jcr.h"
#include "recipe/recipestore.h"
#include "storage/containerstore.h"
#include "utils/lru_cache.h"
#include "restore.h"

unsigned char *get_head(int quality,int *head_len){
	unsigned char qua[3]={'\0'};
	itoa10(quality,qua);
	unsigned char headpath[60]="/picdedup/destor-master1/picheader/head";
	int headlen=strlen(headpath);

	headpath[headlen]=qua[0];
	headpath[headlen+1]=qua[1];
	headpath[headlen+2]='\0';
	FILE *fp1=fopen(headpath,"rb");
	fread(&headlen,sizeof(headlen),1,fp1);//读头信息长度
	unsigned char *header=malloc(headlen);
	memset(header,'\0',headlen);
	fread(header,headlen,1,fp1);
	fclose(fp1);
	*head_len=headlen;
	return header;
}

static void* lru_restore_thread(void *arg) {
	struct lruCache *cache;
	if (destor.simulation_level >= SIMULATION_RESTORE)
		cache = new_lru_cache(destor.restore_cache[1], free_container_meta,
				lookup_fingerprint_in_container_meta);
	else
		cache = new_lru_cache(destor.restore_cache[1], free_container,
				lookup_fingerprint_in_container);

	struct chunk* c;
	while ((c = sync_queue_pop(restore_recipe_queue))) {

		if (CHECK_CHUNK(c, CHUNK_FILE_START) || CHECK_CHUNK(c, CHUNK_FILE_END)) {
			sync_queue_push(restore_chunk_queue, c);
			continue;
		}

		TIMER_DECLARE(1);
		TIMER_BEGIN(1);

		if (destor.simulation_level >= SIMULATION_RESTORE) {
			struct containerMeta *cm = lru_cache_lookup(cache, &c->fp);
			if (!cm) {
				VERBOSE("Restore cache: container %lld is missed", c->id);
				cm = retrieve_container_meta_by_id(c->id);
				assert(lookup_fingerprint_in_container_meta(cm, &c->fp));
				lru_cache_insert(cache, cm, NULL, NULL);
				jcr.read_container_num++;
			}

			TIMER_END(1, jcr.read_chunk_time);
		} else {
			struct container *con = lru_cache_lookup(cache, &c->fp);
			if (!con) {
				VERBOSE("Restore cache: container %lld is missed", c->id);
				con = retrieve_container_by_id(c->id);
				lru_cache_insert(cache, con, NULL, NULL);
				jcr.read_container_num++;
			}
			struct chunk *rc = get_chunk_in_container(con, &c->fp);
			assert(rc);
			TIMER_END(1, jcr.read_chunk_time);
			sync_queue_push(restore_chunk_queue, rc);
		}

		free_chunk(c);
	}

	sync_queue_term(restore_chunk_queue);

	free_lru_cache(cache);

	return NULL;
}

static void* read_recipe_thread(void *arg) {

	int i, j, k;
	//printf("enter read_recipe_thread\n");
	for (i = 0; i < jcr.bv->number_of_files; i++) {
		TIMER_DECLARE(1);
		TIMER_BEGIN(1);

		struct fileRecipeMeta *r = read_next_file_recipe_meta(jcr.bv);

		struct chunk *c = new_chunk(sdslen(r->filename) + 1);
		strcpy(c->data, r->filename);
		SET_CHUNK(c, CHUNK_FILE_START);//获得文件名
		//获得文件宽高
		c->row=r->row;
		c->column=r->column;
		TIMER_END(1, jcr.read_recipe_time);

		sync_queue_push(restore_recipe_queue, c);

		jcr.file_num++;

		for (j = 0; j < r->chunknum; j++) {
			TIMER_DECLARE(1);
			TIMER_BEGIN(1);

			struct chunkPointer* cp = read_next_n_chunk_pointers(jcr.bv, 1, &k);

			struct chunk* c = new_chunk(0);
			memcpy(&c->fp, &cp->fp, sizeof(fingerprint));
			c->size = cp->size;
			c->id = cp->id;
			c->row=cp->row;
			c->column=cp->column;
			TIMER_END(1, jcr.read_recipe_time);
			jcr.data_size += c->size;
			jcr.chunk_num++;

			sync_queue_push(restore_recipe_queue, c);
			free(cp);
		}

		c = new_chunk(0);
		SET_CHUNK(c, CHUNK_FILE_END);
		sync_queue_push(restore_recipe_queue, c);

		free_file_recipe_meta(r);
	}

	sync_queue_term(restore_recipe_queue);
	return NULL;
}

void write_restore_data() {
	
	char *p, *q;
	q = jcr.path + 1;/* ignore the first char*/
	/*
	 * recursively make directory
	 */
	while ((p = strchr(q, '/'))) {
		if (*p == *(p - 1)) {
			q++;
			continue;
		}
		*p = 0;
		if (access(jcr.path, 0) != 0) {
			mkdir(jcr.path, S_IRWXU | S_IRWXG | S_IRWXO);
		}
		*p = '/';
		q = p + 1;
	}

	struct chunk *c = NULL;
	FILE *fp = NULL;
	int row=0;
	int column=0;
	int quality=90;
	unsigned char **picbuf;
	while ((c = sync_queue_pop(restore_chunk_queue))) {
		
		TIMER_DECLARE(1);
		TIMER_BEGIN(1);

		if (CHECK_CHUNK(c, CHUNK_FILE_START)) {
			row=c->row;
			column=c->column;
			
			//图片文件的高不可能为0
			if(PIC_CHUNK_YES_OR_NO&&row!=0)
				c->data[c->size-2]='\0';
			NOTICE("Restoring: %s", c->data);
			sds filepath = sdsdup(jcr.path);
			filepath = sdscat(filepath, c->data);//文件绝对路径

			int len = sdslen(jcr.path);
			char *q = filepath + len;
			char *p;
			while ((p = strchr(q, '/'))) {
				if (*p == *(p - 1)) {
					q++;
					continue;
				}
				*p = 0;
				if (access(filepath, 0) != 0) {
					mkdir(filepath, S_IRWXU | S_IRWXG | S_IRWXO);
				}
				*p = '/';
				q = p + 1;
			}

			if (destor.simulation_level == SIMULATION_NO) {
				assert(fp == NULL);
				fp = fopen(filepath, "wb");
			}

			sdsfree(filepath);

		} else if (CHECK_CHUNK(c, CHUNK_FILE_END)) {
			if (fp)
				fclose(fp);
			fp = NULL;
		} else {
			assert(destor.simulation_level == SIMULATION_NO);
			//启用图片去重且块为图片块
			if(PIC_CHUNK_YES_OR_NO&&row >= PIC_CHUNK_ROW && column>=PIC_CHUNK_ROW){			
				int chunklenth=PIC_CHUNK_ROW;
				int rleft=row%chunklenth;
				int rborder=row-rleft;
				int cleft=column%chunklenth;
				int cborder=column-cleft;
			
				int headlen=0;
				int pr=0;
				int pc=0;
				int i,j;

				picbuf=(unsigned char **)malloc(row*sizeof(unsigned char*));
				for(i=0;i<row;i++){
					picbuf[i]=(unsigned char*)malloc((column*3)*sizeof(unsigned char));
				}
				//printf("pic r=%d c=%d\n",row,column );

				//解压块
				TIMER_DECLARE(1);
				TIMER_BEGIN(1);
				while(!CHECK_CHUNK(c, CHUNK_FILE_END)&&pr<row){
					//处理完整行				
					int temp;
					unsigned char **outbuf;
				
					//完整列,c->row=c->column,outbuf,temp可重复使用
					temp=c->column*3;
					outbuf=malloc_2_array(c->row,temp);
					while(pr<rborder){
						for(i=0;i<c->row;i++)
							memset(outbuf[i],0,temp);

						quality=(int)c->data[c->size-2];	
						unsigned char *header=get_head(quality,&headlen);
						restore_commom_chunk(outbuf,c,header,headlen);	

						for(i=0;i<c->row;i++){
							for(j=0;j<temp;j++){
								picbuf[pr+i][pc+j]=outbuf[i][j];
							}		
						}
						pc+=temp;
						if(pc==cborder*3) {pc=0;pr+=c->row;}

						free_chunk(c);
						c = sync_queue_pop(restore_chunk_queue);
					}
					//释放outbuf
					free_2_array(outbuf,chunklenth);

					//剩余列
					//因为row和column发生了变化，重新申请outbuf
					if(cleft!=0){
						//printf("last chunk r=%d c=%d\n",c->row,c->column );
						temp=c->column*3;
						outbuf=malloc_2_array(c->row,temp);

						quality=(int)c->data[c->size-2];

						unsigned char *header=get_head(quality,&headlen);
						//printf("headlen= %d \n",headlen );
						restore_commom_chunk(outbuf,c,header,headlen);
						
						int temp1=cborder*3;
						//printf("cborder=%d\n",cborder );
						for(i=0;i<c->row;i++){
							for(j=0;j<temp;j++){
								picbuf[i][temp1+j]=outbuf[i][j];
							}
						}
						free_2_array(outbuf,c->row);
						free_chunk(c);
						c = sync_queue_pop(restore_chunk_queue);
					}
				
					//处理剩余行,这时pc=0,pr=rborder
					if(rleft!=0){
						//if(rleft<=chunklenth/4)//剩余行只分为1块

						temp=c->column*3;
						outbuf=malloc_2_array(c->row,temp);
						quality=(int)c->data[c->size-2];	
						unsigned char *header=get_head(quality,&headlen);
						restore_commom_chunk(outbuf,c,header,headlen);
						
						for(i=0;i<c->row;i++){
							for(j=0;j<temp;j++){
								picbuf[pr+i][pc+j]=outbuf[i][j];
							}
						}
						pc+=temp;
						free_chunk(c);
						free_2_array(outbuf,c->row);
						c=sync_queue_pop(restore_chunk_queue);//CHUNK FILE END
					}

				}
				TIMER_END(1,jcr.decompre_time);
				//块解压结束
				//压缩为jpg文件
				TIMER_DECLARE(1);
				TIMER_BEGIN(1);
				if(!write_jpeg_file(fp,picbuf,99,column,row)){
					printf("write jpeg file error!\n");
					exit(1);
				}
				TIMER_END(1,jcr.compre_time);
				
				fclose(fp);
				fp = NULL;
				free_2_array(picbuf,row);

			}else if(PIC_CHUNK_YES_OR_NO && row!=0){//row<100 or column<100
				int temp=c->column*3;
				unsigned char *outbuf=malloc_2_array(c->row,temp);
				int headlen=0;
				quality=(int)c->data[c->size-2];	
				unsigned char *header=get_head(quality,&headlen);
				restore_commom_chunk(outbuf,c,header,headlen);	

				if(!write_jpeg_file(fp,outbuf,99,c->column,c->row)){
					printf("write jpeg file error!\n");
					exit(1);
				}
				
				fclose(fp);
				fp = NULL;
				free_2_array(outbuf,c->row);

			}		
			else{//common chunk
				VERBOSE("Restoring %d bytes", c->size);
				fwrite(c->data, c->size, 1, fp);
			}
		}

		free_chunk(c);

		TIMER_END(1, jcr.write_chunk_time);
	}
}

void do_restore(int revision, char *path) {
	init_recipe_store();
	init_container_store();

	init_restore_jcr(revision, path);

	destor_log(DESTOR_NOTICE, "job id: %d", jcr.id);
	destor_log(DESTOR_NOTICE, "backup path: %s", jcr.bv->path);
	destor_log(DESTOR_NOTICE, "restore to: %s", jcr.path);

	restore_chunk_queue = sync_queue_new(1000);
	restore_recipe_queue = sync_queue_new(1000);

	TIMER_DECLARE(1);
	TIMER_BEGIN(1);

	puts("==== restore begin ====");

	pthread_t recipe_t, read_t;
	pthread_create(&recipe_t, NULL, read_recipe_thread, NULL);

	if (destor.restore_cache[0] == RESTORE_CACHE_LRU) {
		destor_log(DESTOR_NOTICE, "restore cache is LRU");
		pthread_create(&read_t, NULL, lru_restore_thread, NULL);
	} else if (destor.restore_cache[0] == RESTORE_CACHE_OPT) {
		destor_log(DESTOR_NOTICE, "restore cache is OPT");
		pthread_create(&read_t, NULL, optimal_restore_thread, NULL);
	} else if (destor.restore_cache[0] == RESTORE_CACHE_ASM) {
		destor_log(DESTOR_NOTICE, "restore cache is ASM");
		pthread_create(&read_t, NULL, assembly_restore_thread, NULL);
	} else {
		fprintf(stderr, "Invalid restore cache.\n");
		exit(1);
	}

	write_restore_data();

	assert(sync_queue_size(restore_chunk_queue) == 0);
	assert(sync_queue_size(restore_recipe_queue) == 0);

	free_backup_version(jcr.bv);

	TIMER_END(1, jcr.total_time);
	puts("==== restore end ====");

	printf("job id: %d\n", jcr.id);
	printf("restore path: %s\n", jcr.path);
	printf("number of files: %d\n", jcr.file_num);
	printf("number of chunks: %d\n", jcr.chunk_num);
	printf("total size(B): %lld\n", jcr.data_size);
	printf("total time(s): %.3f\n", jcr.total_time / 1000000);
	printf("throughput(MB/s): %.2f\n",
			jcr.data_size * 1000000 / (1024.0 * 1024 * jcr.total_time));
	printf("speed factor: %.2f\n",
			jcr.data_size / (1024.0 * 1024 * jcr.read_container_num));

	printf("read_recipe_time : %.3fs, %.2fMB/s\n",
			jcr.read_recipe_time / 1000000,
			jcr.data_size * 1000000 / jcr.read_recipe_time / 1024 / 1024);
	printf("read_chunk_time : %.3fs, %.2fMB/s\n", jcr.read_chunk_time / 1000000,
			jcr.data_size * 1000000 / jcr.read_chunk_time / 1024 / 1024);
	printf("write_chunk_time : %.3fs, %.2fMB/s\n",
			jcr.write_chunk_time / 1000000,
			jcr.data_size * 1000000 / jcr.write_chunk_time / 1024 / 1024);

	printf("compre_time : %.3fs, %.2fMB/s\n", jcr.compre_time/ 1000000,
			jcr.data_size * 1000000 / jcr.compre_time/ 1024 / 1024);
	printf("decompre_time : %.3fs, %.2fMB/s\n", jcr.decompre_time/ 1000000,
			jcr.data_size * 1000000 / jcr.decompre_time/ 1024 / 1024);
	char logfile[] = "restore.log";
	FILE *fp = fopen(logfile, "a");

	/*
	 * job id,
	 * chunk num,
	 * data size,
	 * actually read container number,
	 * speed factor,
	 * throughput
	 */
	fprintf(fp, "%d %lld %d %.4f %.4f\n", jcr.id, jcr.data_size,
			jcr.read_container_num,
			jcr.data_size / (1024.0 * 1024 * jcr.read_container_num),
			jcr.data_size * 1000000 / (1024 * 1024 * jcr.total_time));

	fclose(fp);

	close_container_store();
	close_recipe_store();
}

