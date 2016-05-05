#include <stdio.h>
#include <jpeglib.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

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
int read_jpeg_file(FILE *infile,unsigned char **buffer,int *width,int *height)
{
	int i;
	struct jpeg_decompress_struct cinfo;
	struct my_error_mgr jerr;
	
    	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = my_error_exit;
	if (setjmp(jerr.setjmp_buffer)) 
	{//file error
	  jpeg_destroy_decompress(&cinfo);
	  printf("解压buffer设置错误\n");
	  return -1; 
	}

	jpeg_create_decompress(&cinfo);
	jpeg_stdio_src(&cinfo, infile);//specify data source
	jpeg_read_header(&cinfo, TRUE);//read the information of the jpg file
	jpeg_start_decompress(&cinfo);
	
	//获得图片文件的宽高
	*width=cinfo.output_width;
	*height=cinfo.output_height;
	for(i=0;i<cinfo.output_height;i++)
	{
		buffer[i][cinfo.output_width*3]='\0';
	}
	
	i=0;
	while (cinfo.output_scanline < cinfo.output_height)
	{
		(void) jpeg_read_scanlines(&cinfo, &buffer[i], 1);
		i++;
	}

	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
    return 1;
}
unsigned long write_to_mem(unsigned char *outbuffer,unsigned char **inbuffer,int quality,int width,int height){
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	unsigned long outsize=0;
	unsigned char *buf=NULL;
      cinfo.err = jpeg_std_error(&jerr);
   	jpeg_create_compress(&cinfo);
   	jpeg_mem_dest (&cinfo,&buf,&outsize);
   	//jpeg_stdio_dest(&cinfo, outfile);
	cinfo.image_width = width; 	
  	cinfo.image_height =height;
  	cinfo.input_components = 3;		
  	cinfo.in_color_space = JCS_RGB;
  	jpeg_set_defaults(&cinfo);
  	jpeg_set_quality(&cinfo,quality, TRUE);
  	jpeg_start_compress(&cinfo, TRUE);
  	int i=0;
	while (cinfo.next_scanline < cinfo.image_height) 
	{
	  	jpeg_write_scanlines(&cinfo, &inbuffer[i], 1);
		i++;
	}
	jpeg_finish_compress(&cinfo);
	memcpy(outbuffer,buf,outsize);
	
	jpeg_destroy_compress(&cinfo);
	return outsize;
}
void read_from_mem(unsigned char *inbuf,unsigned long inlen,unsigned char **outbuf){
	struct jpeg_decompress_struct cinfo;
	struct my_error_mgr jerr;
	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = my_error_exit;
	jpeg_create_decompress(&cinfo);
	jpeg_mem_src(&cinfo,inbuf,inlen);//specify data source
	jpeg_read_header(&cinfo, TRUE);//read the information of the jpg file
	jpeg_start_decompress(&cinfo);
	int i=0;
	while (cinfo.output_scanline < cinfo.output_height)
	{
		(void) jpeg_read_scanlines(&cinfo, &outbuf[i], 1);
		i++;
	}
	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
}
void itoa10(int num,unsigned char *s){
	unsigned char string[]="0123456789";
	int i=0;
	while(num){
		s[i++]=string[num%10];
		num/=10;
	}
	s[i--]='\0';
	int mid=i/2;
	int temp;
	int j=0;
	for(;i>mid;i--){
		temp=s[i];
		s[i]=s[j];
		s[j++]=temp;
	}
}
void main(int argc,char **argv){
	FILE *fp=NULL;
	int width1,height1;
	int *width=&width1;
  	int *height=&height1;
	int  i;
	unsigned long inlen=0;
	unsigned char *outbuffer=NULL;
	unsigned char **buffer;
	buffer=(unsigned char **)malloc(500*sizeof(unsigned char *));
	for(i=0;i<500;i++)
	{
		buffer[i]=(unsigned char*)malloc((1000*3+1)*sizeof(unsigned char));
	}
	outbuffer=(unsigned char*)malloc(1000*sizeof(unsigned char));
	memset(outbuffer,1,1000);
	if((fp=fopen(argv[1],"rb"))==NULL){
		printf("open read file error!\n");
		exit(1);
	}
	
	if(!read_jpeg_file(fp,buffer,width,height)){
		printf("read file error!\n");
		exit(1);
	}
	fclose(fp);
	int quality=100;
	printf("q=");
	scanf("%d",&quality);

	
	inlen=write_to_mem(outbuffer,buffer,quality,width1,height1);

	int j=0;
	int len=0;
	while(j<inlen){
		if(outbuffer[j]==255&&outbuffer[j+1]==218){//FFDA
			break;
		}
		j++;
	}
	len=j;
	printf("len=%d\n",len);

	unsigned char qua[3]={'\0'};
	itoa10(quality,qua);
	unsigned char headpath[60]="head";
	int headlen=strlen(headpath);
	headpath[headlen]=qua[0];
	headpath[headlen+1]=qua[1];
	headpath[headlen+2]='\0';

	fp=fopen(headpath,"wb");
	fwrite(&len,sizeof(len),1,fp);
	fwrite(outbuffer,len,1,fp);
	fclose(fp);
	return ;
}
