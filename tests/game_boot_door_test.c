#include <assert.h>
#include <limits.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#pragma GCC diagnostic ignored "-Wunused-function"
#include "psp/game_boot.h"
#include "ps2/veronica/prog/door.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/room.h"

extern _door_wrk DoorWrk;
static SYS_WORK state;
SYS_WORK *sys=&state;
static unsigned int screen_color;
static float screen_frames;
static int screen_calls,cut_calls;
void bhSetScreenFade(unsigned int color,float frames){screen_color=color;screen_frames=frames;++screen_calls;}
void bhCheckCut(int force){assert(force==1);++cut_calls;}
static NJS_CNK_OBJECT model;
static NJS_TEXLIST textures;
static char order[64];
static unsigned int order_count, sounds[16], sound_count, commands[16], command_count;
static int pushes, pops, draws, texture_sets, fade_draws, rotations[8], rotation_count;
static unsigned int fade_color;
static const char *audio_fault;
static int audio_error;
static jmp_buf failure;
static const char *fault;
void recvxGameBootUnsupported(const char *name) { fault=name; longjmp(failure,1); }
int recvxAudioPlayMidi(int slot,int sound,int volume,int pan)
{ assert(slot==2 && !volume && !pan && sound_count<16); sounds[sound_count++]=(unsigned int)sound; return 0; }
const char *recvxAudioFault(void) { return audio_fault; }
int recvxAudioNativeError(void) { return audio_error; }
void SendSoundCommand(unsigned int command) { assert(command_count<16); commands[command_count++]=command; }
int njPushMatrix(NJS_MATRIX *m) { assert(!m); ++pushes; return 1; }
int njPopMatrix(unsigned int n) { assert(n==1); ++pops; return 1; }
void njUnitMatrix(NJS_MATRIX *m) { assert(!m && pushes==pops+1); }
void njRotateX(NJS_MATRIX *m,int a) { assert(!m); rotations[rotation_count++]=a; }
void njRotateY(NJS_MATRIX *m,int a) { assert(!m); rotations[rotation_count++]=a; }
void njRotateZ(NJS_MATRIX *m,int a) { assert(!m); rotations[rotation_count++]=a; }
void njTranslate(NJS_MATRIX *m,float x,float y,float z)
{ assert(!m && x==-DoorWrk.vew_pos.x && y==-DoorWrk.vew_pos.y && z==-DoorWrk.vew_pos.z); }
void njCnkSetEasyMultiLightMatrices(void) { assert(pushes==pops+1); }
int njSetTexture(NJS_TEXLIST *list) { assert(list==&textures); ++texture_sets; return 0; }
void njSetPerspective(int angle) { assert(angle==12743); }
void njCnkEasyMultiDrawObject(NJS_CNK_OBJECT *object) { assert(object==&model && pushes==pops+1); ++draws; }
void njColorBlendingMode(int target,int mode) { assert((!target && mode==8)||(target==1 && mode==6)); }
void njDrawPolygon2D(NJS_POINT2COL *p,int count,float priority,unsigned int attr)
{
    assert(count==4 && p->num==4 && priority==-0.8f && attr==96);
    assert(p->p[0].x==0 && p->p[2].x==640 && p->p[2].y==480);
    for(unsigned int i=1;i<4;++i) assert(p->col[i].color==p->col[0].color);
    ++fade_draws; fade_color=p->col[0].color;
}
#define REJECT(call, text) do { fault=NULL; if(!setjmp(failure)){call;assert(!"expected door fault");} assert(fault&&strstr(fault,text)); } while(0)
static void mark(char c) { assert(order_count<sizeof(order)); order[order_count++]=c; }
static void fade(void *p) { assert(p==&DoorWrk); mark('f'); }
static void view(void *p) { assert(p==&DoorWrk); mark('v'); }
static void door(void *p) { assert(p==&DoorWrk); mark('d'); }
static void light(void *p) { assert(p==&DoorWrk); mark('l'); }
static void puru(void *p) { assert(p==&DoorWrk); mark('p'); }
static void finish(void *p) { door(p); DoorWrk.status|=0x40; }
static void reset(void)
{
    memset(&DoorWrk,0,sizeof(DoorWrk)); DoorWrk.BgmSet[0]=-1;
    memset(order,0,sizeof(order)); order_count=sound_count=command_count=0;
    pushes=pops=draws=texture_sets=fade_draws=rotation_count=0;
    audio_fault=NULL; audio_error=0; fault=NULL;
}
static void door_start(void)
{
    for(int timer=0;timer<2;++timer){
        reset();memset(&state,0,sizeof(state));sys=&state;
        state.ddmd=0;state.door.flg=0x1234;state.door.stg_no=2;state.door.rom_no=17;
        state.door.pos_no=3;state.door.dor_tp=255;
        state.door.mode0=state.door.mode1=state.door.mode2=state.door.mode3=7;
        state.door.ct0=1;state.door.ct1=2;state.door.ct2=3;state.door.ct3=4;
        state.ts_flg=0x40000800;state.sp_flg=0x90000400u|(timer?0x200:0);
        state.cb_flg=0x1001;state.pad_on=state.pad_ps=~0u;state.ef_ct=70;state.ef_ctb=48;state.error=99;
        state.mn_mode0=state.mn_mode1=state.mn_mode2=state.mn_mode3=7;
        screen_calls=cut_calls=0;
        bhStartDoorDemo();
        assert(screen_calls==1&&screen_color==0xff000000u&&screen_frames==10&&command_count==1&&commands[0]==2);
        assert(!state.door.mode0&&!state.door.mode1&&!state.door.mode2&&!state.door.mode3);
        assert(!state.door.ct0&&!state.door.ct1&&!state.door.ct2&&!state.door.ct3);
        assert(state.door.flg==0x1234&&state.door.dor_tp==255&&state.stg_no==2&&state.rom_no==17&&state.pos_no==3);
        assert(state.ts_flg==0x40100000&&state.sp_flg==(unsigned int)(timer?0x248:0x48));
        assert(!state.pad_on&&!state.pad_ps&&state.ef_ct==48&&!state.error&&state.cb_flg==0x1001);
        assert(state.mn_mode0==2&&!state.mn_mode1&&!state.mn_mode2&&!state.mn_mode3&&state.ddmd==1);
        SYS_WORK waiting=state;
        for(int frame=0;frame<5;++frame)bhStartDoorDemo();
        assert(!memcmp(&state,&waiting,sizeof(state))&&screen_calls==1&&command_count==1&&!cut_calls);
        state.ts_flg|=0x800;state.fade_pbk=0xff112233;
        bhStartDoorDemo();
        assert(screen_calls==2&&!screen_color&&screen_frames==5&&cut_calls==1&&command_count==1);
        assert(!state.fade_pbk&&state.cb_flg==0x1000&&state.sp_flg==~0u&&state.ddmd==1);
    }
    state.ddmd=-1;REJECT(bhStartDoorDemo(),"door start state");
    state.ddmd=2;REJECT(bhStartDoorDemo(),"door start state");
    sys=NULL;REJECT(bhStartDoorDemo(),"door start state");sys=&state;
    puts("PASS: original door entry fade/sound, destination and controller reset, timer retention, unchanged wait frames, completion fade/cut and invalid entry states");
}

int main(void)
{
    for(int speed=0;speed<3;++speed) {
        reset(); DoorWrk.spd_mde=speed; DoorWrk.status=0x1000000f;
        DoorWrk.fde_prcP=fade; DoorWrk.vew_prcP=view; DoorWrk.dor_prcP=door;
        DoorWrk.lgt_prcP=light; DoorWrk.pru_prcP=puru;
        assert(bhControlDoor()==1 && DoorWrk.time==speed+1 && order_count==(unsigned int)(speed+1)*5);
        for(int i=0;i<=speed;++i) assert(!memcmp(order+i*5,"fvdlp",5));
    }
    reset(); DoorWrk.status=0x80|0x1000000f; DoorWrk.dor_prcP=door;
    assert(bhControlDoor()==1 && !order_count && !DoorWrk.time);
    reset(); DoorWrk.status=0x40; assert(bhControlDoor()==0 && !DoorWrk.time);
    reset(); DoorWrk.status=4; DoorWrk.dor_prcP=finish;
    assert(bhControlDoor()==0 && DoorWrk.time==1 && order_count==1);
    reset(); DoorWrk.status=0xf0000; assert(bhControlDoor()==1);
    assert(sound_count==1 && sounds[0]==0x80000101 && !(DoorWrk.status&0xf0000));
    reset(); DoorWrk.status=0xc0000; bhControlDoor(); assert(sounds[0]==0x80000103);
    reset(); DoorWrk.status=0x40000; bhControlDoor(); assert(sounds[0]==0x80000102);
    reset(); DoorWrk.status=0x10000; bhControlDoor(); assert(sounds[0]==0x80000100);
    reset(); DoorWrk.status=0x40|0x20|0xf00000; bhControlDoor();
    assert(sound_count==1 && sounds[0]==0x80000101 && !(DoorWrk.status&0xf00000));
    reset(); DoorWrk.status=0x80|0x10; DoorWrk.BgmSet[0]=0; DoorWrk.BgmSet[1]=0x10; DoorWrk.BgmSet[2]=-1;
    bhControlDoor(); bhControlDoor(); assert(command_count==1 && commands[0]==1 && !DoorWrk.BgmSet[1]);
    reset(); DoorWrk.status=0x200|0x400|0x100; DoorWrk.dor_mdl.objP=&model;
    DoorWrk.dor_mdl.texP=&textures; DoorWrk.vew_pos=(NJS_POINT3){1,2,3};
    DoorWrk.vew_ang[0]=INT_MIN; DoorWrk.vew_ang[1]=12; DoorWrk.vew_ang[2]=34;
    DoorWrk.fde_col=(int)0x80402010u; DoorWrk.fde_rate=0.5f;
    assert(bhControlDoor()==1 && pushes==1 && pops==1 && draws==1 && texture_sets==1 && fade_draws==1);
    assert(rotation_count==5 && rotations[0]==32768 && rotations[1]==65502 && rotations[2]==0 && rotations[3]==65524 && rotations[4]==32768);
    assert(fade_color==0x40201008u);
    reset(); DoorWrk.status=0x40000000; DoorWrk.time=INT_MAX/1024+5; bhControlDoor();
    assert(DoorWrk.dki_mode==1 && DoorWrk.lgt_amb_off.x==0.5f && isfinite(DoorWrk.lgt_pnt_rte.x));
    reset(); DoorWrk.spd_mde=-1; REJECT(bhControlDoor(),"animation clock");
    reset(); DoorWrk.time=INT_MAX; REJECT(bhControlDoor(),"animation clock");
    reset(); memset(DoorWrk.BgmSet,0,sizeof(DoorWrk.BgmSet)); REJECT(bhControlDoor(),"sound schedule");
    reset(); DoorWrk.status=0x200; REJECT(bhControlDoor(),"drawing state");
    reset(); DoorWrk.status=0x300; DoorWrk.dor_mdl.objP=&model; DoorWrk.fde_rate=NAN;
    REJECT(bhControlDoor(),"fade rate");
    reset(); DoorWrk.status=0x10000; audio_fault="door bank unavailable"; REJECT(bhControlDoor(),"bank unavailable");
    reset(); DoorWrk.status=0x10000; audio_error=-1; REJECT(bhControlDoor(),"native door sound");
    door_start();
    puts("PASS: original door callback order and speed, pause/completion, sound priority and schedule consumption, view/model/fade submission, wrapped light timing and malformed state");
    return 0;
}
