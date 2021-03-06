#include <stdio.h>
#include <jpeglib.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
int read_quality(FILE *fp){
	//sum of (quality=80~99)Y table-64
	int ratio[20]={22,87,157,228,305,377,454,528,603,672,750,820,897,967,1045,1115,1187,1262,1334,1413};
	long len=0;
	fseek(fp,0L,SEEK_END);
	len=ftell(fp);
	fseek(fp,0L,SEEK_SET);
	unsigned char *buf=(unsigned char *)malloc(len);
	fread(buf,len,1,fp);
	int i=0;
	int j;
	while(1){if(buf[i]==255&&buf[i+1]==219) break;
			i++;}
	i+=5;
	int sum=0;
	for(j=0;j<64;j++){
		sum+=buf[i];
		i++;		
	}
	free(buf);
	buf=NULL;
	i=0;
	sum-=64;
	while(sum>ratio[i]&&i<20) i++;
	if(i==20) return 80;
	else{
		i=(ratio[i]-sum)<=(sum-ratio[i-1])?i:(i-1);
		i=99-i;
		return i;
	}
}
int set_quality(int quality){
	if(quality>=95) return 90;
	else if(quality>=90) return quality-5;
	else if(quality>=85) return 85;
	else return quality; 
}
void main(int argc,char **argv){
	FILE *fp=fopen(argv[1],"rb");
	int quality=read_quality(fp);
	quality=set_quality(quality);
	printf("q=%d\n",quality);
	fclose(fp);
}
