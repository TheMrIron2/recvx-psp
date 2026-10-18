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

static int message_type,message_id,wall_blocked,wall_queries;
static float floor_y;
static ATR_WORK blocking_wall;
void bhSetMessage(int kind,int id){message_type=kind;message_id=id;}
int bhCheckFloorNum(float y){floor_y=y;return 3;}
ATR_WORK *bhCheckWallType(NJS_POINT3 *p,unsigned int flags,float radius,float height)
{(void)p;(void)flags;(void)radius;(void)height;++wall_queries;return wall_blocked?&blocking_wall:NULL;}
static ATR_WORK *interaction(unsigned int type)
{
    reset();message_type=message_id=-1;wall_blocked=wall_queries=0;memset(&blocking_wall,0,sizeof(blocking_wall));
    room.etc_n=1;ledges->flg=1;ledges->type=type;
    ledges->px=ledges->pz=-10;ledges->w=ledges->d=20;
    state.obwp=state.itwp=(O_WRK *)(arena.bytes+2048);
    ply.ah=2;
    return ledges;
}
static void test_interactions(void)
{
    ATR_WORK *r=interaction(0);r->attr=0x40;r->prm0=2;r->prm1=7;r->prm2=3;r->prm3=9;
    state.cb_flg=0x200000;bhCheckExmAtari(plp);
    expect(state.cb_flg==0x200101&&(state.st_flg&4)&&ply.stflg==0x80000000u&&
        state.door.flg==0x40&&state.door.stg_no==2&&state.door.rom_no==7&&state.door.pos_no==3&&state.door.dor_tp==9,
        "door interaction copies destination and preserves unrelated control flags");
    for(int mask=1;mask<=8;mask<<=3){r=interaction(0);state.gm_flg=mask;bhCheckExmAtari(plp);
        expect((state.cb_flg&0x100)&&!(state.cb_flg&1)&&!ply.stflg,"door transition obeys game lock flags");}
    for(int quadrant=0;quadrant<4;++quadrant){
        static const unsigned int blocked[4]={0x1000,0x800,0x400,0x2000};
        r=interaction(0);ply.ay=quadrant*16384;r->attr=blocked[quadrant];bhCheckExmAtari(plp);
        expect(!(state.cb_flg&0x101)&&!ply.stflg,"door facing restriction blocks its authored quadrant");
    }
    r=interaction(0);r->flr_no=1;state.cb_flg=0x300;bhCheckExmAtari(plp);
    expect(state.cb_flg==0x200,"wrong-floor scan clears only proximity bit");
    for(int down=0;down<2;++down)for(int ladder=0;ladder<2;++ladder){
        r=interaction(1);r->attr=ladder;r->prm0=down;r->prm2=2;ply.py=10;
        bhCheckExmAtari(plp);
        expect(ply.mode0==1&&ply.mode2==(ladder?20:14)+down&&ply.mode3==0&&
            (ply.stflg&0x80010010u)==0x80010010u&&state.pl_htp==r&&(r->attr&0x400000),
            "stairs reserve collision record and select original up/down or ladder mode");
        if(down)expect(ply.flr_no==3&&floor_y==6,"descending stairs calculate destination floor");
    }
    r=interaction(1);r->attr=0x400000;bhCheckExmAtari(plp);
    expect(!ply.stflg&&!state.pl_htp,"occupied stairs cannot start another traversal");
    r=interaction(2);bhCheckExmAtari(plp);
    expect(ply.mode2==16&&(ply.stflg&0x20)&&state.pl_htp==r&&wall_queries==1,"ledge ascent uses quantized direction and wall clearance");
    r=interaction(2);wall_blocked=1;bhCheckExmAtari(plp);
    expect(!ply.stflg&&!state.pl_htp,"solid wall prevents ledge ascent");
    blocking_wall.attr=4;bhCheckExmAtari(plp);
    expect(ply.mode2==16,"passable collision attribute permits ledge ascent");
    interaction(0);room.etc_n=0;ply.stflg=0x20000;bhCheckExmAtari(plp);
    expect(ply.mode2==17&&(ply.stflg&0x20)&&wall_queries==1,"ledge descent starts when no step suppresses it");
    r=interaction(2);r->prm0=1;ply.stflg=0x20000;bhCheckExmAtari(plp);
    expect(ply.mode2==0&&wall_queries==0,"downward step record suppresses automatic ledge descent");
    r=interaction(3);r->attr=9;r->prm0=4;r->prm2=2;
    state.pad_on=1;state.pad_ps=2;state.pad_old=3;cam.flg=0x20;bhCheckExmAtari(plp);
    expect(state.fixcno==4&&state.fixkno==2&&state.exm_attr==9&&state.pad_onb==1&&state.pad_psb==2&&state.pad_oldb==3&&!state.pad_ps&&
        (state.gm_flg&0x11000)==0x11000&&(state.st_flg&0x84)==0x84&&(state.cb_flg&8)&&(state.ef_flg&0x10),
        "examine point saves pad state and starts fixed-camera interaction");
    r=interaction(3);r->attr=0x8000;r->prm1=17;state.pad_ps=0x200;bhCheckExmAtari(plp);
    expect(message_type==0&&message_id==17&&state.pad_psb==0x200&&!state.pad_ps&&(state.cb_flg&0x20)&&(state.st_flg&0x2004)==0x2004,
        "message examine point calls message service and preserves action input backup");
    state.st_flg=0x80;message_id=-1;bhCheckExmAtari(plp);
    expect(message_id==-1,"active examination prevents restarting its message");
    r=interaction(4);state.itwp->flg=1;state.itwp->id=34;bhCheckExmAtari(plp);
    expect(state.sb_id==34&&(state.cb_flg&0x10)&&!ply.stflg,"item prompt selects live item ID");
    r=interaction(4);r->attr=1;state.itwp->flg=1;state.itwp->id=9;bhCheckExmAtari(plp);
    expect(state.sb_id==9&&ply.mode2==19&&(ply.stflg&0x10000),"animated item pickup selects original player mode");
    r=interaction(4);r->attr=6;r->prm0=255;bhCheckExmAtari(plp);
    expect((state.cb_flg&0xc0000)==0xc0000,"object sentinel and special interaction flags avoid object dereference");
    r=interaction(4);r->attr=10;state.obwp->flg=1;state.obwp->mode0=5;state.obwp->mode3=7;bhCheckExmAtari(plp);
    expect((state.cb_flg&0x100000)&&state.obwp->type==100&&!state.obwp->mode0&&!state.obwp->mode3&&
        (ply.stflg&0x18000)==0x18000,"object interaction resets object controller and marks player handoff");
    r=interaction(0);room.etc_n=0;state.metc_n=1;state.metcp[0]=*r;state.metcp[0].prm1=12;bhCheckExmAtari(plp);
    expect(state.etc_idx==0&&state.door.rom_no==12,"dynamic interaction record follows static record namespace");
    r=interaction(0);r->type=5;state.cb_flg=0x100;
    if(!setjmp(fault_jump)){bhCheckExmAtari(plp);expect(0,"bad interaction type must fault");}
    expect(fault&&strstr(fault,"record")&&state.cb_flg==0x100,"invalid probe index faults before player/control mutation");
    r=interaction(0);r->type=255;r->flg=0;bhCheckExmAtari(plp);
    expect(!fault,"inactive interaction record ignores unused type field");
    r=interaction(4);r->prm0=255;
    if(!setjmp(fault_jump)){bhCheckExmAtari(plp);expect(0,"bad item slot must fault");}
    expect(fault&&strstr(fault,"slot"),"item sentinel cannot index outside item work pool");
    interaction(0);ply.exp0=NULL;
    if(!setjmp(fault_jump)){bhCheckExmAtari(plp);expect(0,"missing expression must fault");}
    expect(fault&&strstr(fault,"expression"),"interaction requires owned expression work");
    interaction(0);((EXP_WORK *)ply.exp0)->arn=NAN;
    if(!setjmp(fault_jump)){bhCheckExmAtari(plp);expect(0,"nonfinite probe must fault");}
    expect(fault&&strstr(fault,"dimensions"),"nonfinite interaction probe faults");
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
    reset(); room.etc_n = 3;
    ledges[0].prm3 = 255; ledges[0].attr = 0x20; ledges[1].attr = 0x40;
    bhSetUseKaidanFlag(plp, ledges, 0);
    expect(ply.kdnp == &ledges[0].flg && ply.kdnidx == 0 &&
           ledges[0].attr == 0x400020 && ledges[1].attr == 0x400040,
           "stair reservation marks the authored forward pair and stores actor link");
    bhClrUseKaidanFlag(plp);
    expect(ledges[0].attr == 0x20 && ledges[1].attr == 0x40 && ply.kdnp == &ledges[0].flg,
           "stair release clears only occupancy bits and retains original actor link");
    ledges[1].prm3 = 255; ledges[1].prm0 = 1;
    bhSetUseKaidanFlag(plp, &ledges[1], 1); bhClrUseKaidanFlag(plp);
    expect(ledges[0].attr == 0x20 && ledges[1].attr == 0x40,
           "backward stair pair releases both original room records");
    ledges[0].prm3 = 2;
    bhSetUseKaidanFlag(plp, ledges, 0); bhClrUseKaidanFlag(plp);
    expect(ledges[2].attr == 0, "explicit stair link uses authored partner index");
    state.metc_n = 2; state.metcp[0].prm3 = 255;
    bhSetUseKaidanFlag(&ene[0], state.metcp, room.etc_n); bhClrUseKaidanFlag(&ene[0]);
    expect(!state.metcp[0].attr && !state.metcp[1].attr,
           "dynamic stair pair uses indices after the room's static records");
    ply.kdnp = NULL; bhClrUseKaidanFlag(plp);
    expect(!ply.kdnp, "clearing an unused stair remains an original no-op");
    ledges[0].prm3 = 255; ledges[0].prm0 = 1;
    if (!setjmp(fault_jump)) { bhSetUseKaidanFlag(plp, ledges, 0); expect(0, "stair underflow must fault"); }
    expect(fault && strstr(fault, "adjacent") && ledges[0].attr == 0x20 && !ply.kdnp,
           "invalid backward partner faults before actor or occupancy mutation");
    ply.kdnp = &ledges[2].flg; ply.kdnidx = 2; ledges[2].prm3 = 255;
    ledges[2].attr = 0x400000;
    if (!setjmp(fault_jump)) { bhClrUseKaidanFlag(plp); expect(0, "stair overflow must fault"); }
    expect(fault && strstr(fault, "adjacent") && ledges[2].attr == 0x400000,
           "invalid forward partner faults before clearing the first record");
    ledges[2].prm3 = 3;
    if (!setjmp(fault_jump)) { bhClrUseKaidanFlag(plp); expect(0, "stair link must fault"); }
    expect(fault && strstr(fault, "linked"), "explicit stair partner is bounded by room count");
    test_interactions();
    printf("original game collision and stair occupancy: %u checks passed\n", checks);
    return 0;
}
