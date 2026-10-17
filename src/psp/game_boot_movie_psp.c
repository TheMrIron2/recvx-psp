#include <errno.h>
#include <limits.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pspmpeg.h>
#include <psputils.h>
#include <psputility_modules.h>
#include "psp/game_boot_movie_psp.h"

#define PACKET_BYTES 2048u
#define RING_PACKETS 256
#define MPEG_NO_DATA UINT32_C(0x80618001)
#define FRAME_TICKS UINT64_C(3003)
#define MAX_FRAMES 108000u

static FILE *movie_file;
static SceMpeg mpeg;
static SceMpegStream *video_stream;
static void *ring_data,*mpeg_data,*es_buffer;
/* Newer firmware appends GP to the ring structure. The installed SDK exposes
   the older prefix: reserve zeroed space after it, away from our state flags. */
static struct { SceMpegRingbuffer ring; unsigned char firmware_tail[64]; }
    ring_storage __attribute__((aligned(64)));
static SceMpegAu au __attribute__((aligned(64)));
static unsigned char header[PACKET_BYTES] __attribute__((aligned(64)));
static unsigned int remaining_packets;
static int loaded_module,initialized,constructed,created,stopped,callback_error;
static int last_error;
static const char *error_stage;
static unsigned int expected_frames,output_frames;
static char timing_error[96];

int recvxGameBootMoviePspError(void){return last_error;}
const char *recvxGameBootMoviePspStage(void){return error_stage?error_stage:"none";}

void recvxGameBootMoviePspClose(void)
{
    if(created){
        sceMpegFlushAllStream(&mpeg);
        if(es_buffer)sceMpegFreeAvcEsBuf(&mpeg,es_buffer);
        /* This import also takes the address of the MPEG handle. PSPSDK's
           historical prototype uses the opaque void* typedef by value. */
        if(video_stream)sceMpegUnRegistStream(&mpeg,video_stream);
        sceMpegDelete(&mpeg);
    }
    if(constructed)sceMpegRingbufferDestruct(&ring_storage.ring);
    free(mpeg_data);free(ring_data);
    if(initialized)sceMpegFinish();
    if(loaded_module)sceUtilityUnloadModule(PSP_MODULE_AV_MPEGBASE);
    if(movie_file)fclose(movie_file);
    movie_file=NULL;mpeg=NULL;video_stream=NULL;ring_data=mpeg_data=es_buffer=NULL;
    loaded_module=initialized=constructed=created=stopped=callback_error=0;
    remaining_packets=expected_frames=output_frames=0;
    memset(&ring_storage,0,sizeof(ring_storage));memset(&au,0,sizeof(au));
}
static int fail(const char *stage,int code)
{
    last_error=code<0?code:-EINVAL;error_stage=stage;
    recvxGameBootMoviePspClose();return -1;
}
static int io_error(void){return errno>0?-errno:-EIO;}
static uint64_t be48(const unsigned char *bytes)
{
    uint64_t value=0;
    for(unsigned int i=0;i<6;++i)value=(value<<8)|bytes[i];
    return value;
}
static int frame_count_error(const char *description)
{
    snprintf(timing_error,sizeof(timing_error),"%s: decoded=%u expected=%u",
        description,output_frames,expected_frames);
    return fail(timing_error,-EIO);
}


static SceInt32 read_packets(ScePVoid destination,SceInt32 packets,ScePVoid parameter)
{
    if(parameter!=&remaining_packets||!movie_file||packets<=0||packets>RING_PACKETS){
        callback_error=-EINVAL;return -1;
    }
    unsigned int count=(unsigned int)packets;
    if(count>remaining_packets)count=remaining_packets;
    if(!count)return 0;
    size_t bytes=(size_t)count*PACKET_BYTES;
    if(fread(destination,1,bytes,movie_file)!=bytes){callback_error=io_error();return -1;}
    sceKernelDcacheWritebackRange(destination,(unsigned int)bytes);
    remaining_packets-=count;
    return (SceInt32)count;
}

int recvxGameBootMoviePspOpen(const char *path)
{
    recvxGameBootMoviePspClose();last_error=0;error_stage=NULL;
    if(!path||!*path)return fail("movie path",-EINVAL);
    movie_file=fopen(path,"rb");if(!movie_file)return fail("movie open",io_error());
    if(fseek(movie_file,0,SEEK_END))return fail("movie length",io_error());
    long length=ftell(movie_file);
    if(length<(long)(PACKET_BYTES*2u)||fseek(movie_file,0,SEEK_SET))return fail("movie length",-EINVAL);
    if(fread(header,1,sizeof(header),movie_file)!=sizeof(header))return fail("movie header read",io_error());
    if(memcmp(header,"PSMF0014",8)||header[142]!=30||header[143]!=17)
        return fail("movie PSMF version or dimensions",-EINVAL);
    /* Our cooker writes fixed-rate, baseline AVC with no reordered pictures.
       Firmware AU timestamps may repeat, be absent, or describe packets rather
       than each displayed picture. Schedule actual decoded output frames from
       the authored CFR duration, never synthesize missing pictures. */
    uint64_t start=be48(header+0x54),end=be48(header+0x5a);
    if(start!=90000 || end<=start || (end-start)%FRAME_TICKS ||
       (end-start)/FRAME_TICKS>MAX_FRAMES ||
       be48(header+0x6e)!=start || be48(header+0x74)!=end ||
       header[0x80]!=0 || header[0x81]!=1 || header[0x82]!=0xe0)
        return fail("movie authored CFR timing or stream descriptor",-EINVAL);
    expected_frames=(unsigned int)((end-start)/FRAME_TICKS);
    int result=sceUtilityLoadModule(PSP_MODULE_AV_MPEGBASE);
    if(result<0&&(uint32_t)result!=SCE_ERROR_MODULE_ALREADY_LOADED)return fail("sceUtilityLoadModule(MPEGBASE)",result);
    loaded_module=result>=0;
    result=sceMpegInit();if(result<0)return fail("sceMpegInit",result);initialized=1;
    int ring_bytes=sceMpegRingbufferQueryMemSize(RING_PACKETS);
    if(ring_bytes<=0||ring_bytes>2*1024*1024)return fail("sceMpegRingbufferQueryMemSize",ring_bytes);
    ring_data=memalign(64,(size_t)ring_bytes);
    if(!ring_data)return fail("movie ring allocation",-ENOMEM);
    memset(ring_data,0,(size_t)ring_bytes);
    result=sceMpegRingbufferConstruct(&ring_storage.ring,RING_PACKETS,ring_data,ring_bytes,read_packets,&remaining_packets);
    if(result<0)return fail("sceMpegRingbufferConstruct",result);
    constructed=1;
    int mpeg_bytes=sceMpegQueryMemSize(0);
    if(mpeg_bytes<=0||mpeg_bytes>2*1024*1024)return fail("sceMpegQueryMemSize",mpeg_bytes);
    mpeg_data=memalign(64,(size_t)mpeg_bytes);
    if(!mpeg_data)return fail("movie decoder allocation",-ENOMEM);
    memset(mpeg_data,0,(size_t)mpeg_bytes);
    result=sceMpegCreate(&mpeg,mpeg_data,mpeg_bytes,&ring_storage.ring,RECVX_MOVIE_PSP_STRIDE,0,0);
    if(result<0)return fail("sceMpegCreate",result);
    created=1;
    SceInt32 offset=0,size=0;
    result=sceMpegQueryStreamOffset(&mpeg,header,&offset);
    if(result<0)return fail("sceMpegQueryStreamOffset",result);
    result=sceMpegQueryStreamSize(header,&size);
    if(result<0)return fail("sceMpegQueryStreamSize",result);
    if(offset<(int)PACKET_BYTES||size<=0||(offset&(PACKET_BYTES-1u))||(size&(PACKET_BYTES-1u))||
       offset>length||size>length-offset)return fail("movie stream extent",-EINVAL);
    if(fseek(movie_file,offset,SEEK_SET))return fail("movie stream seek",io_error());
    remaining_packets=(unsigned int)size/PACKET_BYTES;
    video_stream=sceMpegRegistStream(&mpeg,0,0);
    if(!video_stream)return fail("sceMpegRegistStream(video)",-EIO);
    es_buffer=sceMpegMallocAvcEsBuf(&mpeg);
    if(!es_buffer)return fail("sceMpegMallocAvcEsBuf",-ENOMEM);
    result=sceMpegInitAu(&mpeg,es_buffer,&au);
    if(result<0)return fail("sceMpegInitAu",result);
    SceMpegAvcMode mode={-1,SCE_MPEG_AVC_FORMAT_8888};
    result=sceMpegAvcDecodeMode(&mpeg,&mode);
    if(result<0)return fail("sceMpegAvcDecodeMode",result);
    return 0;
}

static int refill(void)
{
    int available=sceMpegRingbufferAvailableSize(&ring_storage.ring);
    if(available<0)return fail("sceMpegRingbufferAvailableSize",available);
    if(available>RING_PACKETS)return fail("movie ring available range",-EINVAL);
    if(!available||!remaining_packets)return 0;
    int packets=available;
    if((unsigned int)packets>remaining_packets)packets=(int)remaining_packets;
    int result=sceMpegRingbufferPut(&ring_storage.ring,packets,available);
    if(callback_error)return fail("movie stream read",callback_error);
    if(result<0)return fail("sceMpegRingbufferPut",result);
    if(result>packets)return fail("movie ring put range",-EINVAL);
    return result;
}
static int decoded(void *pixels,uint64_t *timestamp,SceInt32 status)
{
    if(status!=0&&status!=1)return fail("movie AVC output status",-EINVAL);
    if(!status)return 0;
    if(output_frames>=expected_frames)return frame_count_error("movie AVC exceeded authored frames");
    sceKernelDcacheInvalidateRange(pixels,RECVX_MOVIE_PSP_FRAME_BYTES);
    *timestamp=(uint64_t)output_frames*FRAME_TICKS;
    ++output_frames;
    return 1;
}
int recvxGameBootMoviePspDecode(void *pixels,uint64_t *timestamp)
{
    if(timestamp)*timestamp=0;
    if(!pixels||((uintptr_t)pixels&63u)||!timestamp)return fail("movie output buffer",-EINVAL);
    if(!created)return fail("movie decoder is not open",-EINVAL);
    if(stopped){
        if(output_frames!=expected_frames)return frame_count_error("movie AVC ended before authored frames");
        return 0;
    }
    for(unsigned int attempts=0;attempts<4096;++attempts){
        int added=refill();if(added<0)return -1;
        SceInt32 attributes=0;
        au.iPtsMSB=au.iPts=UINT32_MAX;
        int result=sceMpegGetAvcAu(&mpeg,video_stream,&au,&attributes);
        if((uint32_t)result==MPEG_NO_DATA){
            if(remaining_packets){
                if(!added)return fail("movie demux made no progress",-EIO);
                continue;
            }
            SceInt32 status=0;void *output=pixels;
            sceKernelDcacheWritebackInvalidateRange(pixels,RECVX_MOVIE_PSP_FRAME_BYTES);
            result=sceMpegAvcDecodeStop(&mpeg,RECVX_MOVIE_PSP_STRIDE,&output,&status);
            if(result<0)return fail("sceMpegAvcDecodeStop",result);
            stopped=1;
            int frame=decoded(pixels,timestamp,status);
            if(frame)return frame;
            if(output_frames!=expected_frames)return frame_count_error("movie AVC ended before authored frames");
            return 0;
        }
        if(result<0)return fail("sceMpegGetAvcAu",result);
        SceInt32 status=0;void *output=pixels;
        sceKernelDcacheWritebackInvalidateRange(pixels,RECVX_MOVIE_PSP_FRAME_BYTES);
        result=sceMpegAvcDecode(&mpeg,&au,RECVX_MOVIE_PSP_STRIDE,&output,&status);
        if(result<0)return fail("sceMpegAvcDecode",result);
        int frame=decoded(pixels,timestamp,status);
        if(frame)return frame;
    }
    return fail("movie AVC decode watchdog",-EIO);
}
