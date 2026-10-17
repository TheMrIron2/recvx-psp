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
#include "psp/game_boot_audio.h"

#define PACKET_BYTES 2048u
#define RING_PACKETS 256
#define MPEG_NO_DATA UINT32_C(0x80618001)
#define FRAME_TICKS UINT64_C(3003)
#define MAX_FRAMES 108000u
/* The firmware reserves 192 bytes of 44-byte output-picture descriptors and
   walks one destination pointer per returned picture during hardware CSC. */
#define OUTPUT_PICTURES 4u

static FILE *movie_file;
static SceMpeg mpeg;
static SceMpegStream *video_stream;
static void *ring_data,*mpeg_data,*es_buffer;
static unsigned char *extra_pictures;
static unsigned int pending_pictures,pending_index;
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
static char timing_error[160];

int recvxGameBootMoviePspError(void){return last_error;}
const char *recvxGameBootMoviePspStage(void){return error_stage?error_stage:"none";}

static void close_locked(void)
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
    free(mpeg_data);free(ring_data);free(extra_pictures);extra_pictures=NULL;
    pending_pictures=pending_index=0;
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
    close_locked();return -1;
}
static int io_error(void){return errno>0?-errno:-EIO;}
static uint64_t be48(const unsigned char *bytes)
{
    uint64_t value=0;
    for(unsigned int i=0;i<6;++i)value=(value<<8)|bytes[i];
    return value;
}
static int native_frame_table(const unsigned char *sector)
{
    if(memcmp(sector,"\0\0\1\xba",4))return 0;
    size_t cursor=14u+(sector[13]&7u);
    while(cursor+6u<=PACKET_BYTES){
        if(memcmp(sector+cursor,"\0\0\1",3))return 0;
        unsigned int type=sector[cursor+3];
        size_t size=(size_t)sector[cursor+4]*256u+sector[cursor+5];
        if(size>PACKET_BYTES-cursor-6u)return 0;
        if(type==0xbf){
            const unsigned char *table=sector+cursor+6u;
            if(size<22u||table[0]!=1||table[1]!=0xe0)return 0;
            unsigned int count=(unsigned int)table[16]*256u+table[17];
            unsigned int bytes=(unsigned int)table[14]*256u+table[15];
            return count>0&&size==18u+4u*count&&bytes==2u+4u*count;
        }
        if(type!=0xbb)return 0;
        cursor+=6u+size;
    }
    return 0;
}
static int padding_end_sector(const unsigned char *sector)
{
    /* Our cooker puts program_end_code in its own sector. Native MPEG stops
       parsing at B9 before the sector boundary and returns ILLEGAL_STREAM.
       Exclude only this exact padding-only tail, never a sector with video. */
    if(memcmp(sector,"\0\0\1\xba",4)||(sector[4]&0xc0u)!=0x40u||
       (sector[13]&7u)||memcmp(sector+14,"\0\0\1\xbe\x07\xe8",6)||
       memcmp(sector+PACKET_BYTES-4,"\0\0\1\xb9",4))return 0;
    for(size_t i=20;i<PACKET_BYTES-4;++i)if(sector[i]!=0xff)return 0;
    return 1;
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

static int open_locked(const char *path)
{
    close_locked();last_error=0;error_stage=NULL;
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
    if(fread(header,1,sizeof(header),movie_file)!=sizeof(header))return fail("movie stream header read",io_error());
    if(!native_frame_table(header))return fail("movie missing native frame index; recook data/movies",-EINVAL);
    if(fseek(movie_file,(long)offset+(long)size-(long)PACKET_BYTES,SEEK_SET))
        return fail("movie stream tail seek",io_error());
    if(fread(header,1,sizeof(header),movie_file)!=sizeof(header))return fail("movie stream tail read",io_error());
    unsigned int tail_packets=(unsigned int)padding_end_sector(header);
    if(fseek(movie_file,offset,SEEK_SET))return fail("movie stream seek",io_error());
    remaining_packets=(unsigned int)size/PACKET_BYTES-tail_packets;
    video_stream=sceMpegRegistStream(&mpeg,0,0);
    if(!video_stream)return fail("sceMpegRegistStream(video)",-EIO);
    es_buffer=sceMpegMallocAvcEsBuf(&mpeg);
    if(!es_buffer)return fail("sceMpegMallocAvcEsBuf",-ENOMEM);
    result=sceMpegInitAu(&mpeg,es_buffer,&au);
    if(result<0)return fail("sceMpegInitAu",result);
    SceMpegAvcMode mode={-1,SCE_MPEG_AVC_FORMAT_8888};
    result=sceMpegAvcDecodeMode(&mpeg,&mode);
    if(result<0)return fail("sceMpegAvcDecodeMode",result);
    extra_pictures=memalign(64,(OUTPUT_PICTURES-1u)*RECVX_MOVIE_PSP_FRAME_BYTES);
    if(!extra_pictures)return fail("movie extra output allocation",-ENOMEM);
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
    (void)pixels;
    *timestamp=(uint64_t)output_frames*FRAME_TICKS;
    ++output_frames;
    return 1;
}
static void *hardware_output(void *pixels)
{
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    return (void *)((uintptr_t)pixels|UINT32_C(0x40000000));
#else
    return pixels;
#endif
}
static int accept_pictures(void *pixels,uint64_t *timestamp,SceInt32 count)
{
    if(count<0||(unsigned int)count>OUTPUT_PICTURES)
        return fail("movie AVC output picture count",-EINVAL);
    if((unsigned int)count>expected_frames-output_frames)
        return frame_count_error("movie AVC exceeded authored frames");
    if(count){
        sceKernelDcacheInvalidateRange(pixels,RECVX_MOVIE_PSP_FRAME_BYTES);
        for(int i=1;i<count;++i)
            sceKernelDcacheInvalidateRange(extra_pictures+(i-1)*RECVX_MOVIE_PSP_FRAME_BYTES,RECVX_MOVIE_PSP_FRAME_BYTES);
    }
    pending_pictures=count?((unsigned int)count-1u):0;pending_index=0;
    return decoded(pixels,timestamp,count?1:0);
}
static int decode_locked(void *pixels,uint64_t *timestamp)
{
    if(timestamp)*timestamp=0;
    if(!pixels||((uintptr_t)pixels&63u)||!timestamp)return fail("movie output buffer",-EINVAL);
    if(!created)return fail("movie decoder is not open",-EINVAL);
    if(pending_pictures){
        memcpy(pixels,extra_pictures+pending_index*RECVX_MOVIE_PSP_FRAME_BYTES,RECVX_MOVIE_PSP_FRAME_BYTES);
        ++pending_index;--pending_pictures;
        return decoded(pixels,timestamp,1);
    }
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
                if(!added){
                    snprintf(timing_error,sizeof(timing_error),
                        "movie demux made no progress: remaining=%u free=%d pictures=%u/%u",
                        remaining_packets,sceMpegRingbufferAvailableSize(&ring_storage.ring),output_frames,expected_frames);
                    return fail(timing_error,-EIO);
                }
                continue;
            }
            SceInt32 status=0;
            void *output[OUTPUT_PICTURES]={hardware_output(pixels)};
            for(unsigned int i=1;i<OUTPUT_PICTURES;++i){
                unsigned char *extra=extra_pictures+(i-1u)*RECVX_MOVIE_PSP_FRAME_BYTES;
                sceKernelDcacheWritebackInvalidateRange(extra,RECVX_MOVIE_PSP_FRAME_BYTES);
                output[i]=hardware_output(extra);
            }
            sceKernelDcacheWritebackInvalidateRange(pixels,RECVX_MOVIE_PSP_FRAME_BYTES);
            result=sceMpegAvcDecodeStop(&mpeg,RECVX_MOVIE_PSP_STRIDE,output,&status);
            if(result<0)return fail("sceMpegAvcDecodeStop",result);
            stopped=1;
            int frame=accept_pictures(pixels,timestamp,status);
            if(frame)return frame;
            if(output_frames!=expected_frames)return frame_count_error("movie AVC ended before authored frames");
            return 0;
        }
        if(result<0)return fail("sceMpegGetAvcAu",result);
        SceInt32 status=0;
        void *output[OUTPUT_PICTURES]={hardware_output(pixels)};
        for(unsigned int i=1;i<OUTPUT_PICTURES;++i){
            unsigned char *extra=extra_pictures+(i-1u)*RECVX_MOVIE_PSP_FRAME_BYTES;
            sceKernelDcacheWritebackInvalidateRange(extra,RECVX_MOVIE_PSP_FRAME_BYTES);
            output[i]=hardware_output(extra);
        }
        sceKernelDcacheWritebackInvalidateRange(pixels,RECVX_MOVIE_PSP_FRAME_BYTES);
        result=sceMpegAvcDecode(&mpeg,&au,RECVX_MOVIE_PSP_STRIDE,output,&status);
        if(result<0)return fail("sceMpegAvcDecode",result);
        int frame=accept_pictures(pixels,timestamp,status);
        if(frame)return frame;
    }
    return fail("movie AVC decode watchdog",-EIO);
}

/* SAS and MPEG both enter firmware codec services. Keep initialization,
   synthesis, decoding and teardown mutually exclusive, including failures. */
void recvxGameBootMoviePspClose(void)
{recvxAudioLock();close_locked();recvxAudioUnlock();}
int recvxGameBootMoviePspOpen(const char *path)
{recvxAudioLock();int result=open_locked(path);recvxAudioUnlock();return result;}
int recvxGameBootMoviePspDecode(void *pixels,uint64_t *timestamp)
{recvxAudioLock();int result=decode_locked(pixels,timestamp);recvxAudioUnlock();return result;}
