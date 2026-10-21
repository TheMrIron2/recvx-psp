#include "psp/game_boot_enemy_sound.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "psp/game_boot.h"
#include "psp/game_boot_audio.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/sdfunc.h"
#include "ps2/veronica/prog/adxwrap.h"

NEXTSOUND_INFO NextSoundInfo;
static int room_case, init_script, current_bgm = -1, pending_bgm = -1, pending_volume;
static int system_slot, weapon_slot, foot_slot[3], request_kind;
static int background[3], background_pending[3];
static short background_fade[3];
typedef struct EventSoundControl {
    unsigned int flags;
    int volume,pan,vol_start,vol_end,pan_start,pan_end,vol_frames,pan_frames,vol_tick,pan_tick;
} EventSoundControl;
static EventSoundControl event_control[12][16];
static int bgm_volume=-45;
static void event_control_tick(void);
extern void recvxGameBootEnemySoundReset(void) __attribute__((weak));
extern void ExecEnemySeManager(void) __attribute__((weak));
static void object_sound_reset(void);
static void object_sound_tick(void);
static void object_pool_stop(void);
static int max_event_slot = 4, max_object_slot = 1;
static void original_spatial(const NJS_POINT3 *position, int room_fx, int *volume, int *pan);
extern char ThreeDVolTbl[510], PanTbl360[68], PanTbl360Vol[68];
static void check(void)
{
    const char *error = recvxAudioFault();
    if (error || recvxAudioNativeError()) {
        char reason[192];
        snprintf(reason, sizeof(reason), "audio: %.80s SAS=%08x call=%.60s", error ? error : "native service failed",
            (unsigned int)recvxAudioNativeError(), recvxAudioNativeErrorCall());
        recvxGameBootUnsupported(reason);
    }
}
void recvxGameBootSoundReset(void)
{
    memset(event_control,0,sizeof(event_control));bgm_volume=-45;
    object_sound_reset();
    if(recvxGameBootEnemySoundReset)recvxGameBootEnemySoundReset();
    room_case = init_script = system_slot = weapon_slot = request_kind = 0;
    current_bgm = pending_bgm = -1; memset(foot_slot, 0, sizeof(foot_slot));
    max_event_slot = 4; max_object_slot = 1;
    for (int i = 0; i < 3; ++i) { background[i] = -1; background_pending[i] = 0; }
}
void recvxGameBootSoundTick(void)
{
    recvxAudioService();
    event_control_tick();
    object_sound_tick();
    if(ExecEnemySeManager)ExecEnemySeManager();
    recvxAudioLock(); recvxAudioAdvanceFades(); recvxAudioUnlock();
    check();
}
int MountSoundAfs(void) { recvxAudioMount(); check(); return 0; }
void UnmountSoundAfs(void) { recvxAudioShutdown(); }
static void request_bank(unsigned int key, const char *kind)
{
    recvxAudioRequestBank(key); check();
    recvxGameBootLog("sound bank requested: %s key=%04x", kind, key);
    request_kind = !strcmp(kind, "weapon") ? 2 : 1;
}
void RequestRoomSoundBank(int stage, int room, int variant)
{
    if (stage < 0 || stage > 9 || room < 0 || room > 99 || variant < 0 || variant > 9)
        recvxGameBootUnsupported("invalid room sound bank request");
    request_bank((unsigned int)(stage * 1000 + room * 10 + variant), "room");
}
void RequestArmsSoundBank(int weapon)
{
    /* WpnTab's unarmed entry explicitly has snd_wpno = -1. */
    if (weapon == -1) { recvxGameBootLog("sound bank: unarmed player has no weapon bank"); return; }
    if (weapon < 0 || weapon > 255) recvxGameBootUnsupported("invalid weapon sound bank");
    request_bank((unsigned int)weapon | 0x4000u, "weapon");
}
void RequestPlayerVoiceSoundBank(int player) { if (player < 0 || player > 3) recvxGameBootUnsupported("invalid player sound bank"); request_bank((unsigned int)player | 0xfff0u, "player"); }
void RequestDoorSoundBank(int door) { if (door < 0 || door > 255) recvxGameBootUnsupported("invalid door sound bank"); request_bank((unsigned int)door | 0x8000u, "door"); }
int CheckTransEndSoundBank(void) { check(); return recvxAudioBankBusy(); }
void SetRoomSoundCaseNo(int number) { room_case = number; }
int GetRoomSoundCaseNo(void) { int result = room_case; room_case = 0; return result; }
void InitGameSoundSystem(void)
{
    current_bgm = pending_bgm = -1;
    object_sound_reset();
    if(recvxGameBootEnemySoundReset)recvxGameBootEnemySoundReset();
    for (int i = 0; i < 8; ++i) recvxAudioStopMidi(i);
    recvxGameBootLog("game sound initialized: native SAS ADPCM");
}
void ExecSoundSystemMonitor(void) { check(); }
void SetVolumeAdx2(unsigned int slot, float volume) { recvxAudioVolume(slot, (int)volume, 0); }
void SetVolumeAdx(unsigned int slot, int volume) { SetVolumeAdx2(slot, (float)volume); }
void StopAdx(unsigned int slot) { recvxAudioStop(slot); }
void PlayAdx(unsigned int slot, unsigned int partition, unsigned int file)
{
    recvxAudioAdx(slot, partition, file); check();
    recvxGameBootLog("native ADPCM stream: slot=%u partition=%u entry=%u", slot, partition, file);
}
void PlayAdxEx(unsigned int slot, unsigned int partition, unsigned int file, int pause)
{ PlayAdx(slot, partition, file); if (pause) recvxAudioPause(slot, 1); }
int GetAdxStatus(unsigned int slot) { return recvxAudioStatus(slot) ? 3 : 5; }
/* Retail returns AdxPlayFlag: scripts wait while a stream is playing. */
int CheckPlayEndAdx(int slot) { return recvxAudioStatus((unsigned int)slot); }
int GetTimeAdx(int slot) { return (int)(recvxAudioTime((unsigned int)slot) / 10); }
void PlayBgmEx2(unsigned int partition, int bgm, int fade, int volume)
{
    if (sys->ss_flg & 0x4000000) return;
    recvxAudioVolume(0, fade ? -127 : volume, 0);
    PlayAdx(0, partition, (unsigned int)bgm);
    if (fade) recvxAudioVolume(0, volume, fade);
    current_bgm = bgm;bgm_volume=volume;
}
void PlayBgmEx(int bgm, int fade, int volume) { PlayBgmEx2((unsigned int)PatId[0], bgm, fade, volume); }
void PlayBgm(int bgm, int fade) { PlayBgmEx(bgm, fade, -45); }
void PlayBgm2(int bgm, int volume) { pending_bgm = bgm; pending_volume = volume; }
void StopBgm(int fade) { if (fade) recvxAudioVolume(0, -127, fade); else StopAdx(0); current_bgm = pending_bgm = -1; }
void StopBgm2(void) { StopBgm(0); }
void PlayVoiceEx2(int partition, int voice, NJS_POINT3 *position, int mode, int fade, int pause)
{
    int volume = 0, pan = 0;
    if (mode < 0 || mode > 2 || fade < 0)
        recvxGameBootUnsupported("invalid original voice parameters");
    if (recvxAudioStatus(1)) StopAdx(1);
    if (mode != 1) original_spatial(position, 0, &volume, &pan);
    recvxAudioPan(1, 0, pan);
    recvxAudioVolume(1, mode == 1 && fade ? -127 : volume, 0);
    if (mode != 2) PlayAdxEx(1, (unsigned int)partition, (unsigned int)voice, pause);
    if (mode == 1 && fade) recvxAudioVolume(1, 0, fade);
}
void PlayVoiceEx(int voice, NJS_POINT3 *position, int mode, int fade, int pause)
{ PlayVoiceEx2(PatId[1], voice, position, mode, fade, pause); }
void PlayVoice(int voice, NJS_POINT3 *position, int mode, int fade) { PlayVoiceEx(voice, position, mode, fade, 0); }
void StopVoice(int fade) { if (fade) recvxAudioVolume(1, -127, fade); else StopAdx(1); }
void ContinuePlayVoice(void) { recvxAudioPause(1, 0); }
void RequestAllStopSoundEx(int streams, int sounds, int frames)
{
    if (streams) { StopBgm(frames); StopVoice(frames); }
    if (sounds) { object_sound_reset(); if(recvxGameBootEnemySoundReset)recvxGameBootEnemySoundReset(); recvxAudioStopSounds(frames); }
}
static void se(int slot, int sound, int volume, int pan)
{ recvxAudioPlaySe(slot, sound, volume, pan); check(); }
static void spatial(const NJS_POINT3 *position, int field, int *volume, int *pan)
{
    if (!position || (sys->gm_flg & 0x01000000)) {
        *volume = 0; *pan = 0; return;
    }
    original_spatial(position, 1, volume, pan);
    *volume += recvxAudioEnvironment((unsigned int)field);
}
static void original_spatial(const NJS_POINT3 *position, int room_fx, int *volume, int *pan)
{
    double x, y, z, distance;
    unsigned int angle, index;
    int bin;
    if (!position || !isfinite(position->x) || !isfinite(position->y) || !isfinite(position->z) ||
        !isfinite(cam.wpx) || !isfinite(cam.wpy) || !isfinite(cam.wpz))
        recvxGameBootUnsupported("invalid original spatial sound position");
    x = (double)cam.wpx - position->x;
    y = (double)cam.wpy - position->y;
    z = (double)cam.wpz - position->z;
    distance = sqrt(x * x + y * y + z * z);
    bin = distance >= 510 ? 509 : (int)distance;
    angle = (unsigned int)(cam.ay + (int)(10430.381f * atan2f((float)x, (float)z))) & 65535u;
    index = (unsigned int)(0.005493164f * angle / 5.0f);
    if (index >= 68) index = 67;
    *pan = PanTbl360[index];
    *volume = ThreeDVolTbl[bin] + PanTbl360Vol[index] - (room_fx ? recvxAudioEnvironment(9) / 12 : 0);
}
int CustomMidiSlotDef(int objects, int events)
{
    object_pool_stop();
    if (objects < 0 || events < 0 || objects > 3 || events > 5 || objects + events > 5) {
        max_object_slot = 1; max_event_slot = 4; return 1;
    }
    max_object_slot = objects; max_event_slot = events;
    return 0;
}
int CallNativeEventSe(int slot, NJS_POINT3 *position, int sound, int mode)
{
    const int mapping[5] = {7, 6, 5, 4, 3};
    int volume = 0, pan = 0;
    if (slot < 0 || slot >= 5 || mode < 0 || mode > 1)
        recvxGameBootUnsupported("invalid native event sound parameters");
    if (slot > max_event_slot) return 1;
    if (!mode) original_spatial(position, 1, &volume, &pan);
    recvxAudioPlayMidi(mapping[slot], (sound & 0xffff00ff) | 0x200, volume, pan);
    check();
    return 0;
}
int StopNativeEventSe(int slot)
{
    const int mapping[5] = {7, 6, 5, 4, 3};
    if (slot < 0 || slot >= 5) recvxGameBootUnsupported("invalid native event sound slot");
    if (slot > max_event_slot) return 1;
    recvxAudioStopMidi(mapping[slot]); check(); return 0;
}
void CallSystemSeBasic(int sound, int volume, int fx)
{
    (void)fx;
    if ((unsigned int)sound & 0x80000000u) { recvxAudioPlayMidi(2, sound, volume, 0); check(); }
    else se(17 + (system_slot++ & 1), sound, volume, 0);
}
void CallSystemSe(int slot, int sound) { (void)slot; CallSystemSeBasic(sound, 0, 0); }
void CallSystemSeEx(int sound, int volume) { CallSystemSeBasic(sound, volume, 0); }
void CallPlayerVoice(int sound)
{ NJS_POINT3 p = {plp->px, plp->py, plp->pz}; int volume, pan; spatial(&p, 0, &volume, &pan); se(7, sound, volume, pan); }
void CallPlayerActionSe(int sound, int flag) { CallPlayerFootStepSe(sound, 3, flag); }
void CallPlayerWeaponSeEx(NJS_POINT3 *position, int sound, int slot)
{
    int volume, pan;
    if (request_kind == 2 && recvxAudioBankBusy()) return;
    spatial(position, 2, &volume, &pan);
    se(slot ? 19 : 8 + (weapon_slot++ & 1), (sound & 255) | 0x100, volume, pan);
}
void CallPlayerFootStepSe(int floor, int type, int flag)
{ NJS_POINT3 p = {plp->px, plp->py, plp->pz}; CallPlayerFootStepSeEx(floor, type, flag, 0, &p); }
void CallPlayerFootStepSeEx(int floor, int type, int flag, int id, NJS_POINT3 *position)
{
    const int walk[4] = {0,256,256,0}, run[4] = {512,768,768,512};
    int slot, program, volume, pan, pitch = 0;
    if (id < 0 || id >= 3 || type < 0 || type > 3 || floor < 0 || (type < 2 && floor > 4))
        recvxGameBootUnsupported("invalid footstep sound parameters");
    slot = type < 2 ? 11 + id * 2 + (foot_slot[id] ^= 1) : 6;
    program = type == 2 ? 5 : floor;
    spatial(position, 1, &volume, &pan);
    if (flag) {
        if (type < 2) pitch = (type ? run : walk)[rand() & 3];
        se(slot, 0x200 | program, volume, pan);
    }
    recvxAudioUpdateSe(slot, volume, pan, pitch);
}
void AllStopEnemySe(void) { if(recvxGameBootEnemySoundReset)recvxGameBootEnemySoundReset(); for (int i = 0; i < 6; ++i) recvxAudioStopSe(i); }
void CallEnemySe(int slot, NJS_POINT3 *position, int sound)
{ int volume, pan; if (slot < 0 || slot >= 6) recvxGameBootUnsupported("invalid enemy sound slot"); spatial(position, 4, &volume, &pan); se(slot, (sound & 255) | 0x300, volume, pan); }
void StopEnemySe(int slot) { recvxAudioStopSe(slot); }
void CallBackGroundSeEx(unsigned int slot, int sound, short frames)
{
    if (slot >= 3) recvxGameBootUnsupported("invalid background sound slot");
    background[slot] = sound; background_fade[slot] = frames;
    if (init_script) { background_pending[slot] = 1; return; }
    if (frames < 0) recvxGameBootUnsupported("invalid background sound fade");
    recvxAudioPlayMidiFade((int)slot, (sound & 255) | 0x300,
        recvxAudioEnvironment(5 + (slot == 1)), 0, frames); check();
}
void CallBackGroundSe(unsigned int slot, int sound) { CallBackGroundSeEx(slot, sound, 0); }
void CallBackGroundSe2(unsigned int slot, int sound)
{ if (slot >= 3) recvxGameBootUnsupported("invalid background sound slot"); if (background[slot] != sound) CallBackGroundSeEx(slot, sound, 0); }
void StopBackGroundSeEx(unsigned int slot, short frames)
{
    if (slot >= 3 || frames < 0) recvxGameBootUnsupported("invalid background sound stop");
    background[slot] = -1; background_pending[slot] = 0;
    if (frames) recvxAudioMidiVolume((int)slot, -127, frames, 1);
    else recvxAudioStopMidi((int)slot);
    check();
}
void StopBackGroundSe2(unsigned int slot) { StopBackGroundSeEx(slot, 0); }
void SendSoundCommand(unsigned int command)
{
    switch (command) {
    case 1:
        init_script = 0;
        if (pending_bgm >= 0) { int bgm = pending_bgm; pending_bgm = -1; PlayBgmEx(bgm, 120, pending_volume); }
        for (unsigned int i = 0; i < 3; ++i) if (background_pending[i]) {
            background_pending[i] = 0; CallBackGroundSeEx(i, background[i], background_fade[i]);
        }
        break;
    case 2: RequestAllStopSoundEx(1, 1, 12); break;
    case 3: init_script = 1; for (int i = 3; i < 8; ++i) recvxAudioStopMidi(i); break;
    /* Retail command 0 has a null callback; 4 and 5..9 dispatch to empty
       Com_FinishInitScript / Com_ExecCallBgm_And_BgSe bodies. Door timing
       legitimately consumes these commands as it crosses animation states. */
    case 0: case 4: case 5: case 6: case 7: case 8: case 9: break;
    default: recvxGameBootUnsupported("unsupported original sound command");
    }
    check();
}


/* Script control is retained by logical sound slot, independently of whether
   a voice has started yet. SAS receives only gain/pan updates; ADPCM stays
   compressed. The original type 9/11 selectors have no control operation. */
static EventSoundControl *event_sound(int type,int slot)
{
    int limit=type==1||type==6?3:type==5?6:type==7?5:type==8?16:1;
    if(type<0||type>11||slot<0||((type==1||type==5||type==6||type==7||type==8)&&slot>=limit))
        recvxGameBootUnsupported("invalid script sound control slot");
    return &event_control[type][limit==1?0:slot];
}
static void apply_event_control(int type,int slot,EventSoundControl *c)
{
    int slots[3],count=1,is_midi=0;
    switch(type){
    case 0:slots[0]=7;break;
    case 1:slots[0]=11+slot*2;slots[1]=slots[0]+1;count=2;break;
    case 2:slots[0]=6;break;
    case 3:slots[0]=8;slots[1]=9;slots[2]=19;count=3;break;
    case 4:slots[0]=10;break;
    case 5:slots[0]=slot;break;
    case 6:slots[0]=slot;is_midi=1;break;
    case 7:slots[0]=7-slot;is_midi=1;break;
    default:return;
    }
    unsigned int flags=type==6?c->flags&~4u:c->flags;
    for(int i=0;i<count;++i)recvxAudioSoundControl(is_midi,slots[i],flags,c->volume,c->pan);
}
void Set3dSoundFlag(int type,int slot,unsigned int flags)
{
    EventSoundControl *c=event_sound(type,slot);
    *c=(EventSoundControl){.flags=flags};
    if(type==10)recvxAudioVolume(0,bgm_volume,0);
    else apply_event_control(type,slot,c);
}
void Reset3dSoundFlag(void)
{
    for(int type=0;type<12;++type){
        int limit=type==1||type==6?3:type==5?6:type==7?5:type==8?16:1;
        for(int slot=0;slot<limit;++slot)Set3dSoundFlag(type,slot,0);
    }
}
void SetUserSoundVolume(int type,int slot,int first,int last,int frames)
{
    EventSoundControl *c=event_sound(type,slot);
    if(!(c->flags&2)||type==9||type==11)return;
    if(frames<0)recvxGameBootUnsupported("invalid script sound volume duration");
    c->volume=frames?first:last;c->vol_start=first;c->vol_end=last;c->vol_frames=frames;c->vol_tick=0;
    if(type==10){recvxAudioVolume(0,first,0);recvxAudioVolume(0,last,frames);}
    else apply_event_control(type,slot,c);
}
void SetUserSoundPan(int type,int slot,int first,int last,int frames)
{
    EventSoundControl *c=event_sound(type,slot);
    if(!(c->flags&4)||type==6||type>=9)return;
    if(frames<0)recvxGameBootUnsupported("invalid script sound pan duration");
    c->pan=frames?first:last;c->pan_start=first;c->pan_end=last;c->pan_frames=frames;c->pan_tick=0;
    apply_event_control(type,slot,c);
}
static void event_control_tick(void)
{
    for(int type=0;type<9;++type)for(int slot=0;slot<16;++slot){
        EventSoundControl *c=&event_control[type][slot];int changed=0;
        if(c->vol_tick<c->vol_frames){
            c->volume=c->vol_start+(c->vol_end-c->vol_start)*++c->vol_tick/c->vol_frames;changed=1;
        }
        if(c->pan_tick<c->pan_frames){
            c->pan=c->pan_start+(c->pan_end-c->pan_start)*++c->pan_tick/c->pan_frames;changed=1;
        }
        if(changed)apply_event_control(type,slot,c);
    }
}
void CallYakkyouSe(NJS_POINT3 *position,int sound)
{
    int volume,pan;
    if(request_kind==2&&recvxAudioBankBusy())return;
    spatial(position,3,&volume,&pan);se(10,(sound&255)|0x100,volume,pan);
}

/* Persistent object emitters share the original configurable MIDI pool. */
typedef struct ObjectEmitter { NJS_POINT3 position; int sound, priority; } ObjectEmitter;
static ObjectEmitter object_emitter[16];
static unsigned char object_order[16];
static int object_count, object_owner[3]={-1,-1,-1}, object_sound[3];
static void object_sound_reset(void)
{
    object_count=0;
    for(int i=0;i<3;++i)object_owner[i]=-1;
}
void RegistObjectSe(int object,NJS_POINT3 *position,int sound,int priority)
{
    int volume,pan;
    if(object<0||object>=16)recvxGameBootUnsupported("object sound ID out of range");
    original_spatial(position,0,&volume,&pan);
    object_emitter[object]=(ObjectEmitter){*position,(sound&0xffff00ff)|0x200,priority};
    for(int i=0;i<object_count;++i)if(object_order[i]==object)return;
    object_order[object_count++]=(unsigned char)object;
}
void FreeObjectSe(int object)
{
    if(object<0||object>=16)recvxGameBootUnsupported("object sound ID out of range");
    for(int i=0;i<object_count;++i)if(object_order[i]==object){
        memmove(object_order+i,object_order+i+1,(size_t)(--object_count-i));break;
    }
    for(int i=0;i<3;++i)if(object_owner[i]==object){recvxAudioStopMidi(3+i);recvxAudioSoundControl(1,3+i,0,0,0);object_owner[i]=-1;}
    check();
}
static void object_sound_tick(void)
{
    if(init_script)return;
    /* The PS2 manager assigns emitters in registration order and retains slots. */
    int selected[3]={-1,-1,-1}, count=object_count<max_object_slot?object_count:max_object_slot;
    for(int i=0;i<count;++i){
        int object=object_order[i],slot=-1;
        for(int j=0;j<max_object_slot;++j)if(object_owner[j]==object){slot=j;break;}
        if(slot>=0)selected[slot]=object;
    }
    for(int j=0;j<3;++j)if(object_owner[j]>=0&&selected[j]<0){
        recvxAudioStopMidi(3+j);recvxAudioSoundControl(1,3+j,0,0,0);object_owner[j]=-1;
    }
    for(int i=0;i<count;++i){
        int object=object_order[i],slot=-1;
        for(int j=0;j<max_object_slot;++j)if(selected[j]==object){slot=j;break;}
        if(slot<0)for(int j=0;j<max_object_slot;++j)if(selected[j]<0){slot=j;selected[j]=object;break;}
        if(slot<0)continue;
        ObjectEmitter *e=&object_emitter[object]; EventSoundControl *c=&event_control[8][object];
        int volume,pan;original_spatial(&e->position,0,&volume,&pan);
        volume+=recvxAudioEnvironment(7);
        if(c->flags&2)volume=c->volume;
        if(c->flags&4)pan=c->pan;
        recvxAudioSoundControl(1,3+slot,6,volume,pan);
        if(object_owner[slot]!=object||object_sound[slot]!=e->sound){
            recvxAudioPlayMidi(3+slot,e->sound,volume,pan);
            object_owner[slot]=object;object_sound[slot]=e->sound;
        }
    }
}

static void object_pool_stop(void)
{
    for(int i=0;i<3;++i)if(object_owner[i]>=0){
        recvxAudioStopMidi(3+i);recvxAudioSoundControl(1,3+i,0,0,0);object_owner[i]=-1;
    }
}

int recvxGameBootSoundInitScript(void) { return init_script; }
void recvxGameBootEnemySpatial(NJS_POINT3 *position,char *pan,char *volume,float *distance)
{
    int v,p;original_spatial(position,0,&v,&p);
    *pan=(char)p;*volume=(char)v;
    float x=position->x-cam.wpx,y=position->y-cam.wpy,z=position->z-cam.wpz;
    *distance=sqrtf(x*x+y*y+z*z);
}
void CallEnemySeMain(unsigned int slot,int sound,char pan,char volume,int flag,int frames)
{
    if(slot>=6)recvxGameBootUnsupported("invalid native enemy sound slot");
    if(flag){
        recvxAudioPlaySe(slot,sound,frames>0?-127:volume,pan);
        if(frames>0)recvxAudioSeVolume(slot,volume,frames);
    }else recvxAudioUpdateSe(slot,volume,pan,0);
    check();
}
