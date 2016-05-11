#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct jpeg_decompress_struct{
	int output_height;
	int output_width;
	unsigned char **data;
};
unsigned char ** malloc_2_array(int r,int c){
	int i;
	unsigned char **buf=(unsigned char **)malloc(r*sizeof(unsigned char *));
	for(i=0;i<r;i++)
		buf[i]=(unsigned char *)malloc(c*sizeof(unsigned char));
	return buf;
}
//free two-dimensional array
void free_2_array(unsigned char **buf,int r){
	int i;
	for(i=0;i<r;i++){
		free(buf[i]);
		buf[i]=NULL;
	}
}
//read bmp to buf
/*static void read_bmp(unsigned char **buf,struct jpeg_decompress_struct *cinfo) {
	int i=0;
	for(i=0;i<cinfo->output_height;i++){
		jpeg_read_scanlines(cinfo,&buf[i],1);
	}
}*/

void copyto(unsigned char *dst,unsigned char **res,int h_size,int w_size,int h_offset,int w_offset){
	unsigned char *p=dst;
	unsigned char *q;
	int i=h_offset;
	int i_end=h_offset+h_size;
	for(;i<i_end;i++){
		q=res[i]+w_offset;
		memcpy(p,q,w_size);
		p+=w_size;
	}
}
struct jpeg_decompress_struct *init_jpeg(){
	struct jpeg_decompress_struct *a=(struct jpeg_decompress_struct *)malloc(sizeof(struct jpeg_decompress_struct));
	
	printf("sizeof=%d\n",sizeof( struct jpeg_decompress_struct));
	printf("height=");
	scanf("%d",&a->output_height);
	printf("width=");
	scanf("%d",&a->output_width);
	a->data=malloc_2_array(a->output_height,a->output_width);
	int i,j;
	while(1){
		char c=getchar();
		printf("%d\n",c);
		if(c=='\r'||c=='\n'||c==EOF) break;
	}
	for(i=0;i<a->output_height;i++){
		for(j=0;j<a->output_width;j++){
			scanf("%c",&a->data[i][j]);
		}

	}
	for(i=0;i<a->output_height;i++){
		for(j=0;j<a->output_width;j++){
			printf("%c ",a->data[i][j]);
		}
		printf("\n");
	}
	return a;
}
void echo(unsigned char *a,int len){
	int i=0;
	printf("data= ");
	for(;i<len;i++){
		printf("%c ",a[i]);
	}
	printf("len=%d\n",len);
}
void pic_chunk(struct jpeg_decompress_struct *cinfo){
	
	int height=cinfo->output_height;
	int width=cinfo->output_width;

	unsigned char **buf=cinfo->data;
	unsigned char *data=(unsigned char *)malloc(height*width);
	memset(data,0,height*width);
	if(height<1||width<1){
		copyto(data,buf,height,width,0,0);
		echo(data,height*width);
	}else{
		int clen=3;//chunk length
		int ccsize=clen*clen;//buf size of complete chunk 

		int h_left=height%clen;
		int w_left=width%clen;

		int h_split=height-h_left;
		int w_split=width-w_left;

		//complete chunk
		int h_offset=0;
		int w_offset;
		while(h_offset<h_split){
			w_offset=0;
			while(w_offset<w_split){
				copyto(data,buf,clen,clen,h_offset,w_offset);
				echo(data,ccsize);
				w_offset+=clen;
			}
			h_offset+=clen;
		}

		//width left
		copyto(data,buf,h_split,w_left,0,w_split);
		echo(data,h_split*w_left);

		//height left
		copyto(data,buf,h_left,width,h_split,0);
		echo(data,h_left*width);

	}
	free_2_array(buf,height);
	free(data);
}



int main(){
	struct jpeg_decompress_struct *a=init_jpeg();
	pic_chunk(a);
	return 0;
}