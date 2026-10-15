/* Checked original camera code against owned room and actor fixtures. */
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/cut.h"
#include "ps2/veronica/prog/event.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/player.h"
#include "ps2/veronica/prog/ps2_event.h"

static SYS_WORK work;
static ROM_WORK room;
static BH_PWORK actor;
static struct {
    CUT_WORK cuts[3];
    CUT_WRK areas[3];
    NJS_CNK_OBJECT objects[3];
    LGT_WORK lights[6];
    EVC_WORK events[1];
    EXP_WORK extra;
    O_WORK model;
} data;
SYS_WORK *sys = &work;
ROM_WORK *rom = &room;
BH_PWORK *plp = &actor;
CAM_WORK cam;
unsigned char *freemem;
unsigned int Ps2_ice_flag;
unsigned char BackColorFlag;
float GameNear, GameFar;
static jmp_buf fault;
static const char *fault_name;
static unsigned int vectors;
static float clip_near, clip_far, clip_x, clip_y;
static unsigned int background;

void recvxGameBootUnsupported(const char *name)
{ fault_name = name; longjmp(fault, 1); }
void bhInitPlEyeCamera(void) { recvxGameBootUnsupported("eye camera"); }
void bhControlPlEyeCamera(unsigned int attr) { (void)attr; recvxGameBootUnsupported("eye camera"); }
void bhControlEventCamera(void) { recvxGameBootUnsupported("event camera"); }
void bhControlMonitorCamera(void) { recvxGameBootUnsupported("monitor camera"); }
void bhMakeCameraVector(void) { ++vectors; }
void bhGetCameraPosition(void) { cam.wpx = cam.px; cam.wpy = cam.py; cam.wpz = cam.pz; }
void njClipZ(float near, float far) { clip_near = near; clip_far = far; }
void _Make_ClipVolume(float x, float y) { clip_x = x; clip_y = y; }
void njSetBackColor(unsigned int a, unsigned int b, unsigned int c)
{ assert(a == b && b == c); background = a; }

#define REJECT(call, message) do { \
    fault_name = NULL; \
    if (!setjmp(fault)) { call; assert(!"expected checked camera fault"); } \
    assert(fault_name && strstr(fault_name, message)); \
} while (0)

static int close_to(float a, float b) { return fabsf(a - b) < 0.0001f; }
static void reset(void)
{
    memset(&work, 0, sizeof(work)); memset(&room, 0, sizeof(room));
    memset(&actor, 0, sizeof(actor)); memset(&cam, 0, sizeof(cam));
    memset(&data, 0, sizeof(data));
    sys->rdtp = (unsigned char *)&data; sys->rdtsz = sizeof(data);
    freemem = (unsigned char *)&data; sys->endp = freemem + sizeof(data);
    room.cutp = data.cuts; room.cut_n = 3;
    room.lgtp = data.lights; room.lgt_n = 6;
    room.mdl.objP = data.objects; room.mdl.obj_num = 3;
    room.evcp = data.events; room.evc_n = 1; data.events[0].keyf_n = 1;
    actor.exp0 = (unsigned char *)&data.extra;
    actor.mlwP = &actor.mdl[0]; actor.mlwP->owP = &data.model;
    for (unsigned int i = 0; i < 3; ++i) {
        CUT_WORK *cut = &data.cuts[i];
        cut->flg = 1; cut->ctab_n = 1; cut->cuttp = &data.areas[i];
        cut->cx = i * 10; cut->cw = cut->cd = 10;
        data.areas[i].minx = i * 10; data.areas[i].maxx = (i + 1) * 10;
        data.areas[i].maxz = 10;
        for (unsigned int j = 0; j < 2; ++j) {
            CAM_WRK *ci = &cut->cam[j];
            ci->px = 20 + i * 10 + j * 100; ci->py = 8; ci->pz = -10;
            ci->pers = 8192; ci->am_spd = 0.5f; ci->aa_spd = 50;
            ci->fog_col = 0x123456; ci->fog_nr = -10; ci->fog_fr = -100;
        }
        data.objects[i].model = (NJS_CNK_MODEL *)&data;
    }
    actor.gpx = actor.gpz = 5; sys->sp_flg = 0x40;
    vectors = 0; Ps2_ice_flag = 0; BackColorFlag = 0;
    clip_near = clip_far = clip_x = clip_y = 0; background = 0;
}

static void selection(void)
{
    reset();
    assert(bhCheckCutArea(0, 0, 0) == 0);
    assert(bhCheckCutArea(10, 0, 0) == 1);
    assert(bhCheckCutArea(30, 0, 0) == -1);
    assert(bhCheckCutArea(5, 10, 0) == -1);
    assert(bhCheckCutArea(5, 5, 1) == -1);
    data.areas[1].minx = 4;
    assert(bhCheckCutArea(5, 5, 0) == 0); /* Array priority in overlap. */
    data.cuts[0].flg = 0; assert(bhCheckCutArea(5, 5, 0) == 1);
    for (int kind = 1; kind <= 4; ++kind) {
        CUT_WRK *area = &data.areas[0]; area->atr_tp = kind;
        assert(bhCheckCutAreaInnerTriangle(5, 5, area) == 0); /* Diagonal excluded. */
        assert(bhCheckCutAreaInnerTriangle(5, kind <= 2 ? 8 : 2, area) == 1);
        assert(bhCheckCutAreaInnerTriangle(5, kind <= 2 ? 2 : 8, area) == 0);
        assert(bhCheckCutAreaInnerTriangle(-1, 5, area) == 0);
        /* Reciprocal in reconstructed source used to divide by zero here. */
        assert(bhCheckCutAreaInnerTriangle(kind == 1 || kind == 4 ? 10 : 0, 5, area) == 1);
    }
    reset(); bhCheckCut(1); assert(cam.ncut == 0 && vectors == 1 && close_to(cam.px, 20));
    actor.gpx = 10.5f; bhCheckCut(0); assert(cam.ncut == 0); /* One-unit hysteresis. */
    actor.gpx = 12; bhCheckCut(0); assert(cam.ncut == 1 && cam.ocut == 0 && vectors == 2);
}

static void variants_and_masks(void)
{
    CAM_WRK *ci;
    reset(); ci = &data.cuts[0].cam[1]; data.areas[0].attr = 0x8000;
    ci->hidobj[0] = 0x80000000u >> 1; ci->hidlgt[0] = 0x80000000u >> 4;
    ci->flg = 0x8000 | 0x200 | 0x400; ci->fil_no = 3; ci->fil_rt = 90;
    data.lights[3].flg = 0x42; data.lights[4].flg = data.lights[5].flg = 2;
    bhCheckCut(1);
    assert(cam.camver == 1 && cam.attr == 0x8000 && close_to(cam.px, 120));
    assert(!(data.objects[0].evalflags & 8) && (data.objects[1].evalflags & 8));
    assert(data.lights[3].flg == 0x42 && !(data.lights[4].flg & 2) && (data.lights[5].flg & 2));
    assert((cam.flg & 0x38) == 0x38 && sys->fil_no == 3 && sys->fil_rt == 90);
    assert(cam.fog_col == 0x123456 && cam.fog_nr == -10 && cam.fog_fr == -100);
    assert(sys->gm_flg & 0x10);
    ci->hidobj[1] = 0x80000000u >> 2; bhSetHideObject(1);
    assert(!(data.objects[1].evalflags & 8) && (data.objects[2].evalflags & 8));
    sys->st_flg = 1; cam.flg |= 2; data.events[0].keyf[0].hidobj[0] = 0x80000000u;
    bhSetHideObject(0); assert(data.objects[0].evalflags & 8);
    REJECT(bhSetHideObject(8), "event camera object mask");
    cam.keyf_no = 1; REJECT(bhSetHideObject(0), "event camera object mask");
    reset(); bhSetFixedCut(2);
    assert((sys->st_flg & 1) && cam.ncut == 2 && close_to(cam.px, 40));
    cam.camver = 1; bhSetRenderCut(1);
    assert(cam.camver == 0 && cam.ncut == 1 && close_to(cam.px, 30));
}

static void interpolation_and_transitions(void)
{
    CUT_WORK *cut;
    CAM_WRK *ci;
    float x, z;
    reset(); cut = &data.cuts[0]; ci = &cut->cam[0];
    ci->y0 = 0; ci->y1 = 10; ci->y2 = 20; ci->y3 = 30;
    ci->laz0 = 0; ci->laz1 = 100; ci->laz2 = 200; ci->laz3 = 300;
    cam.plx = 2.5f; cam.plz = 7.5f;
    assert(close_to(bhCalcActiveYpos(cut), 17.5f));
    assert(bhCalcActiveZang(cut) == 175);
    ci->pers = 0x281e140a; /* Packed field: four corner FOV values 10/20/30/40. */
    assert(bhCalcActivePers(cut) == 5005);
    ci->pers = 8192; ci->y0 = ci->y1 = ci->y2 = ci->y3 = 0;
    ci->laz0 = ci->laz1 = ci->laz2 = ci->laz3 = 0;
    ci->w = 100; ci->d = 50;
    actor.gpx = 2; actor.gpz = 8;
    bhInitActiveCamera(1);
    assert(close_to(cam.px, 17) && close_to(cam.pz, -8.5f));
    cam.plx = 8; cam.plz = 2; bhControlActiveCamera();
    assert(close_to(cam.pxp, 23) && close_to(cam.pzp, -11.5f));
    assert(close_to(cam.px, 20) && close_to(cam.pz, -10));
    bhCheckNearAttrOffCutRange(cut, 0x20, &x, &z);
    assert(close_to(x, 0) && close_to(z, 5));
    data.areas[0].attr = 0x20;
    REJECT(bhCheckNearAttrOffCutRange(cut, 0x20, &x, &z), "no eligible camera boundary");
    reset(); ci = &data.cuts[0].cam[0]; ci->flg = 2; ci->spd = 3;
    cam.px = 0; bhSetCut(0, 0); assert((cam.flg & 1) && cam.px == 0 && cam.pxp == 20);
    for (unsigned int i = 0; i < 4; ++i) bhControlActiveCamera();
    assert(!(cam.flg & 1) && cam.hct == 4 && close_to(cam.px, 18.125f));
    bhControlActiveCamera(); assert(close_to(cam.px, 19.0625f));
    ci->flg = 0xc0; cam.vx = 1; cam.vy = cam.vz = 0;
    cam.wpx = ci->px + 5; cam.wpy = ci->py; cam.wpz = ci->pz;
    bhControlActiveCamera(); assert(close_to(cam.fog_nr, -5) && close_to(cam.fog_fr, -95));
}

static void invalid_state(void)
{
    reset(); REJECT(bhSetCut(-1, 0), "cut index");
    cam.camver = 2; REJECT(bhCheckCutArea(1, 1, 0), "room cut state");
    reset(); REJECT(bhCheckCutArea(NAN, 1, 0), "query position");
    REJECT(bhCheckCutAreaInnerTriangle(1, 1, &data.areas[0]), "triangle cut area");
    data.areas[0].atr_tp = 5; REJECT(bhCheckCutArea(1, 1, 0), "cut area");
    reset(); data.cuts[0].cuttp = (CUT_WRK *)(sys->rdtp + sizeof(data) - 4);
    REJECT(bhCheckCutArea(1, 1, 0), "cut areas");
    reset(); data.cuts[0].cw = 0; data.cuts[0].cam[0].y1 = 1;
    REJECT(bhSetCut(0, 0), "interpolation extent");
    reset(); data.cuts[0].cam[0].flg = 2; data.cuts[0].cam[0].spd = -1;
    REJECT(bhSetCut(0, 0), "transition duration");
    reset(); actor.exp0 = NULL; REJECT(bhControlActiveCamera(), "player work");
    reset(); actor.stflg = 0x10; actor.mlwP->owP = (O_WORK *)1;
    actor.gpx = 15; REJECT(bhCheckCut(0), "player model");
    reset(); actor.stflg = 0x10; data.model.mtx[12] = NAN;
    REJECT(bhControlActiveCamera(), "player matrix");
    reset(); actor.stflg = 0x10; actor.mlwP = (ML_WORK *)((unsigned char *)actor.mdl + 4);
    REJECT(bhControlActiveCamera(), "player model");
    reset(); actor.flr_no = 30; REJECT(bhControlActiveCamera(), "room cut state");
    reset(); room.mdl.obj_num = 513; REJECT(bhSetHideObject(0), "room cut state");
    reset(); room.cut_n = INT_MAX; REJECT(bhCheckCutArea(1, 1, 0), "room cut state");
    reset(); room.lgt_n = 0; room.lgtp = NULL; bhSetHideObjLgt(0);
    reset(); sys->st_flg = 1; cam.flg = 2; room.evc_n = INT_MAX;
    REJECT(bhSetHideObject(0), "event camera object mask");
    reset(); REJECT(bhSetHideObject(16), "mask offset");
    reset(); cam.flg = 0x80000000; sys->sp_flg = 0;
    bhCheckCut(0); assert(!vectors); /* Disabled selection does not touch room/actor records. */
}

static void room_view_settings(void)
{
    reset(); sys->rom_no = 31; room.bak_col = 0x334455; BackColorFlag = 1;
    bhChangeViewClipRM(); bhChangeClipVolumeRM(); bhChangeBackColor();
    assert(GameNear == -2 && GameFar == -20000 && clip_near == -2 && clip_far == -20000);
    assert(clip_x == 320 && clip_y == 240 && background == 0x334455);
    sys->rom_no = 0; cam.ncut = 1; bhChangeViewClipRM();
    assert(clip_near == -10 && clip_far == -1000);
    sys->rom_no = 7; cam.ncut = 6; bhChangeClipVolumeRM();
    assert(clip_x == 1024 && clip_y == 960);
    sys->rom_no = 12; cam.ncut = 1; bhChangeBackColor();
    assert(background == 0x1e1a10);
    cam.flg = 0x40; bhChangeViewClipRM(); assert(clip_near == -1);
    sys->stg_no = 9; sys->rom_no = 5; bhChangeViewClipRM(); assert(Ps2_ice_flag == 1);
    sys->stg_no = 10;
    REJECT(bhChangeViewClipRM(), "settings stage");
    REJECT(bhChangeClipVolumeRM(), "settings stage");
    REJECT(bhChangeBackColor(), "settings stage");
}

int main(void)
{
    selection(); variants_and_masks(); interpolation_and_transitions(); invalid_state(); room_view_settings();
    puts("PASS: checked original cut boundaries, hysteresis, variants, object/light masks, event mask bounds, active interpolation, timed transitions, dynamic fog, room view settings and malformed state");
    return 0;
}
