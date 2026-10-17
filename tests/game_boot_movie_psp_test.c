#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <fcntl.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pspmpeg.h>
#include <psputils.h>
#include <psputility_modules.h>
#include "psp/game_boot_movie_psp.h"

/* SDK protocol test doubles only: the production adapter calls sceMpeg and
   contains no host decoder or generated picture data. */
enum Call { LOAD=1,INIT,RING_SIZE,CONSTRUCT,MEM_SIZE,CREATE,OFFSET,SIZE,REGISTER,ES,AU_INIT,MODE,
    AVAILABLE,PUT,GET_AU,DECODE,STOP };
static int fail_at,borrowed,module_live,library_live,ring_live,mpeg_live,stream_live,es_live;
static unsigned int checks,loads,unloads,finishes,deletes,destructs,flushes,stop_calls;
static unsigned int packets_read,buffered,au_count,output_count,cache_before,cache_after;
static int delayed,put_zero,missing_pts,reordered,repeated;
static int declared_frame_adjust;
static uint64_t first_pts;
static SceMpeg *handle_address;
static SceMpegRingbuffer *live_ring;
static void *last_output;
static const int native_error=(int32_t)0x80629999u;
static char movie_path[]="/tmp/recvx-native-mpeg-XXXXXX";
static unsigned int packet_count=5,stream_offset=4096;
#define CHECK(condition) do{++checks;assert(condition);}while(0)
static SceInt32 result(int call){return fail_at==call?native_error:0;}
static void put_be32(unsigned char *p,uint32_t value)
{p[0]=value>>24;p[1]=value>>16;p[2]=value>>8;p[3]=value;}
static uint32_t be32(const unsigned char *p)
{return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static void put_be48(unsigned char *p,uint64_t value)
{for(unsigned int i=0;i<6;++i)p[5-i]=(unsigned char)(value>>(8*i));}
static void fixture(void)
{
    FILE *file=fopen(movie_path,"wb");CHECK(file);
    unsigned char block[2048]={0};memcpy(block,"PSMF0014",8);
    put_be32(block+8,stream_offset);put_be32(block+12,packet_count*2048);
    block[142]=30;block[143]=17;
    uint64_t end=90000+(uint64_t)((int)packet_count+declared_frame_adjust)*3003;
    put_be48(block+0x54,90000);put_be48(block+0x5a,end);
    put_be48(block+0x6e,90000);put_be48(block+0x74,end);
    block[0x81]=1;block[0x82]=0xe0;
    CHECK(fwrite(block,1,sizeof(block),file)==sizeof(block));
    memset(block,0,sizeof(block));
    for(unsigned int offset=2048;offset<stream_offset;offset+=2048)CHECK(fwrite(block,1,sizeof(block),file)==sizeof(block));
    for(unsigned int i=0;i<packet_count+1;++i){
        memset(block,i==packet_count?0xfe:(int)(i&255u),sizeof(block));
        CHECK(fwrite(block,1,sizeof(block),file)==sizeof(block));
    }
    CHECK(!fclose(file));
}
int sceUtilityLoadModule(int module)
{
    CHECK(module==PSP_MODULE_AV_MPEGBASE&&!module_live);++loads;
    if(fail_at==LOAD)return native_error;
    module_live=1;return borrowed?(int32_t)SCE_ERROR_MODULE_ALREADY_LOADED:0;
}
int sceUtilityUnloadModule(int module)
{CHECK(module==PSP_MODULE_AV_MPEGBASE&&module_live&&!library_live&&!borrowed);module_live=0;++unloads;return 0;}
SceInt32 sceMpegInit(void){CHECK(module_live&&!library_live);if(fail_at==INIT)return native_error;library_live=1;return 0;}
SceVoid sceMpegFinish(void){CHECK(library_live&&!mpeg_live&&!ring_live);library_live=0;++finishes;}
SceInt32 sceMpegRingbufferQueryMemSize(SceInt32 packets)
{CHECK(library_live&&packets==256);return fail_at==RING_SIZE?native_error:packets*(2048+104);}
SceInt32 sceMpegRingbufferConstruct(SceMpegRingbuffer *ring,SceInt32 packets,ScePVoid data,SceInt32 size,sceMpegRingbufferCB callback,ScePVoid parameter)
{
    CHECK(library_live&&!ring_live&&packets==256&&size==packets*(2048+104)&&!((uintptr_t)data&63u));
    const unsigned char *zero=(const unsigned char *)ring;
    for(size_t i=0;i<sizeof(*ring)+32;++i)CHECK(zero[i]==0);
    if(fail_at==CONSTRUCT)return native_error;
    ring->iPackets=packets;ring->pData=data;ring->Callback=callback;ring->pCBparam=parameter;
    ((unsigned char *)ring)[sizeof(*ring)+4]=0xab; /* Modern firmware's extra fields fit. */
    live_ring=ring;ring_live=1;return 0;
}
SceVoid sceMpegRingbufferDestruct(SceMpegRingbuffer *ring)
{CHECK(ring_live&&!mpeg_live&&ring==live_ring);ring_live=0;live_ring=NULL;++destructs;}
SceInt32 sceMpegQueryMemSize(int unknown){CHECK(library_live&&ring_live&&unknown==0);return fail_at==MEM_SIZE?native_error:65536;}
SceInt32 sceMpegCreate(SceMpeg *mpeg,ScePVoid data,SceInt32 size,SceMpegRingbuffer *ring,SceInt32 width,SceInt32 a,SceInt32 b)
{
    CHECK(ring_live&&!mpeg_live&&ring==live_ring&&width==512&&a==0&&b==0&&size==65536&&!((uintptr_t)data&63u));
    if(fail_at==CREATE)return native_error;
    handle_address=mpeg;*mpeg=data;mpeg_live=1;return 0;
}
SceVoid sceMpegDelete(SceMpeg *mpeg)
{CHECK(mpeg_live&&mpeg==handle_address&&!stream_live&&!es_live);mpeg_live=0;handle_address=NULL;++deletes;}
SceInt32 sceMpegQueryStreamOffset(SceMpeg *mpeg,ScePVoid buffer,SceInt32 *offset)
{CHECK(mpeg_live&&mpeg==handle_address&&!((uintptr_t)buffer&63u));*offset=be32((unsigned char *)buffer+8);return result(OFFSET);}
SceInt32 sceMpegQueryStreamSize(ScePVoid buffer,SceInt32 *size)
{CHECK(mpeg_live);*size=be32((unsigned char *)buffer+12);return result(SIZE);}
SceMpegStream *sceMpegRegistStream(SceMpeg *mpeg,SceInt32 type,SceInt32 channel)
{CHECK(mpeg_live&&mpeg==handle_address&&type==0&&channel==0);if(fail_at==REGISTER)return NULL;stream_live=1;return (SceMpegStream *)(uintptr_t)0x1234;}
SceVoid sceMpegUnRegistStream(SceMpeg mpeg,SceMpegStream *stream)
{CHECK(mpeg==(void *)handle_address&&stream_live&&stream==(SceMpegStream *)(uintptr_t)0x1234);stream_live=0;}
ScePVoid sceMpegMallocAvcEsBuf(SceMpeg *mpeg)
{CHECK(mpeg==handle_address&&stream_live);if(fail_at==ES)return NULL;es_live=1;return (void *)(uintptr_t)1;}
SceVoid sceMpegFreeAvcEsBuf(SceMpeg *mpeg,ScePVoid buffer)
{CHECK(mpeg==handle_address&&es_live&&buffer==(void *)(uintptr_t)1);es_live=0;}
SceInt32 sceMpegFlushAllStream(SceMpeg *mpeg){CHECK(mpeg_live&&mpeg==handle_address);++flushes;return 0;}
SceInt32 sceMpegInitAu(SceMpeg *mpeg,ScePVoid buffer,SceMpegAu *au)
{CHECK(mpeg==handle_address&&es_live&&buffer==(void *)(uintptr_t)1&&!((uintptr_t)au&63u));memset(au,0,sizeof(*au));au->iEsBuffer=1;return result(AU_INIT);}
SceInt32 sceMpegAvcDecodeMode(SceMpeg *mpeg,SceMpegAvcMode *mode)
{CHECK(mpeg==handle_address&&mode->iUnk0==-1&&mode->iPixelFormat==SCE_MPEG_AVC_FORMAT_8888);return result(MODE);}
SceInt32 sceMpegRingbufferAvailableSize(SceMpegRingbuffer *ring)
{CHECK(ring==live_ring);return fail_at==AVAILABLE?native_error:256-(int)buffered;}
SceInt32 sceMpegRingbufferPut(SceMpegRingbuffer *ring,SceInt32 packets,SceInt32 available)
{
    CHECK(ring==live_ring&&packets>0&&packets<=available&&available==256-(int)buffered);
    if(fail_at==PUT)return native_error;
    if(put_zero)return 0;
    SceInt32 parts[2]={packets/2,packets-packets/2},total=0;
    for(unsigned int part=0;part<2;++part){
        if(!parts[part])continue;
        unsigned char *data=(unsigned char *)ring->pData+(size_t)total*2048;
        SceInt32 count=ring->Callback(data,parts[part],ring->pCBparam);
        if(count<0)return count;
        CHECK(count==parts[part]);
        for(SceInt32 i=0;i<count;++i)CHECK(data[i*2048]==((packets_read+(unsigned int)i)&255u));
        packets_read+=(unsigned int)count;total+=count;
    }
    buffered+=(unsigned int)total;return total;
}
SceInt32 sceMpegGetAvcAu(SceMpeg *mpeg,SceMpegStream *stream,SceMpegAu *au,SceInt32 *attributes)
{
    CHECK(mpeg==handle_address&&stream_live&&stream==(SceMpegStream *)(uintptr_t)0x1234);
    CHECK(au->iPtsMSB==UINT32_MAX&&au->iPts==UINT32_MAX);
    if(fail_at==GET_AU)return native_error;
    if(!buffered)return (int32_t)0x80618001u;
    uint64_t pts=(first_pts+(uint64_t)au_count*3003)&UINT64_C(0x1ffffffff);
    if(repeated)pts=first_pts;
    if(reordered&&au_count)pts=(first_pts-1)&UINT64_C(0x1ffffffff);
    au->iPtsMSB=missing_pts?UINT32_MAX:(uint32_t)(pts>>32);au->iPts=(uint32_t)pts;
    au->iDtsMSB=au->iDts=UINT32_MAX;au->iAuSize=2048;*attributes=1;
    ++au_count;--buffered;return 0;
}
void sceKernelDcacheWritebackRange(const void *data,unsigned int size)
{CHECK(ring_live&&data&&size&&!(size&2047u));}
void sceKernelDcacheWritebackInvalidateRange(const void *pixels,unsigned int size)
{CHECK(size==RECVX_MOVIE_PSP_FRAME_BYTES&&!((uintptr_t)pixels&63u));last_output=(void *)pixels;++cache_before;}
void sceKernelDcacheInvalidateRange(const void *pixels,unsigned int size)
{CHECK(size==RECVX_MOVIE_PSP_FRAME_BYTES&&pixels==last_output&&cache_before>cache_after);++cache_after;}
SceInt32 sceMpegAvcDecode(SceMpeg *mpeg,SceMpegAu *au,SceInt32 width,ScePVoid pointer,SceInt32 *status)
{
    CHECK(mpeg==handle_address&&width==512&&au->iEsBuffer==1&&*(void **)pointer==last_output);
    CHECK(*status==0&&cache_before==au_count);
    if(fail_at==DECODE)return native_error;
    *status=delayed&&au_count==1?0:1;
    if(*status){((uint32_t *)last_output)[0]=0xabc00000u|output_count;++output_count;}
    return 0;
}
SceInt32 sceMpegAvcDecodeStop(SceMpeg *mpeg,SceInt32 width,ScePVoid pointer,SceInt32 *status)
{
    CHECK(mpeg==handle_address&&width==512&&*(void **)pointer==last_output&&*status==0);++stop_calls;
    if(fail_at==STOP)return native_error;
    *status=output_count<au_count;
    if(*status){((uint32_t *)last_output)[0]=0xabc00000u|output_count;++output_count;}
    return 0;
}
static void reset(void)
{
    recvxGameBootMoviePspClose();
    CHECK(!library_live&&!ring_live&&!mpeg_live&&!stream_live&&!es_live);
    if(borrowed)module_live=0;
    CHECK(!module_live);
    fail_at=borrowed=delayed=put_zero=missing_pts=reordered=repeated=declared_frame_adjust=0;
    loads=unloads=finishes=deletes=destructs=flushes=stop_calls=0;
    packets_read=buffered=au_count=output_count=cache_before=cache_after=0;
    first_pts=90000;packet_count=5;stream_offset=4096;fixture();
}
static void playback(void)
{
    uint32_t *buffers[2]={memalign(64,RECVX_MOVIE_PSP_FRAME_BYTES),memalign(64,RECVX_MOVIE_PSP_FRAME_BYTES)};
    CHECK(buffers[0]&&buffers[1]);uint64_t pts;
    for(unsigned int delay=0;delay<2;++delay){
        reset();delayed=(int)delay;CHECK(recvxGameBootMoviePspOpen(movie_path)==0);
        for(unsigned int i=0;i<packet_count;++i){
            unsigned int slot=i&1u;
            CHECK(recvxGameBootMoviePspDecode(buffers[slot],&pts)==1&&pts==(uint64_t)i*3003);
            CHECK(buffers[slot][0]==(0xabc00000u|i));
            if(i)CHECK(buffers[slot^1u][0]==(0xabc00000u|(i-1)));
        }
        CHECK(recvxGameBootMoviePspDecode(buffers[0],&pts)==0&&stop_calls==1);
        CHECK(recvxGameBootMoviePspDecode(buffers[1],&pts)==0&&stop_calls==1);
        CHECK(packets_read==packet_count&&cache_after==packet_count&&recvxGameBootMoviePspError()==0);
        recvxGameBootMoviePspClose();CHECK(unloads==1&&finishes==1&&deletes==1&&destructs==1&&flushes==1);
        recvxGameBootMoviePspClose();CHECK(unloads==1);
    }
    reset();borrowed=1;CHECK(recvxGameBootMoviePspOpen(movie_path)==0);
    recvxGameBootMoviePspClose();CHECK(!unloads&&module_live);
    reset();first_pts=UINT64_C(0x200000000)-3003;CHECK(recvxGameBootMoviePspOpen(movie_path)==0);
    CHECK(recvxGameBootMoviePspDecode(buffers[0],&pts)==1&&pts==0);
    CHECK(recvxGameBootMoviePspDecode(buffers[1],&pts)==1&&pts==3003);
    free(buffers[0]);free(buffers[1]);
}
static void failures(void)
{
    void *pixels=memalign(64,RECVX_MOVIE_PSP_FRAME_BYTES);CHECK(pixels);uint64_t pts;
    for(int call=LOAD;call<=MODE;++call){
        reset();fail_at=call;CHECK(recvxGameBootMoviePspOpen(movie_path)==-1&&recvxGameBootMoviePspError()<0);
        CHECK(!module_live&&!library_live&&!ring_live&&!mpeg_live&&!stream_live&&!es_live);
    }
    for(int call=AVAILABLE;call<=STOP;++call){
        reset();CHECK(recvxGameBootMoviePspOpen(movie_path)==0);fail_at=call;int value;
        do{value=recvxGameBootMoviePspDecode(pixels,&pts);}while(value==1);
        CHECK(value==-1&&recvxGameBootMoviePspError()==native_error&&pts==0);
        CHECK(!module_live&&!library_live&&!ring_live&&!mpeg_live&&!stream_live&&!es_live);
    }
    /* Real PSP regression: repeated packet PTS must not repeat picture time.
       Missing/wrapped/reordered AU PTS likewise do not change our CFR asset. */
    for(unsigned int variant=0;variant<3;++variant)for(unsigned int delay=0;delay<2;++delay){
        reset();missing_pts=variant==0;reordered=variant==1;repeated=variant==2;delayed=(int)delay;
        CHECK(!recvxGameBootMoviePspOpen(movie_path));
        for(unsigned int i=0;i<packet_count;++i)
            CHECK(recvxGameBootMoviePspDecode(pixels,&pts)==1&&pts==(uint64_t)i*3003);
        CHECK(recvxGameBootMoviePspDecode(pixels,&pts)==0);
    }
    for(int adjust=-1;adjust<=1;adjust+=2){
        reset();declared_frame_adjust=adjust;fixture();CHECK(!recvxGameBootMoviePspOpen(movie_path));
        int value;do{value=recvxGameBootMoviePspDecode(pixels,&pts);}while(value==1);
        CHECK(value==-1&&strstr(recvxGameBootMoviePspStage(),adjust<0?"exceeded authored":"ended before authored"));
    }
    reset();FILE *timing_file=fopen(movie_path,"r+b");CHECK(timing_file);
    CHECK(!fseek(timing_file,0x5f,SEEK_SET));CHECK(fputc(1,timing_file)!=EOF);fclose(timing_file);
    CHECK(recvxGameBootMoviePspOpen(movie_path)==-1&&loads==0&&strstr(recvxGameBootMoviePspStage(),"CFR timing"));
    reset();put_zero=1;CHECK(!recvxGameBootMoviePspOpen(movie_path));
    CHECK(recvxGameBootMoviePspDecode(pixels,&pts)==-1&&strstr(recvxGameBootMoviePspStage(),"no progress"));
    reset();CHECK(!recvxGameBootMoviePspOpen(movie_path));CHECK(!truncate(movie_path,2048));
    CHECK(recvxGameBootMoviePspDecode(pixels,&pts)==-1&&strstr(recvxGameBootMoviePspStage(),"stream read"));
    reset();CHECK(!recvxGameBootMoviePspOpen(movie_path));pts=999;
    CHECK(recvxGameBootMoviePspDecode((unsigned char *)pixels+1,&pts)==-1&&pts==0);
    reset();FILE *file=fopen(movie_path,"r+b");CHECK(file&&fseek(file,142,SEEK_SET)==0);CHECK(fputc(29,file)!=EOF);fclose(file);
    CHECK(recvxGameBootMoviePspOpen(movie_path)==-1&&loads==0&&strstr(recvxGameBootMoviePspStage(),"dimensions"));
    reset();file=fopen(movie_path,"r+b");CHECK(file&&fseek(file,12,SEEK_SET)==0);
    unsigned char bad_size[4]={0x7f,0xff,0xf8,0};CHECK(fwrite(bad_size,1,4,file)==4);fclose(file);
    CHECK(recvxGameBootMoviePspOpen(movie_path)==-1&&strstr(recvxGameBootMoviePspStage(),"extent"));
    free(pixels);recvxGameBootMoviePspClose();
}
int main(void)
{
    int descriptor=mkstemp(movie_path);CHECK(descriptor>=0);close(descriptor);
    playback();failures();CHECK(!unlink(movie_path));
    printf("PASS: native MPEG call protocol, streamed sector callback, double-buffer ownership, authored CFR timing despite repeated/missing packet PTS, frame-count bounds, delayed EOF drain, all initialization/decode failures and cleanup: %u checks\n",checks);
    return 0;
}
