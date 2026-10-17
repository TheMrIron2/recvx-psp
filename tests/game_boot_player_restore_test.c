#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/engine_original_fault.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/player.h"
#include "ps2/veronica/prog/objitm.h"
#include "ps2/veronica/prog/eneset.h"

static SYS_WORK state;
BH_PWORK ply;
#define actor ply
SYS_WORK *sys = &state;
BH_PWORK *plp = &actor, ene[128];
unsigned char *freemem;
static unsigned char arena[65536] __attribute__((aligned(64)));
static unsigned int objects, eggs, motions, models;
static unsigned int shadows, floors, allocations;
static NJS_TEXLIST room_textures;
static int equipment_id;
static jmp_buf abort_call;
static const char *fault;
extern const float PlyInfo[4][2];
extern const unsigned short PlMtnAct[2][3][7];
extern const char PlyFlip[23];
void recvxGameBootUnsupported(const char *name) { fault = name; longjmp(abort_call, 1); }
#define REJECT(call, expected) do { fault = NULL; \
    if (!setjmp(abort_call)) { call; assert(!"expected restore fault"); } \
    assert(fault && strstr(fault, expected)); } while (0)
int bhCkFlg(unsigned int *flags, unsigned int bit) { return !!(flags[bit / 32] & (1u << (bit & 31))); }
O_WRK *bhSetObject(ETTY_WORK *entry, int number, unsigned char *link)
{
    assert(number == 2 && link == (unsigned char *)plp);
    ++objects; equipment_id = entry->id; sys->obwp[2].mdlver = 4;
    return &sys->obwp[2];
}
BH_PWORK *bhSetEnemy(EGG_WORK *entry, int unused)
{ assert(entry->id == 27 && entry->type < 3 && !unused); ++eggs; return ene; }
int bhSetMotion(BH_PWORK *work, int add, int mode, void *data)
{
    assert(work == plp && !mode && work->mnwP == work->mnwPb);
    assert((!add && !data) || (add == 65536 && data == (void *)PlyFlip));
    work->frm_no += add; ++motions; return 0;
}
void bhCalcModel(BH_PWORK *work) { assert(work == plp); ++models; }
int bhSetShadow(char *route, unsigned char *link, int joint, float x, float y, float z)
{
    assert(!route && link == (unsigned char *)plp && joint == 1 &&
        x == 4.5f && y == 4 && z == 3.5f); ++shadows; return 0;
}
void bhCheckFloorP(BH_PWORK *work) { assert(work == plp); work->py = 8; ++floors; }
unsigned char *bhGetFreeMemory(int bytes, int alignment)
{
    assert(bytes == 1712 && alignment == 32);
    uintptr_t address = ((uintptr_t)sys->memp + 31u) & ~(uintptr_t)31u;
    assert(address >= (uintptr_t)arena && address + bytes <= (uintptr_t)sys->endp);
    sys->memp = (unsigned char *)address + bytes; ++allocations;
    return (unsigned char *)address;
}

static EXP_WORK *setup(int character)
{
    memset(&state, 0, sizeof(state)); memset(&actor, 0, sizeof(actor));
    memset(ene, 0, sizeof(ene)); memset(arena, 0, sizeof(arena));
    sys = &state; plp = &actor; freemem = arena; sys->endp = arena + sizeof(arena);
    sys->ply_id = character; sys->obwp = (O_WRK *)arena;
    plp->exp0 = arena + 8192; plp->mlwP = plp->mdl;
    plp->exp1 = arena + 8704; plp->txp[0] = &room_textures;
    sys->memp = arena + 60000;
    plp->mdl[0].obj_num = 22; plp->mdl[0].objP = (NJS_CNK_OBJECT *)(arena + 12288);
    plp->mdl[0].owP = (O_WORK *)(arena + 16384);
    plp->mnwPb = (MN_WORK *)(arena + 32768);
    plp->mnwP = plp->mnwPb;
    for (unsigned int i = 0; i < 100; ++i) plp->mnwPb[i].frm_num = 20;
    objects = eggs = motions = models = shadows = floors = allocations = 0; fault = NULL;
    recvxEngineOriginalFaultReset();
    return (EXP_WORK *)plp->exp0;
}

int main(void)
{
    for (int character = 0; character < 4; ++character) {
        EXP_WORK *extra = setup(character);
        extra->wpntp = character & 1; extra->dmlvl = character % 3;
        memset(plp->exp1, 0x5a, 124);
        plp->px = 10; plp->py = 20; plp->pz = 30;
        plp->flg = plp->stflg = 0xffffffffu;
        /* No egg-spawning state in this transition fixture. */
        plp->stflg &= ~0x38000000u;
        plp->mode0 = plp->mode1 = plp->mode2 = plp->mode3 = 8;
        plp->hokan_count = 10; plp->hokan_rate = 15; plp->kdnp = (void *)arena;
        plp = &ene[0]; /* Original room entry must select ply on its own. */
        bhInitRoomChangePlayer();
        assert(plp == &ply && plp->mode0 == 1 && !plp->mode1 && !plp->mode2 && !plp->mode3);
        assert(plp->flg == ((0xffffffffu & 0x20119u) | 0x118u | 0x800u));
        assert(plp->stflg == ((0xffffffffu & ~0x38000000u) & 0x78280000u));
        assert(plp->ar == PlyInfo[character][0] && plp->ah == PlyInfo[character][1]);
        assert(plp->mdflg == 0x20 && !plp->at_flg && !plp->kdnp);
        assert(plp->mlwP->texP == &room_textures);
        assert(extra->bpx == 10 && extra->bpy == 20.5f && extra->bpz == 30);
        assert(plp->mtn_no == PlMtnAct[extra->wpntp][extra->dmlvl][0] && plp->frm_no == 65536);
        assert(!plp->hokan_count && !plp->hokan_rate && !plp->frm_mode);
        assert(plp->mtn_add == 65536 && !plp->mtn_md && plp->mtn_tp == (unsigned char *)PlyFlip);
        assert(((int *)plp->exp1)[0] == 1 && ((short *)plp->exp1)[34] == -1 && !plp->exp1[120]);
        assert(!((int *)plp->exp1)[15] && !((int *)plp->exp1)[29]);
        assert(shadows == 1 && floors == 1 && motions == 1 && models == 1 && allocations == 1);
        assert(plp->exp2 && !((uintptr_t)plp->exp2 & 31u));
        if (!character) assert(sys->obwp[2].lkwkp == (unsigned char *)plp);
        assert((plp->mlwP->owP[7].flg & 8) && (plp->mlwP->owP[11].flg & 8));
    }
    setup(0); actor.exp1 = NULL; plp = &ene[0];
    REJECT(bhInitRoomChangePlayer(), "room-change player work"); assert(plp == &ene[0] && !shadows);
    setup(0); actor.mlwP = NULL; REJECT(bhInitRoomChangePlayer(), "room-change player model");
    setup(0); actor.txp[0] = NULL; REJECT(bhInitRoomChangePlayer(), "room-change player model");
    EXP_WORK *invalid = setup(0); invalid->dmlvl = 3;
    REJECT(bhInitRoomChangePlayer(), "room-change player selectors");
    setup(0); actor.mnwP = NULL; REJECT(bhInitRoomChangePlayer(), "room-change player motion");
    setup(0); sys->obwp = NULL; REJECT(bhInitRoomChangePlayer(), "room-change player equipment");
    for (int character = 0; character < 4; ++character) {
        EXP_WORK *extra = setup(character);
        sys->ply_hp[character] = 29; sys->ply_stflg[character] = 0x280000;
        sys->ev_flg[0] = 1u << (character + 6);
        plp->mdl[4].obj_num = 7; plp->skp[4] = (int *)(arena + 60000);
        plp->mlwP = &plp->mdl[1];
        ene[0].flg = ene[127].flg = ene[1].flg = 1;
        ene[0].id = ene[127].id = 27; ene[1].id = 12;
        bhResetPlayer();
        assert(plp->hp == 29 && plp->stflg == 0x40280000u && extra->dmlvl == 2);
        assert(plp->ar == PlyInfo[character][0] && plp->ah == PlyInfo[character][1]);
        assert(plp->car == plp->ar - 1 && plp->cah == plp->ah - 1 && plp->mlwP == plp->mdl);
        assert(!ene[0].flg && !ene[127].flg && ene[1].flg == 1);
        assert((plp->mdl[0].owP[7].flg & 8) && (plp->mdl[0].owP[11].flg & 8));
        assert(objects == (unsigned int)(character == 0 || character == 3));
        if (objects) {
            assert(equipment_id == (character == 0 ? 1211 : 1212));
            assert(sys->obwp[2].lkono == 5 && sys->obwp[2].mdl[0].obj_num == 7);
            assert(sys->obwp[2].mlwP == sys->obwp[2].mdl);
            assert(sys->obwp[2].loy == (character == 0 ? 1.5869f : 0.85f));
        }
        assert(character == 3 || (sys->gm_flg & 0x8000000u));
        sys->ply_stflg[character] = 0x38000000; sys->ply_hp[character] = 120;
        bhResetPlayer(); assert(extra->dmlvl == 0 && eggs == 3);
    }
    const int health[] = {-1, 0, 29, 30, 119, 120, 160};
    for (unsigned int i = 0; i < sizeof(health)/sizeof(health[0]); ++i) {
        EXP_WORK *extra = setup(0);
        plp->hp = health[i]; plp->px = 12; plp->pz = -7;
        plp->flg = 0xffffffffu; plp->stflg = 0x18000;
        extra->wpntp = i & 1; extra->dmlvl = -99;
        plp->mode0 = plp->mode1 = plp->mode2 = plp->mode3 = 7;
        plp->hokan_count = 9; plp->hokan_rate = 10; plp->frm_no = 123456;
        bhStandPlayerMotion();
        int damage = health[i] >= 120 ? 0 : health[i] >= 30 ? 1 : 2;
        assert(extra->dmlvl == damage && plp->mtn_no == PlMtnAct[extra->wpntp][damage][0]);
        assert(plp->mode0 == 1 && !plp->mode1 && !plp->mode2 && !plp->mode3);
        assert(plp->flg == (0xc96efffbu | 8) && !(plp->stflg & 0x18000));
        assert(!plp->hokan_count && !plp->hokan_rate && !plp->frm_no);
        assert(extra->spx == 12 && extra->spz == -7 && motions == 1 && models == 1);
    }
    EXP_WORK *extra = setup(0); plp->hp = 160; plp->stflg = 0x280000;
    bhStandPlayerMotion(); assert(extra->dmlvl == 1 && plp->mtn_no == PlMtnAct[0][1][0]);
    setup(4); REJECT(bhResetPlayer(), "restore work");
    setup(0); plp->exp0 = NULL; REJECT(bhResetPlayer(), "restore work");
    setup(0); plp->mdl[0].obj_num = 7; REJECT(bhResetPlayer(), "restore model");
    setup(0); sys->obwp = NULL; REJECT(bhResetPlayer(), "restore equipment");
    extra = setup(0); extra->wpntp = 2; REJECT(bhStandPlayerMotion(), "standing selectors");
    setup(0); plp->px = NAN; REJECT(bhStandPlayerMotion(), "standing selectors");
    setup(0); plp->mnwPb = NULL; REJECT(bhStandPlayerMotion(), "standing motion");
    setup(0); plp->mnwPb[PlMtnAct[0][2][0]].frm_num = 0;
    REJECT(bhStandPlayerMotion(), "standing motion");
    assert(!motions && !models);
    puts("PASS: original room-change player selection, flags/input/texture/shadow/floor/motion/pitch setup; player reset across four characters, injury thresholds, equipment, eggs and standing motion; invalid work bounds");
    return 0;
}
