#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/sub1.h"
#include "ps2/veronica/prog/fileview.h"

static SYS_WORK state;
static ROM_WORK room;
static BH_PWORK player;
SYS_WORK *sys = &state;
ROM_WORK *rom = &room;
BH_PWORK *plp = &player;
CAM_WORK cam;
S_WORK swork;
FV_WORK fvwork;
unsigned int palbuf[4096], Ps2_rendertex_initflag;
int SoftResetFlag;
float BHD_ASPECT_X = 1.174f, BHD_ASPECT_Y = 1, GameNear = -2, GameFar = -20000;
static unsigned char arena[8192];
unsigned char *freemem = arena;
static NJS_SCREEN screen;
static float ax, ay, clip_near, clip_far;
static unsigned int stops, sounds, releases, restores, standing, file_releases, clips, palette_mode;
static unsigned int fog;
static jmp_buf failure;
static const char *fault;
void recvxGameBootUnsupported(const char *name) { fault = name; longjmp(failure, 1); }
void StopVibrationEx(void) { ++stops; }
void CallSystemSe(int mode, int number) { assert(!mode && number == 3); ++sounds; }
void njSetScreen(NJS_SCREEN *value) { screen = *value; }
void njSetAspect(Float x, Float y) { ax = x; ay = y; }
void njClipZ(Float near, Float far) { clip_near = near; clip_far = far; }
void njSetFogColor(Uint32 value) { fog = value; }
void njControl3D(Uint32 flags) { assert(!flags); }
void njSetPaletteMode(Uint32 value) { palette_mode = value; }
void njMemCopy4(void *dst, const void *src, Sint32 words) { memcpy(dst, src, (size_t)words * 4); }
void bhStandPlayerMotion(void) { ++standing; }
void bhReleaseFreeMemory(void *pointer) { ++file_releases; sys->memp = pointer; }
Sint32 njReleaseTexture(NJS_TEXLIST *list) { assert(list == &swork.subtx_list); ++releases; return 0; }
void bhGarbageTexture(NJS_TEXMEMLIST *list, unsigned int count) { assert(!list && !count); }
void bhCopyMainmem2Texmem(NJS_TEXLIST *list) { assert(list == rom->mdl.texP); ++restores; }
void bhChangeViewClip(char stage, char room_no, char room_case, int camera)
{ assert(stage == sys->stg_no && room_no == sys->rom_no && room_case == sys->rcase && camera == cam.evc_no); ++clips; }

#define REJECT(call, message) do { fault = NULL; \
    if (!setjmp(failure)) { call; assert(!"expected inventory task fault"); } \
    assert(fault && strstr(fault, message)); } while (0)
static void reset(void)
{
    memset(&state, 0, sizeof(state)); memset(&room, 0, sizeof(room));
    memset(&player, 0, sizeof(player)); memset(&swork, 0, sizeof(swork)); memset(&fvwork, 0, sizeof(fvwork));
    memset(&screen, 0, sizeof(screen));
    sys->memp = arena + 4096; sys->endp = arena + sizeof(arena); sys->sbs_sp = arena + 1024;
    swork.subp = arena + 3072; swork.ssp = arena + 2048;
    swork.subtx_list.textures = swork.subtx; swork.subtx_list.nbTexture = 5;
    stops = sounds = releases = restores = standing = file_releases = clips = palette_mode = fog = 0;
    ax = ay = clip_near = clip_far = 0;
}

int main(void)
{
    reset(); ItemTaskCheck(); assert(!standing && !releases && !stops);
    sys->ts_flg = 0x200; swork.subscreenmode = 0xbe; sys->pad_ps = 0xffff;
    ItemTaskCheck();
    assert(stops == 1 && sounds == 1 && swork.subp == arena + 4096);
    assert((sys->ts_flg & 0x80) && !(sys->ts_flg & 0x200) && !sys->pad_ps);
    assert(swork.subscreenmode == 1 && !swork.taskloop && screen.dist == 500);
    assert(screen.w == 352 && screen.h == 184 && screen.cx == 235 && screen.cy == 224 && ax == 1 && ay == 1);
    swork.statusflg |= 0x80200; fvwork.afsmode = 0x2f80;
    sys->cb_flg = 0x1e0010; sys->typ_flg = 123; sys->st_flg = 8; cam.fog_col = 0x123456;
    for (unsigned int phase = 1; phase <= 4; ++phase) { ItemTaskCheck(); assert(swork.taskloop == phase); }
    assert(releases == 1 && !standing);
    sys->mn_md1 = 1; ItemTaskCheck(); assert(!standing);
    sys->mn_md1 = 0; ItemTaskCheck();
    assert(standing == 1 && file_releases == 1 && fvwork.afsmode == 0x2000);
    assert(sys->memp == sys->sbs_sp && !sys->typ_flg && !sys->cb_flg && !(sys->st_flg & 8));
    assert((sys->ts_flg & 0x200) && !(sys->ts_flg & 0x80) && !(swork.statusflg & 0x200));
    assert(swork.statusflg & 0x80000); /* Retail leaves this latch for the outer task. */
    assert(screen.w == 640 && screen.h == 480 && screen.cx == 320 && screen.cy == 240);
    assert(ax == BHD_ASPECT_X && ay == BHD_ASPECT_Y && clip_near == GameNear && clip_far == GameFar);
    assert(fog == cam.fog_col && palette_mode == 2 && sys->sp_flg == UINT32_MAX);

    reset(); swork.flgchk = 2; swork.statusflg = 0x80000; sys->cb_flg = 0x1e0010;
    ItemTaskCheck(); assert(sys->memp == swork.subp && swork.subscreenmode == 1 && !standing);
    assert((swork.statusflg & 0x200) && !(swork.statusflg & 0x80000) && sys->cb_flg == 0x20000);
    reset(); swork.flgchk = 8; swork.statusflg = 0x80000; sys->ts_flg = 0x480;
    for (unsigned int phase = 0; phase < 5; ++phase) ItemTaskCheck();
    assert(!releases && !restores && standing == 1 && sys->mpmd == 4 && swork.subscreenmode == 0x40);
    assert((sys->st_flg & 0x40000) && !(sys->ts_flg & 0x400) && !palette_mode);
    reset(); swork.statusflg = 0x80000; swork.taskloop = 4; sys->gm_flg = 0x40; player.wpnr_no = 10;
    ItemTaskCheck(); assert(clip_near == -1.1f && clip_far == -20000);
    reset(); swork.statusflg = 0x80000; swork.taskloop = 4; player.stflg = 0x1000000;
    sys->st_flg = 0x100000; sys->fog_col = 0xff445566; sys->cb_flg = 4;
    ItemTaskCheck(); assert(clips == 1 && fog == 0x445566 && sys->sp_flg == 0xcc);
    reset(); sys->ts_flg = 0x200; sys->memp = (unsigned char *)((uintptr_t)sys->endp + 1);
    REJECT(ItemTaskCheck(), "entry cursor"); assert(!stops);
    reset(); swork.statusflg = 0x80000; swork.taskloop = 5;
    REJECT(ItemTaskCheck(), "close phase"); assert(!releases);
    reset(); swork.statusflg = 0x80000; swork.subtx_list.nbTexture = 6;
    REJECT(ItemTaskCheck(), "release list"); assert(!releases);
    reset(); swork.statusflg = 0x80000; swork.taskloop = 4; sys->sbs_sp = sys->endp;
    REJECT(ItemTaskCheck(), "return state"); assert(!standing);
    puts("PASS: original inventory entry and four close phases, monitor wait, gameplay/map/selection returns, memory/palette/view/fog restoration, scoped/event clip branches and invalid state bounds");
    return 0;
}
