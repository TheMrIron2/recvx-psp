#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/engine_original_matrix.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/player.h"
#include "ps2/veronica/prog/pwksub.h"

SYS_WORK *sys;
BH_PWORK *plp;
ROM_WORK *rom;
unsigned char *freemem;
BH_PWORK ene[128];
O_WRK eff[512];
static SYS_WORK system_work;
static ROM_WORK room;
static BH_PWORK player;
static unsigned char arena[100000] __attribute__((aligned(64)));
static NJS_TEXLIST eyes[3];
static jmp_buf failure;
static const char *fault;
static unsigned int visibility_calls;
static int obstructed;
static NJS_CAPSULE last_capsule;

void recvxGameBootUnsupported(const char *service)
{ fault = service; longjmp(failure, 1); }

/* Visibility is a separate, explicitly unported callback in the game build.
   This test double verifies that target selection actually invokes it. */
int bhCheckC2WallN(NJS_CAPSULE *capsule, unsigned int flags)
{
    assert(flags == 0x400);
    last_capsule = *capsule;
    ++visibility_calls;
    return obstructed;
}

static void setup(void)
{
    unsigned int i;
    memset(&system_work, 0, sizeof(system_work));
    memset(&room, 0, sizeof(room));
    memset(&player, 0, sizeof(player));
    memset(ene, 0, sizeof(ene)); memset(eff, 0, sizeof(eff));
    memset(arena, 0, sizeof(arena));
    sys = &system_work; plp = &player; rom = &room; freemem = arena;
    sys->endp = arena + sizeof(arena);
    plp->exp1 = arena;
    plp->mlwP = plp->mdl;
    plp->mlwP->obj_num = 22;
    plp->mlwP->objP = (NJS_CNK_OBJECT *)(arena + 1024);
    plp->mlwP->owP = (O_WORK *)(arena + 4096);
    plp->mlwP->owP[5].mtx[12] = 2;
    plp->mlwP->owP[5].mtx[13] = 3;
    plp->mlwP->owP[5].mtx[14] = 4;
    for (i = 0; i < 3; ++i) plp->txp[i] = &eyes[i];
    plp->mode0 = 4; /* Suppress random blink starts in deterministic tests. */
    fault = NULL; visibility_calls = 0; obstructed = 0;
    recvxEngineOriginalMatrixReset();
}

static void expect_failure(const char *message, int target)
{
    if (!setjmp(failure)) {
        if (target) bhSetHeadRotation(0, 0); else bhControlPlayerHead();
        assert(!"expected head validation fault");
    }
    assert(fault && strcmp(fault, message) == 0);
}

int main(void)
{
    unsigned int i;
    int angle, id;
    float distance;
    NJS_CNK_OBJECT *objects;
    setup(); objects = plp->mlwP->objP;
    EXP1_F(12) = 10; EXP1_F(16) = 11; EXP1_F(20) = 12;
    EXP1_S(36) = 100; EXP1_S(38) = 200; EXP1_S(40) = 300;
    plp->ax = 4; plp->ay = 5; plp->az = 6;
    for (i = 0; i < 6; ++i) {
        objects[i].ang[0] = 10; objects[i].ang[1] = 20; objects[i].ang[2] = 30;
    }
    bhControlPlayerHead();
    assert(EXP1_F(24) == 10 && EXP1_F(28) == 11 && EXP1_F(32) == 12);
    assert(EXP1_S(42) == 100 && EXP1_S(44) == 200 && EXP1_S(46) == 300);
    assert(EXP1_S(36) == 64 && EXP1_S(38) == 125 && EXP1_S(40) == 186);
    assert(EXP1_F(12) == 2 && EXP1_F(16) == 3 && EXP1_F(20) == 4);

    EXP1_I(0) = 0x1e0;
    EXP1_S(48) = EXP1_S(50) = EXP1_S(54) = EXP1_S(56) = 1000;
    bhControlPlayerHead();
    assert(EXP1_S(48) == 753 && EXP1_S(50) == 755);
    assert(objects[4].ang[0] == 753 && objects[5].ang[1] == 755);

    EXP1_UC(120) = 0x10;
    EXP1_I(84) = 1000; EXP1_I(88) = 2000;
    EXP1_I(96) = 100; EXP1_I(100) = 200;
    EXP1_I(108) = 200; EXP1_I(112) = 400;
    bhControlPlayerHead();
    assert(objects[4].ang[0] == 300 && objects[4].ang[1] == 600);
    assert(objects[5].ang[0] == 300 && objects[5].ang[1] == 600);
    EXP1_UC(120) = 0x70;
    EXP1_I(84) = -100; EXP1_I(88) = -200;
    bhControlPlayerHead();
    assert(objects[4].ang[0] == 200 && objects[4].ang[1] == 400);

    setup(); objects = plp->mlwP->objP;
    EXP1_I(0) = 3;
    for (i = 0; i < 8; ++i) {
        static const unsigned int frames[8] = {0,0,1,1,2,2,1,0};
        bhControlPlayerHead();
        assert(plp->mlwP->texP == &eyes[frames[i]]);
    }
    assert(!(EXP1_I(0) & 2) && EXP1_I(4) == 0 && EXP1_I(8) == 0);

    EXP1_I(64) = 1; EXP1_S(68) = 0;
    EXP1_F(72) = 10; EXP1_F(76) = EXP1_F(16); EXP1_F(80) = 0;
    bhSetHeadRotation(0, 0);
    assert(objects[4].ang[1] == -1024 && objects[5].ang[1] == -1024);
    assert(EXP1_S(50) == -1024 && EXP1_S(56) == -1024);

    /* Retail falls back to target zero when a previously selected ID expires. */
    sys->obwp = (O_WRK *)(arena + 10000); rom->obj_n = 1;
    sys->itwp = (O_WRK *)(arena + 20000); rom->itm_n = 1;
    sys->obwp[0].px = sys->itwp[0].px = eff[7].px = 10;
    sys->obwp[0].py = sys->itwp[0].py = eff[7].py = EXP1_F(16);
    rom->eff_n = 1; sys->efid[0] = 7;
    rom->pos_n = 1; rom->posp = (POS *)(arena + 30000);
    rom->posp[0].px = 10; rom->posp[0].py = EXP1_F(16);
    for (i = 3; i <= 6; ++i) {
        EXP1_I(64) = i; EXP1_S(68) = 9; EXP1_S(70) = 0;
        EXP1_S(50) = EXP1_S(56) = 0;
        bhSetHeadRotation(0, 0);
        assert(EXP1_S(68) == 0 && EXP1_S(50) == -1024);
    }
    EXP1_I(64) = 5; sys->efid[0] = -1;
    expect_failure("invalid player head effect target", 1);

    setup(); sys->ewk_n = 2;
    ene[0].flg = ene[1].flg = 1;
    ene[0].pz = -10; ene[1].pz = -5;
    EXP1_F(16) = 2;
    angle = 23665; distance = 0; id = -1;
    assert(bhSearchNearEnemy2(plp, &angle, &distance, &id));
    assert(id == 1 && distance > 5 && distance < 6);
    assert(visibility_calls == 1 && last_capsule.c2.z == -5);
    EXP1_I(60) = 31;
    bhLookNearEnemy();
    assert((EXP1_I(0) & 0x10) && EXP1_I(64) == 2 && EXP1_S(68) == 1);
    obstructed = 1;
    bhLookNearEnemy();
    assert(!(EXP1_I(0) & 0x10) && (EXP1_I(0) & 0x1e0) == 0x1e0);
    assert(EXP1_S(68) == -1 && EXP1_I(60) == 1);

    setup(); EXP1_I(0) = 1; EXP1_I(4) = 8;
    expect_failure("invalid player eye animation frame", 0);
    EXP1_I(4) = 0; EXP1_I(64) = 0;
    expect_failure("invalid player head target mode", 1);
    EXP1_I(64) = 2; EXP1_S(68) = 0;
    expect_failure("invalid player head enemy target", 1);
    sys->ewk_n = 1; EXP1_S(70) = 128;
    expect_failure("invalid head tracking model joint", 1);
    EXP1_S(70) = 0; EXP1_I(64) = 6; rom->pos_n = 1; rom->posp = NULL;
    expect_failure("invalid player head room position target", 1);
    puts("original head history, smoothing, script, blinking, targets and bounds pass");
    return 0;
}
