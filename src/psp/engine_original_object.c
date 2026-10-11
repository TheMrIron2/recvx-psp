#include <string.h>

#include "ps2/veronica/prog/types.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/objitm.h"
#include "ps2/veronica/prog/njplus.h"

// Extracted from original objitm.c object/work setup
void bhInitObjItm(void)
{
    npSetMemory((unsigned char *)sys->obwp, 39936, 0);
    npSetMemory((unsigned char *)sys->itwp, 39936, 0);
}

// Extracted from original objitm.c::bhSetObject
O_WRK *bhSetObject(ETTY_WORK *otp, int no, unsigned char *lkp)
{
    O_WRK *opp = &sys->obwp[no];

    npSetMemory((unsigned char *)opp, sizeof(O_WRK), 0);
    opp->flg = otp->flg;
    if (opp->flg & 0x80000000)
        opp->mdflg |= 0x10;
    if (opp->flg & 0x20000000)
        opp->mdflg |= 0x40;
    opp->id = otp->id;
    opp->type = (unsigned char)otp->type;
    opp->param = otp->type >> 8;
    opp->flr_no = otp->flr_no;
    opp->mdlver = otp->mdlver;
    opp->draw_tp = otp->prm1;
    opp->px = otp->px;
    opp->py = otp->py;
    opp->pz = otp->pz;
    opp->ax = otp->ax;
    opp->ay = otp->ay;
    opp->az = otp->az;
    opp->aspd = otp->aspd;
    opp->sx = opp->sxb = 1.0f;
    opp->sy = opp->syb = 1.0f;
    opp->sz = opp->szb = 1.0f;
    memcpy(opp->hide, otp->hide, sizeof(opp->hide));
    opp->lkwkp = lkp;
    opp->mtx = (void *)opp->mtxbuf;
    opp->clp_jno[0] = 0;
    opp->clp_jno[1] = -1;
    opp->idx_ct = no;
    return opp;
}

// Extracted from original objitm.c::bhSetItem
O_WRK *bhSetItem(ETTY_WORK *itp, int no, unsigned char *lkp)
{
    O_WRK *opp = &sys->itwp[no];

    npSetMemory((unsigned char *)opp, sizeof(O_WRK), 0);
    opp->flg = itp->flg;
    if (opp->flg & 0x80000000)
        opp->mdflg |= 0x10;
    if (opp->flg & 0x20000000)
        opp->mdflg |= 0x40;
    opp->id = itp->id;
    opp->type = (unsigned char)itp->type;
    opp->param = itp->type >> 8;
    opp->flr_no = itp->flr_no;
    opp->mdlver = itp->mdlver;
    opp->draw_tp = itp->prm1;
    opp->px = itp->px;
    opp->py = itp->py;
    opp->pz = itp->pz;
    opp->ax = itp->ax;
    opp->ay = itp->ay;
    opp->az = itp->az;
    opp->aspd = itp->aspd;
    opp->sx = opp->sxb = 1.0f;
    opp->sy = opp->syb = 1.0f;
    opp->sz = opp->szb = 1.0f;
    memcpy(opp->hide, itp->hide, sizeof(opp->hide));
    opp->lkwkp = lkp;
    opp->mtx = (void *)opp->mtxbuf;
    opp->clp_jno[0] = 0;
    opp->clp_jno[1] = -1;
    opp->idx_ct = no;
    return opp;
}
