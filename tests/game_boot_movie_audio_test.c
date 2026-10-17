/* Compressed-block reader model: checks streaming ownership/order, never PCM. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "psp/game_boot_audio.h"
#include "psp/game_boot_movie_audio.h"

static struct {const unsigned char *data;unsigned int size,block,sample,source;int active;} voices[2];
static int locked,fail_start;
static unsigned int flushes,starts;
void recvxAudioLock(void){assert(!locked);locked=1;}
void recvxAudioUnlock(void){assert(locked);locked=0;}
void recvxAudioStop(unsigned int slot){assert(!locked&&slot<2);}
void recvxAudioNativeStop(unsigned int voice){assert(locked&&voice<2);voices[voice].active=0;}
void recvxAudioNativeFlush(const void *p,size_t size){assert(locked&&p&&size==4096);++flushes;}
void recvxAudioNativeVolume(unsigned int voice,int volume,int pan)
{assert(locked&&voice<2&&volume>=0&&volume<=4096&&pan==(voice?127:-127));}
int recvxAudioNativeStart(unsigned int voice,const void *data,size_t size,unsigned int rate,int loop,int e1,int e2,int volume,int pan)
{
    assert(locked&&voice<2&&data&&!((uintptr_t)data&63)&&size==8192&&rate==44100&&loop==1);
    assert(e1==-1&&e2==-1&&volume==4096&&pan==(voice?127:-127));
    if(fail_start&&voice==1)return -1;
    voices[voice].data=data;voices[voice].size=(unsigned int)size;voices[voice].block=0;
    voices[voice].sample=28;voices[voice].source=0;voices[voice].active=1;++starts;return 0;
}
unsigned int recvxAudioNativeEnded(void)
{assert(locked);return (!voices[0].active?1u:0u)|(!voices[1].active?2u:0u);}
static void put32(unsigned char *p,unsigned int value)
{for(unsigned int i=0;i<4;++i)p[i]=(unsigned char)(value>>(8*i));}
static unsigned int get32(const unsigned char *p)
{return p[0]|(unsigned int)p[1]<<8|(unsigned int)p[2]<<16|(unsigned int)p[3]<<24;}
static void fixture(const char *path,unsigned int channels,unsigned int samples,int badblock)
{
    unsigned int blocks=(samples+27)/28,bytes=(blocks+1)*16;
    unsigned char header[64]={0};memcpy(header,"RVAG",4);
    put32(header+4,1);put32(header+8,channels);put32(header+12,44100);put32(header+16,bytes);put32(header+24,samples);
    FILE *file=fopen(path,"wb");assert(file);assert(fwrite(header,1,64,file)==64);
    if(channels<=2)for(unsigned int c=0;c<channels;++c)for(unsigned int i=0;i<=blocks;++i){
        unsigned char block[16]={0};block[0]=(unsigned char)(badblock?0xf0:0);
        put32(block+2,i+1+c*100000);assert(fwrite(block,1,16,file)==16);
    }
    assert(!fclose(file));
}
static void render(unsigned int blocks)
{
    recvxAudioLock();
    for(unsigned int c=0;c<2;++c)if(voices[c].active)for(unsigned int i=0;i<256;++i){
        if(voices[c].sample==28){
            const unsigned char *block=voices[c].data+voices[c].block*16;
            unsigned int id=get32(block+2);
            if(voices[c].source<blocks)assert(id==voices[c].source+1+c*100000);
            else assert(id==0);
            assert(block[1]==(voices[c].block==0?6:voices[c].block==511?3:2));
            ++voices[c].source;voices[c].block=(voices[c].block+1)%512;voices[c].sample=0;
        }
        ++voices[c].sample;
    }
    assert(!recvxMovieAudioRenderLocked(256));recvxAudioUnlock();
}
int main(void)
{
    char directory[]="/tmp/recvx-movie-audio-XXXXXX",path[128];
    assert(mkdtemp(directory));snprintf(path,sizeof(path),"%s/movie.vag",directory);
    unsigned int samples=2000*28-9;
    fixture(path,2,samples,0);assert(!recvxMovieAudioStart(path)&&starts==2&&flushes==4);
    for(unsigned int i=0;recvxMovieAudioActive();++i){assert(i<300);render(2000);}
    assert(recvxMovieAudioSamples()>=samples&&recvxMovieAudioSamples()<samples+256&&flushes>12);
    recvxMovieAudioStop();assert(!recvxMovieAudioSamples());
    assert(!recvxMovieAudioStart(path));render(2000);recvxMovieAudioVolume(512);recvxMovieAudioStop();
    fail_start=1;assert(recvxMovieAudioStart(path)<0&&!voices[0].active&&!voices[1].active);fail_start=0;
    fixture(path,UINT32_MAX,samples,0);assert(recvxMovieAudioStart(path)<0);
    fixture(path,2,samples,1);assert(recvxMovieAudioStart(path)<0);
    fixture(path,2,1,0);assert(!recvxMovieAudioStart(path));render(1);assert(!recvxMovieAudioActive());recvxMovieAudioStop();
    fixture(path,2,samples,0);assert(!recvxMovieAudioStart(path));
    recvxAudioLock();assert(recvxMovieAudioRenderLocked(128)<0);recvxAudioUnlock();recvxMovieAudioStop();
    assert(!unlink(path)&&!rmdir(directory));
    puts("PASS native movie ADPCM bounded ring: compressed block ordering across wraps, pan, EOF, restart, failure cleanup and malformed streams; no decoding");
    return 0;
}
