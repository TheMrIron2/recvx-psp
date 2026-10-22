#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/engine_original_matrix.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/player.h"
#include "ps2/veronica/prog/pwksub.h"
#include "ps2/veronica/prog/effect.h"
#include "ps2/veronica/prog/sdfunc.h"

SYS_WORK *sys;
BH_PWORK *plp, ene[128];
O_WRK eff[512];
CAM_WORK cam;
unsigned char *freemem;
const unsigned short PlMtnAct[2][3][7] = {
    {{42,39,0,4,16,52,9},{43,40,2,7,18,52,9},{44,41,3,8,19,52,9}},
    {{42,39,1,6,17,54,9},{43,40,2,7,18,54,9},{44,41,3,8,19,54,9}}};
static SYS_WORK state;
static BH_PWORK player;
static unsigned char arena[60000] __attribute__((aligned(64)));
static jmp_buf failure;
static const char *fault;
static unsigned int footsteps, additions, visibility;
static int foot_type, add_angle;

void recvxGameBootUnsupported(const char *service)
{ fault = service; longjmp(failure, 1); }
void recvxGameBootLog(const char *format, ...) { (void)format; }
#define BOUNDARY(name) void name(void) { recvxGameBootUnsupported(#name); }
BOUNDARY(bhCPM1_act_atk)
BOUNDARY(bhCPM2_act_kdu)
BOUNDARY(bhCPM2_act_kdd)
BOUNDARY(bhCPM2_act_dnu)
BOUNDARY(bhCPM2_act_dnd)
BOUNDARY(bhCPM2_act_psh)
BOUNDARY(bhCPM2_act_cro)
BOUNDARY(bhCPM2_act_hsu)
BOUNDARY(bhCPM2_act_hsd)
BOUNDARY(bhCPM2_act_rpsh)
void bhAddSpeed(BH_PWORK *actor, int angle)
{ assert(actor == plp); ++additions; add_angle = angle; }
int bhCheckFloorSound(BH_PWORK *actor, int floor, float x, float z)
{ assert(actor == plp); (void)floor; (void)x; (void)z; return 3; }
void CallPlayerFootStepSe(int floor, int type, int flag)
{ assert(floor == 3 && flag == 1); ++footsteps; foot_type = type; }
int bhSetEffect(int id, POINT *point, unsigned char *link, int joint)
{ (void)id; (void)point; (void)link; (void)joint; recvxGameBootUnsupported("bhSetEffect"); }
void bhSetWaterSplash(BH_PWORK *actor, int joint, int type, float x, float y, float z)
{ (void)actor; (void)joint; (void)type; (void)x; (void)y; (void)z; recvxGameBootUnsupported("bhSetWaterSplash"); }
void bhSetWaterSplash3(NJS_POINT3 *point, int angle, int type, float x, float y, float z)
{ (void)point; (void)angle; (void)type; (void)x; (void)y; (void)z; recvxGameBootUnsupported("bhSetWaterSplash3"); }
int bhCheckC2WallN(NJS_CAPSULE *capsule, unsigned int flags)
{ assert(flags == 0x400); (void)capsule; ++visibility; return 0; }

#define REJECT(call, expected) do { fault = NULL; \
    if (!setjmp(failure)) { call; assert(!"expected original action boundary"); } \
    assert(fault && strcmp(fault, expected) == 0); } while (0)

static EXP_WORK *setup(void)
{
    memset(&state, 0, sizeof(state)); memset(&player, 0, sizeof(player));
    memset(ene, 0, sizeof(ene)); memset(arena, 0, sizeof(arena));
    sys = &state; plp = &player; freemem = arena; sys->endp = arena + sizeof(arena);
    plp->exp0 = arena; plp->exp1 = arena + 256;
    sys->plmthp = arena + 1024; plp->mnwP = (MN_WORK *)sys->plmthp;
    sys->plzmtp = arena + 16384;
    plp->mlwP = plp->mdl; plp->mlwP->obj_num = 22;
    plp->mlwP->owP = (O_WORK *)(arena + 30000);
    for (unsigned int i = 0; i < 64; ++i) plp->mnwP[i].frm_num = 64;
    size_t cursor = 0;
    for (unsigned int i = 0; i < 11; ++i) {
        uint32_t size = 65 * sizeof(float);
        memcpy(sys->plzmtp + cursor, &size, 4); cursor += 4;
        for (unsigned int j = 0; j < 65; ++j) {
            float speed = i + j / 100.0f;
            memcpy(sys->plzmtp + cursor + j * 4, &speed, 4);
        }
        cursor += size;
    }
    plp->mode0 = 1; plp->mtn_no = 42; plp->hp = 200;
    fault = NULL; footsteps = additions = visibility = 0;
    recvxEngineOriginalMatrixReset();
    return (EXP_WORK *)plp->exp0;
}

int main(void)
{
    /* Claire's retail table has empty slots (including 47). A room event can
       leave its motion number behind when restoring the persistent table.
       These entry paths select a fresh animation before using frame counts. */
    static void (*const replace_motion[])(void) = {
        bhCPM2_act_std, bhCPM2_act_srt, bhCPM2_act_sta,
        bhCPM2_act_wlk, bhCPM2_act_bak
    };
    static const unsigned int replacement[] = {42, 39, 9, 0, 16};
    for (unsigned int i = 0; i < sizeof(replacement) / sizeof(replacement[0]); ++i) {
        setup(); plp->mtn_no = 47; plp->mnwP[47].frm_num = 0;
        if (!setjmp(failure)) replace_motion[i]();
        assert(!fault);
        assert(plp->mtn_no == replacement[i] && plp->mode3 == 1);
    }
    /* The same hole must still be rejected when phase conversion consumes it. */
    setup(); plp->mtn_no = 47; plp->mnwP[47].frm_num = 0;
    REJECT(bhCPM2_act_run(), "invalid original player action motion");
    REJECT(bhCPM2_act_bk2(), "invalid original player action motion");
    plp->flg = 0x800000;
    REJECT(bhCPM2_act_wlk(), "invalid original player action motion");
    plp->flg = 0x2000000;
    REJECT(bhCPM2_act_bak(), "invalid original player action motion");
    plp->flg = 0; plp->mode3 = 1;
    REJECT(bhCPM2_act_std(), "invalid original player action motion");
    setup(); plp->mnwP[42].frm_num = 0;
    REJECT(bhCPM2_act_std(), "invalid original player action motion");

    EXP_WORK *expression = setup();
    plp->px = 10; plp->pz = 20; expression->spx = 12; expression->spz = 24;
    bhCPM0_action();
    assert(plp->mode2 == 0 && plp->mode3 == 1 && plp->mtn_no == 42);
    assert(plp->hokan_count == 5 && plp->hokan_rate == 42598);
    assert(fabsf(plp->px - 10.6f) < 0.0001f && fabsf(plp->pz - 21.2f) < 0.0001f);
    assert(plp->mtn_add == 65536 && (EXP1_I(0) & 4));

    sys->pad_on = 8; bhCPM0_action();
    assert(plp->mode2 == 1 && plp->mtn_no == 39 && plp->ay == -1310);
    assert(plp->mtn_add == -65536 && footsteps == 1 && foot_type == 0);
    sys->pad_on = 4; bhCPM0_action();
    assert(plp->mode2 == 2 && plp->ay == 0);

    sys->pad_old = 0; sys->pad_on = 1; bhCPM0_action();
    assert(plp->mode2 == 3 && plp->mode3 == 1 && plp->frm_no == 5 * 65536);
    assert(fabsf(plp->spd - 0.06f) < 0.0001f && additions == 1 && add_angle == 0);
    plp->frm_no = 10 * 65536; sys->pad_old = 1; bhCPM0_action();
    assert(footsteps == 3 && plp->stflg & 0x200);
    sys->pad_on = 0x401; bhCPM0_action();
    assert(plp->mode2 == 6 && plp->mode3 == 0 && plp->flg & 0x10000);
    bhCPM0_action();
    assert(plp->mtn_no == 4 && plp->mode3 == 1 && plp->spd > 3);
    sys->pad_on = 1; bhCPM0_action();
    assert(plp->mode2 == 3 && plp->mode3 == 0 && !(plp->flg & 0x10000));

    setup(); sys->pad_on = 2; bhCPM0_action();
    assert(plp->mode2 == 9 && plp->mtn_no == 16 && plp->spd < -6);
    assert(add_angle == 32768);
    ene[0].flg = 0x21; ene[0].watr.c1.z = ene[0].watr.c2.z = -5; sys->ewk_n = 1;
    sys->pad_old = 2; bhCPM0_action();
    assert(plp->mode2 == 12 && plp->mode3 == 0 && visibility == 1);
    bhCPM0_action();
    assert(plp->mtn_no == 52 && plp->mode3 == 1 && plp->spd < -10);

    expression = setup(); plp->ct3 = 300; plp->mode3 = 1;
    bhCPM2_act_std();
    assert(plp->mode2 == 13 && plp->mode3 == 0);
    bhCPM1_act_bas();
    assert(plp->mtn_no == 9 && plp->mode3 == 1);
    plp->frm_no = 63 * 65536; bhCPM1_act_bas();
    assert(plp->mtn_no == 10 && plp->mode3 == 2);

    expression = setup(); plp->hp = 10;
    assert(bhCheckPlayerKegaMotion(0, 0, 0) == 1);
    assert(expression->dmlvl == 2 && plp->mtn_no == 44 && plp->hokan_count == 3);
    /* These outputs follow the USA PS2 ELF's instruction sequence at
       0x14e51c..0x14e56c. In particular, the clamp after the 16-bit shift
       compares against the unshifted new count. A conventional phase mapping
       returning new_count<<16 would change authentic game behavior. */
    static const struct { unsigned int old_frames, new_frames; int frame, expected; } frame_cases[] = {
        {64,64,0,0}, {64,64,65535,0}, {64,64,65536,64},
        {100,50,65536,0}, {100,50,2*65536,50}, {100,50,99*65536,50},
        {50,100,65536,100}, {64,64,63*65536+65535,64},
        {32767,1,32766*65536,0}, {32767,32767,32766*65536,32767}};
    for (unsigned int i = 0; i < sizeof(frame_cases) / sizeof(frame_cases[0]); ++i)
        assert(bhGetFrameNum(frame_cases[i].old_frames, frame_cases[i].new_frames,
                             frame_cases[i].frame) == frame_cases[i].expected);
    REJECT(bhGetFrameNum(0, 64, 0), "invalid original player frame conversion");
    REJECT(bhGetTransZ(-1), "invalid original player translation stream");
    plp->frm_no = 65 * 65536;
    REJECT(bhGetTransZ(0), "invalid original player translation frame");
    plp->frm_no = 0; expression->wpntp = 2;
    REJECT(bhCPM0_action(), "invalid original player action selectors");
    expression->wpntp = 0; plp->mode2 = 7;
    REJECT(bhCPM1_act_bas(), "invalid original player baseline action");
    plp->mode2 = 14; REJECT(bhCPM1_act_bas(), "bhCPM2_act_kdu");
    plp->flg = 0x20000; sys->pad_on = 0x10;
    REJECT(bhCPM0_action(), "bhCPM1_act_atk");
    setup(); plp->stflg = 0x100000; sys->pad_on = 1;
    REJECT(bhCPM0_action(), "bhSetEffect");
    puts("original player input, idle, turn, walk/run/back, injury and action boundaries pass");
    return 0;
}
