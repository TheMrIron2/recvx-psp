#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/game_boot_event_movie.h"
#include "ps2/veronica/prog/event.h"
#include "ps2/veronica/prog/main.h"

static SYS_WORK state;
SYS_WORK *sys = &state;
static unsigned char arena[512] __attribute__((aligned(64)));
unsigned char *freemem = arena;
unsigned char *bhScePtr;
static int started, stopped, prepared, played, active, start_result, prepare_result, play_result;
static unsigned int fog_on, fog_off, background, title, sound_stop, reset_clear;
static jmp_buf fault;
static int rejecting;
static const char *reason;

void recvxGameBootUnsupported(const char *name)
{
    if (!rejecting) { fprintf(stderr,"unexpected movie task fault: %s\n",name); abort(); }
    reason=name; longjmp(fault,1);
}
#define REJECT(call, message) do { rejecting=1;reason=NULL; \
    if(!setjmp(fault)){call;assert(!"expected movie task fault");} \
    rejecting=0;assert(reason&&strstr(reason,message)); } while(0)
int PlayStartMovieEx(int movie, int type, int pause)
{ assert(movie==3 && type==2 && pause==1); ++started; active=1; return start_result; }
int WaitPrePlayMovie(int mode) { assert(!mode); ++prepared; return prepare_result; }
int PlayMovieMain(int mode) { assert(!mode); ++played; return play_result; }
int CheckPlayEndMovie(void) { return active; }
void PlayStopMovieEx(int mode) { assert(!mode); ++stopped; active=0; }
void RequestAllStopSoundEx(int streams,int sounds,int frames)
{ assert(streams==1 && sounds==1 && !frames); ++sound_stop; }
void bhReturnTitle(void) { ++title; }
void ClearSoftResetKeyFlag(int controller) { assert(controller==-1); ++reset_clear; }
void njFogEnable(void) { ++fog_on; }
void njFogDisable(void) { ++fog_off; }
void njSetBackColor(unsigned int a,unsigned int b,unsigned int c)
{ assert(!a&&!b&&!c); ++background; }

static void setup(void)
{
    memset(&state,0,sizeof(state));
    sys=&state; sys->memp=arena+1; sys->endp=arena+sizeof(arena);
    sys->mvi_no=3; sys->mvi_tp=2; sys->mvi_spb=0x12345678; sys->mvi_tsb=0x33445566;
    sys->sp_flg=4; sys->ts_flg=0x1c0;
    started=stopped=prepared=played=active=start_result=prepare_result=play_result=0;
    fog_on=fog_off=background=title=sound_stop=reset_clear=0;
}

int main(void)
{
    unsigned char command[8]={94,21,255,255};
    setup(); bhScePtr=command;
    assert(bhMovieStart()==1 && bhScePtr==command+4 && sys->mvi_no==21 && (sys->cb_flg&0x4000000));
    active=1; bhScePtr=command;
    assert(bhMovieStop()==1 && bhScePtr==command+2 && !(sys->cb_flg&0x4000000) && active);
    bhScePtr=command; assert(bhMovieCk()==1 && bhScePtr==command+6);
    active=0; bhScePtr=command; assert(!bhMovieCk() && bhScePtr==command+6);

    setup();
    recvxGameBootSysCallMovie();
    assert(sys->mvi_md==1 && sys->memp==arena+64 && sys->mvi_memp==arena+64 && !sys->mvi_flg);
    recvxGameBootSysCallMovie(); assert(sys->mvi_md==2 && !started);
    recvxGameBootSysCallMovie(); assert(sys->mvi_md==3 && started==1 && active);
    prepare_result=1; recvxGameBootSysCallMovie();
    assert(sys->mvi_md==3 && prepared==1 && sys->sp_flg==4 && !played);
    prepare_result=0; recvxGameBootSysCallMovie();
    assert(sys->mvi_md==4 && sys->sp_flg==0x34 && sys->ts_flg==0xc0 && background==1);
    recvxGameBootSysCallMovie(); assert(sys->mvi_md==4 && played==1 && fog_off==1);
    play_result=1; active=0; recvxGameBootSysCallMovie();
    assert(sys->mvi_md==5 && played==2 && !(sys->ts_flg&0x40));
    sys->memp=arena+128;
    recvxGameBootSysCallMovie(); assert(sys->mvi_md==6 && sys->memp==arena+64);
    sys->st_flg=2; recvxGameBootSysCallMovie();
    assert(sys->bcl_ct==1 && (sys->gm_flg&0x8000) && fog_on==1);
    assert(sys->sp_flg==sys->mvi_spb && sys->ts_flg==sys->mvi_tsb);

    setup(); sys->mvi_md=3; prepare_result=2;
    recvxGameBootSysCallMovie(); assert(sys->mvi_md==5 && !(sys->ts_flg&0x40));
    setup(); sys->mvi_md=3; prepare_result=3; sys->ss_flg=0x81800;
    recvxGameBootSysCallMovie();
    assert(stopped==1 && sound_stop==1 && title==1 && reset_clear==1 && sys->ss_flg==0x800);
    for(int result=1;result<=3;++result) {
        setup(); sys->mvi_md=4; play_result=result;
        recvxGameBootSysCallMovie(); assert(sys->mvi_md==5 && !(sys->ts_flg&0x40));
    }

    setup(); sys->memp=arena+512; sys->endp=arena+511;
    REJECT(recvxGameBootSysCallMovie(),"arena");
    setup(); sys->memp=arena+510; sys->endp=arena+511;
    REJECT(recvxGameBootSysCallMovie(),"arena");
    setup(); sys->mvi_md=5; sys->mvi_memp=arena+1;
    REJECT(recvxGameBootSysCallMovie(),"alignment");
    setup(); sys->mvi_md=5; sys->mvi_memp=(unsigned char *)1;
    REJECT(recvxGameBootSysCallMovie(),"arena");
    setup(); sys->mvi_md=7;
    REJECT(recvxGameBootSysCallMovie(),"mode");
    setup(); sys->mvi_md=2; sys->mvi_no=22;
    REJECT(recvxGameBootSysCallMovie(),"ID");
    setup(); sys->mvi_md=2; start_result=1;
    REJECT(recvxGameBootSysCallMovie(),"start"); assert(sys->mvi_md==2);
    setup(); sys->mvi_md=3; prepare_result=-1;
    REJECT(recvxGameBootSysCallMovie(),"preparation");
    setup(); sys->mvi_md=4; play_result=-1;
    REJECT(recvxGameBootSysCallMovie(),"playback");
    setup(); sys=NULL; REJECT(recvxGameBootSysCallMovie(),"mode");
    puts("PASS: original movie event request/stop/active polarity, seven task stages, real-service wait/end/cancel contracts, flag/memory restoration, and bounded movie state");
    return 0;
}
