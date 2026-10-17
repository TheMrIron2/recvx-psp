#include <assert.h>
#include <stdio.h>
#include <setjmp.h>
#include <math.h>
#include <string.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/event.h"
#include "ps2/veronica/prog/main.h"

unsigned char *bhScePtr;
static int last_id, last_fade, last_volume, calls;
static int last_slot, last_mode, stopped_voice;
static NJS_POINT3 last_position;
static SYS_WORK work;
static ROM_WORK room;
SYS_WORK *sys = &work;
ROM_WORK *rom = &room;
BH_PWORK ply, ene[128];
BH_PWORK *plp = &ply;
BH_SCEWORK bhEtask[16];
BH_SCEWORK *bhCetask = bhEtask;
static struct { O_WORK world[3]; ETTY_WORK placement; O_WRK objects[2]; } arena;
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
{ last_slot = slot; last_position = *position; last_id = sound; last_mode = mode; ++calls; return 0; }
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
    puts("PASS original BGM/voice/background/event sound operands, native routing, actor matrices, mode guards and PC advancement");
    return 0;
}
