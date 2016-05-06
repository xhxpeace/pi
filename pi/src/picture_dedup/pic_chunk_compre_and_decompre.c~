#include <stdio.h>
#include <stdlib.h>
#include "minilzo.h"

#define HEAP_ALLOC(var,size) \
    lzo_align_t  var [ ((size) + (sizeof(lzo_align_t) - 1)) / sizeof(lzo_align_t) ]
static HEAP_ALLOC(wrkmem, LZO1X_1_MEM_COMPRESS);
//块压缩算法，压缩成功返回压缩后长度，压缩失败0
int32_t pic_chunk_compre(unsigned char *srcbuffer,int32_t in_len,unsigned char *desbuffer)
{
    lzo_uint out_len=0;
    //lzo_uint in_len=(lzo_uint)strlen(srcbuffer);
	if (lzo_init() != LZO_E_OK)
	{
        printf("lzo_init error.\n");
        return 0;
    }
    if(lzo1x_1_compress(srcbuffer,(lzo_uint)in_len,desbuffer,&out_len,wrkmem)!=LZO_E_OK)
    {
    	printf(" compre internal error.\n");
    	return 0;
    }
    else return (int32_t)out_len;
}
//块解压算法，解压成功返回1，解压失败0
int32_t  pic_chunk_decompre(unsigned char *srcbuffer,int32_t in_len,unsigned char *desbuffer)
{
	lzo_uint out_len=0;
	if (lzo_init() != LZO_E_OK)
	{
        printf("lzo_init error.\n");
        return 0;
    }
    if(lzo1x_decompress(srcbuffer,(lzo_uint)in_len,desbuffer,&out_len,NULL)!=LZO_E_OK)
    {
    	printf("decompre internal error.\n");
    	return 0;
    }
    else return (int32_t)out_len;
}
