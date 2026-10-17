/* Integration-test call model for unavailable PSP hardware. Reads real muxed
 * access-unit timestamps; never decodes, fabricates or validates video pixels.
 * Production PSP builds link game_boot_movie_psp.c instead of this file. */
#ifndef RECVX_GAME_BOOT_HOST_TEST
#error Movie hardware call model is for host tests only
#endif
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "psp/game_boot_movie_psp.h"
static FILE *input;
static uint32_t sectors;
static uint64_t first_pts,last_pts;
static int have_pts,error;
static const char *stage;
static uint32_t be32(const unsigned char *p)
{return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
void recvxGameBootMoviePspClose(void)
{if(input)fclose(input);input=NULL;sectors=0;have_pts=0;first_pts=last_pts=0;}
static int fail(const char *where)
{stage=where;error=-EINVAL;recvxGameBootMoviePspClose();return -1;}
int recvxGameBootMoviePspError(void){return error;}
const char *recvxGameBootMoviePspStage(void){return stage?stage:"host MPEG call model";}
int recvxGameBootMoviePspOpen(const char *path)
{
    unsigned char header[2048];long length;
    recvxGameBootMoviePspClose();error=0;stage=NULL;
    input=fopen(path,"rb");if(!input)return fail("host movie open");
    if(fseek(input,0,SEEK_END)||(length=ftell(input))<4096||fseek(input,0,SEEK_SET)||
       fread(header,1,sizeof(header),input)!=sizeof(header))return fail("host movie header");
    uint32_t offset=be32(header+8),bytes=be32(header+12);
    if(memcmp(header,"PSMF0014",8)||header[142]!=30||header[143]!=17||
       offset<2048||(offset&2047)||!bytes||(bytes&2047)||
       offset>(unsigned long)length||bytes>(unsigned long)length-offset||fseek(input,offset,SEEK_SET))
        return fail("host movie extent");
    sectors=bytes/2048;return 0;
}
int recvxGameBootMoviePspDecode(void *pixels,uint64_t *pts)
{
    unsigned char sector[2048];
    if(!pixels||((uintptr_t)pixels&63)||!pts||!input)return fail("host movie output");
    *pts=0;
    while(sectors){
        if(fread(sector,1,sizeof(sector),input)!=sizeof(sector))return fail("host movie sector");
        --sectors;
        if(memcmp(sector,"\0\0\1\xba",4)||(sector[4]>>6)!=1)return fail("host movie pack");
        unsigned int p=14+(sector[13]&7);
        while(p+4<=sizeof(sector)){
            if(memcmp(sector+p,"\0\0\1",3))return fail("host movie packet");
            unsigned int code=sector[p+3];
            if(code==0xb9)return 0;
            if(p+6>sizeof(sector))return fail("host movie packet header");
            unsigned int count=((unsigned int)sector[p+4]<<8)|sector[p+5];
            if(count>sizeof(sector)-p-6)return fail("host movie packet extent");
            if(code==0xe0){
                if(count<3||sector[p+8]>count-3)return fail("host movie PES");
                if(sector[p+7]&128){
                    if(sector[p+8]<5)return fail("host movie PTS");
                    const unsigned char *v=sector+p+9;
                    if(!(v[0]&1)||!(v[2]&1)||!(v[4]&1))return fail("host movie PTS markers");
                    uint64_t raw=(uint64_t)((v[0]>>1)&7)<<30|(uint64_t)v[1]<<22|
                        (uint64_t)(v[2]>>1)<<15|(uint64_t)v[3]<<7|(v[4]>>1);
                    if(!have_pts){first_pts=raw;have_pts=1;}
                    else if(raw<=last_pts)return fail("host movie PTS order");
                    last_pts=raw;*pts=raw-first_pts;return 1;
                }
            }
            p+=6+count;
        }
    }
    return 0;
}
