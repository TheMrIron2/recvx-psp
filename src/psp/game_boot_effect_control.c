#include "psp/game_boot.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/effect.h"

static int first_update;
void recvxGameBootEffectControlReset(void) { first_update = 1; }
void recvxGameBootEffectUpdated(void)
{
    if (first_update && ((sys->sp_flg & 8u) || (sys->ef_flg & 1u))) {
        first_update = 0;
        recvxGameBootLog("effects updated: active=%d sprites=%d shadows=%d overlays=%d callbacks=%d",
            sys->ef_ctrn, sys->ef_trsn, sys->ef_mdfn, sys->ef_trs2dn, sys->ef_fncn);
    }
}

static int actor_array(const void *pointer, const void *base, size_t stride, size_t count)
{
    uintptr_t address = (uintptr_t)pointer, first = (uintptr_t)base;
    return base && address >= first && address - first < stride * count &&
        (address - first) % stride == 0;
}

void recvxGameBootEffectValidate(O_WRK *effect)
{
    BH_PWORK *parent;
    if (!(effect->flg & 0x80)) return;
    parent = (BH_PWORK *)effect->lkwkp;
    if (!parent || (parent != plp && !actor_array(parent, ene, sizeof(*ene), 128) &&
        !actor_array(parent, sys->obwp, sizeof(O_WRK), 39936u / sizeof(O_WRK)) &&
        !actor_array(parent, sys->itwp, sizeof(O_WRK), 39936u / sizeof(O_WRK)) &&
        !actor_array(parent, eff, sizeof(*eff), 512)))
        recvxGameBootUnsupported("effect links an invalid actor");
    if (((parent->flg & 1u) || effect->id == 1) && (!parent->mlwP || !parent->mlwP->owP ||
        effect->lkono < 0 || (unsigned int)effect->lkono >= parent->mlwP->obj_num))
        recvxGameBootUnsupported("effect links an invalid actor joint");
}

void recvxGameBootEffectQueueCheck(int *count, size_t capacity)
{
    if (*count < 0 || (size_t)*count >= capacity)
        recvxGameBootUnsupported("original effect render queue is full");
}

/* Preserve the original effect texture selection using owned descriptors.
   The reconstructed alternate-list cast in bhGetGidx is not a TEXLIST. */
unsigned int bhGetGidx(O_WRK *effect)
{
    NJS_TEXLIST *list;
    unsigned int index;
    if (effect->tex_id < 0 || effect->ani_ct < 0)
        recvxGameBootUnsupported("invalid original effect texture selection");
    if (effect->stflg & 0x20) {
        list = effect->txp[0];
        index = (unsigned int)effect->tex_id;
    } else {
        if (effect->tex_id >= 450 || sys->ef_tn[effect->tex_id] < 0)
            recvxGameBootUnsupported("invalid original effect texture mapping");
        list = &sys->ef_tlist;
        index = (unsigned int)sys->ef_tn[effect->tex_id];
    }
    index += (unsigned int)effect->ani_ct;
    if (!recvxGameBootTextureGet(list, index)->source)
        recvxGameBootUnsupported("effect texture has no owned surface");
    return ((NJS_TEXMEMLIST *)(uintptr_t)list->textures[index].texaddr)->globalIndex;
}

/* Original effect contexts are auxiliary slots in the same bounded pool. */
#include "psp/game_boot_effect_callbacks.h"
#include "ps2/veronica/prog/effsub3.h"
#include "ps2/veronica/prog/effsub6.h"
static void context(const void *pointer,size_t bytes)
{
    if(bytes>sizeof(O_WRK)||!actor_array(pointer,eff,sizeof(*eff),512)||
        !(((const O_WRK *)pointer)->flg&2u))
        recvxGameBootUnsupported("invalid effect auxiliary context");
}
void recvxGameBootEffectCallbackPrepare(unsigned int kind,O_WRK *e)
{
    if(!sys||!rom||!actor_array(e,eff,sizeof(*eff),512))
        recvxGameBootUnsupported("invalid effect callback actor");
    switch(kind){
    case 0:case 7:
        if(kind==0&&(sys->yk_ct==e->flr_no||e->py < -100))break;
        if(e->mode0>(kind==0?5:2))recvxGameBootUnsupported("invalid casing effect mode");
        if(!e->mode0){O_WRK linked=*e;linked.flg|=0x80;recvxGameBootEffectValidate(&linked);}
        if(kind==7&&!e->mode0&&e->mdlver){
            if(!sys->obwp||!sys->obwp->mlwP||sys->obwp->mlwP->obj_num<3||!sys->obwp->mlwP->objP)
                recvxGameBootUnsupported("invalid casing weapon model");
        }
        break;
    case 2:
        if(e->mode0>1||e->type>10||e->ct0<0||e->ct0>64||
            (e->mode0&&!e->exp0))recvxGameBootUnsupported("invalid muzzle effect state");
        break;
    case 300:{
        if(e->mode0>2)recvxGameBootUnsupported("invalid blood mesh effect mode");
        context(e->exp0,sizeof(E17_WORK));
        E17_WORK *w=(E17_WORK *)e->exp0;
        for(int i=0;i<5;++i)if(w->srt_vtx[i]<0||w->srt_vtx[i]>=16)
            recvxGameBootUnsupported("invalid blood mesh vertex index");
        if(e->lkwkp){O_WRK linked=*e;linked.flg|=0x80;recvxGameBootEffectValidate(&linked);}
        break;}
    case 301:
        if(e->type>=7||e->mode0>25||(e->mode0&&(e->ct2<0||e->ct2>=9)))recvxGameBootUnsupported("invalid blood pool effect state");break;
    case 302:{
        if(e->mode0>1||e->type>=20)recvxGameBootUnsupported("invalid staged particle mode/type");
        context(e->exp0,sizeof(E02_WORK));context(e->exp1,sizeof(E02_WRK));
        if(e->exp0==e->exp1||e->exp0==(unsigned char *)e||e->exp1==(unsigned char *)e)
            recvxGameBootUnsupported("overlapping staged particle contexts");
        if(e->mode0){
            E02_WORK *w=(E02_WORK *)e->exp0;E02_WRK *v=(E02_WRK *)e->exp1;
            if(w->stg_stt<0||w->stg_stt>6||w->stg_num<0||w->stg_num>6-w->stg_stt||
                w->vtx_num!=16*w->stg_num||w->stg_erase<=0)
                recvxGameBootUnsupported("invalid staged particle counts");
            for(int i=0;i<6;++i)if(w->stg_buf[i]!=&v->vtx_pos[i*16]||w->stg_spd[i]!=&w->vtx_spd[i*16])
                recvxGameBootUnsupported("invalid staged particle buffers");
        }
        break;}
    case 253:{
        static const unsigned int frames[6]={12,15,16,15,15,11};
        if(e->mode0>1||e->type>=3||e->mdlver>=2||(e->mode0&&(e->ct1<0||(unsigned int)e->ct1>=frames[e->mdlver*3+e->type])))
            recvxGameBootUnsupported("invalid sand dust animation");break;}
    case 254:case 1000:
        if(e->type>=3||e->mode0>1)recvxGameBootUnsupported("invalid sand particle state");
        if(e->mode0||kind==1000){context(e->exp0,sizeof(P_WRK));
            P_WRK *p=(P_WRK *)e->exp0;if(p->num<0||p->num>16)
                recvxGameBootUnsupported("invalid sand particle count");}
        break;
    case 255:if(e->mode0>1)recvxGameBootUnsupported("invalid sand emitter mode");break;
    }
}
