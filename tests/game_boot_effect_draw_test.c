#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/engine_original_fault.h"
#include "psp/engine_original_matrix.h"
#include "ps2/veronica/prog/effect.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/ps2_NaMatrix.h"
#include "ps2/veronica/prog/ps2_NaTextureFunction.h"

SYS_WORK *sys;
CAM_WORK cam;
BH_PWORK *plp, ene[128];
O_WRK eff[512];
O_WORK *ef_Zanzo[6];
unsigned char *freemem;
static SYS_WORK state;
static struct { NJS_CNK_OBJECT object[2]; NJS_CNK_MODEL model; O_WORK pose[2]; } arena;
static NJS_MATRIX view, drawn;
static unsigned int checks, events[32], count, models, rectangles;
static unsigned int controls[16], control_count, filter;
static NJS_TEXLIST *list;
static jmp_buf jump;
static const char *fault;
static void check(int condition) { ++checks; assert(condition); }
void recvxGameBootUnsupported(const char *name) { fault = name; longjmp(jump, 1); }
void njControl3D(Uint32 flags) { check(control_count < 16); controls[control_count++] = flags; }
void njCnkSetEasyLightColor(float r, float g, float b) { check(r == 1 && g == 1 && b == 1); }
void njCnkSetEasyLightIntensity(float i, float a) { check(i == 0.7f && a == 0.2f); }
void njCnkSetEasyLight(float x, float y, float z) { check(x == 0 && y == 0.3f && z == 0.7f); }
void njCnkEasyDrawModel(NJS_CNK_MODEL *model)
{
    check(model == &arena.model); check(list && list->nbTexture == 6); njGetMatrix(&drawn); ++models; events[count++] = 100;
}
void njTextureFilterMode(Int mode) { filter = mode; }
Sint32 njSetTexture(NJS_TEXLIST *value) { list = value; return 0; }
Sint32 njSetTextureNum(Uint32 number) { check(list && number < list->nbTexture); return 0; }
void njDrawPolygon(NJS_POLYGON_VTX *p, Int n, Int trans)
{ check(p == eff[1].pv && n == 4 && trans == 1); ++rectangles; events[count++] = 200; }
void bhSetHalfLight(void) { events[count++] = 300; }
static void callback(void *p) { events[count++] = 400 + ((O_WRK *)p)->id; }
static void overflow_callback(void *p) { (void)p; sys->ef_fncn = 513; }

static void reset(void)
{
    memset(&state, 0, sizeof(state)); memset(eff, 0, sizeof(eff)); memset(&arena, 0, sizeof(arena));
    memset(ef_Zanzo, 0, sizeof(ef_Zanzo)); sys = &state; plp = NULL;
    freemem = (unsigned char *)&arena; sys->endp = freemem + sizeof(arena);
    recvxEngineOriginalFaultReset(); recvxEngineOriginalMatrixReset();
    njUnitMatrix(&view); view[14] = 100; cam.mtx = &view;
    sys->ef_tlist.textures = sys->ef_tex; sys->ef_tlist.nbTexture = 8;
    eff[0].flg = 1; eff[0].mtx = (NJS_MATRIX *)eff[0].mtxbuf;
    eff[0].px = 1; eff[0].py = 2; eff[0].pz = 3;
    eff[0].sx = eff[0].sy = eff[0].sz = 2;
    eff[0].mlwP = &sys->efm[0]; sys->efm[0].obj_num = 1; sys->efm[0].objP = arena.object;
    arena.object[0].model = &arena.model; eff[0].tex_id = 1; eff[0].ani_ct = 1;
    sys->ef_pol[0] = &eff[0]; sys->ef_poln = 1;
    eff[1].flg = 1; eff[1].pvp = eff[1].pv; eff[1].pn = 4;
    sys->ef_ntx2d[0] = &eff[1];
    for (unsigned int i = 0; i < 4; ++i) eff[i + 2].func = callback;
    models = rectangles = count = control_count = 0; filter = 99; fault = NULL;
}
#define REJECT(call, message) do { fault = NULL; if (!setjmp(jump)) { call; check(0); } \
    check(fault && strstr(fault, message)); } while (0)

int main(void)
{
    reset(); bhDrawEffect(); check(models == 1 && filter == 1);
    check(drawn[12] == 1 && drawn[13] == 2 && drawn[14] == 103 && drawn[0] == 2);
    check(sys->ef_tlist.textures == sys->ef_tex && sys->ef_tlist.nbTexture == 8);
    check(control_count == 7 && controls[0] == 0x100 && controls[1] == 0x2400 &&
          controls[2] == 0x100 && controls[3] == 0x4000 && controls[4] == 0 &&
          controls[5] == 0x4000 && controls[6] == 0);

    reset(); sys->ef_ntx2dn = 1; sys->ef_flg = 4; sys->ef_fncn = 2;
    eff[2].id = 2; eff[3].id = 3; eff[4].id = 4; eff[5].id = 5;
    sys->ef_fnc[0] = &eff[2]; sys->ef_fnc[1] = &eff[3];
    ef_Zanzo[0] = (O_WORK *)&eff[4]; ef_Zanzo[5] = (O_WORK *)&eff[5];
    bhDrawEffect();
    check(count == 7 && events[0] == 100 && events[1] == 200 && events[2] == 300 &&
          events[3] == 402 && events[4] == 403 && events[5] == 405 && events[6] == 404);
    check(rectangles == 1);

    reset(); eff[0].flg |= 0x1000000; eff[0].mlwP = NULL;
    bhDrawEffect(); check(models == 0);
    reset(); eff[0].flg |= 0x20000000; bhDrawEffect(); check(filter == 0);
    reset(); sys->gm_flg = 0x4000; eff[0].mdflg = 0x40; bhDrawEffect(); check(!models);
    reset(); sys->ef_poln = 0; sys->ef_mdfn = 1; sys->ef_mdf[0] = &eff[0];
    REJECT(bhDrawEffect(), "bhDrawMdfEffect"); check(!models && controls[1] == 0x2400);
    reset(); sys->ef_poln = 0; sys->ef_trsn = 1; sys->ef_trs[0] = &eff[0];
    REJECT(bhDrawEffect(), "bhDrawTrsEffect3D");
    reset(); sys->ef_poln = 0; sys->ef_trs2dn = 1; sys->ef_trs2d[0] = &eff[0];
    REJECT(bhDrawEffect(), "bhDrawTrsEffect2D");
    reset(); sys->ef_poln = 513; REJECT(bhDrawEffect(), "queue count"); check(!models);
    reset(); sys->ef_poln = -1; REJECT(bhDrawEffect(), "queue count");
    reset(); sys->ef_pol[0] = (O_WRK *)1; REJECT(bhDrawEffect(), "queue actor");
    reset(); eff[0].mdlver = 1; REJECT(bhDrawEffect(), "model object");
    reset(); eff[0].tex_id = 8; REJECT(bhDrawEffect(), "texture frame");
    reset(); eff[0].sx = NAN; REJECT(bhDrawEffect(), "transform");
    reset(); eff[0].flg |= 0x80; eff[0].lkwkp = (unsigned char *)1;
    REJECT(bhDrawEffect(), "parent work");
    reset(); sys->ef_poln = 0; sys->ef_ntx2dn = 1; eff[1].pn = 3;
    REJECT(bhDrawEffect(), "2D effect polygon");
    reset(); sys->ef_poln = 0; sys->ef_ntx2dn = 1; eff[1].pv[3].col = 0xffffffffu;
    REJECT(bhDrawEffect(), "gradient or depth");
    reset(); sys->ef_poln = 0; sys->ef_fncn = 1; sys->ef_fnc[0] = &eff[0];
    REJECT(bhDrawEffect(), "callback");
    reset(); sys->ef_poln = 0; sys->ef_fncn = 1; sys->ef_fnc[0] = &eff[0]; eff[0].func = overflow_callback;
    REJECT(bhDrawEffect(), "callback count");
    reset(); sys->ef_poln = 0; ef_Zanzo[0] = (O_WORK *)1;
    REJECT(bhDrawEffect(), "afterimage");
    reset(); sys->ef_poln = 0; sys->stg_no = 1; sys->rom_no = 9;
    REJECT(bhDrawEffect(), "bhDrawThunder");
    printf("original effect drawing: %u checks passed\n", checks);
    return 0;
}
