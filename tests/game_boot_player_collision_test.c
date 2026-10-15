#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/hitchk.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/player.h"

CAM_WORK cam;
SYS_WORK *sys;
ROM_WORK *rom;
BH_PWORK ply, ene[128], *plp;
O_WRK eff[512];
unsigned char *freemem;
static SYS_WORK state;
static ROM_WORK room;
static union {
    unsigned char bytes[4096];
    EXP_WORK alignment;
} arena;
static ATR_WORK *floors, *ledges;
static jmp_buf fault_jump;
static const char *fault;
static unsigned int checks;

void recvxGameBootUnsupported(const char *name)
{
    fault = name;
    longjmp(fault_jump, 1);
}

static void expect(int condition, const char *name)
{
    ++checks;
    if (!condition) { fprintf(stderr, "FAIL: %s\n", name); exit(1); }
}

static int near(float a, float b) { return fabsf(a - b) < 0.002f; }

static void reset(void)
{
    memset(&state, 0, sizeof(state)); memset(&room, 0, sizeof(room));
    memset(&ply, 0, sizeof(ply)); memset(ene, 0, sizeof(ene));
    memset(&arena, 0, sizeof(arena));
    sys = &state; rom = &room; plp = &ply; freemem = arena.bytes;
    state.endp = arena.bytes + sizeof(arena.bytes);
    ply.flg = 9; ply.car = 1; ply.cah = 2; ply.ar = 0.5f;
    ply.exp0 = arena.bytes;
    floors = (ATR_WORK *)(arena.bytes + 256);
    ledges = (ATR_WORK *)(arena.bytes + 1024);
    room.flrp = floors; room.etcp = ledges;
    fault = NULL;
}

static void enemy(unsigned int index, float x, float y, float z)
{
    BH_PWORK *p = &ene[index];
    p->flg = 0x49; p->car = 1; p->cah = 2;
    p->px = x; p->py = y; p->pz = z;
    if (state.ewk_n <= (int)index) state.ewk_n = index + 1;
}

static void floor_record(ATR_WORK *p, unsigned int type)
{
    p->flg = 1; p->type = type; p->px = -1; p->py = -1; p->pz = -1;
    p->w = p->h = p->d = 2;
}

int main(void)
{
    reset(); enemy(0, 1, 0, 0);
    bhCheckEnemies(plp);
    expect(near(ply.px, -1) && near(ply.pz, 0) && (ene[0].stflg & 4),
           "original enemy cylinder push and player-contact marker");

    reset(); enemy(0, 1, 0, 0); enemy(1, 0, 0, 1);
    bhCheckEnemies(plp);
    expect(near(ply.px, -0.5f) && near(ply.pz, -0.5f),
           "multiple original contacts average their corrected positions");

    reset(); enemy(0, 1, 3, 0);
    bhCheckEnemies(plp);
    expect(ply.px == 0 && !ene[0].stflg, "separated vertical cylinders do not collide");
    ene[0].py = 0; ene[0].flg &= ~0x40u;
    bhCheckEnemies(plp);
    expect(ply.px == 0, "player only contacts enemies with retail collision flag 0x40");
    ene[0].flg |= 0x40u; ply.flg2 = 1;
    bhCheckEnemies(plp);
    expect(ply.px == 0, "retail movement lock bypasses enemy push");

    reset(); enemy(0, 1, 0, 0); enemy(1, 0, 0, 0);
    bhCheckEnemies(&ene[1]);
    expect(near(ene[1].px, -1) && (ene[0].stflg & 2),
           "enemy pair excludes self and marks enemy-contact state");

    reset(); enemy(0, 1, 0, 0); ene[0].flg |= 0x80;
    ene[0].lkwkp = (unsigned char *)&ene[1]; ene[1].stflg = 0x1000000;
    bhCheckEnemies(plp);
    expect(ply.px == 0, "inactive linked actor suppresses the original collision");
    ene[0].lkwkp = NULL;
    if (!setjmp(fault_jump)) { bhCheckEnemies(plp); expect(0, "null link must fault"); }
    expect(fault && strstr(fault, "linked work"), "malformed linked actor fails before dereference");

    reset(); state.ewk_n = 129;
    if (!setjmp(fault_jump)) { bhCheckEnemies(plp); expect(0, "enemy count must fault"); }
    expect(fault && strstr(fault, "count"), "enemy contact scratch array is bounded to 128 actors");

    reset(); enemy(0, 1, 0, 0);
    bhCheckPlayer(&ene[0]);
    expect(near(ply.px, -0.5f) && near(ene[0].px, 1.5f) &&
           (ene[0].stflg & 4) && (ply.stflg & 2), "original reciprocal cylinder push shares displacement");
    reset(); enemy(0, 1, 0, 0); ply.flg2 = 1;
    bhCheckPlayer(&ene[0]);
    expect(ply.px == 0 && near(ene[0].px, 2), "locked player pushes the other actor only");

    reset();
    for (unsigned int i = 0; i < 128; ++i) enemy(i, 1, 0, 0);
    bhCheckEnemies(plp);
    expect(near(ply.px, -1) && (ene[127].stflg & 4),
           "all 128 enemy contacts fit the retail scratch buffer");

    reset(); enemy(0, 0, 0, 0); ply.flg = 1;
    ((EXP_WORK *)ply.exp0)->nlxb = -1;
    bhCheckPlayer(&ene[0]);
    expect(near(ply.px, -0.5f) && near(ene[0].px, 0.5f),
           "root-driven player collision reads original expression coordinates");

    reset(); room.flr_n = 1; floor_record(floors, 1); floors->prm0 = 4;
    bhCheckFloorP(plp);
    expect(ply.flr_snd == 4, "floor collision updates the original footstep material");
    expect(bhCheckFloorSound(plp, 0, 0, 0) == 4,
           "floor material query returns the same original sound id");
    floors->attr = 1; ply.flr_snd = 7;
    bhCheckFloorP(plp);
    expect(ply.flr_snd == 7, "restricted floor material preserves airborne sound state");
    ply.stflg |= 0x10; bhCheckFloorP(plp);
    expect(ply.flr_snd == 4, "restricted floor material is active on ground");

    reset(); state.mflr_n = 2;
    floor_record(&state.mflrp[0], 3); state.mflrp[0].prm0 = 1;
    floor_record(&state.mflrp[1], 3); state.mflrp[1].prm0 = 3;
    bhCheckFloorP(plp);
    expect((ply.stflg & 0x100000) && (ply.flg2 & 8) && ply.footeff == 2,
           "live dynamic floor volumes apply original player and foot-effect flags");
    expect(bhCheckFloorEffect(0, 0, 0) == &state.mflrp[0] &&
           !bhCheckFloorEffect(1, 0, 0), "floor effect query includes dynamic volumes on the current floor");
    state.mflrp[0].type = 2;
    expect(bhCheckFloorEnemy(0, 0, 0) == &state.mflrp[0],
           "enemy floor query preserves the original type filter");
    ply.px = 10; bhCheckFloorP(plp);
    expect(!(ply.stflg & 0x100000) && !(ply.flg2 & 8),
           "leaving dynamic floor clears the original transient flags");

    reset(); room.flr_n = 1; floor_record(floors, 0);
    bhCheckFloorP(plp);
    expect((state.cb_flg & 0x200) && state.flr_idx == 0, "original floor action volume selects floor index");
    floors->attr = 0x1000; bhCheckFloorP(plp);
    expect(!(state.cb_flg & 0x200), "floor action obeys original facing-direction restriction");

    reset(); room.etc_n = 1; floor_record(ledges, 2); ledges->prm0 = 1;
    expect(bhCheckDansaAtari(0, 0, 0) == ledges && !bhCheckDansaAtari(1, 0, 0),
           "original ledge query respects current floor number");
    bhCheckFloorP(plp);
    expect(ply.stflg & 0x20000, "floor controller detects original ledge collision");
    bhCheckDansa(plp);
    expect(ply.stflg & 0x20000, "standalone ledge controller uses original room records");
    state.metc_n = 65;
    if (!setjmp(fault_jump)) { bhCheckFloorP(plp); expect(0, "floor count must fault"); }
    expect(fault && strstr(fault, "count"), "dynamic floor work cannot exceed its 64 records");

    reset(); room.flr_n = 1; room.flrp = (ATR_WORK *)(arena.bytes + sizeof(arena.bytes) - 4);
    if (!setjmp(fault_jump)) { bhCheckFloorP(plp); expect(0, "floor span must fault"); }
    expect(fault && strstr(fault, "table"), "room collision records must fit original arena");

    reset(); floor_record(floors, 0); floors->attr = 0x300; floors->prm1 = 4;
    cam.ncut = 4;
    expect(!bhCheckWallAttrB89(floors), "original wall attribute permits listed camera cut");
    cam.ncut = 7;
    expect(bhCheckWallAttrB89(floors), "original wall attribute blocks other camera cuts");
    printf("original game collision: %u checks passed\n", checks);
    return 0;
}
