#include <math.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/player.h"

unsigned int ulDrawGeneralPurposeWater;

static int arena(const void *pointer, size_t bytes)
{
    uintptr_t first = (uintptr_t)freemem, last = (uintptr_t)sys->endp, p = (uintptr_t)pointer;
    return pointer && freemem && last >= first && !(p & 3u) && p >= first && p <= last && bytes <= last-p;
}

static int member(const void *pointer, const void *base, size_t count, size_t stride)
{
    uintptr_t p = (uintptr_t)pointer, first = (uintptr_t)base;
    return base && pointer && p >= first && p-first < count*stride && !((p-first)%stride);
}

/* Linked enemies and effect fragments select entries in another actor's mdl
   array. Recognize those fixed tables without accepting arbitrary arena data
   as a model descriptor. O_WRK and BH_PWORK share this part of their layout. */
_Static_assert(offsetof(O_WRK,mdl)==offsetof(BH_PWORK,mdl),"shared actor model table layout");
static int model_array(const ML_WORK *model,const void *base,size_t count,size_t stride)
{
    if(!base||!model)return 0;
    uintptr_t p=(uintptr_t)model,first=(uintptr_t)base+offsetof(O_WRK,mdl);
    if(p<first)return 0;
    uintptr_t offset=p-first;
    return offset/stride<count && offset%stride<sizeof(((O_WRK *)0)->mdl) &&
        !((offset%stride)%sizeof(ML_WORK));
}
static int registered_model(const ML_WORK *model)
{
    return (plp&&member(model,plp->mdl,16,sizeof(*model))) ||
        model_array(model,ene,128,sizeof(*ene)) ||
        model_array(model,eff,512,sizeof(*eff)) ||
        member(model,sys->efm,450,sizeof(*model)) ||
        ((model_array(model,sys->obwp,39936u/sizeof(O_WRK),sizeof(O_WRK)) ||
          model_array(model,sys->itwp,39936u/sizeof(O_WRK),sizeof(O_WRK))) && arena(model,sizeof(*model)));
}

static void actor(const O_WRK *work)
{
    if (!work || (work != (const O_WRK *)plp && !member(work, ene, 128, sizeof(*ene)) &&
        !member(work, eff, 512, sizeof(*eff)) &&
        !(member(work, sys->obwp, 39936u/sizeof(O_WRK), sizeof(O_WRK)) && arena(work,sizeof(*work))) &&
        !(member(work, sys->itwp, 39936u/sizeof(O_WRK), sizeof(O_WRK)) && arena(work,sizeof(*work)))))
        recvxGameBootUnsupported("invalid original draw actor");
}

static void base(void)
{
    if (!sys || !rom || !plp || sys->ewk_n < 0 || sys->ewk_n > 128 ||
        sys->en_objn < 0 || sys->en_objn > 16)
        recvxGameBootUnsupported("invalid original draw state");
    const char indices[] = {rom->amb_rom, rom->amb_chr, rom->amb_obj, rom->amb_itm};
    for (unsigned int i=0;i<4;++i) {
        if (indices[i]<0 || indices[i]>=4 || !isfinite(rom->amb_r[(unsigned char)indices[i]]) ||
            !isfinite(rom->amb_g[(unsigned char)indices[i]]) || !isfinite(rom->amb_b[(unsigned char)indices[i]]))
            recvxGameBootUnsupported("invalid original draw ambient");
    }
}

void recvxGameBootDrawActorPrepare(BH_PWORK *work, int drawing)
{
    base(); actor((const O_WRK *)work);
    const ML_WORK *model = work->mlwP;
    int registered=registered_model(model);
    if (!registered || model->obj_num > 512 ||
        (model->obj_num && (!arena(model->objP, model->obj_num*sizeof(*model->objP)) ||
         !arena(model->owP, model->obj_num*sizeof(*model->owP))))) {
        recvxGameBootLog("draw model rejected: actor=%p id=%u type=%u model=%p registered=%d objects=%u nodes=%p poses=%p drawing=%d",
            (void *)work,work->id,work->type,(const void *)model,registered,
            registered?model->obj_num:0,registered?(void *)model->objP:NULL,
            registered?(void *)model->owP:NULL,drawing);
        recvxGameBootUnsupported("invalid original draw model work");
    }
    if (drawing && (work->draw_tp<0 || work->draw_tp>=4 || work->mdl_no>=16))
        recvxGameBootUnsupported("invalid original model draw selector");
    if (!drawing) {
        for (unsigned int i=0;i<8 && work->clp_jno[i]!=-1;++i)
            if (work->clp_jno[i]<0 || (unsigned int)work->clp_jno[i]>=model->obj_num)
                recvxGameBootUnsupported("invalid original model clip joint");
    }
    if (!isfinite(work->px) || !isfinite(work->py) || !isfinite(work->pz))
        recvxGameBootUnsupported("invalid original draw actor position");
}

void recvxGameBootDrawObjectPrepare(O_WRK *work)
{
    base(); actor(work);
    if (work->flg & 0x80u) actor((const O_WRK *)work->lkwkp);
}

void recvxGameBootDrawSpecialPrepare(O_WRK *work)
{
    recvxGameBootDrawObjectPrepare(work);
    if (work->type == 5 && (work->aspd<0 || work->aspd>=rom->lgt_n ||
        !arena(rom->lgtp, (size_t)rom->lgt_n*sizeof(*rom->lgtp))))
        recvxGameBootUnsupported("invalid original special draw light");
}

void recvxGameBootDrawPrepare(int group)
{
    base();
    if (group==0) return;
    if (group==1 || group==2) {
        int count=group==1 ? sys->ewk_n : sys->en_objn;
        for (int i=0;i<count;++i) {
            BH_PWORK *enemy=group==1 ? &ene[i] : sys->en_obj[i];
            actor((const O_WRK *)enemy);
            if ((enemy->flg&1u) && !(enemy->stflg&0x41000000u)) {
                if (enemy->flg&0x80u) actor((const O_WRK *)enemy->lkwkp);
                recvxGameBootDrawActorPrepare(enemy, 0);
            }
        }
        return;
    }
    if (sys->ob_nlgn<0 || sys->ob_nlgn>32 || sys->ob_hlgn<0 || sys->ob_hlgn>32 ||
        sys->ob_spcn<0 || sys->ob_spcn>32 || rom->itm_n<0 ||
        (size_t)rom->itm_n>39936u/sizeof(O_WRK) ||
        (rom->itm_n && !arena(sys->itwp,(size_t)rom->itm_n*sizeof(O_WRK))))
        recvxGameBootUnsupported("invalid original object draw queues");
    if (sys->pt_flg&4u) {
        for (int i=0;i<sys->ob_nlgn;++i) actor(sys->ob_nlg[i]);
        for (int i=0;i<sys->ob_hlgn;++i) actor(sys->ob_hlg[i]);
        for (int i=0;i<sys->ob_spcn;++i) actor(sys->ob_spc[i]);
    }
}
