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
    room_case = init_script = system_slot = weapon_slot = request_kind = 0;
    current_bgm = pending_bgm = -1; memset(foot_slot, 0, sizeof(foot_slot));
    for (int i = 0; i < 3; ++i) { background[i] = -1; background_pending[i] = 0; }
}
void recvxGameBootSoundTick(void)
{
    recvxAudioService();
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
int CheckPlayEndAdx(int slot) { return !recvxAudioStatus((unsigned int)slot); }
int GetTimeAdx(int slot) { return (int)recvxAudioTime((unsigned int)slot); }
void PlayBgmEx2(unsigned int partition, int bgm, int fade, int volume)
{
    if (sys->ss_flg & 0x4000000) return;
    recvxAudioVolume(0, fade ? -127 : volume, 0);
    PlayAdx(0, partition, (unsigned int)bgm);
    if (fade) recvxAudioVolume(0, volume, fade);
    current_bgm = bgm;
}
void PlayBgmEx(int bgm, int fade, int volume) { PlayBgmEx2((unsigned int)PatId[0], bgm, fade, volume); }
void PlayBgm(int bgm, int fade) { PlayBgmEx(bgm, fade, -45); }
void PlayBgm2(int bgm, int volume) { pending_bgm = bgm; pending_volume = volume; }
void StopBgm(int fade) { if (fade) recvxAudioVolume(0, -127, fade); else StopAdx(0); current_bgm = pending_bgm = -1; }
void StopBgm2(void) { StopBgm(0); }
void PlayVoiceEx2(int partition, int voice, NJS_POINT3 *position, int mode, int fade, int pause)
{
    (void)position;
    recvxAudioVolume(1, fade ? -127 : 0, 0);
    if (mode != 2) PlayAdxEx(1, (unsigned int)partition, (unsigned int)voice, pause);
    if (fade) recvxAudioVolume(1, 0, fade);
}
void PlayVoiceEx(int voice, NJS_POINT3 *position, int mode, int fade, int pause)
{ PlayVoiceEx2(PatId[1], voice, position, mode, fade, pause); }
void PlayVoice(int voice, NJS_POINT3 *position, int mode, int fade) { PlayVoiceEx(voice, position, mode, fade, 0); }
void StopVoice(int fade) { if (fade) recvxAudioVolume(1, -127, fade); else StopAdx(1); }
void ContinuePlayVoice(void) { recvxAudioPause(1, 0); }
void RequestAllStopSoundEx(int streams, int sounds, int frames)
{
    if (streams) { StopBgm(frames); StopVoice(frames); }
    if (sounds) recvxAudioStopSounds(frames);
}
static void se(int slot, int sound, int volume, int pan)
{ recvxAudioPlaySe(slot, sound, volume, pan); check(); }
static void spatial(const NJS_POINT3 *position, int field, int *volume, int *pan)
{
    float x, y, z;
    unsigned int angle, index;
    int distance;
    *volume = recvxAudioEnvironment((unsigned int)field); *pan = 0;
    if (!position || (sys->gm_flg & 0x01000000)) { *volume = 0; return; }
    x = position->x - cam.wpx; y = position->y - cam.wpy; z = position->z - cam.wpz;
    distance = (int)sqrtf(x*x + y*y + z*z);
    if (distance > 509) distance = 509;
    angle = (unsigned int)(cam.ay + (int)(10430.381f * atan2f(x, z))) & 65535u;
    index = (unsigned int)(0.005493164f * angle / 5.0f);
    /* Original tables cover 68 bins; bound rear-angle lookups. */
    if (index >= 68) index = 67;
    *pan = PanTbl360[index];
    *volume += ThreeDVolTbl[distance] + PanTbl360Vol[index] - recvxAudioEnvironment(9) / 12;
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
void AllStopEnemySe(void) { for (int i = 0; i < 6; ++i) recvxAudioStopSe(i); }
void CallEnemySe(int slot, NJS_POINT3 *position, int sound)
{ int volume, pan; if (slot < 0 || slot >= 6) recvxGameBootUnsupported("invalid enemy sound slot"); spatial(position, 4, &volume, &pan); se(slot, (sound & 255) | 0x300, volume, pan); }
void StopEnemySe(int slot) { recvxAudioStopSe(slot); }
void CallBackGroundSeEx(unsigned int slot, int sound, short frames)
{
    if (slot >= 3) recvxGameBootUnsupported("invalid background sound slot");
    background[slot] = sound; background_fade[slot] = frames;
    if (init_script) { background_pending[slot] = 1; return; }
    recvxAudioPlayMidi((int)slot, (sound & 255) | 0x300, recvxAudioEnvironment(5 + (slot == 1)), 0); check();
}
void CallBackGroundSe(unsigned int slot, int sound) { CallBackGroundSeEx(slot, sound, 0); }
void CallBackGroundSe2(unsigned int slot, int sound)
{ if (slot >= 3) recvxGameBootUnsupported("invalid background sound slot"); if (background[slot] != sound) CallBackGroundSeEx(slot, sound, 0); }
void StopBackGroundSeEx(unsigned int slot, short frames) { (void)frames; recvxAudioStopMidi((int)slot); }
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
    case 4: break;
    default: recvxGameBootUnsupported("unsupported original sound command");
    }
    check();
}
