/* The original lighting program feeds the actual shared Ninja state adapter. */
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/engine_original_fault.h"
#include "psp/engine_original_lighting.h"
#include "psp/engine_original_matrix.h"
#include "psp/engine_original_view.h"
#include "ps2/veronica/prog/light.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/ps2_NaView.h"

static SYS_WORK work;
static ROM_WORK room;
static BH_PWORK player;
static struct {
    LGT_WORK lights[8], event_lights[4];
    CUT_WORK cuts[2];
    ETTY_WORK enemies[2];
    O_WRK objects[2], items[2];
    NJS_CNK_OBJECT nodes[8];
    O_WORK poses[8];
} data;
SYS_WORK *sys = &work;
ROM_WORK *rom = &room;
BH_PWORK *plp = &player;
BH_PWORK ene[128];
O_WRK eff[512];
CAM_WORK cam;
NJS_MATRIX *cmat;
unsigned char *freemem;
unsigned char pl_sleep_cnt;
extern float cmmat[2][16];
static jmp_buf fault;
static const char *fault_name;
static int expecting_fault;

void recvxGameBootUnsupported(const char *name)
{
    if (!expecting_fault) { fprintf(stderr, "unexpected light fault: %s\n", name); abort(); }
    fault_name = name; longjmp(fault, 1);
}
#define REJECT(call, message) do { \
    fault_name = NULL; expecting_fault = 1; \
    if (!setjmp(fault)) { call; assert(!"expected light fault"); } \
    expecting_fault = 0; assert(fault_name && strstr(fault_name, message)); \
} while (0)

static int close_to(float a, float b) { return fabsf(a - b) < 0.0001f; }
static const RecvxEngineOriginalLightingState *state(void) { return recvxEngineOriginalLightingGet(); }
static LGT_WORK *light(unsigned int n, unsigned int type, int source)
{
    LGT_WORK *lp = &data.lights[n];
    lp->flg = 3; lp->type = type; lp->lsrc = source;
    lp->vx = 1; lp->r = 1; lp->g = 0.5f; lp->b = 0.25f;
    lp->px = n * 10; lp->py = 2; lp->pz = 3; lp->nr = 5; lp->fr = 50;
    return lp;
}
static void actor_model(BH_PWORK *actor)
{
    actor->flg = 1; actor->mlwP = &actor->mdl[0];
    actor->mtx = (NJS_MATRIX *)actor->mtxbuf;
    njUnitMatrix(actor->mtx); (*actor->mtx)[12] = 10;
    actor->mlwP->obj_num = 2; actor->mlwP->objP = data.nodes; actor->mlwP->owP = data.poses;
    njUnitMatrix((NJS_MATRIX *)data.poses[1].mtx);
    data.poses[1].mtx[12] = 20; data.poses[1].mtx[13] = 20; data.poses[1].mtx[14] = 20;
}
static void reset(void)
{
    memset(&work, 0, sizeof(work)); memset(&room, 0, sizeof(room));
    memset(&player, 0, sizeof(player)); memset(&data, 0, sizeof(data)); memset(&cam, 0, sizeof(cam));
    recvxEngineOriginalFaultReset(); recvxEngineOriginalMatrixReset();
    recvxEngineOriginalViewReset(); recvxEngineOriginalLightingReset();
    freemem = (unsigned char *)&data; sys->endp = freemem + sizeof(data);
    sys->rdtp = freemem; sys->rdtsz = sizeof(data); sys->sp_flg = 0x80;
    room.lgtp = data.lights; room.lgt_n = 8; room.evlp = data.event_lights;
    room.cutp = data.cuts; room.cut_n = 2; room.enep = data.enemies; room.ene_n = 2;
    room.obj_n = room.itm_n = 2; sys->obwp = data.objects; sys->itwp = data.items;
    for (unsigned int i = 0; i < 256; ++i) sys->efid[i] = -1;
    cam.mtx = (NJS_MATRIX *)&cmmat[0]; njUnitMatrix(cam.mtx);
    njSetPerspective(16384); actor_model(&player); srand(1);
}

static void directional_and_points(void)
{
    LGT_WORK *lp;
    reset(); light(0, 0, 2); light(1, 0, 2)->r = 9;
    for (unsigned int i = 2; i < 6; ++i) light(i, 0, 4);
    bhControlLight();
    assert(sys->lgtp == room.lgtp && (sys->st_flg & 0x10000) && !(sys->st_flg & 0x1000000));
    assert(sys->lg_r == 1 && sys->lg_vx == 1 && sys->lg_pnt == 3);
    assert(state()->easy_count == 4 && state()->simple_count == 4 && state()->matrix_updates == 2);
    assert(state()->easy[1].enabled && state()->easy[1].color.x == 1);
    for (unsigned int i = 0; i < 3; ++i) {
        assert(sys->lg_ptb[i].ps.x == (i + 2) * 10);
        assert(state()->easy[i + 2].enabled && state()->simple[i + 2].enabled);
        assert(state()->easy[i + 2].near_range == 5 && state()->easy[i + 2].far_range == 50);
    }
    bhSetHalfLight(); assert(state()->easy[1].color.x == 0.5f && state()->easy[2].color.x == 0.5f);
    bhSetLight(); assert(state()->easy[1].color.x == 1 && state()->easy[2].color.x == 1);
    lp = &data.lights[2]; sys->sp_flg = 0; lp->px = 999; lp->r = 9;
    bhControlLight(); assert(sys->lg_ptb[0].ps.x == 20 && sys->lg_ptb[0].cr == 1);
    for (unsigned int i = 0; i < 8; ++i) data.lights[i].flg = 0;
    bhControlLight(); assert(sys->lg_pnt == 0 && !(sys->st_flg & 0x10000));
    for (unsigned int i = 1; i <= 4; ++i) assert(!state()->easy[i].enabled && !state()->simple[i].enabled);
}

static void animation_types(void)
{
    LGT_WORK *lp;
    for (unsigned int type = 0; type <= 13; ++type) {
        reset(); lp = light(0, type, 2); lp->aspd = 10; lp->ct0 = 8192;
        bhControlLight(); assert(isfinite(sys->lg_r) && isfinite(sys->lg_vx));
        if (type == 1) assert(close_to(sys->lg_r, 0.9267767f));
        if (type == 2) assert(close_to(sys->lg_r, 0.8535534f));
        if (type == 3) assert(close_to(sys->lg_r, 0.7071068f));
        if (type == 7) assert(sys->lg_r == 0);
        if (type == 8 || type == 9) {
            assert(lp->way != 0 && close_to(sys->lg_vx * sys->lg_vx + sys->lg_vz * sys->lg_vz, 1));
        }
        if (type == 10 || type == 11) assert(lp->mode == 1 && lp->spc == 1);
        if (type == 13) assert(close_to(sys->lg_r, 0.7071068f));
        for (unsigned int i = 0; i < 25; ++i) bhControlLight();
        assert(isfinite(sys->lg_r) && isfinite(sys->lg_g) && isfinite(sys->lg_b));
    }
    reset(); lp = light(0, 100, 2); bhControlLight(); assert(lp->mode == 1 && (lp->flg & 2));
    bhControlLight(); assert(!(lp->flg & 2));
    reset(); lp = light(0, 101, 2); lp->aspd = 64; bhControlLight(); assert(!(lp->flg & 2));
    reset(); lp = light(0, 100, 2); sys->gm_flg = 0x80;
    bhControlLight(); assert(state()->easy_intensity == 3 && state()->simple_ambient == 0.2f);
    bhSetHalfLight(); assert(state()->easy_intensity == 0.6f && !(lp->flg & 2));
    for (int kind = 8; kind <= 9; ++kind) {
        const int stored[] = {0x1000001, -0x1000001, INT_MAX, INT_MIN};
        for (unsigned int i = 0; i < sizeof(stored) / sizeof(stored[0]); ++i) {
            uint32_t expected;
            NJS_VECTOR vector;
            int step;
            reset(); lp = light(0, kind, 2); lp->aspd = 30; lp->way = stored[i];
            step = (int)(182.04445f * (0.5f * lp->aspd)) & 0xffff;
            expected = (uint32_t)stored[i] + (uint32_t)(kind == 8 ? -step : step);
            bhControlLight(); assert((uint32_t)lp->way == expected);
            bhGetLightVector(0, expected & 0xffffu, 0, &vector);
            assert(close_to(sys->lg_vx, vector.x) && close_to(sys->lg_vz, vector.z));
        }
        reset(); lp = light(0, kind, 2); lp->aspd = 30;
        for (unsigned int i = 0; i < 8000; ++i) bhControlLight();
        assert(lp->way > 0x1000000 || lp->way < -0x1000000);
    }
}

static void linked_and_event_lights(void)
{
    for (int kind = 1; kind <= 5; ++kind) {
        LGT_WORK *lp;
        BH_PWORK *actor;
        reset(); lp = light(0, 0, 4); lp->lkflg = kind; lp->lx = lp->ly = lp->lz = 1;
        if (kind == 1) actor = &player;
        else if (kind == 2) { data.enemies[0].wrk_no = 1; actor = &ene[1]; }
        else if (kind == 3) actor = (BH_PWORK *)&data.objects[0];
        else if (kind == 4) actor = (BH_PWORK *)&data.items[0];
        else { sys->efid[0] = 7; actor = (BH_PWORK *)&eff[7]; }
        actor_model(actor);
        if (kind == 5) { sys->efm[7] = *actor->mlwP; actor->mlwP = &sys->efm[7]; }
        bhControlLight(); assert(sys->lg_ptb[0].ps.x == 11);
        lp->lkono = 1; bhControlLight();
        assert(sys->lg_ptb[0].ps.x == 21 && sys->lg_ptb[0].ps.y == 21 && sys->lg_ptb[0].ps.z == 21);
        actor->flg = 0; bhControlLight(); assert(!(lp->flg & 3) && sys->lg_pnt == 0);
    }
    reset(); light(4, 0, 4); cam.flg = 2; room.evl_n = 1;
    data.event_lights[0] = *light(0, 0, 4); data.lights[0].flg = 0;
    data.event_lights[0].px = 99; cam.ncut = -1;
    bhControlLight(); assert(sys->lgtp == room.evlp && (sys->st_flg & 0x1000000));
    assert(sys->lg_pnt == 1 && sys->lg_ptb[0].ps.x == 99); /* Room slots 4+ omitted in event cameras. */
    reset(); data.cuts[0].cam[0].flg = 0x10; data.cuts[0].cam[0].lgtclip = 0;
    light(0, 0, 4)->px = 100000; bhControlLight(); assert(sys->lg_pnt == 0);
    data.lights[0].px = 0; bhControlLight(); assert(sys->lg_pnt == 1);
}

static void setup_and_invalid_state(void)
{
    LGT_WK setup = {0};
    NJS_VECTOR vector;
    reset(); setup.flg = 3; setup.lsrc = 2; setup.r = 2; setup.g = 1; setup.ax = 16384;
    bhSetLightTab(&setup, 2); assert(data.lights[2].r == 2 && data.lights[2].mode == 0);
    assert(close_to(data.lights[2].vy, 1));
    bhGetLightVector(0, 0, 0, &vector); assert(close_to(vector.z, -1));
    REJECT(bhGetLightVector(0, 0, 0, NULL), "vector output");
    REJECT(bhSetLightTab(&setup, -1), "setup record");
    setup.r = NAN; REJECT(bhSetLightTab(&setup, 0), "setup parameters");
    reset(); room.lgt_n = INT_MAX; REJECT(bhControlLight(), "room light table");
    reset(); room.lgtp = (LGT_WORK *)1; REJECT(bhControlLight(), "room light table");
    reset(); cam.mtx = (NJS_MATRIX *)1; REJECT(bhControlLight(), "camera matrix");
    reset(); (*cam.mtx)[3] = NAN; REJECT(bhControlLight(), "lighting matrix");
    reset(); cam.ncut = -1; REJECT(bhControlLight(), "camera cut");
    reset(); cam.flg = 2; room.evl_n = INT_MAX; REJECT(bhControlLight(), "event light table");
    reset(); light(0, 14, 2); REJECT(bhControlLight(), "light animation");
    data.lights[0].type = 0; data.lights[0].r = NAN; REJECT(bhControlLight(), "light parameters");
    reset(); light(0, 0, 4)->lkflg = 2; data.lights[0].lkno = 2;
    REJECT(bhControlLight(), "actor link");
    data.lights[0].lkflg = 5; data.lights[0].lkno = 0;
    REJECT(bhControlLight(), "actor link");
    data.lights[0].lkflg = 1; data.lights[0].lkono = 2;
    REJECT(bhControlLight(), "actor joint");
    reset(); sys->lg_pnt = 4; REJECT(bhSetLight(), "point light count");
    reset(); sys->st_flg = 0x10000; sys->lg_r = NAN;
    REJECT(bhSetHalfLight(), "cached directional light");
    reset(); sys->lg_pnt = 1; sys->lg_ptb[0].cb = NAN;
    REJECT(bhSetLight(), "cached point light");
    reset(); light(0, 0, 2); sys->sp_flg = 0; data.lights[0].wr = NAN;
    REJECT(bhControlLight(), "cached light parameters");
    reset(); light(0, 0, 4)->lkflg = 5; sys->efid[0] = 7;
    actor_model((BH_PWORK *)&eff[7]); eff[7].mlwP = NULL;
    bhControlLight(); assert(sys->lg_ptb[0].ps.x == 10); /* Root links need no mesh model. */
    sys->efm[7].obj_num = 2; sys->efm[7].objP = data.nodes; sys->efm[7].owP = (O_WORK *)1;
    eff[7].mlwP = &sys->efm[7]; data.lights[0].lkono = 1;
    REJECT(bhControlLight(), "shared effect light model");
}

int main(void)
{
    directional_and_points(); animation_types(); linked_and_event_lights(); setup_and_invalid_state();
    puts("PASS: original directional/point lighting, three-light selection, all animation types, actor/effect links, event lights, screen clipping, full/half intensity, setup and malformed state");
    return 0;
}
