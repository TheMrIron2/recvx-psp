#include <stdlib.h>
#include <string.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/fileview.h"
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
#include "ps2/veronica/prog/ps2_texture.h"
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
NJS_MATRIX lcmat[12];
S_WORK swork;
FV_WORK fvwork;
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
    if (input->buttons & RECVX_BUTTON_UP) on |= 0x1000;
    if (input->buttons & RECVX_BUTTON_DOWN) on |= 0x4000;
    if (input->buttons & RECVX_BUTTON_LEFT) on |= 0x8000;
    if (input->buttons & RECVX_BUTTON_RIGHT) on |= 0x2000;
    if (input->buttons & RECVX_BUTTON_CROSS) on |= 0x40;
    if (input->buttons & RECVX_BUTTON_CIRCLE) on |= 0x20;
    if (input->buttons & RECVX_BUTTON_SQUARE) on |= 0x80;
    if (input->buttons & RECVX_BUTTON_TRIANGLE) on |= 0x10;
    if (input->buttons & RECVX_BUTTON_LTRIGGER) on |= 4;
    if (input->buttons & RECVX_BUTTON_RTRIGGER) on |= 8;
    peripheral.on = on; peripheral.old = previous; peripheral.off = ~on;
    peripheral.press = on & ~previous; peripheral.release = previous & ~on;
    /* bhSetPad negates these axes before applying its direction thresholds.
       Match the corrected D-pad directions, with an exact neutral at 128. */
    peripheral.x1 = 128 - (int)input->analog_x;
    peripheral.y1 = 128 - (int)input->analog_y;
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
void TypewriterKeepMemory(void) { sys->typ_exp = bhGetFreeMemory(sizeof(TYP_WORK), 32); }
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
    if(sys->ss_flg & 0x40u)return;
    size_t size=recvxGameBootSourceRemaining(data),cursor=0;
    if(recvxGameBootValidateEffectSource(data,size))
        recvxGameBootUnsupported("invalid SYSTEM font payload");
    /* The retail SYSTEM font package shares the four model records preceding
       the common effect/font PVP. Font initialization relocates only textures. */
    for(unsigned int i=0;i<4;++i) {
        uint32_t length;memcpy(&length,(unsigned char *)data+cursor,4);
        cursor+=4u+length;
    }
    cursor=(cursor+4u+31u)&~(size_t)31u;
    sys->ef_tlist.textures=sys->ef_tex;sys->ef_tlist.nbTexture=0;
    sys->ef_ct=bhSetMemPvpTexture(&sys->ef_tlist,(unsigned char *)data+cursor,0);
    if(sys->ef_ct<4)recvxGameBootUnsupported("incomplete SYSTEM font texture pages");
    sys->ef_tlist.nbTexture=4;sys->ss_flg|=0x40u;
    recvxGameBootLog("SYSTEM font initialized: four original font texture pages");
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

/* Original sound output configuration. */
void SetSoundMode(int mode) { sound_mode = mode; }
int GetSoundMode(void) { return sound_mode; }
Sint32 syCfgSetSoundMode(Sint32 mode) { sound_mode = mode; return 0; }
void SetAdjustDisplay(void) { }
void RequestAdjustDisplay(int x, int y) { sys->adjust_x = x; sys->adjust_y = y; }
void SetSoundModeEx(int mode, int flag) { (void)flag; sound_mode = mode; }
int GetUseVibrationUnit(void) { return 0; }
int CheckVibrationUnit(int port) { (void)port; return 0; }
void SetUseVibrationUnit(int enabled) { (void)enabled; }
/* Preserve the scenario mode even though stock PSP has no rumble unit. */
int EventVibrationMode;
void SetEventVibrationMode(int mode) { EventVibrationMode = mode; }
void StartVibrationEx(int attribute, int vibration) { (void)attribute; (void)vibration; }
void StopVibrationEx(void) { }
