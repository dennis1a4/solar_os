#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "solar_os_stb_image.h"
int main(int argc,char **argv) {
    assert(argc==3);
    for(int i=1;i<argc;++i) {
        FILE *f=fopen(argv[i],"rb");assert(f);fseek(f,0,SEEK_END);size_t n=ftell(f);rewind(f);
        unsigned char *input=malloc(n);assert(input);assert(fread(input,1,n,f)==n);fclose(f);
        for(int cycle=0;cycle<3;++cycle) {
            uint8_t *rgb=NULL;uint32_t w=0,h=0;
            assert(solar_os_stb_decode_rgb(input,n,320*192,&rgb,&w,&h)==ESP_OK);
            assert(w==320 && h==192 && rgb);
            size_t red=(100*320+20)*3;assert(rgb[red]>240 && rgb[red+1]<10 && rgb[red+2]<10);
            size_t blue=(100*320+100)*3;assert(rgb[blue]<10 && rgb[blue+1]<10 && rgb[blue+2]>240);
            solar_os_stb_image_free(rgb);rgb=NULL;
            assert(solar_os_stb_decode_rgb(input,n,100,&rgb,&w,&h)!=ESP_OK);assert(!rgb);
            assert(solar_os_stb_decode_rgb(input,8,320*192,&rgb,&w,&h)!=ESP_OK);assert(!rgb);
        }
        free(input);
    }
    puts("PASS: PNG/JPEG colors, dimensions, repeated cleanup, pixel limit and truncated input");
}
