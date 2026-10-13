#include <stdlib.h>
#include <string.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/ps2_dummy.h"
#include "ps2/veronica/prog/njplus.h"
#include "ps2/veronica/prog/padman.h"
#include "ps2/veronica/prog/sdfunc.h"
#include "ps2/veronica/prog/adxwrap.h"
#include "ps2/veronica/prog/bup_00.h"
#include "ps2/veronica/prog/ps2_SystemLoadScreen.h"
#include "ps2/veronica/prog/ps2_MemoryCard..h"
#include "ps2/veronica/prog/ps2_sg_sycfg.h"
#include "ps2/veronica/prog/ps2_sg_pdvib.h"
#include "ps2/veronica/prog/ps2_sg_maloc.h"
#include "ps2/veronica/prog/message.h"
#include "ps2/veronica/prog/sync.h"
#include "psp/original_clock.h"
#include "psp/engine_original_memory.h"
#include "ps2/veronica/prog/ps2_sg_maloc.h"
#include "ps2/veronica/prog/system.h"
#include "ps2/veronica/prog/pwksub.h"
#include "ps2/veronica/prog/ps2_sg_pad.h"
#include "ps2/veronica/prog/sub1.h"

int PatId[4] = {0, 1, 2, 3};
unsigned int Ps2_sys_cnt;
unsigned int Ps2_albinoid_flag, Ps2_ice_flag, Ps2_rendertex_initflag;
unsigned int palbuf[4096];
unsigned char BackColorFlag;
float GameNear = -2.0f, GameFar = -20000.0f;
PAD_ACT Pad_act[20];
PAD_INFO Ps2_pad;
NJS_MATRIX crmat;
S_WORK swork;
float cmmat[2][16];
static NJS_PERIPHERAL peripheral;
static unsigned int repeat_ticks[16];
static int sound_mode;
extern unsigned int KeyWaitFirst, KeyWaitNext;

void recvxGameBootReadKeys(const RecvxInputState *input)
{
    unsigned int on = 0, i, previous = peripheral.on;
    if (!input) recvxGameBootUnsupported("missing engine input sample");
    if (input->buttons & RECVX_BUTTON_START) on |= 0x800;
    if (input->buttons & RECVX_BUTTON_SELECT) on |= 0x100;
    if (input->buttons & RECVX_BUTTON_UP) on |= 0x4000;
    if (input->buttons & RECVX_BUTTON_DOWN) on |= 0x1000;
    if (input->buttons & RECVX_BUTTON_LEFT) on |= 0x2000;
    if (input->buttons & RECVX_BUTTON_RIGHT) on |= 0x8000;
    if (input->buttons & RECVX_BUTTON_CROSS) on |= 0x40;
    if (input->buttons & RECVX_BUTTON_CIRCLE) on |= 0x20;
    if (input->buttons & RECVX_BUTTON_SQUARE) on |= 0x80;
    if (input->buttons & RECVX_BUTTON_TRIANGLE) on |= 0x10;
    if (input->buttons & RECVX_BUTTON_LTRIGGER) on |= 4;
    if (input->buttons & RECVX_BUTTON_RTRIGGER) on |= 8;
    peripheral.on = on; peripheral.old = previous; peripheral.off = ~on;
    peripheral.press = on & ~previous; peripheral.release = previous & ~on;
    peripheral.x1 = (int)input->analog_x - 128;
    peripheral.y1 = (int)input->analog_y - 128;
    Pad[0].on = Pad[0].Flag ? 0 : on;
    Pad[0].press = Pad[0].Flag ? 0 : peripheral.press;
    Pad[0].x1 = peripheral.x1; Pad[0].y1 = peripheral.y1;
    Pad[0].Rept = Pad[0].press;
    for (i = 0; i < 16; ++i) {
        if (!(on & (1u << i))) repeat_ticks[i] = 0;
        else if (++repeat_ticks[i] > KeyWaitFirst &&
                 (repeat_ticks[i] - KeyWaitFirst - 1) % (KeyWaitNext ? KeyWaitNext : 1) == 0 && !Pad[0].Flag)
            Pad[0].Rept |= 1u << i;
    }
}
const NJS_PERIPHERAL *njGetPeripheral(Uint32 port)
{ return port == 0 ? &peripheral : NULL; }
const PDS_PERIPHERAL *pdGetPeripheral(Uint32 port)
{ return port == 0 ? &peripheral : NULL; }

void bhReleaseFreeMemory(void *memory)
{
    if ((uintptr_t)memory < (uintptr_t)freemem || (uintptr_t)memory > (uintptr_t)sys->memp)
        recvxGameBootUnsupported("invalid ADV stack release");
    sys->memp = memory;
}
void TypewriterKeepMemory(void) { sys->typ_exp = bhGetFreeMemory(800, 32); }
void syMallocStat(Uint32 *free_size, Uint32 *largest)
{ *free_size = *largest = (Uint32)(sys->endp - sys->memp); }
void *syMalloc(Uint32 size) { return malloc(size); }
void syFree(void *memory) { free(memory); }
void bhClearVSync(void) { }
void bhInitVSync(void)
{
    sys->sys_partid = 6; sys->itm_partid = 4; sys->dor_partid = 5;
}
float FontScaleX, FontScaleY, FontScaleCR;
void bhFontScaleSet(float x, float y, float cr)
{ FontScaleX = x; FontScaleY = y; FontScaleCR = cr; }
void bhSetFontTexture(void *data)
{
    /* ADV's pictures contain their own lettering. In-game text is a later service. */
    if (recvxGameBootSourceRemaining(data) == 0)
        recvxGameBootUnsupported("font resource is outside the loaded SYSTEM entry");
    recvxGameBootLog("SYSTEM font payload loaded; in-game font drawing pending");
}
void ExitApplication(void) { recvxGameBootUnsupported("unexpected ADV application exit"); }
void bhCheckSoftReset(void) { }
void PS2_jikken(void) { recvxGameBootPresent(); }
void njWaitVSync(void) { /* Presentation owns both physical vblank waits. */ }

/* No PS2 memory card is attached. The original title disables Load Game. */
MEMORYCARDSTATE *CreateMemoryCard(MEMORYCARDSTATE *card)
{ memset(card, 0, sizeof(*card)); return card; }
int GetMcSelectPortType(MEMORYCARDSTATE *card, unsigned int port)
{ (void)card; (void)port; return 0; }
int CheckMcSelectPortInfoState(unsigned int port) { (void)port; return 0; }
SYSLOAD_SCREEN *CreateSysLoadScreen(SYSLOAD_SCREEN *screen, void *work)
{ (void)work; memset(screen, 0, sizeof(*screen)); return screen; }
int ExecuteSysLoadScreen(SYSLOAD_SCREEN *screen)
{ (void)screen; return 1; /* Original caller installs default options, no saved config. */ }

/* Muted sound backend: record requests, retain the selected configuration. */
void SetSoundMode(int mode) { sound_mode = mode; }
int GetSoundMode(void) { return sound_mode; }
Sint32 syCfgSetSoundMode(Sint32 mode) { sound_mode = mode; return 0; }
void SetAdjustDisplay(void) { }
void RequestAdjustDisplay(int x, int y) { sys->adjust_x = x; sys->adjust_y = y; }
void SetSoundModeEx(int mode, int flag) { (void)flag; sound_mode = mode; }
int GetUseVibrationUnit(void) { return 0; }
int CheckVibrationUnit(int port) { (void)port; return 0; }
void SetUseVibrationUnit(int enabled) { (void)enabled; }
void SetEventVibrationMode(int mode) { (void)mode; }
void StartVibrationEx(int port, int vibration) { (void)port; (void)vibration; }
void StopVibrationEx(void) { }
void RequestAllStopSoundEx(int a, int b, int frames) { (void)a; (void)b; (void)frames; }
void CallSystemSe(int slot, int sound) { (void)slot; (void)sound; }
void CallSystemSeBasic(int sound, int volume, int pan) { (void)sound; (void)volume; (void)pan; }
void StopAdx(unsigned int slot) { (void)slot; }
void SetVolumeAdx2(unsigned int slot, float volume) { (void)slot; (void)volume; }
void PlayAdx(unsigned int slot, unsigned int partition, unsigned int file)
{ recvxGameBootLog("muted ADX slot=%u partition=%u file=%u", slot, partition, file); }
void InitGameSoundSystem(void) { recvxGameBootLog("new-game sound initialization (muted)"); }
void ExecSoundSystemMonitor(void) { }
void SendSoundCommand(unsigned int command)
{
    if (command != 1 && command != 3 && command != 4)
        recvxGameBootUnsupported("unported sound control command");
    recvxGameBootLog("sound command=%u (muted)", command);
}
int PlayStartMovieEx(int movie, int mode, int flag)
{ (void)mode; (void)flag; recvxGameBootLog("movie=%d skipped: PSP movie decoding pending", movie); return 0; }
int WaitPrePlayMovie(int mode) { (void)mode; return 3; }
int PlayMovieMain(int mode) { (void)mode; return 3; }
void PlayStopMovieEx(int mode) { (void)mode; }
