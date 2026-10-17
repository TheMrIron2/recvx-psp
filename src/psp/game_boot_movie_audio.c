#include <limits.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot_audio.h"
#include "psp/game_boot_movie_audio.h"

#define HALF_BLOCKS 256u
#define HALF_BYTES (HALF_BLOCKS * 16u)
#define HALF_SAMPLES (HALF_BLOCKS * 28u)
#define RING_BYTES (2u * HALF_BYTES)
static struct {
    FILE *file;
    unsigned char *ring[2];
    unsigned int channels, bytes, blocks, next_block, samples;
    uint64_t played, boundary;
    int active;
} movie;
static unsigned int le32(const unsigned char *p)
{ return p[0]|(unsigned int)p[1]<<8|(unsigned int)p[2]<<16|(unsigned int)p[3]<<24; }

void recvxMovieAudioStopLocked(void)
{
    if (movie.file || movie.ring[0] || movie.ring[1]) {
        for (unsigned int c=0;c<movie.channels&&c<2;++c) recvxAudioNativeStop(c);
        for (unsigned int c=0;c<2;++c) free(movie.ring[c]);
        if (movie.file) fclose(movie.file);
    }
    memset(&movie,0,sizeof(movie));
}
void recvxMovieAudioStop(void)
{ recvxAudioLock();recvxMovieAudioStopLocked();recvxAudioUnlock(); }

static int fill(unsigned int half,unsigned int source_block)
{
    unsigned int blocks=source_block<movie.blocks?movie.blocks-source_block:0;
    if(blocks>HALF_BLOCKS)blocks=HALF_BLOCKS;
    for(unsigned int c=0;c<movie.channels;++c){
        unsigned char *data=movie.ring[c]+half*HALF_BYTES;
        /* Filter zero silence is independent of the previous ADPCM history. */
        memset(data,0,HALF_BYTES);
        if(blocks&&(fseek(movie.file,(long)(64u+c*movie.bytes+source_block*16u),SEEK_SET)||
            fread(data,16,blocks,movie.file)!=blocks))return -1;
        for(unsigned int i=0;i<HALF_BLOCKS;++i){
            if((data[i*16]>>4)>4||(data[i*16]&15)>12)return -1;
            unsigned int ring_block=half*HALF_BLOCKS+i;
            data[i*16+1]=ring_block==0?6:ring_block==2*HALF_BLOCKS-1?3:2;
        }
        recvxAudioNativeFlush(data,HALF_BYTES);
    }
    return 0;
}
int recvxMovieAudioStart(const char *path)
{
    unsigned char header[64];long length;int result=-1;
    if(!path)return -1;
    recvxMovieAudioStop();
    recvxAudioStop(0);recvxAudioStop(1);
    recvxAudioLock();recvxMovieAudioStopLocked();
    movie.file=fopen(path,"rb");
    if(!movie.file||fread(header,1,sizeof(header),movie.file)!=sizeof(header)||
       memcmp(header,"RVAG",4)||le32(header+4)!=1||le32(header+12)!=RECVX_AUDIO_RATE||le32(header+20))goto done;
    movie.channels=le32(header+8);movie.bytes=le32(header+16);movie.samples=le32(header+24);
    if(movie.channels<1||movie.channels>2||!movie.samples||movie.samples>RECVX_AUDIO_RATE*3600u||
       !movie.bytes||(movie.bytes&15)||movie.bytes>(unsigned int)(LONG_MAX-64)/movie.channels)goto done;
    movie.blocks=(movie.samples+27u)/28u;
    if(movie.bytes<(movie.blocks+1u)*16u||fseek(movie.file,0,SEEK_END)||
       (length=ftell(movie.file))<0||(unsigned long)length!=64u+(unsigned long)movie.channels*movie.bytes)goto done;
    for(unsigned int c=0;c<movie.channels;++c){movie.ring[c]=memalign(64,RING_BYTES);if(!movie.ring[c])goto done;}
    if(fill(0,0)||fill(1,HALF_BLOCKS))goto done;
    for(unsigned int c=0;c<movie.channels;++c)
        if(recvxAudioNativeStart(c,movie.ring[c],RING_BYTES,RECVX_AUDIO_RATE,1,-1,-1,4096,
           movie.channels==1?0:c?127:-127))goto done;
    /* Channel zero is left, matching the original Sony stereo PCM order. */
    if(movie.channels==2){recvxAudioNativeVolume(0,4096,-127);recvxAudioNativeVolume(1,4096,127);}
    movie.next_block=2*HALF_BLOCKS;movie.boundary=HALF_SAMPLES;movie.active=1;result=0;
done:
    if(result)recvxMovieAudioStopLocked();
    recvxAudioUnlock();return result;
}
int recvxMovieAudioRenderLocked(size_t frames)
{
    if(!movie.active)return 0;
    if(frames!=RECVX_AUDIO_FRAMES)return -1;
    unsigned int mask=(1u<<movie.channels)-1;
    if(recvxAudioNativeEnded()&mask)return -1;
    movie.played+=frames;
    if(movie.played>=movie.samples){
        for(unsigned int c=0;c<movie.channels;++c)recvxAudioNativeStop(c);
        movie.active=0;return 0;
    }
    if(movie.played>=movie.boundary){
        unsigned int half=(unsigned int)(movie.boundary/HALF_SAMPLES-1u)&1u;
        /* SAS has finished this half. Refill behind its reader while locked;
           the unconsumed half retains predictor continuity across the wrap. */
        if(fill(half,movie.next_block))return -1;
        movie.next_block+=HALF_BLOCKS;movie.boundary+=HALF_SAMPLES;
    }
    return 0;
}
int recvxMovieAudioActive(void)
{ int active;recvxAudioLock();active=movie.active;recvxAudioUnlock();return active; }
uint64_t recvxMovieAudioSamples(void)
{ uint64_t samples;recvxAudioLock();samples=movie.played;recvxAudioUnlock();return samples; }
void recvxMovieAudioVolume(int volume)
{
    if(volume<0)volume=0;
    if(volume>4096)volume=4096;
    recvxAudioLock();
    if(movie.active)for(unsigned int c=0;c<movie.channels;++c)
        recvxAudioNativeVolume(c,volume,movie.channels==1?0:c?127:-127);
    recvxAudioUnlock();
}
