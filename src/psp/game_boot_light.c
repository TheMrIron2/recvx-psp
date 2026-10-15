#include <limits.h>
#include <math.h>
#include <string.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/light.h"

extern float cmmat[2][16];
extern void recvxGameBootWorldModelPrepare(O_WRK *object);

int recvxGameBootLightRotate(int angle, int step)
{
    uint32_t bits = (uint32_t)angle + (uint32_t)step;
    int result;
    _Static_assert(sizeof(result) == sizeof(bits), "original light angles require 32-bit integers");
    memcpy(&result, &bits, sizeof(result));
    return result;
}

static int room_span(const void *pointer, size_t bytes)
{
    uintptr_t start = (uintptr_t)sys->rdtp, p = (uintptr_t)pointer;
    return pointer && sys->rdtp && sys->rdtsz > 0 && !(p & 3u) && p >= start &&
        p - start <= (size_t)sys->rdtsz && bytes <= (size_t)sys->rdtsz - (p - start);
}

static int arena_span(const void *pointer, size_t bytes)
{
    uintptr_t first = (uintptr_t)freemem, last = (uintptr_t)sys->endp, p = (uintptr_t)pointer;
    return pointer && freemem && last >= first && !(p & 3u) && p >= first && p <= last && bytes <= last - p;
}

static void matrix_prepare(NJS_MATRIX *matrix)
{
    if (!matrix || ((uintptr_t)matrix & 3u)) recvxGameBootUnsupported("invalid lighting matrix");
    for (unsigned int i = 0; i < 16; ++i)
        if (!isfinite((*matrix)[i])) recvxGameBootUnsupported("invalid lighting matrix");
}

static void room_lights(void)
{
    if (!rom || rom->lgt_n < 0 || rom->lgt_n > 256 ||
        (rom->lgt_n && !room_span(rom->lgtp, (size_t)rom->lgt_n * sizeof(LGT_WORK))))
        recvxGameBootUnsupported("invalid original room light table");
}

void recvxGameBootLightSpecialPrepare(float intensity)
{
    room_lights();
    if (!rom->lgt_n || !isfinite(intensity)) recvxGameBootUnsupported("invalid special directional light");
    if (rom->lgtp->aspd < 0 || rom->lgtp->aspd > 32767 ||
        rom->lgtp->ct0 < 0 || rom->lgtp->ct0 > INT_MAX - 8388352)
        recvxGameBootUnsupported("invalid special light animation");
}

void recvxGameBootLightPrepare(int control)
{
    if (!sys || !rom) recvxGameBootUnsupported("invalid original light state");
    if (sys->gm_flg & 0x80u) { recvxGameBootLightSpecialPrepare(1); return; }
    if (cam.mtx != (NJS_MATRIX *)&cmmat[0] && cam.mtx != (NJS_MATRIX *)&cmmat[1] &&
        !arena_span(cam.mtx, sizeof(NJS_MATRIX)))
        recvxGameBootUnsupported("invalid lighting camera matrix");
    matrix_prepare(cam.mtx);
    if (control) {
        room_lights();
        if (cam.flg & 2u) {
            if (rom->lgt_n < 4 || rom->evl_n < 0 || rom->evl_n > 256 ||
                (rom->evl_n && !room_span(rom->evlp, (size_t)rom->evl_n * sizeof(LGT_WORK))))
                recvxGameBootUnsupported("invalid event light table");
        } else if (sys->rdtsz <= 0 || rom->cut_n <= 0 ||
            (size_t)rom->cut_n > (size_t)sys->rdtsz / sizeof(CUT_WORK) ||
            !room_span(rom->cutp, (size_t)rom->cut_n * sizeof(CUT_WORK)) ||
            cam.ncut < 0 || cam.ncut >= rom->cut_n || (unsigned int)cam.camver >= 2)
            recvxGameBootUnsupported("invalid lighting camera cut");
    } else {
        if (sys->lg_pnt < 0 || sys->lg_pnt > 3)
            recvxGameBootUnsupported("invalid cached point light count");
        if (sys->st_flg & 0x10000u) {
            const float values[] = {sys->lg_vx, sys->lg_vy, sys->lg_vz, sys->lg_r, sys->lg_g, sys->lg_b};
            for (unsigned int i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
                if (!isfinite(values[i])) recvxGameBootUnsupported("invalid cached directional light");
        }
        for (int i = 0; i < sys->lg_pnt; ++i) {
            const LPTB *point = &sys->lg_ptb[i];
            const float values[] = {point->ps.x, point->ps.y, point->ps.z, point->nr, point->fr,
                point->cr, point->cg, point->cb};
            for (unsigned int j = 0; j < sizeof(values) / sizeof(values[0]); ++j)
                if (!isfinite(values[j])) recvxGameBootUnsupported("invalid cached point light");
        }
    }
}

static BH_PWORK *linked_actor(LGT_WORK *lp)
{
    int index = lp->lkno;
    switch (lp->lkflg) {
    case 1:
        if (!plp) break;
        return plp;
    case 2:
        if (rom->ene_n <= 0 || rom->ene_n > 128 || !room_span(rom->enep, (size_t)rom->ene_n * sizeof(ETTY_WORK)) ||
            index < 0 || index >= rom->ene_n || rom->enep[index].wrk_no >= 128) break;
        return &ene[rom->enep[index].wrk_no];
    case 3:
        if (rom->obj_n < 0 || (size_t)rom->obj_n > 39936u / sizeof(O_WRK) ||
            index < 0 || index >= rom->obj_n || !arena_span(sys->obwp, (size_t)rom->obj_n * sizeof(O_WRK))) break;
        return (BH_PWORK *)&sys->obwp[index];
    case 4:
        if (rom->itm_n < 0 || (size_t)rom->itm_n > 39936u / sizeof(O_WRK) ||
            index < 0 || index >= rom->itm_n || !arena_span(sys->itwp, (size_t)rom->itm_n * sizeof(O_WRK))) break;
        return (BH_PWORK *)&sys->itwp[index];
    case 5:
        if (index < 0 || index >= 256 || sys->efid[index] < 0 || sys->efid[index] >= 512) break;
        return (BH_PWORK *)&eff[sys->efid[index]];
    }
    recvxGameBootUnsupported("invalid light actor link");
    return NULL;
}

void recvxGameBootLightRecordPrepare(LGT_WORK *lp)
{
    const float values[] = {lp->px, lp->py, lp->pz, lp->lx, lp->ly, lp->lz, lp->vx, lp->vy, lp->vz,
        lp->r, lp->g, lp->b, lp->nr, lp->fr, lp->spc, lp->dif, lp->amb};
    const float cached[] = {lp->wpx, lp->wpy, lp->wpz, lp->wvx, lp->wvy, lp->wvz,
        lp->wr, lp->wg, lp->wb, lp->wnr, lp->wfr};
    for (unsigned int i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
        if (!isfinite(values[i])) recvxGameBootUnsupported("invalid light parameters");
    if (!(sys->sp_flg & 0x80u))
        for (unsigned int i = 0; i < sizeof(cached) / sizeof(cached[0]); ++i)
            if (!isfinite(cached[i])) recvxGameBootUnsupported("invalid cached light parameters");
    if ((lp->type > 13 && lp->type != 100 && lp->type != 101) ||
        lp->aspd < 0 || lp->aspd > 32767 || lp->ct0 < 0 || lp->ct0 > INT_MAX - 8388352 ||
        (lp->type == 10 && lp->mode > 3) || (lp->type == 11 && lp->mode > 2) ||
        ((lp->type == 12 || lp->type == 100) && lp->mode > 1))
        recvxGameBootUnsupported("invalid light animation");
    if (lp->lkflg >= 1 && lp->lkflg <= 5) {
        BH_PWORK *actor = linked_actor(lp);
        if (!(actor->flg & 1u)) return;
        if (lp->lkono == 0) {
            if (actor->mtx != (NJS_MATRIX *)actor->mtxbuf && !arena_span(actor->mtx, sizeof(NJS_MATRIX)))
                recvxGameBootUnsupported("invalid light actor matrix");
            matrix_prepare(actor->mtx);
        }
        else {
            if (lp->lkflg == 5) {
                ML_WORK *model = actor->mlwP;
                uintptr_t address = (uintptr_t)model, first = (uintptr_t)sys->efm;
                if (address >= first && address - first < sizeof(sys->efm) &&
                    (address - first) % sizeof(*model) == 0) {
                    if (model->obj_num > 512 || (model->obj_num &&
                        (!arena_span(model->objP, model->obj_num * sizeof(*model->objP)) ||
                         !arena_span(model->owP, model->obj_num * sizeof(*model->owP)))))
                        recvxGameBootUnsupported("invalid shared effect light model");
                } else recvxGameBootWorldModelPrepare((O_WRK *)actor);
            } else recvxGameBootWorldModelPrepare((O_WRK *)actor);
            if (lp->lkono < 0 || (unsigned int)lp->lkono >= actor->mlwP->obj_num)
                recvxGameBootUnsupported("invalid light actor joint");
            matrix_prepare((NJS_MATRIX *)actor->mlwP->owP[lp->lkono].mtx);
        }
    }
}

void recvxGameBootLightTablePrepare(const LGT_WK *table, int index)
{
    const float values[] = {table ? table->px : 0, table ? table->py : 0, table ? table->pz : 0,
        table ? table->lx : 0, table ? table->ly : 0, table ? table->lz : 0,
        table ? table->r : 0, table ? table->g : 0, table ? table->b : 0,
        table ? table->nr : 0, table ? table->fr : 0};
    room_lights();
    if (!table || index < 0 || index >= rom->lgt_n || table->aspd < 0 || table->aspd > 32767)
        recvxGameBootUnsupported("invalid light setup record");
    for (unsigned int i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
        if (!isfinite(values[i])) recvxGameBootUnsupported("invalid light setup parameters");
}
