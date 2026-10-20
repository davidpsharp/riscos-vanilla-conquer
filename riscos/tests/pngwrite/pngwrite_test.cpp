#include <string>
#include "pngwrite.h"
#include <stdlib.h>
#include <stdio.h>
int main(int argc,char**argv){
  int w=atoi(argv[1]),h=atoi(argv[2]),pitch=atoi(argv[3]); const char*out=argv[4];
  unsigned char pal[768]; for(int i=0;i<768;i++) pal[i]=(unsigned char)((i*7+3)&255);
  unsigned char*px=(unsigned char*)malloc((size_t)pitch*h); unsigned s=12345;
  for(long i=0;i<(long)pitch*h;i++){ s=s*1664525u+1013904223u; px[i]=(i%3)? (s>>24):0xFF; }
  FILE*f=fopen(out,"wb"); bool ok=PNG_Write_Paletted(f,w,h,px,pitch,pal); fclose(f);
  FILE*r=fopen((std::string(out)+".raw").c_str(),"wb"); for(int y=0;y<h;y++) fwrite(px+(size_t)y*pitch,1,w,r); fclose(r);
  FILE*p=fopen((std::string(out)+".pal").c_str(),"wb"); fwrite(pal,1,768,p); fclose(p);
  return ok?0:1;}
