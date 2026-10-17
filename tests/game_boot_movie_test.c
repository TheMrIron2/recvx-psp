#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/game_boot_movie.h"
#include "psp/game_boot_movie_psp.h"
#include "ps2/veronica/prog/padman.h"
#include "psp/game_boot_movie_audio.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/sdfunc.h"

static SYS_WORK state;
SYS_WORK *sys=&state;
PAD_WRK Pad[4];
int CurrentPortId;
static uint64_t frames[32], samples, wall_us;
static unsigned int frame_count, decoded, opened, closed, bgm_stops, voice_stops, audio_starts, audio_stops;
static int audio_active, volume, start_error, open_error, decode_error, reset;
static unsigned int fade_calls, fade_controls, fade_draws;
static char video_path[576], audio_path[576];
static jmp_buf fault;
static int rejecting;
static const char *fault_text;
static char copied_fault[256];

void recvxGameBootUnsupported(const char *reason)
{
    if(!rejecting){fprintf(stderr,"unexpected movie service fault: %s\n",reason);abort();}
    snprintf(copied_fault,sizeof(copied_fault),"%s",reason);fault_text=copied_fault;
    longjmp(fault,1);
}
#define REJECT(call,text) do{rejecting=1;fault_text=NULL; \
    if(!setjmp(fault)){call;assert(!"expected movie service fault");} \
    rejecting=0;assert(fault_text&&strstr(fault_text,text));}while(0)
void recvxGameBootLog(const char *format,...) {(void)format;}
uint64_t recvxPlatformTimeUs(void) {return wall_us;}
int recvxGameBootMoviePspOpen(const char *path)
{assert(bgm_stops==1&&voice_stops==1);++opened;snprintf(video_path,sizeof(video_path),"%s",path);return open_error?-1:0;}
int recvxGameBootMoviePspDecode(void *pixels,uint64_t *pts)
{
    assert(!((uintptr_t)pixels&63u));
    if(decode_error)return -1;
    if(decoded==frame_count)return 0;
    *pts=frames[decoded];
    for(unsigned int i=0;i<512u*272u;++i)((uint32_t *)pixels)[i]=0xff000000u|decoded;
    ++decoded;return 1;
}
void recvxGameBootMoviePspClose(void) {++closed;}
int recvxGameBootMoviePspError(void) {return (int)0x80610002u;}
const char *recvxGameBootMoviePspStage(void) {return "mock sceMpegAvcDecode";}
int recvxAudioNativeError(void) {return (int)0x80420016u;}
void StopBgm(int frames_to_fade) {assert(!frames_to_fade);++bgm_stops;}
void StopVoice(int frames_to_fade) {assert(!frames_to_fade);++voice_stops;}
int recvxMovieAudioStart(const char *path)
{++audio_starts;snprintf(audio_path,sizeof(audio_path),"%s",path);audio_active=!start_error;return start_error?-1:0;}
void recvxMovieAudioStop(void) {++audio_stops;audio_active=0;}
int recvxMovieAudioActive(void) {return audio_active;}
uint64_t recvxMovieAudioSamples(void) {return samples;}
void recvxMovieAudioVolume(int level) {assert(level>=0&&level<=4096);volume=level;}
int CheckSoftResetKeyFlag(int controller) {assert(controller==-1);return reset;}
void bhSetScreenFade(unsigned int color,float count)
{assert(color==0xff000000u&&count==30);++fade_calls;sys->cb_flg|=2;sys->fade_an=0;}
void bhControlScreenFade(void)
{++fade_controls;sys->fade_an=fminf(255,fade_controls*8.5f);if(fade_controls==30)sys->cb_flg&=~2u;}
void bhDrawScreenFade(void) {assert(sys->fade_an>0);++fade_draws;}

static void setup(unsigned int count)
{
    recvxGameBootMovieFrameConsumed();recvxGameBootMovieShutdown();
    memset(&state,0,sizeof(state));
    memset(Pad,0,sizeof(Pad));CurrentPortId=0;
    frame_count=count;decoded=opened=closed=bgm_stops=voice_stops=audio_starts=audio_stops=0;
    audio_active=volume=start_error=open_error=decode_error=reset=0;
    fade_calls=fade_controls=fade_draws=0;
    samples=0;wall_us=1000000;
    for(unsigned int i=0;i<count;++i)frames[i]=(uint64_t)i*3003u;
    assert(!recvxGameBootMovieInit("assets"));
}

static void clock_at(uint64_t pts)
{
    /* Add the output-block latency intentionally removed by the service. */
    samples=256u+(pts*44100u+89999u)/90000u;
}
static uint32_t front_color(void)
{
    const uint32_t *frame=recvxGameBootMovieGetFrame();
    assert(frame&&!((uintptr_t)frame&63u));
    uint32_t value=frame[0];
    assert(frame[512u*272u]==0 && frame[512u*512u-1u]==0);
    recvxGameBootMovieFrameConsumed();return value;
}

int main(void)
{
    setup(3);
    assert(!PlayStartMovieEx(3,0,1));
    assert(opened==1&&decoded==1&&!audio_starts&&bgm_stops==1&&voice_stops==1);
    assert(!strcmp(video_path,"assets/movies/MV_003.pmf"));
    assert(CheckPlayEndMovie()==1&&GetTimeMoive()==0&&front_color()==0xff000000);
    assert(PlayStartMovieEx(4,0,1)==2 && opened==1);
    assert(!WaitPrePlayMovie(0)&&audio_starts==1&&volume==4096);
    assert(!strcmp(audio_path,"assets/movies/MV_003.vag"));
    assert(!PlayMovieMain(0)&&decoded==2&&front_color()==0xff000000);
    clock_at(3002); assert(!PlayMovieMain(0)&&front_color()==0xff000000);
    clock_at(3003); assert(!PlayMovieMain(0)&&front_color()==0xff000001);
    clock_at(6006); assert(!PlayMovieMain(0)&&front_color()==0xff000002);
    clock_at(9009); assert(!PlayMovieMain(0)&&CheckPlayEndMovie()); /* Audio still owns completion. */
    audio_active=0;
    assert(PlayMovieMain(0)==1&&!CheckPlayEndMovie()&&closed==1&&audio_stops==1);
    assert(!recvxGameBootMovieGetFrame()&&GetTimeMoive()==0);

    setup(3); assert(!PlayStartMovieEx(0,0,1)); assert(!WaitPrePlayMovie(0));
    const uint32_t *pinned=recvxGameBootMovieGetFrame();
    clock_at(6006); assert(!PlayMovieMain(0)&&decoded==1&&pinned[0]==0xff000000);
    assert(recvxGameBootMovieGetFrame()==pinned);
    recvxGameBootMovieFrameConsumed();
    assert(!PlayMovieMain(0)&&front_color()==0xff000002);
    pinned=recvxGameBootMovieGetFrame();
    recvxGameBootMovieShutdown(); assert(pinned[0]==0xff000002);
    assert(!recvxGameBootMovieGetFrame());
    recvxGameBootMovieFrameConsumed(); /* Deferred free occurs only here. */

    setup(1); assert(!PlayStartMovieEx(1,0,0)); assert(audio_starts==1);
    audio_active=0;samples=256;
    assert(!PlayMovieMain(0));
    wall_us+=33366;assert(!PlayMovieMain(0));
    wall_us+=1;assert(PlayMovieMain(0)==1); /* Last frame held for one 3003-tick interval. */

    setup(12); assert(!PlayStartMovieEx(1,0,1)); assert(!WaitPrePlayMovie(0));
    clock_at(11*3003u);assert(!PlayMovieMain(0)&&decoded==9); /* Bounded eight-frame catchup. */
    assert(!PlayMovieMain(0)&&front_color()==0xff00000b);
    assert(GetTimeMoive()==36);

    setup(2);assert(!PlayStartMovieEx(3,0,1));sys->pad_ps=0x100;
    assert(!WaitPrePlayMovie(0)&&CheckPlayEndMovie()); /* Gameplay mapping is paused during FMVs. */
    CurrentPortId=1;Pad[1].press=0x100;
    assert(PlayMovieMain(0)==2&&!CheckPlayEndMovie());
    setup(2);assert(!PlayStartMovieEx(3,0,1));CurrentPortId=4;
    REJECT(WaitPrePlayMovie(0),"controller");CurrentPortId=0;
    setup(2);assert(!PlayStartMovieEx(3,0,1));Pad[0].press=0x100;
    assert(WaitPrePlayMovie(0)==2&&!CheckPlayEndMovie()&&!audio_starts);
    setup(2);assert(!PlayStartMovieEx(15,0,1));Pad[0].press=0x100;
    assert(!WaitPrePlayMovie(0)&&CheckPlayEndMovie()); /* Type table disallows cancel. */
    setup(2);assert(!PlayStartMovieEx(15,1,1));Pad[0].press=0x100;
    assert(WaitPrePlayMovie(0)==2); /* Explicit override permits it. */
    setup(2);assert(!PlayStartMovieEx(17,0,1));Pad[0].press=0x100;
    assert(!WaitPrePlayMovie(0));assert(!PlayMovieMain(0)&&CheckPlayEndMovie());
    setup(2);assert(!PlayStartMovieEx(3,0,1));assert(!WaitPrePlayMovie(0));Pad[0].press=0x100;
    assert(PlayMovieMain(0)==2&&audio_stops==1);
    setup(2);assert(!PlayStartMovieEx(16,0,1));assert(!WaitPrePlayMovie(0));Pad[0].press=0x100;
    assert(!PlayMovieMain(0)&&!fade_calls); /* Movie16 uses START. */
    Pad[0].press=0x800;
    int previous=4096;
    for(unsigned int i=0;i<30;++i){assert(!PlayMovieMain(0));assert(volume<=previous);previous=volume;
        if(i==0)assert(volume==3961);}
    assert(fade_calls==1&&fade_controls==30&&fade_draws==30&&volume<=5);
    assert(PlayMovieMain(0)==2&&!CheckPlayEndMovie());

    setup(2);assert(!PlayStartMovieEx(3,0,1));reset=1;
    assert(WaitPrePlayMovie(0)==3&&!CheckPlayEndMovie());
    setup(2);assert(!PlayStartMovieEx(3,0,1));assert(!WaitPrePlayMovie(0));reset=1;
    assert(PlayMovieMain(0)==3&&!CheckPlayEndMovie());

    setup(2);open_error=1;REJECT(PlayStartMovieEx(3,0,1),"AVC open");
    assert(strstr(fault_text,"80610002")&&strstr(fault_text,"sceMpegAvcDecode"));
    setup(2);decode_error=1;REJECT(PlayStartMovieEx(3,0,1),"AVC decode");
    setup(0);REJECT(PlayStartMovieEx(3,0,1),"empty");
    setup(2);frames[0]=1;REJECT(PlayStartMovieEx(3,0,1),"timestamp");
    setup(2);frames[1]=0;assert(!PlayStartMovieEx(3,0,1));assert(!WaitPrePlayMovie(0));
    REJECT(PlayMovieMain(0),"timestamp");
    setup(2);assert(!PlayStartMovieEx(3,0,1));start_error=1;
    REJECT(WaitPrePlayMovie(0),"ADPCM start");assert(strstr(fault_text,"80420016"));
    setup(2);REJECT(PlayStartMovieEx(22,0,1),"start state");
    setup(2);REJECT(PlayStartMovieEx(-1,0,1),"start state");
    recvxGameBootMovieShutdown();
    puts("PASS: native movie service prepared/playing/end/cancel states, frame PTS vs ADPCM clock, bounded catchup, pinned two-buffer lifetime, EOF tail, retail cancel/fade policy, and decoder/audio error propagation");
    return 0;
}
