/* The real scenario sound service calls firmware-backed audio controls.
   This fixture records those calls; it never decodes compressed samples. */
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include "psp/game_boot.h"
#include "psp/game_boot_audio.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/sdfunc.h"

static SYS_WORK work;
static BH_PWORK player;
BH_PWORK *plp = &player;
SYS_WORK *sys = &work;
CAM_WORK cam;
int PatId[4] = {0, 1, 2, 3};
char ThreeDVolTbl[510], PanTbl360[68], PanTbl360Vol[68];
static int slot_seen, sound_seen, volume_seen, pan_seen, fade_seen, stop_seen;
static int midi_starts, adx_starts, adx_stops, stream_active;
static unsigned int partition_seen;
static unsigned int flags_seen;
static int control_midi,controls;
extern void recvxGameBootSoundTick(void);
void recvxAudioService(void){}
void recvxAudioLock(void){}
void recvxAudioUnlock(void){}
void recvxAudioAdvanceFades(void){}
void recvxAudioSoundControl(int midi,int slot,unsigned int flags,int volume,int pan)
{control_midi=midi;slot_seen=slot;flags_seen=flags;volume_seen=volume;pan_seen=pan;++controls;}


extern void recvxGameBootSoundReset(void);
void recvxGameBootLog(const char *format, ...) { (void)format; }
void recvxGameBootUnsupported(const char *reason) { fprintf(stderr, "%s\n", reason); abort(); }
const char *recvxAudioFault(void) { return NULL; }
int recvxAudioNativeError(void) { return 0; }
const char *recvxAudioNativeErrorCall(void) { return NULL; }
int recvxAudioEnvironment(unsigned int field) { return field == 9 ? 24 : -(int)field - 10; }
static int se_starts, se_slot, se_volume, se_pan;
int recvxAudioPlaySe(int slot, int sound, int volume, int pan)
{ se_slot=slot; sound_seen=sound; se_volume=volume; se_pan=pan; ++se_starts; return 1; }
void recvxAudioUpdateSe(int slot, int volume, int pan, int pitch)
{ assert(slot==se_slot&&volume==se_volume&&pan==se_pan); (void)pitch; }
unsigned int recvxAudioTime(unsigned int slot) { assert(slot==1); return 2345; }
int recvxAudioPlayMidiFade(int slot, int sound, int volume, int pan, int frames)
{
    slot_seen = slot; sound_seen = sound; volume_seen = volume; pan_seen = pan;
    fade_seen = frames; ++midi_starts; return 0;
}
int recvxAudioPlayMidi(int slot, int sound, int volume, int pan)
{ return recvxAudioPlayMidiFade(slot, sound, volume, pan, 0); }
void recvxAudioStopMidi(int slot) { slot_seen = slot; stop_seen = 1; }
void recvxAudioStopSounds(int frames) { (void)frames; }
void recvxAudioMidiVolume(int slot, int volume, int frames, int stop)
{ slot_seen = slot; volume_seen = volume; fade_seen = frames; stop_seen = stop; }
void recvxAudioVolume(unsigned int slot, int volume, int frames)
{ slot_seen = (int)slot; volume_seen = volume; fade_seen = frames; }
void recvxAudioPan(unsigned int slot, unsigned int channel, int pan)
{ assert(slot == 1 && channel == 0); pan_seen = pan; }
int recvxAudioAdx(unsigned int slot, unsigned int partition, unsigned int file)
{ slot_seen = (int)slot; partition_seen = partition; sound_seen = (int)file; stream_active = 1; ++adx_starts; return 0; }
void recvxAudioPause(unsigned int slot, int pause) { assert(slot == 1 && pause == 1); }
int recvxAudioStatus(unsigned int slot) { assert(slot == 1); return stream_active; }
void recvxAudioStop(unsigned int slot) { slot_seen = (int)slot; stream_active = 0; ++adx_stops; }

int main(void)
{
    recvxGameBootSoundReset();
    SendSoundCommand(0);
    for(unsigned int command=4;command<10;++command)SendSoundCommand(command);
    assert(!midi_starts&&!adx_starts&&!adx_stops&&!controls);
    for (int i = 0; i < 68; ++i) { PanTbl360[i] = (char)(i - 34); PanTbl360Vol[i] = -3; }
    ThreeDVolTbl[10] = -20;
    NJS_POINT3 p = {10, 0, 0};
    /* The listener-to-emitter direction must match voices/event sounds.
       A camera looking at an actor must not treat it as behind the listener. */
    player.px=10;
    CallPlayerFootStepSe(0,0,1);
    assert(se_starts==1&&se_slot==12&&sound_seen==0x200&&se_pan==20&&se_volume==-36);
    CallPlayerFootStepSeEx(4,1,1,2,&p);
    assert(se_starts==2&&se_slot==16&&sound_seen==0x204&&se_pan==20&&se_volume==-36);
    CallPlayerVoice(0x400);
    assert(se_slot==7&&sound_seen==0x400&&se_pan==20&&se_volume==-35);
    assert(!CheckPlayEndAdx(1)&&GetTimeAdx(1)==234);

    assert(CallNativeEventSe(0, &p, 0x123, 0) == 0);
    assert(slot_seen == 7 && sound_seen == 0x223 && pan_seen == 20 && volume_seen == -25);
    assert(CallNativeEventSe(4, &p, 0x456, 1) == 0);
    assert(slot_seen == 3 && sound_seen == 0x256 && pan_seen == 0 && volume_seen == 0);
    for (int i = 0; i < 5; ++i) { StopNativeEventSe(i); assert(slot_seen == 7 - i); }
    assert(CustomMidiSlotDef(3, 2) == 0);
    int before = midi_starts;
    assert(CallNativeEventSe(3, &p, 0, 0) == 1 && midi_starts == before);
    assert(CustomMidiSlotDef(3, 4) == 1);
    assert(CallNativeEventSe(4, &p, 0, 0) == 0);

    CallBackGroundSeEx(2, 0x123, 30);
    assert(slot_seen == 2 && sound_seen == 0x323 && fade_seen == 30 && volume_seen == -15);
    StopBackGroundSeEx(2, 40);
    assert(slot_seen == 2 && volume_seen == -127 && fade_seen == 40 && stop_seen == 1);
    SendSoundCommand(3);
    before = midi_starts;
    CallBackGroundSeEx(1, 7, 80);
    assert(midi_starts == before);
    SendSoundCommand(1);
    assert(midi_starts == before + 1 && slot_seen == 1 && sound_seen == 0x307 && fade_seen == 80);

    PlayVoiceEx2(1, 19, &p, 0, 50, 0);
    assert(partition_seen == 1 && sound_seen == 19 && pan_seen == 20 && volume_seen == -23 && fade_seen == 0);
    assert(CheckPlayEndAdx(1)==1); /* Retail AdxPlayFlag, not its inverse. */
    StopVoice(0); assert(!CheckPlayEndAdx(1));
    adx_stops=0;
    PlayVoiceEx2(1,19,&p,0,0,0);

    before = adx_starts;
    PlayVoiceEx2(1, 20, &p, 1, 60, 1);
    assert(adx_starts == before + 1 && adx_stops == 1 && pan_seen == 0 && volume_seen == 0 && fade_seen == 60);
    before = adx_starts;
    PlayVoiceEx2(1, 21, &p, 2, 90, 0);
    assert(adx_starts == before && adx_stops == 2 && pan_seen == 20 && volume_seen == -23);
    StopVoice(70); assert(slot_seen == 1 && volume_seen == -127 && fade_seen == 70);
    Set3dSoundFlag(7,2,6);
    assert(control_midi==1&&slot_seen==5&&flags_seen==6&&volume_seen==0&&pan_seen==0);
    SetUserSoundVolume(7,2,-20,-60,2);SetUserSoundPan(7,2,-100,100,2);
    assert(volume_seen==-20&&pan_seen==-100);
    recvxGameBootSoundTick();assert(slot_seen==5&&volume_seen==-40&&pan_seen==0);
    recvxGameBootSoundTick();assert(volume_seen==-60&&pan_seen==100);
    int old_controls=controls;recvxGameBootSoundTick();assert(controls==old_controls);
    Set3dSoundFlag(3,255,2);assert(!control_midi&&slot_seen==19&&flags_seen==2);
    SetUserSoundVolume(3,255,-90,-10,0);assert(volume_seen==-10);
    old_controls=controls;SetUserSoundPan(3,255,-128,127,20);assert(controls==old_controls);
    Set3dSoundFlag(6,1,6);assert(control_midi&&slot_seen==1&&flags_seen==2);
    Set3dSoundFlag(10,0,2);SetUserSoundVolume(10,0,-100,-30,20);
    assert(slot_seen==0&&volume_seen==-30&&fade_seen==20);
    Reset3dSoundFlag();assert(flags_seen==0);
    recvxGameBootSoundReset();
    RegistObjectSe(4,&p,0x123,3);RegistObjectSe(8,&p,0x124,1);
    before=midi_starts;recvxGameBootSoundTick();
    assert(midi_starts==before+1&&slot_seen==3&&sound_seen==0x223&&volume_seen==-40&&pan_seen==20);
    recvxGameBootSoundTick();assert(midi_starts==before+1);
    RegistObjectSe(4,&p,0x125,3);recvxGameBootSoundTick();
    assert(midi_starts==before+2&&sound_seen==0x225);
    Set3dSoundFlag(8,4,6);SetUserSoundVolume(8,4,-80,-20,2);SetUserSoundPan(8,4,-60,60,2);
    recvxGameBootSoundTick();assert(slot_seen==3&&volume_seen==-50&&pan_seen==0);
    recvxGameBootSoundTick();assert(volume_seen==-20&&pan_seen==60);
    FreeObjectSe(4);recvxGameBootSoundTick();assert(sound_seen==0x224&&slot_seen==3);
    FreeObjectSe(8);before=midi_starts;recvxGameBootSoundTick();assert(midi_starts==before);
    assert(!CustomMidiSlotDef(3,2));
    for(int id=0;id<16;++id)RegistObjectSe(id,&p,id,0);
    recvxGameBootSoundTick();assert(midi_starts==before+3&&slot_seen==5);
    before=midi_starts;recvxGameBootSoundTick();assert(midi_starts==before);
    for(int id=0;id<16;++id)FreeObjectSe(id);
    recvxGameBootSoundTick();assert(midi_starts==before);
    puts("PASS persistent native object sounds: registration order, slot retention, sound replacement, spatial gain/pan, scripted ramps, freeing and full 16-emitter capacity");
    puts("PASS real native sound service: scripted slot masks, exact gain/pan ramps and native stream fades; event/background slot ownership, positional volume/pan, fade frames and voice modes");
    return 0;
}
