#include <math.h>
#include <stdint.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/effect.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/ps2_NaMatrix.h"

static int arena(const void *pointer, size_t size)
{
    uintptr_t first = (uintptr_t)freemem, end = sys ? (uintptr_t)sys->endp : 0, p = (uintptr_t)pointer;
    return pointer && freemem && !(p & 3u) && p >= first && p <= end && size <= end - p;
}
static int member(const void *pointer, const void *base, size_t count, size_t stride)
{
    uintptr_t p = (uintptr_t)pointer, b = (uintptr_t)base;
    return pointer && base && p >= b && p - b < count * stride && !((p - b) % stride);
}
static int actor(const O_WRK *object)
{
    return object && (object == (O_WRK *)plp || member(object, ene, 128, sizeof(*ene)) ||
        member(object, eff, 512, sizeof(*eff)) ||
        (member(object, sys->obwp, 39936u / sizeof(O_WRK), sizeof(O_WRK)) && arena(object, sizeof(*object))) ||
        (member(object, sys->itwp, 39936u / sizeof(O_WRK), sizeof(O_WRK)) && arena(object, sizeof(*object))));
}
static int hidden(const O_WRK *object)
{
    return (object->flg & 0x1000000u) || (object->stflg & 0x1000000u) ||
        ((sys->gm_flg & 0x4000u) && (object->mdflg & 0x40u));
}
static void queue(O_WRK *const *objects, int count, size_t capacity)
{
    if (count < 0 || (size_t)count > capacity)
        recvxGameBootUnsupported("invalid original effect draw queue count");
    for (int i = 0; i < count; ++i)
        if (!actor(objects[i])) recvxGameBootUnsupported("invalid original effect draw queue actor");
}
void recvxGameBootEffectDrawQueuePrepare(O_WRK **objects, int count)
{
    size_t capacity = 0;
    if (!sys) recvxGameBootUnsupported("missing original effect draw state");
#define QUEUE(field) if (objects == sys->field) capacity = sizeof(sys->field) / sizeof(sys->field[0])
    QUEUE(ef_pol); QUEUE(ef_mdf); QUEUE(ef_lin); QUEUE(ef_ntx); QUEUE(ef_trs); QUEUE(ef_pnc);
    QUEUE(ef_opq); QUEUE(ef_thl); QUEUE(ef_ntx2d); QUEUE(ef_trs2d); QUEUE(ef_pnc2d); QUEUE(ef_fnc);
#undef QUEUE
    if (!capacity) recvxGameBootUnsupported("invalid original effect draw queue pointer");
    queue(objects, count, capacity);
}
void recvxGameBootEffectDrawPrepare(void)
{
    if (!sys || !cam.mtx) recvxGameBootUnsupported("missing original effect draw state");
#define QUEUE(field, count) queue(sys->field, sys->count, sizeof(sys->field) / sizeof(sys->field[0]))
    QUEUE(ef_pol, ef_poln); QUEUE(ef_mdf, ef_mdfn); QUEUE(ef_lin, ef_linn); QUEUE(ef_ntx, ef_ntxn);
    QUEUE(ef_trs, ef_trsn); QUEUE(ef_pnc, ef_pncn); QUEUE(ef_opq, ef_opqn); QUEUE(ef_thl, ef_thln);
    QUEUE(ef_ntx2d, ef_ntx2dn); QUEUE(ef_trs2d, ef_trs2dn); QUEUE(ef_pnc2d, ef_pnc2dn); QUEUE(ef_fnc, ef_fncn);
#undef QUEUE
    for (unsigned int i = 0; i < 6; ++i)
        if (ef_Zanzo[i] && !actor((O_WRK *)ef_Zanzo[i]))
            recvxGameBootUnsupported("invalid original effect afterimage work");
}
int recvxGameBootEffectDrawCallbackCount(void)
{
    if (sys->ef_fncn < 0 || sys->ef_fncn > 512)
        recvxGameBootUnsupported("invalid original effect callback count");
    return sys->ef_fncn;
}
void recvxGameBootEffectDrawCallbackPrepare(O_WRK *object)
{
    if (!actor(object) || !object->func)
        recvxGameBootUnsupported("invalid original effect draw callback");
}

static void matrix(const O_WRK *object)
{
    if (object->mtx != (NJS_MATRIX *)object->mtxbuf && !arena(object->mtx, sizeof(NJS_MATRIX)))
        recvxGameBootUnsupported("invalid polygon effect transform matrix");
}
static void model(const O_WRK *object)
{
    const ML_WORK *work = object->mlwP;
    if (!member(work, sys->efm, 450, sizeof(*work)) &&
        !member(work, object->mdl, 16, sizeof(*work)))
        recvxGameBootUnsupported("invalid polygon effect model work");
    if (!work->obj_num || work->obj_num > 512 || object->mdlver >= work->obj_num ||
        !arena(work->objP, work->obj_num * sizeof(NJS_CNK_OBJECT)))
        recvxGameBootUnsupported("invalid polygon effect model object");
}
void recvxGameBootEffectDrawObjectPrepare(O_WRK *object, int polygon)
{
    if (!actor(object)) recvxGameBootUnsupported("invalid original effect draw actor");
    if (hidden(object)) return;
    if (!polygon) {
        if (object->pn != 4 || (object->pvp != object->pv &&
            !arena(object->pvp, 4 * sizeof(NJS_POLYGON_VTX))))
            recvxGameBootUnsupported("invalid untextured 2D effect polygon");
        for (unsigned int i = 0; i < 4; ++i) {
            const NJS_POLYGON_VTX *v = &object->pvp[i];
            if (!isfinite(v->x) || !isfinite(v->y) || !isfinite(v->z))
                recvxGameBootUnsupported("invalid untextured 2D effect vertex");
            if (v->col != object->pvp[0].col || v->z != object->pvp[0].z)
                recvxGameBootUnsupported("unported untextured 2D effect gradient or depth");
        }
        return;
    }
    const float values[] = {object->px, object->py, object->pz, object->sx, object->sy, object->sz};
    for (unsigned int i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
        if (!isfinite(values[i])) recvxGameBootUnsupported("invalid polygon effect transform");
    matrix(object); model(object);
    if (!(object->stflg & 0x20u)) {
        if (object->tex_id < 0 || object->ani_ct < 0 ||
            (unsigned int)object->tex_id >= sys->ef_tlist.nbTexture ||
            (unsigned int)object->ani_ct >= sys->ef_tlist.nbTexture - (unsigned int)object->tex_id)
            recvxGameBootUnsupported("invalid polygon effect texture frame");
    } else if (!object->txp[0]) recvxGameBootUnsupported("missing polygon effect texture list");
    if ((object->flg & 0x80u) && !(object->flg & 0x200000u)) {
        const O_WRK *parent = (const O_WRK *)object->lkwkp;
        if (!actor(parent)) recvxGameBootUnsupported("invalid polygon effect parent work");
        model(parent);
        if (object->lkono < 0 || (unsigned int)object->lkono >= parent->mlwP->obj_num ||
            !arena(parent->mlwP->owP, parent->mlwP->obj_num * sizeof(O_WORK)) ||
            !isfinite(object->lox) || !isfinite(object->loy) || !isfinite(object->loz))
            recvxGameBootUnsupported("invalid polygon effect parent joint");
    }
}

void bhDrawThunder(void);
void recvxGameBootEffectDrawThunder(void) { bhDrawThunder(); }

/* Scalar equivalent of the original VU translation wrapper. */
void njTranslateEx(NJS_VECTOR *translation)
{
    if (!translation || !isfinite(translation->x) || !isfinite(translation->y) || !isfinite(translation->z))
        recvxGameBootUnsupported("invalid original vector translation");
    njTranslate(NULL, translation->x, translation->y, translation->z);
}
