#include <assert.h>
#include <stdio.h>
#include <setjmp.h>
#include <math.h>
#include <string.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/event.h"
#include "ps2/veronica/prog/sdfunc.h"
#include "ps2/veronica/prog/main.h"

unsigned char *bhScePtr;
static int last_id, last_fade, last_volume, calls;
static int last_slot, last_mode, stopped_voice;
static NJS_POINT3 last_position;
NEXTSOUND_INFO NextSoundInfo;
static int control_type,control_slot,control_flags,first_seen,last_seen,frames_seen,route;
void Set3dSoundFlag(int type,int slot,unsigned int flags)
{control_type=type;control_slot=slot;control_flags=flags;}
void Reset3dSoundFlag(void){control_flags=0;}
void SetUserSoundVolume(int type,int slot,int first,int last,int frames)
{control_type=type;control_slot=slot;first_seen=first;last_seen=last;frames_seen=frames;}
void SetUserSoundPan(int type,int slot,int first,int last,int frames)
{control_type=type;control_slot=slot;first_seen=first;last_seen=last;frames_seen=frames;}
int CustomMidiSlotDef(int objects,int events){last_slot=objects;last_mode=events;return 0;}
void StopEnemySe(int slot){last_slot=slot;++calls;}
void StopBackGroundSe2(unsigned int slot){last_slot=(int)slot;++calls;}
void StopBgm2(void){++calls;}
void CallBackGroundSe2(unsigned int slot,int sound){last_slot=(int)slot;last_id=sound;++calls;}
void PlayBgm2(int sound,int volume){last_id=sound;last_volume=volume;++calls;}
void SetRoomSoundCaseNo(int number){last_id=number;++calls;}
void CallSystemSe(int slot,int sound){last_slot=slot;last_id=sound;++calls;}
void CallPlayerVoice(int sound){route=0;last_id=sound;++calls;}
void CallPlayerFootStepSeEx(int floor,int type,int flag,int id,NJS_POINT3 *position)
{route=1;last_id=floor;last_mode=type;last_slot=id;last_position=*position;assert(flag==1);++calls;}
void CallPlayerActionSe(int sound,int flag){route=2;last_id=sound;assert(flag==1);++calls;}
void CallPlayerWeaponSeEx(NJS_POINT3 *position,int sound,int slot)
{route=3;last_id=sound;last_slot=slot;last_position=*position;++calls;}
void CallYakkyouSe(NJS_POINT3 *position,int sound){route=4;last_id=sound;last_position=*position;++calls;}
void CallEnemySe(int slot,NJS_POINT3 *position,int sound)
{route=5;last_slot=slot;last_id=sound;last_position=*position;++calls;}
void CallBackGroundSe(unsigned int slot,int sound){route=6;last_slot=(int)slot;last_id=sound;++calls;}

static SYS_WORK work;
static ROM_WORK room;
SYS_WORK *sys = &work;
ROM_WORK *rom = &room;
BH_PWORK ply, ene[128];
BH_PWORK *plp = &ply;
BH_SCEWORK bhEtask[16];
BH_SCEWORK *bhCetask = bhEtask;
static struct { O_WORK world[3]; NJS_CNK_OBJECT nodes[2]; ETTY_WORK placement; O_WRK objects[2]; } arena;
unsigned char *freemem = (unsigned char *)&arena;
static jmp_buf jump;
static const char *fault;
size_t recvxGameBootSourceRemaining(const void *pointer) { (void)pointer; return 0; }
void recvxGameBootUnsupported(const char *reason) { fault = reason; longjmp(jump, 1); }

void PlayBgm(int id, int fade)
{ last_id = id; last_fade = fade; ++calls; }
void PlayBgmEx(int id, int fade, int volume)
{ last_id = id; last_fade = fade; last_volume = volume; ++calls; }
void StopBgm(int fade)
{ last_fade = fade; ++calls; }
int CallNativeEventSe(int slot, NJS_POINT3 *position, int sound, int mode)
{ route=7;last_slot = slot; last_position = *position; last_id = sound; last_mode = mode; ++calls; return 0; }
int StopNativeEventSe(int slot) { last_slot = slot; ++calls; return 0; }
void PlayVoice(int voice, NJS_POINT3 *position, int mode, int fade)
{ last_id = voice; last_position = *position; last_mode = mode; last_fade = fade; ++calls; }
void StopVoice(int fade) { last_fade = fade; ++stopped_voice; }
void CallBackGroundSeEx(unsigned int slot, int sound, short frames)
{ last_slot = (int)slot; last_id = sound; last_fade = frames; ++calls; }
void StopBackGroundSeEx(unsigned int slot, short frames)
{ last_slot = (int)slot; last_fade = frames; ++calls; }

static void reject(unsigned int (*handler)(void), unsigned char *command, const char *reason)
{
    int before = calls;
    bhScePtr = command;
    if (setjmp(jump) == 0) { handler(); assert(!"malformed event sound accepted"); }
    assert(fault && strstr(fault, reason) && calls == before && bhScePtr == command);
}

int main(void)
{
    work.endp = (unsigned char *)&arena + sizeof(arena);
    work.obwp = arena.objects; room.enep = &arena.placement; room.ene_n = 1;
    arena.placement.wrk_no = 11;
    ply.mlwP = ply.mdl; ply.mdl[0].owP = &arena.world[0];
    ene[11].mlwP = ene[11].mdl; ene[11].mdl[0].owP = &arena.world[1];
    arena.objects[1].mlwP = arena.objects[1].mdl; arena.objects[1].mdl[0].owP = &arena.world[2];
    for (int i = 0; i < 3; ++i) {
        arena.world[i].mtx[12] = 10 + i;
        arena.world[i].mtx[13] = 20 + i;
        arena.world[i].mtx[14] = 30 + i;
    }
    unsigned char start[] = {21, 73, 12, 255};
    bhScePtr = start;
    assert(bhBgmOn() == 1 && bhScePtr == start + sizeof(start));
    assert(calls == 1 && last_id == 73 && last_fade == 120);
    unsigned char stop[] = {22, 255};
    bhScePtr = stop;
    assert(bhBgmOff() == 1 && bhScePtr == stop + sizeof(stop));
    assert(calls == 2 && last_fade == 2550);
    unsigned char volume[] = {166, 255, 255, 127};
    bhScePtr = volume;
    assert(bhBgmOnEx() == 1 && bhScePtr == volume + sizeof(volume));
    assert(calls == 3 && last_id == 255 && last_fade == 2550 && last_volume == -127);
    volume[2] = 0; volume[3] = 0; bhScePtr = volume;
    assert(bhBgmOnEx() == 1 && last_fade == 0 && last_volume == 0);
    unsigned char se[] = {23, 4, 0, 0, 0x34, 0x12, 1, 255};
    bhScePtr = se;
    assert(bhSeOn() == 1 && bhScePtr == se + sizeof(se));
    assert(last_slot == 4 && last_id == 0x1234 && last_mode == 1 && last_position.x == 10);
    se[2] = 1; bhScePtr = se; bhSeOn(); assert(last_position.x == 11 && last_position.y == 21);
    se[2] = 2; se[3] = 1; bhScePtr = se; bhSeOn(); assert(last_position.z == 32);
    unsigned char off[] = {24, 3}; bhScePtr = off;
    assert(bhSeOff() == 1 && bhScePtr == off + 2 && last_slot == 3);
    unsigned char voice[] = {25, 1, 0xff, 0x02, 0, 255, 0, 255};
    bhCetask->mode2 = 1; bhScePtr = voice;
    assert(bhVoiceOn() == 1 && bhScePtr == voice + 8 && bhCetask->mode2 == 1);
    assert(stopped_voice == 1 && last_id == 767 && last_fade == 2550 && last_mode == 0 && last_position.x == 11);
    unsigned char voice_off[] = {26, 4}; bhScePtr = voice_off;
    assert(bhVoiceOff() == 1 && bhScePtr == voice_off + 2 && last_fade == 40 && bhCetask->mode2 == 0);
    unsigned char background[] = {28, 2, 0x23, 0x01, 255, 255}; bhScePtr = background;
    assert(bhBGSeOn() == 1 && bhScePtr == background + 6 && last_slot == 2 && last_id == 291 && last_fade == 2550);
    unsigned char background_off[] = {29, 1, 3, 255}; bhScePtr = background_off;
    assert(bhBGSeOff() == 1 && bhScePtr == background_off + 4 && last_slot == 1 && last_fade == 30);
    se[1] = 5; reject(bhSeOn, se, "slot"); se[1] = 0;
    se[2] = 3; reject(bhSeOn, se, "mode"); se[2] = 0;
    se[6] = 2; reject(bhSeOn, se, "mode"); se[6] = 0;
    unsigned char *saved_arena = freemem;
    freemem = NULL; reject(bhSeOn, se, "position"); freemem = saved_arena;
    unsigned char *saved_end = work.endp;
    work.endp = (unsigned char *)((uintptr_t)freemem - 1u);
    reject(bhSeOn, se, "position"); work.endp = saved_end;
    arena.world[0].mtx[12] = NAN; reject(bhSeOn, se, "position"); arena.world[0].mtx[12] = 10;
    background[1] = 3; reject(bhBGSeOn, background, "slot");
    bhCetask = NULL; reject(bhVoiceOn, voice, "task"); bhCetask = bhEtask;
    ply.mdl[0].obj_num=ene[11].mdl[0].obj_num=arena.objects[1].mdl[0].obj_num=1;
    work.itwp=arena.objects;
    unsigned char easy[]={134,7,0,20,50,128,160,10,0,0,0,0,0,255,0x34,0x12};
    for(unsigned int kind=0;kind<4;++kind){
        easy[9]=kind;easy[10]=kind>=2?1:0;bhScePtr=easy;
        assert(bhEasySESet()==1&&bhScePtr==easy+16);
        assert(control_type==7&&control_slot==0&&control_flags==6&&first_seen==0&&last_seen==32&&frames_seen==10);
        assert(last_id==0x1234&&last_mode==1&&last_position.x==10+(kind==0?0:kind==1?1:2));
    }
    for(int type=0;type<12;++type){
        easy[1]=type;bhScePtr=easy;route=-1;int before=calls;
        bhEasySESet();assert(control_type==type&&control_flags==6);
        if(type<7)assert(route==type&&calls==before+1);
        if(type==7)assert(route==7&&calls==before+1);
        if(type>=8)assert(calls==before);
    }
    easy[1]=7;easy[4]=1;easy[6]=127;bhScePtr=easy;bhEasySESet();assert(control_flags==0);
    easy[9]=4;reject(bhEasySESet,easy,"mode");easy[9]=0;
    easy[11]=1;reject(bhEasySESet,easy,"joint");easy[11]=0;
    easy[1]=12;reject(bhEasySESet,easy,"type");easy[1]=5;
    easy[2]=6;reject(bhEasySESet,easy,"slot");easy[2]=0;
    unsigned char next[]={71,1,2,3,0x34,0x12,0x78,0x56,255,0};
    bhScePtr=next;assert(bhSetNextRoomBgSe()==1&&bhScePtr==next+10);
    assert(NextSoundInfo.ComNextBgSe[1]==2&&NextSoundInfo.PointNextBgSe[1]==3&&
           NextSoundInfo.OfsPointBgSe[1]==0x1234&&NextSoundInfo.NextBgSeNo[1]==0x5678&&
           NextSoundInfo.FadeNextBgSe[1]==2550&&NextSoundInfo.SetNextBgSeFlag[1]==1);
    next[1]=2;reject(bhSetNextRoomBgSe,next,"slot");
    unsigned char ctl[]={119,7,4,255,1,255};bhScePtr=ctl;
    assert(bhSoundVolumeSet()==1&&bhScePtr==ctl+6&&first_seen==-255&&last_seen==-1&&frames_seen==255);
    ctl[0]=127;ctl[3]=0;ctl[4]=255;bhScePtr=ctl;
    assert(bhSoundPanSet()==1&&bhScePtr==ctl+6&&first_seen==-128&&last_seen==127);
    unsigned char next_bgm[]={70,255,2,3,0x34,0x12,99,255};bhScePtr=next_bgm;
    assert(bhSetNextRoomBgm()==1&&bhScePtr==next_bgm+8&&NextSoundInfo.ComNextBgm==2&&
        NextSoundInfo.PointNextBgm==3&&NextSoundInfo.OfsPointBgm==0x1234&&
        NextSoundInfo.NextBgmNo==99&&NextSoundInfo.FadeNextBgm==2550);
    unsigned char midi[]={117,3,2,255};bhScePtr=midi;
    assert(bhInitMidiSlotSet()==1&&bhScePtr==midi+4&&last_slot==3&&last_mode==2);
    unsigned char flag[]={118,7,4,255};bhScePtr=flag;
    assert(bh3dSoundFlagSet()==1&&bhScePtr==flag+4&&control_type==7&&control_slot==4&&control_flags==255);
    unsigned char reset[]={135,255};bhScePtr=reset;
    assert(bhSoundFlagReSet()==1&&bhScePtr==reset+2&&!control_flags);
    unsigned char bg2[]={148,2,0x34,0x12};bhScePtr=bg2;
    assert(bhBGSeOn2()==1&&bhScePtr==bg2+4&&last_slot==2&&last_id==0x1234);
    bg2[0]=146;bhScePtr=bg2;assert(bhBGSeOff2()==1&&bhScePtr==bg2+2&&last_slot==2);
    unsigned char bgm2[]={149,99,255,255};bhScePtr=bgm2;
    assert(bhBgmOn2()==1&&bhScePtr==bgm2+4&&last_id==99&&last_volume==-45);
    bgm2[0]=167;bhScePtr=bgm2;assert(bhBgmOn2Ex()==1&&bhScePtr==bgm2+4&&last_id==99&&last_volume==-255);
    bgm2[0]=147;bhScePtr=bgm2;assert(bhBgmOff2()==1&&bhScePtr==bgm2+2);
    unsigned char room_case[]={182,255};bhScePtr=room_case;
    assert(bhRoomSoundCase()==1&&bhScePtr==room_case+2&&last_id==255);
    unsigned char sys_se[]={212,255,0x34,0x12};bhScePtr=sys_se;
    assert(bhCallSysSe()==1&&bhScePtr==sys_se+4&&last_slot==0&&last_id==0x1234);
    unsigned char fade_noop[]={131,255};bhScePtr=fade_noop;int before_fade=calls;
    assert(bhSoundFadeOut()==1&&bhScePtr==fade_noop+2&&calls==before_fade); /* Retail handler has no side effects. */
    ene[11].mdl[0].objP=arena.nodes;ene[11].mdl[0].obj_num=2;
    arena.nodes[1].pos[0]=41;arena.nodes[1].pos[1]=42;arena.nodes[1].pos[2]=43;
    unsigned char enemy[]={121,5,0,1,0x34,0x12};bhScePtr=enemy;
    assert(bhEneSeOn()==1&&bhScePtr==enemy+6&&last_slot==5&&last_id==0x1234&&last_position.x==41&&last_position.z==43);
    enemy[0]=122;bhScePtr=enemy;assert(bhEneSeOff()==1&&bhScePtr==enemy+2&&last_slot==5);
    enemy[0]=121;enemy[3]=2;reject(bhEneSeOn,enemy,"joint");
    puts("PASS easy-SE all actor selectors, native routes, control sentinels, scripted gain/pan and next-room fields; original BGM/voice/background/event sound operands, native routing, actor matrices, mode guards and PC advancement");
    return 0;
}
