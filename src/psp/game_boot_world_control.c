#include <math.h>
#include <string.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/objitm.h"
#include "ps2/veronica/prog/player.h"
#include "ps2/veronica/prog/system.h"

static int owned(const void *pointer, size_t size)
{
    uintptr_t first = (uintptr_t)freemem, last = (uintptr_t)sys->endp, p = (uintptr_t)pointer;
    return pointer && freemem && last >= first && !(p & 3u) && p >= first && p <= last && size <= last - p;
}

static int element(const void *pointer, const void *base, size_t count, size_t stride)
{
    uintptr_t p = (uintptr_t)pointer, first = (uintptr_t)base;
    return base && p >= first && p - first < count * stride && (p - first) % stride == 0;
}

static BH_PWORK *parent(O_WRK *op)
{
    BH_PWORK *p = (BH_PWORK *)op->lkwkp;
    if (!p || (p != plp && !element(p, ene, 128, sizeof(*ene)) &&
        !element(p, sys->obwp, 39936u / sizeof(O_WRK), sizeof(O_WRK)) &&
        !element(p, sys->itwp, 39936u / sizeof(O_WRK), sizeof(O_WRK)) &&
        !element(p, eff, 512, sizeof(*eff))))
        recvxGameBootUnsupported("invalid object parent work");
    return p;
}

void recvxGameBootWorldModelPrepare(O_WRK *op)
{
    ML_WORK *m = op->mlwP;
    const float values[] = {op->px, op->py, op->pz, op->lox, op->loy, op->loz};
    if (!element(m, op->mdl, 16, sizeof(ML_WORK)) || m->obj_num > 512 ||
        (m->obj_num && (!owned(m->objP, m->obj_num * sizeof(*m->objP)) ||
                        !owned(m->owP, m->obj_num * sizeof(*m->owP)))) ||
        (op->mtx != (void *)op->mtxbuf && !owned(op->mtx, sizeof(NJS_MATRIX))))
        recvxGameBootUnsupported("invalid object model work");
    for (unsigned int i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
        if (!isfinite(values[i])) recvxGameBootUnsupported("invalid object transform");
}

static void linked_model(O_WRK *op)
{
    BH_PWORK *p = parent(op);
    recvxGameBootWorldModelPrepare((O_WRK *)p);
    if (op->lkono < 0 || (unsigned int)op->lkono >= p->mlwP->obj_num)
        recvxGameBootUnsupported("invalid object parent joint");
}

void recvxGameBootWorldPrepare(void)
{
    int count = (sys->cb_flg & 0x40000000u) ? 4 : rom ? rom->obj_n : -1;
    size_t capacity = 39936u / sizeof(O_WRK);
    if (!rom || count < 0 || (size_t)count > capacity ||
        (count && !owned(sys->obwp, (size_t)count * sizeof(O_WRK))) ||
        (!(sys->cb_flg & 0x40000000u) && (rom->itm_n < 0 || (size_t)rom->itm_n > capacity ||
        (rom->itm_n && !owned(sys->itwp, (size_t)rom->itm_n * sizeof(O_WRK))))))
        recvxGameBootUnsupported("invalid room object/item work tables");
}

void recvxGameBootWorldObjectPrepare(O_WRK *op, int item)
{
    unsigned int sleeping = op->stflg & 0x1000000u;
    if (!item && (op->flg & 0x80u)) sleeping = parent(op)->stflg & 0x1000000u;
    if (!(op->flg & 1u) || (sleeping && (item || !pl_sleep_cnt))) return;
    if (!item && ((op->id >= 1200 && op->id >= 1213) || (op->id < 1200 && op->type >= 101)))
        recvxGameBootUnsupported("invalid original object callback index");
    recvxGameBootWorldModelPrepare(op);
    if (op->flg & 0x80u) linked_model(op);
}

void recvxGameBootWorldQueueCheck(int *count, size_t capacity)
{
    if (*count < 0 || (size_t)*count >= capacity)
        recvxGameBootUnsupported("original object render queue is full");
}

void recvxGameBootWorldWallPrepare(O_WRK *op)
{
    if (op->mode0 == 0) {
        if (op->flg & 0x100000u) return;
        if (sys->mwal_n < 0 || sys->mwal_n >= 64 || op->flr_no < -2 || op->flr_no > 29 ||
            !isfinite(rom->grand[op->flr_no + 2]))
            recvxGameBootUnsupported("invalid linked object wall allocation");
    } else {
        recvxGameBootWorldModelPrepare(op);
        if (op->ct0 < 0 || op->ct0 >= sys->mwal_n || op->ct0 >= 64 || !op->mlwP->obj_num)
            recvxGameBootUnsupported("invalid linked object wall index");
    }
}

void recvxGameBootWorldHairPrepare(O_WRK *op)
{
    BH_PWORK *p;
    if (!(op->flg & 0x80u) || op->mode0 > 1) return;
    linked_model(op); p = parent(op);
    recvxGameBootWorldModelPrepare(op);
    if (op->mlwP->obj_num < 4 || p->mlwP->obj_num < 6)
        recvxGameBootUnsupported("invalid linked hair model");
    if (op->mode0 == 0 && !(op->flg & 0x100000u)) {
        if (p == plp && !owned(sys->pletcp, 4096 + 0x4000))
            recvxGameBootUnsupported("invalid player hair allocation");
    } else if (!owned(op->exp0, 0x4000) || !owned(op->exp3, sizeof(HAIR_WORK)) ||
        (op->mode0 == 1 && (op->ct0 < 0 || op->ct0 >= 128)))
        recvxGameBootUnsupported("invalid linked hair state");
}

/* The searchlight billboard uses three consecutive model joints and may
   drive a room event-light entry selected by the authored animation speed. */
void recvxGameBootWorldSearchLightPrepare(O_WRK *op)
{
    recvxGameBootWorldModelPrepare(op);
    if(op->mlwP->obj_num<3||!isfinite(cam.wpx)||!isfinite(cam.wpy)||!isfinite(cam.wpz))
        recvxGameBootUnsupported("invalid original searchlight model or camera");
    if(op->aspd>0&&(!rom||rom->evl_n<=op->aspd||rom->evl_n>512||
        !owned(rom->evlp,(size_t)rom->evl_n*sizeof(*rom->evlp))))
        recvxGameBootUnsupported("invalid original searchlight event light");
}

void recvxGameBootWorldAlphaPrepare(O_WRK *op)
{
    recvxGameBootWorldModelPrepare(op);
    int joints[2];memcpy(joints,&op->pv[0],sizeof(joints));
    if(joints[0]<0||joints[1]<0||(unsigned int)joints[0]>op->mlwP->obj_num||
       (unsigned int)joints[1]>op->mlwP->obj_num-(unsigned int)joints[0]||
       !isfinite(op->pv[1].x)||!isfinite(op->pv[1].y))
        recvxGameBootUnsupported("invalid original object alpha fade");
    /* Zero-duration requests set the target in this update without dividing
       by zero in the original interpolation. */
    if(op->pv[1].y<=0)op->pv[1].y=1;
    for(int i=0;i<joints[1];++i){
        NJS_CNK_OBJECT *object=&op->mlwP->objP[joints[0]+i];
        if(object->model&&!(object->evalflags&8)&&
           (!owned(object->model,sizeof(*object->model))||!owned(object->model->plist,8)))
            recvxGameBootUnsupported("invalid original object alpha material");
    }
}
