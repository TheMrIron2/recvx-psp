#include <math.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/player.h"

static int contains(const void *base, size_t capacity, const void *pointer, size_t size)
{
    uintptr_t first = (uintptr_t)base, address = (uintptr_t)pointer;
    return base && pointer && !(address & 3u) && address >= first &&
        address - first <= capacity && size <= capacity - (address - first);
}

static int arena(const void *pointer, size_t size)
{
    uintptr_t first = (uintptr_t)freemem, end = (uintptr_t)sys->endp;
    return end >= first && contains(freemem, end - first, pointer, size);
}

static int member(const void *pointer, const void *base, size_t count, size_t stride)
{
    uintptr_t address = (uintptr_t)pointer, first = (uintptr_t)base;
    return base && pointer && address >= first && address - first < count * stride &&
        !((address - first) % stride);
}

static void model(const O_WRK *actor, unsigned int joint)
{
    const ML_WORK *work = actor->mlwP;
    if (!contains(actor->mdl, sizeof(actor->mdl), work, sizeof(*work)) ||
        ((uintptr_t)work - (uintptr_t)actor->mdl) % sizeof(*work) ||
        work->obj_num <= joint || work->obj_num > 128 ||
        !arena(work->objP, work->obj_num * sizeof(*work->objP)) ||
        !arena(work->owP, work->obj_num * sizeof(*work->owP)))
        recvxGameBootUnsupported("invalid head tracking model joint");
    if (!isfinite(work->owP[joint].mtx[12]) ||
        !isfinite(work->owP[joint].mtx[13]) || !isfinite(work->owP[joint].mtx[14]))
        recvxGameBootUnsupported("invalid head tracking joint position");
}

static void position(const O_WRK *actor)
{
    if (!isfinite(actor->px) || !isfinite(actor->py) || !isfinite(actor->pz))
        recvxGameBootUnsupported("invalid head tracking actor position");
}

static void base(void)
{
    if (!sys || !plp || !arena(plp->exp1, 124))
        recvxGameBootUnsupported("invalid original player head work");
    model((const O_WRK *)plp, 5);
    position((const O_WRK *)plp);
}

void recvxGameBootHeadEnemySearchPrepare(BH_PWORK *actor)
{
    int i;
    base();
    if (actor != plp || sys->ewk_n < 0 || sys->ewk_n > 128)
        recvxGameBootUnsupported("invalid head tracking enemy count");
    for (i = 0; i < sys->ewk_n; ++i) {
        BH_PWORK *enemy = &ene[i];
        if (!(enemy->flg & 1u) || (enemy->flg & 0x8000u) || (enemy->stflg & 0x41000000u))
            continue;
        if (enemy->flg & 0x80u) {
            const O_WRK *link = (const O_WRK *)enemy->lkwkp;
            if (!link || (link != (const O_WRK *)plp &&
                !member(link, ene, 128, sizeof(*ene)) &&
                !member(link, eff, 512, sizeof(*eff)) && !arena(link, sizeof(*link))))
                recvxGameBootUnsupported("invalid head tracking linked actor");
            if (link->stflg & 0x1000000u) continue;
        }
        position((const O_WRK *)enemy);
        if (enemy->lok_jno) model((const O_WRK *)enemy, enemy->lok_jno);
    }
}

void recvxGameBootPlayerHeadPrepare(int target)
{
    unsigned int i;
    O_WRK *actor = NULL;
    int kind, id, count, joint;
    base();
    /* All previous positions consumed by atan2 and enemy distances are finite. */
    for (i = 12; i <= 32; i += 4)
        if (!isfinite(EXP1_F(i))) recvxGameBootUnsupported("invalid player head position history");
    if (!target) {
        if (plp->mdl_no != 3 && (EXP1_I(0) & 1) &&
            (EXP1_I(4) < 0 || EXP1_I(4) >= 8))
            recvxGameBootUnsupported("invalid player eye animation frame");
        if (EXP1_UC(120) & 0xf0) {
            static const unsigned int offsets[] = {84, 88, 96, 100, 108, 112};
            for (i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i)
                if (EXP1_I(offsets[i]) < -65536 || EXP1_I(offsets[i]) > 65536)
                    recvxGameBootUnsupported("invalid scripted player head rotation");
        }
        return;
    }
    kind = EXP1_I(64); id = EXP1_S(68); joint = EXP1_S(70);
    if (kind < 1 || kind > 6 || id < 0 || joint < 0)
        recvxGameBootUnsupported("invalid player head target mode");
    for (i = 72; i <= 80; i += 4)
        if (!isfinite(EXP1_F(i))) recvxGameBootUnsupported("invalid player head target position");
    if (kind == 1) return;
    if (kind == 2) {
        count = sys->ewk_n;
        if (count <= 0 || count > 128) recvxGameBootUnsupported("invalid player head enemy target");
        actor = (O_WRK *)&ene[id < count ? id : 0];
    } else {
        if (!rom) recvxGameBootUnsupported("invalid player head target room");
        if (kind == 3 || kind == 4) {
            O_WRK *table = kind == 3 ? sys->obwp : sys->itwp;
            count = kind == 3 ? rom->obj_n : rom->itm_n;
            if (count <= 0 || (size_t)count > SIZE_MAX / sizeof(*table) ||
                !arena(table, (size_t)count * sizeof(*table)))
                recvxGameBootUnsupported("invalid player head object target");
            actor = &table[id < count ? id : 0];
        } else if (kind == 5) {
            count = rom->eff_n;
            if (count <= 0 || count > 256 || sys->efid[id < count ? id : 0] < 0 ||
                sys->efid[id < count ? id : 0] >= 512)
                recvxGameBootUnsupported("invalid player head effect target");
            actor = &eff[sys->efid[id < count ? id : 0]];
        } else {
            const POS *point;
            count = rom->pos_n;
            if (count <= 0 || (size_t)count > SIZE_MAX / sizeof(*rom->posp) ||
                !arena(rom->posp, (size_t)count * sizeof(*rom->posp)))
                recvxGameBootUnsupported("invalid player head room position target");
            point = &rom->posp[id < count ? id : 0];
            if (!isfinite(point->px) || !isfinite(point->py) || !isfinite(point->pz))
                recvxGameBootUnsupported("invalid player head room position");
            return;
        }
    }
    position(actor);
    if (joint) model(actor, (unsigned int)joint);
}
