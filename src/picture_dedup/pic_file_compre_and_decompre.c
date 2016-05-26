#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <jpeglib.h>
#include <setjmp.h>
#include <unistd.h>
#include <sys/types.h>
#include "../backup.h"
#include "../destor.h"
#include "../utils/sync_queue.h"
#include "../jcr.h"

extern SyncQueue* read_queue;

struct my_error_mgr
{
	struct jpeg_error_mgr pub;    /* "public" fields */
	jmp_buf setjmp_buffer;    /* for return to caller */
};
typedef struct my_error_mgr * my_error_ptr;

METHODDEF(void) my_error_exit (j_common_ptr cinfo)
{
	my_error_ptr myerr = (my_error_ptr) cinfo->err;
	(*cinfo->err->output_message) (cinfo);
	longjmp(myerr->setjmp_buffer, 1);
}


void pic_chunk(struct chunk *c, struct jpeg_decompress_struct *cinfo, Queue *queue);

//read and chunk jpg file
/*int read_chunk_jpeg(FILE *infile,struct chunk *c)
{
	struct jpeg_decompress_struct cinfo;
	struct my_error_mgr jerr;
     cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = my_error_exit;
	if (setjmp(jerr.setjmp_buffer))
	{//file error
	  jpeg_destroy_decompress(&cinfo);
	  fclose(infile);
	  printf("pic_file/read_jpeg_file:decompress jpeg file error!\n");
	  return 0;
	}
	jpeg_create_decompress(&cinfo);
	jpeg_stdio_src(&cinfo, infile);//specify data source
	jpeg_read_header(&cinfo, TRUE);//read the information of the jpg file
	//获得图片文件的宽高
	jpeg_start_decompress(&cinfo);

	c->row=cinfo.output_height;
	c->column=cinfo.output_width;
	sync_queue_push(read_queue, c);

	pic_chunk(c,&cinfo);

	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
    return 1;
}*/

int write_jpeg_file(FILE *outfile, unsigned char **data, int quality, int image_width, int image_height)
{
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	int i = 0;

	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_compress(&cinfo);

	jpeg_stdio_dest(&cinfo, outfile);

	cinfo.image_width = image_width;
	cinfo.image_height = image_height;
	cinfo.input_components = 3;
	cinfo.in_color_space = JCS_RGB;
	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, quality, TRUE);//涉及图片质量，需要与原图一致，quality是0~100的整数

	jpeg_start_compress(&cinfo, TRUE);

	while (cinfo.next_scanline < cinfo.image_height)
	{
		(void) jpeg_write_scanlines(&cinfo, &data[i], 1);
		i++;
	}

	jpeg_finish_compress(&cinfo);
	jpeg_destroy_compress(&cinfo);
	fclose(outfile);
	return 1;
}
int write_to_mem(unsigned char **outbuf, unsigned char *data, int quality, int width, int height) {
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	unsigned long len = 0; //int len will lead to error
	unsigned char *buf = NULL;

	int offsetNum = width * 3;
	unsigned char *offset = data;
	unsigned char *inbuf = (unsigned char *)malloc(offsetNum);

	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_compress(&cinfo);
	jpeg_mem_dest(&cinfo, &buf, &len);
	cinfo.image_width = width;
	cinfo.image_height = height;
	cinfo.input_components = 3;
	cinfo.in_color_space = JCS_RGB;
	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, quality, TRUE);
	jpeg_start_compress(&cinfo, TRUE);
	while (cinfo.next_scanline < cinfo.image_height)
	{
		memcpy(inbuf, offset, offsetNum);
		//echodata(inbuf,offsetNum);
		jpeg_write_scanlines(&cinfo, &inbuf, 1);
		offset += offsetNum;
	}
	jpeg_finish_compress(&cinfo);
	//*outbuf=(unsigned char *)malloc(len*sizeof(unsigned char));
	*outbuf = buf;
	//memcpy(*outbuf,buf,len);
	jpeg_destroy_compress(&cinfo);
	free(inbuf);
	return (int)len;
}

void read_from_mem(unsigned char *inbuf, unsigned long inlen, unsigned char **outbuf) {
	struct jpeg_decompress_struct cinfo;
	struct my_error_mgr jerr;
	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = my_error_exit;
	jpeg_create_decompress(&cinfo);
	jpeg_mem_src(&cinfo, inbuf, inlen); //specify data source
	jpeg_read_header(&cinfo, TRUE);//read the information of the jpg file
	jpeg_start_decompress(&cinfo);
	int i = 0;
	while (cinfo.output_scanline < cinfo.output_height)
	{
		(void) jpeg_read_scanlines(&cinfo, &outbuf[i], 1);
		i++;
	}
	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
}
int mem_read_chunk_jpeg(struct chunk *fc, struct chunk *c, Queue *queue) {
	struct jpeg_decompress_struct cinfo;
	struct my_error_mgr jerr;
	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = my_error_exit;

	if (setjmp(jerr.setjmp_buffer))
	{
		jpeg_destroy_decompress(&cinfo);
		printf("pic_file/read_jpeg_file:decompress jpeg file error!\n");
		return 0;
	}

	jpeg_create_decompress(&cinfo);
	jpeg_mem_src(&cinfo, fc->data, fc->size); //specify data source
	jpeg_read_header(&cinfo, TRUE);//read the information of the jpg file
	jpeg_start_decompress(&cinfo);

	c->row = cinfo.output_height; //jpg height
	c->column = cinfo.output_width; //jpg width
	queue_push(queue, c);//push jpg FILE_START

	pic_chunk(c, &cinfo, queue);

	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
	return 1;
}
//判断是否为图片文件,0表示不是图片文件，1表示是图片文件
int file_judge(char *filename)
{
	int len = strlen(filename); //实际长度
	int i = len - 1;
	int j = 0;
	while (filename[i] != '.') i--;
	char *s = (char *)malloc((len - i) * sizeof(char));
	for (i++; i < len; i++)
	{
		s[j] = filename[i];
		j++;
	}
	s[j] = '\0';
	if (strcmp(s, "jpg") == 0 || strcmp(s, "jpeg") == 0 || strcmp(s, "JPG") == 0 || strcmp(s, "JPEG") == 0) return 1;
	else return 0;
}

//判断是否为图片块,0表示不是图片块，1表示是完整图片块，2表示是不完整图片块
int chunk_judge(struct chunk *c)
{
	if (c->row == 0 || c->column == 0) return NOT_PIC_CHUNK;
	else if (c->row == PIC_CHUNK_ROW && c->column == PIC_CHUNK_ROW) return COMPLETE_PIC_CHUNK;
	else return INCOMPLETE_PIC_CHUNK;
}
//create two-dimensional array
unsigned char ** malloc_2_array(int r, int c) {
	int i;
	unsigned char **buf = (unsigned char **) malloc(r * sizeof(unsigned char *));
	for (i = 0; i < r; i++) {
		buf[i] = (unsigned char *)malloc(c);
		memset(buf[i], 0, c);
	}
	return buf;
}

//free two-dimensional array
void free_2_array(unsigned char **buf, int r) {
	int i;
	for (i = 0; i < r; i++) {
		free(buf[i]);
		buf[i] = NULL;
	}
}

//read bmp to buf
static void read_bmp(unsigned char **buf, struct jpeg_decompress_struct *cinfo) {
	TIMER_DECLARE(1);
	TIMER_BEGIN(1);
	int i = 0;
	for (; i < cinfo->output_height; i++) {
		jpeg_read_scanlines(cinfo, &buf[i], 1);
	}
	TIMER_END(1, jcr.decompre_time);
}

void copyto(unsigned char *dst, unsigned char **res, int h_size, int w_size, int h_offset, int w_offset) {
	unsigned char *p = dst;
	unsigned char *q;
	int i = h_offset;
	int i_end = h_offset + h_size;
	for (; i < i_end; i++) {
		q = res[i] + w_offset;
		memcpy(p, q, w_size);
		p += w_size;
	}
	//echodata(dst,h_size*w_size);
}
void echodata(unsigned char *d, int size) {
	int i;
	for (i = 0; i < size; i++)
		printf("%d ", d[i]);
	printf("\n");
}
static void echoarray(unsigned char **arr, int r, int c) {
	int i, j;
	for (i = 0; i < r; i++) {
		for (j = 0; j < c; j++) {
			printf("%d ", arr[i][j] );
		}
		printf("\n");
	}
}
void pic_chunk(struct chunk *c, struct jpeg_decompress_struct *cinfo, Queue *queue) {
	int height = cinfo->output_height;
	int width = cinfo->output_width;
	int width3 = width * 3;

	unsigned char **buf = malloc_2_array(height, width3);
	TIMER_DECLARE(1);
	TIMER_BEGIN(1);
	read_bmp(buf, cinfo);
	TIMER_END(1, jcr.decompre_time);


	if (height < PIC_CHUNK_ROW || width < PIC_CHUNK_ROW) {
		c = new_chunk(height * width3);
		copyto(c->data, buf, height, width3, 0, 0);

		c->row = height;
		c->column = width;
		queue_push(queue, c);
		c = NULL;
	} else {
		int clen = PIC_CHUNK_ROW; //chunk length
		int clen3 = clen * 3;
		int ccsize = clen * clen3; //buf size of complete chunk

		int h_left = height % clen;
		int w_left = width % clen;

		int h_split = height - h_left;
		int w_split3 = (width - w_left) * 3;

		//complete chunk
		int h_offset = 0;
		int w_offset;

		while (h_offset < h_split) {
			w_offset = 0;
			while (w_offset < w_split3) {
				c = new_chunk(ccsize);
				copyto(c->data, buf, clen, clen3, h_offset, w_offset);
				c->row = clen;
				c->column = clen;
				queue_push(queue, c);
				c = NULL;
				w_offset += clen3;
			}
			h_offset += clen;
		}

		//width left
		if (w_left != 0) {
			int w_left3 = w_left * 3;
			c = new_chunk(h_split * w_left3);
			copyto(c->data, buf, h_split, w_left3, 0, w_split3);
			c->row = h_split;
			c->column = w_left;
			queue_push(queue, c);
			c = NULL;
		}

		//height left
		if (h_left != 0) {
			c = new_chunk(h_left * width3);
			copyto(c->data, buf, h_left, width3, h_split, 0);
			c->row = h_left;
			c->column = width;
			queue_push(queue, c);
			c = NULL;
		}

	}
	free_2_array(buf, height);
}

float sqrt_Newton(float x)
{
	float val = x;
	float last;
	do
	{
		last = val;
		val = (val + x / val) / 2;
	} while (abs(val - last) > 0.00000000001);
	return val;
}

//获取块宽高
int chunk_row_column_deal(int width, int height)
{
	//int size=PIC_CHUNK_MIN;
	int size = 64;
	int min = (width < height) ? width : height;
	if (min < size) return min; //PIC_CHUNK_MIN
	//else return size;
	int temp = (int)(sqrt_Newton(width * 1.0 * height / 200));
	if (temp <= size) return size;
	if (temp % 16 < 8) temp /= 16;
	else temp = temp / 16 + 1;
	//printf("\n\nchunklenth=%d\n\n",temp*16);
	return temp * 16;

}

//pure chunk return 1,else return 0
int judge_pure_chunk(struct chunk *c) {
	unsigned char s[11] = {'\0'};
	memcpy(s, c->data, 10);
	if (strcmp(s, "pure chunk") == 0) return 1;
	else return 0;
}
//set pure chunk
void set_pure_chunk(struct chunk *c) {
	unsigned char s[21] = "pure chunk"; //长度为10
	int len = strlen(s);
	int i;
	unsigned char rgb[3];
	for (i = 0; i < 3; i++)
		rgb[i] = c->data[i]; //get R,G,B
	free(c->data);
	c->data = NULL;
	c->data = (unsigned char *)malloc(17 * sizeof(unsigned char));
	c->size = 17;
	memcpy(c->data, s, len);
	for (i = 0; i < 3; i++)
		c->data[len++] = rgb[i];
	int temp;
	temp = c->row;
	c->data[len + 1] = temp;
	c->data[len] = temp >> 8;
	temp = c->column;
	c->data[len + 3] = temp;
	c->data[len + 2] = temp >> 8;

	//for(i=13;i<c->size;i++) printf("%0x ",c->data[i]);
	//printf("\n");
}
void restore_pure_chunk(struct chunk *c, unsigned char **buf) {
	int temp = c->column * 3;
	int i, j;
	for (i = 0; i < c->row; i++) {
		for (j = 0; j < temp; j += 3) {
			buf[i][j] = c->data[10];
			buf[i][j + 1] = c->data[11];
			buf[i][j + 2] = c->data[12];
		}
	}
}

//jpeg decompress
void restore_commom_chunk(unsigned char **outbuf, struct chunk *c, unsigned char *header) {
	int realsize = c->size - 4;
	unsigned char *inbuf = malloc(HEADLEN + realsize);
	memcpy(inbuf, header, HEADLEN);

	//write the width and height to the head information
	int i = 158; //FFC0的FF所在位置

	if (c->row <= 255) inbuf[i + 6] = c->row;
	else {
		inbuf[i + 6] = c->row;
		inbuf[i + 5] = c->row >> 8;
	}
	if (c->column <= 255)inbuf[i + 8] = c->column;
	else {
		inbuf[i + 8] = c->column;
		inbuf[i + 7] = c->column >> 8;
	}

	memcpy(inbuf + HEADLEN, c->data, realsize);
	//decompress to outbuf
	read_from_mem(inbuf, HEADLEN + realsize, outbuf);
	/*free(c->data);
	c->data=NULL;*/
	free(inbuf);
}
int read_quality(unsigned char *data, unsigned int len) {
	//sum of (quality=80~99)Y table-64
	int ratio[20] = {22, 87, 157, 228, 305, 377, 454, 528, 603, 672, 750, 820, 897, 967, 1045, 1115, 1187, 1262, 1334, 1413};
	int i = 0;
	int j;
	while (i < len - 1) {
		if (data[i] == 255 && data[i + 1] == 219) break;
		i++;
	}
	if (i == len - 1) {
		printf("read quality error\n");
		exit(-1);
	}

	i += 5;
	int sum = 0;
	for (j = 0; j < 64; j++) {
		sum += data[i];
		i++;
	}

	sum -= 64;
	i = 0;
	while (i < 20 && sum > ratio[i]) i++;
	if (i >= 20) return 80;
	else {
		i = (ratio[i] - sum) <= (sum - ratio[i - 1]) ? i : (i - 1);
		i = 99 - i;
		return i;
	}
}

int set_quality(int quality) {
	//return quality;
	if (quality >= 95) return 90;
	else if (quality >= 90) return quality - 5;
	else if (quality >= 85) return 85;
	else return quality;
}

void itoa10(int num, unsigned char *s) {
	unsigned char string[] = "0123456789";
	int i = 0;
	while (num) {
		s[i++] = string[num % 10];
		num /= 10;
	}
	s[i--] = '\0';
	int mid = i / 2;
	int temp;
	int j = 0;
	for (; i > mid; i--) {
		temp = s[i];
		s[i] = s[j];
		s[j++] = temp;
	}
}
