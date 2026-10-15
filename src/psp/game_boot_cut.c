#include <math.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/player.h"

static int room_span(const void *pointer, size_t size)
{
    uintptr_t start = (uintptr_t)sys->rdtp, address = (uintptr_t)pointer;
    return pointer && sys->rdtp && sys->rdtsz > 0 && !(address & 3u) && address >= start &&
        address - start <= (size_t)sys->rdtsz && size <= (size_t)sys->rdtsz - (address - start);
}

static int arena_span(const void *pointer, size_t size)
{
    uintptr_t start = (uintptr_t)freemem, end = (uintptr_t)sys->endp, address = (uintptr_t)pointer;
    return pointer && freemem && end >= start && !(address & 3u) && address >= start &&
        address <= end && size <= end - address;
}

void recvxGameBootCutAreaPrepare(const CUT_WRK *area)
{
    if (!room_span(area, sizeof(*area)) || area->atr_tp < 0 || area->atr_tp > 4 ||
        !isfinite(area->minx) || !isfinite(area->maxx) || !isfinite(area->minz) || !isfinite(area->maxz) ||
        area->minx > area->maxx || area->minz > area->maxz ||
        (area->atr_tp && area->minx == area->maxx))
        recvxGameBootUnsupported("invalid original cut area");
}

void recvxGameBootCutWorkPrepare(const CUT_WORK *cut)
{
    const CAM_WRK *camera;
    if (!room_span(cut, sizeof(*cut)) || (unsigned int)cam.camver >= 2)
        recvxGameBootUnsupported("invalid original cut record or camera variant");
    camera = &cut->cam[cam.camver];
    {
        const float values[] = {cut->cx, cut->cz, cut->cw, cut->cd, camera->px, camera->py, camera->pz,
            camera->ln, camera->w, camera->h, camera->d, camera->y0, camera->y1, camera->y2, camera->y3,
            camera->am_spd, camera->fog_nr, camera->fog_fr};
        for (unsigned int i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
            if (!isfinite(values[i])) recvxGameBootUnsupported("invalid original camera parameters");
    }
    if ((camera->y0 || camera->y1 || camera->y2 || camera->y3 || camera->laz0 || camera->laz1 ||
        camera->laz2 || camera->laz3 || (camera->flg & 0x20)) && (cut->cw <= 0 || cut->cd <= 0))
        recvxGameBootUnsupported("invalid active camera interpolation extent");
    if (((cam.flg & 1u) || (camera->flg & 2u) || (cam.ocflg & 2u)) && camera->spd < 0)
        recvxGameBootUnsupported("invalid camera transition duration");
    if (cut->ctab_n && !room_span(cut->cuttp, cut->ctab_n * sizeof(CUT_WRK)))
        recvxGameBootUnsupported("cut areas exceed original room");
    for (unsigned int i = 0; i < cut->ctab_n; ++i) recvxGameBootCutAreaPrepare(&cut->cuttp[i]);
}

void recvxGameBootCutPrepare(int index)
{
    if (!rom || sys->rdtsz <= 0 || rom->cut_n <= 0 ||
        (size_t)rom->cut_n > (size_t)sys->rdtsz / sizeof(CUT_WORK) ||
        !room_span(rom->cutp, (size_t)rom->cut_n * sizeof(CUT_WORK)) ||
        (unsigned int)cam.camver >= 2 || !plp || plp->flr_no < -2 || plp->flr_no > 29 ||
        !isfinite(plp->gpx) || !isfinite(plp->gpy) || !isfinite(plp->gpz) ||
        rom->mdl.obj_num > 512 || (rom->mdl.obj_num && !room_span(rom->mdl.objP, rom->mdl.obj_num * sizeof(NJS_CNK_OBJECT))) ||
        rom->lgt_n < 0 || rom->lgt_n > 256 || (rom->lgt_n && !room_span(rom->lgtp, (size_t)rom->lgt_n * sizeof(LGT_WORK))))
        recvxGameBootUnsupported("invalid original room cut state");
    if (index < -1 || index >= rom->cut_n) recvxGameBootUnsupported("invalid original cut index");
    for (int i = 0; i < rom->cut_n; ++i) if (rom->cutp[i].flg & 1u) recvxGameBootCutWorkPrepare(&rom->cutp[i]);
    if (index >= 0) recvxGameBootCutWorkPrepare(&rom->cutp[index]);
}

void recvxGameBootCutActorPrepare(void)
{
    if (!arena_span(plp->exp0, sizeof(EXP_WORK)))
        recvxGameBootUnsupported("invalid active camera player work");
    if ((plp->stflg & 0x30u) || plp->mode0 == 7) {
        uintptr_t model = (uintptr_t)plp->mlwP;
        if (model < (uintptr_t)plp->mdl || model - (uintptr_t)plp->mdl >= sizeof(plp->mdl) ||
            (model - (uintptr_t)plp->mdl) % sizeof(ML_WORK) ||
            !arena_span(plp->mlwP->owP, sizeof(*plp->mlwP->owP)))
            recvxGameBootUnsupported("invalid active camera player model");
        for (unsigned int i = 12; i <= 14; ++i)
            if (!isfinite(plp->mlwP->owP->mtx[i]))
                recvxGameBootUnsupported("invalid active camera player matrix");
    }
}
