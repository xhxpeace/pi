#include "destor.h"
#include "jcr.h"
#include "utils/sync_queue.h"
#include "index/index.h"
#include "backup.h"
#include "storage/containerstore.h"

/* defined in index.c */
extern struct {
	/* Requests to the key-value store */
	int lookup_requests;
	int update_requests;
	int lookup_requests_for_unique;
	/* Overheads of prefetching module */
	int read_prefetching_units;
} index_overhead;

void do_backup(char *path) {

	init_recipe_store();//��ȡ��ʷ���ݰ汾����Ϣ
	init_container_store();//��contianer.pool����ȡcontainer_count,�򴴽�contianer.pool
	init_index();//��ʼ�������㷨

	init_backup_jcr(path);//��ʼ����ǰ�汾��

	puts("==== backup begin ====");

	TIMER_DECLARE(1);
	TIMER_BEGIN(1);

	if (destor.simulation_level == SIMULATION_ALL) {
		start_read_trace_phase();
	} else {
		start_read_phase();//�����ݣ�ÿ�ζ�1MB
		start_chunk_phase();//���ݷֿ�
		start_hash_phase();//Ҫ��������п�����ѹ��
	}
	start_dedup_phase();
	start_rewrite_phase();
	start_filter_phase();

	if (destor.simulation_level == SIMULATION_ALL) {
		stop_read_trace_phase();
	} else {
		stop_read_phase();
		stop_chunk_phase();
		stop_hash_phase();
	}
	stop_dedup_phase();
	stop_rewrite_phase();
	stop_filter_phase();

	TIMER_END(1, jcr.total_time);

	close_index();
	close_container_store();
	close_recipe_store();

	update_backup_version(jcr.bv);

	free_backup_version(jcr.bv);

	puts("==== backup end ====");

	printf("job id: %d\n", jcr.id);
	printf("backup path: %s\n", jcr.path);
	printf("number of files: %d\n", jcr.file_num);
	printf("number of chunks: %d (%lld bytes on average)\n", jcr.chunk_num,
	       jcr.data_size / jcr.chunk_num);
	printf("number of unique chunks: %d\n", jcr.unique_chunk_num);
	printf("number of duplicate chunks: %d\n", jcr.chunk_num-jcr.unique_chunk_num);
	printf("dedup ratio by chunk num:%.4f\n",(jcr.chunk_num-jcr.unique_chunk_num)*1.0/jcr.chunk_num);
	printf("total size(B): %lld\n", jcr.data_size);
	printf("stored data size(B): %lld\n",
	       jcr.unique_data_size + jcr.rewritten_chunk_size);
	printf("deduplication ratio: %.4f, %.4f\n",
	       jcr.data_size != 0 ?
	       (jcr.data_size - jcr.unique_data_size
	        - jcr.rewritten_chunk_size)
	       / (double) (jcr.data_size) :
	       0,
	       jcr.data_size
	       / (double) (jcr.unique_data_size + jcr.rewritten_chunk_size));
	printf("f-ratio:%.4f\n", (jcr.data_size - jcr.unique_data_size - 28 * jcr.unique_chunk_num) / (double)(jcr.data_size));
	printf("total time(s): %.3f\n", jcr.total_time / 1000000);
	printf("throughput(MB/s): %.2f\n",
	       (double) jcr.data_size * 1000000 / (1024 * 1024 * jcr.total_time));
	printf("number of zero chunks: %d\n", jcr.zero_chunk_num);
	printf("size of zero chunks: %lld\n", jcr.zero_chunk_size);
	printf("number of rewritten chunks: %d\n", jcr.rewritten_chunk_num);
	printf("size of rewritten chunks: %lld\n", jcr.rewritten_chunk_size);
	printf("rewritten rate in size: %.3f\n",
	       jcr.rewritten_chunk_size / (double) jcr.data_size);

	destor.data_size += jcr.data_size;
	destor.stored_data_size += jcr.unique_data_size + jcr.rewritten_chunk_size;

	destor.chunk_num += jcr.chunk_num;
	destor.stored_chunk_num += jcr.unique_chunk_num + jcr.rewritten_chunk_num;
	destor.zero_chunk_num += jcr.zero_chunk_num;
	destor.zero_chunk_size += jcr.zero_chunk_size;
	destor.rewritten_chunk_num += jcr.rewritten_chunk_num;
	destor.rewritten_chunk_size += jcr.rewritten_chunk_size;

	printf("read_time : %.3fs, %.2fMB/s\n", jcr.read_time / 1000000,
	       jcr.data_size * 1000000 / jcr.read_time / 1024 / 1024);
	printf("decompre_time : %.3fs, %.2fMB/s\n", jcr.decompre_time / 1000000,
	       jcr.data_size * 1000000 / jcr.decompre_time / 1024 / 1024);
	printf("chunk_time : %.3fs, %.2fMB/s\n", jcr.chunk_time / 1000000,
	       jcr.data_size * 1000000 / jcr.chunk_time / 1024 / 1024);
	printf("hash_time : %.3fs, %.2fMB/s\n", jcr.hash_time / 1000000,
	       jcr.data_size * 1000000 / jcr.hash_time / 1024 / 1024);
	printf("compre_time : %.3fs, %.2fMB/s\n", jcr.compre_time / 1000000,
	       jcr.data_size * 1000000 / jcr.compre_time / 1024 / 1024);
	printf("dedup_time : %.3fs, %.2fMB/s\n",
	       jcr.dedup_time / 1000000,
	       jcr.data_size * 1000000 / jcr.dedup_time / 1024 / 1024);

	printf("rewrite_time : %.3fs, %.2fMB/s\n", jcr.rewrite_time / 1000000,
	       jcr.data_size * 1000000 / jcr.rewrite_time / 1024 / 1024);

	printf("filter_time : %.3fs, %.2fMB/s\n",
	       jcr.filter_time / 1000000,
	       jcr.data_size * 1000000 / jcr.filter_time / 1024 / 1024);

	printf("write_time : %.3fs, %.2fMB/s\n", jcr.write_time / 1000000,
	       jcr.data_size * 1000000 / jcr.write_time / 1024 / 1024);

	//double seek_time = 0.005; //5ms
	//double bandwidth = 120 * 1024 * 1024; //120MB/s

	/*	double index_lookup_throughput = jcr.data_size
	 / (index_read_times * seek_time
	 + index_read_entry_counter * 24 / bandwidth) / 1024 / 1024;

	 double write_data_throughput = 1.0 * jcr.data_size * bandwidth
	 / (jcr->unique_chunk_num) / 1024 / 1024;
	 double index_read_throughput = 1.0 * jcr.data_size / 1024 / 1024
	 / (index_read_times * seek_time
	 + index_read_entry_counter * 24 / bandwidth);
	 double index_write_throughput = 1.0 * jcr.data_size / 1024 / 1024
	 / (index_write_times * seek_time
	 + index_write_entry_counter * 24 / bandwidth);*/

	/*	double estimated_throughput = write_data_throughput;
	 if (estimated_throughput > index_read_throughput)
	 estimated_throughput = index_read_throughput;*/
	/*if (estimated_throughput > index_write_throughput)
	 estimated_throughput = index_write_throughput;*/

	char logfile[] = "backup.log";
	FILE *fp = fopen(logfile, "a");
	/*
	 * job id,
	 * the size of backup
	 * accumulative consumed capacity,
	 * deduplication rate,
	 * rewritten rate,
	 * total container number,
	 * sparse container number,
	 * inherited container number,
	 * 4 * index overhead (4 * int)
	 * throughput,
	 */
	fprintf(fp, "%d %lld %lld %.4f %.4f %d %d %d %d %d %d %d %.2f\n",
	        jcr.id,
	        jcr.data_size,
	        destor.stored_data_size,
	        jcr.data_size != 0 ?
	        (jcr.data_size - jcr.rewritten_chunk_size - jcr.unique_data_size) / (double) (jcr.data_size)
	        : 0,
	        jcr.data_size != 0 ? (double) (jcr.rewritten_chunk_size) / (double) (jcr.data_size) : 0,
	        jcr.total_container_num,
	        jcr.sparse_container_num,
	        jcr.inherited_sparse_num,
	        index_overhead.lookup_requests,
	        index_overhead.lookup_requests_for_unique,
	        index_overhead.update_requests,
	        index_overhead.read_prefetching_units,
	        (double) jcr.data_size * 1000000 / (1024 * 1024 * jcr.total_time));

	fclose(fp);

}
