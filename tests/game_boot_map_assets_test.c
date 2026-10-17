#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot_map.h"
static unsigned int assets;
static void put32(unsigned char *p,unsigned int n) {p[0]=n;p[1]=n>>8;p[2]=n>>16;p[3]=n>>24;}
int main(int argc,char **argv)
{
    assert(argc==2);
    for(unsigned int i=0;i<71;++i) {
        char path[512]; snprintf(path,sizeof(path),"%s/archives/MRY/%u.bin",argv[1],i);
        FILE *f=fopen(path,"rb"); assert(f); assert(!fseek(f,0,SEEK_END)); long bytes=ftell(f); assert(bytes>0); rewind(f);
        size_t size=(size_t)bytes; unsigned char *data=malloc(size); assert(data);
        assert(fread(data,1,size,f)==size); fclose(f);
        RecvxMapAsset asset;
        const char *error=recvxGameBootMapValidateAsset(data,size,i<5,&asset);
        if(error) {fprintf(stderr,"MRY/%u: %s\n",i,error); return 1;}
        ++assets;
        if(i<5) {
            assert(asset.markers==4 && asset.floor);
            assert(recvxGameBootMapFloor(asset.floor,asset.floor_size,0,0)==0);
            assert(recvxGameBootMapFloor(asset.floor,asset.floor_size,9999,0)==-1);
            assert(recvxGameBootMapFloor(asset.floor,asset.floor_size,0,-99999)==0);
            size_t floor=(size_t)(asset.floor-data);
            unsigned char saved=data[floor+6]; data[floor+6]=0;
            assert(recvxGameBootMapValidateAsset(data,size,1,&asset)); data[floor+6]=saved;
        }
        if(!memcmp(data,"MPAC",4)) {
            unsigned int saved=data[4]|(unsigned int)data[5]<<8|(unsigned int)data[6]<<16|(unsigned int)data[7]<<24;
            put32(data+4,0xfffffff0u); assert(recvxGameBootMapValidateAsset(data,size,i<5,&asset));
            put32(data+4,saved);
            assert(recvxGameBootMapValidateAsset(data,size-1,i<5,&asset));
        }
        free(data);
    }
    printf("PASS: %u original MRY index/floor assets validated, encoded models/textures/opcode extents, jumps, floor lookup boundaries and corrupt MPAC rejection\n",assets);
    return 0;
}
