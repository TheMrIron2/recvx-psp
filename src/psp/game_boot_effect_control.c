#include <limits.h>
#include <string.h>
#include "psp/game_boot_skin.h"
#include "psp/game_boot_morph.h"
#include "ps2/veronica/prog/effsub5.h"
#include "ps2/veronica/prog/effsub4.h"
#include "ps2/veronica/prog/njplus.h"
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
#include "psp/game_boot_player_remaining.h"
#include "ps2/veronica/prog/effsub1.h"
#include "ps2/veronica/prog/effsub3.h"
#include "ps2/veronica/prog/effsub6.h"
static void context(const void *pointer,size_t bytes)
{
    if(bytes>sizeof(O_WRK)||!actor_array(pointer,eff,sizeof(*eff),512)||
        !(((const O_WRK *)pointer)->flg&2u))
        recvxGameBootUnsupported("invalid effect auxiliary context");
}
static int effect_arena(const void *pointer,size_t bytes)
{
    uintptr_t p=(uintptr_t)pointer,b=(uintptr_t)freemem,end=sys?(uintptr_t)sys->endp:0;
    return pointer && freemem && !(p&3u) && p>=b && p<=end && bytes<=end-p;
}
/* Snapshot the actual stream, including its terminator. Skin outputs have no
   retail scratch padding; copying the reconstructed fixed +72 would overread. */
static size_t effect_source(const void *pointer)
{
    size_t n=recvxGameBootSkinRemaining(pointer);
    if(n)return n;
    n=recvxGameBootMorphRemaining(pointer);if(n)return n;
    if(!effect_arena(pointer,0))return 0;
    n=(uintptr_t)sys->endp-(uintptr_t)pointer;
    size_t source=recvxGameBootSourceRemaining(pointer);
    return source&&source<n?source:n;
}
size_t recvxGameBootEffectVlistBytes(const int *stream)
{
    const unsigned char *p=(const unsigned char *)stream;
    size_t left=effect_source(p),total=0;
    for(unsigned int i=0;i<65536;++i){
        if(left<4)break;
        if(p[0]==255)return total+4;
        size_t bytes=4u+((size_t)p[2]+((size_t)p[3]<<8))*4u;
        if((p[0]!=41&&p[0]!=51)||bytes<12||bytes>left)break;
        total+=bytes;p+=bytes;left-=bytes;
    }
    recvxGameBootUnsupported("invalid afterimage vertex stream");
    return 0;
}
int npCopyVlist(int *destination,int *source)
{
    size_t bytes=recvxGameBootEffectVlistBytes(source);
    if(!effect_arena(destination,bytes))recvxGameBootUnsupported("invalid afterimage snapshot destination");
    memcpy(destination,source,bytes);return (int)bytes;
}
void npSetAllMatAlphaColor(NJS_CNK_OBJECT *objects,int count,unsigned char alpha)
{
    if(count<0||count>512||!effect_arena(objects,(size_t)count*sizeof(*objects)))
        recvxGameBootUnsupported("invalid afterimage material objects");
    for(int i=0;i<count;++i){
        if(!objects[i].model||(objects[i].evalflags&8))continue;
        if(!effect_arena(objects[i].model,sizeof(NJS_CNK_MODEL)))
            recvxGameBootUnsupported("invalid afterimage material model");
        unsigned char *p=(unsigned char *)objects[i].model->plist;
        size_t left=effect_source(p);
        for(unsigned int commands=0;;++commands){
            if(commands>=65536||left<2)recvxGameBootUnsupported("invalid afterimage material stream");
            unsigned int type=p[0];if(type==255)break;
            size_t bytes=2;
            if(type){
                if(left<4)recvxGameBootUnsupported("truncated afterimage material command");
                if((type>=1&&type<=3)||type==8)bytes=4;
                else if((type>=17&&type<=23)||(type>=56&&type<=58)||(type>=64&&type<=66))
                    bytes=4u+((size_t)p[2]+((size_t)p[3]<<8))*2u;
                else recvxGameBootUnsupported("unsupported afterimage material command");
            }
            if(bytes>left)recvxGameBootUnsupported("afterimage material exceeds stream");
            if(type==17||type==19||type==21||type==23){
                size_t required=(type==19||type==23)?12:8;
                if(bytes<required)recvxGameBootUnsupported("truncated afterimage alpha material");
                p[7]=alpha;if(required==12)p[11]=alpha;
            }
            p+=bytes;left-=bytes;
        }
    }
}
static ML_WORK *afterimage_model(O_WRK *e,unsigned int slot)
{
    O_WRK *parent=(O_WRK *)e->lkwkp;
    if(!parent||(parent!=(O_WRK *)plp&&!actor_array(parent,ene,sizeof(*ene),128)&&
       !actor_array(parent,eff,sizeof(*eff),512)))
        recvxGameBootUnsupported("invalid afterimage parent");
    if(slot>=16)recvxGameBootUnsupported("invalid afterimage model slot");
    ML_WORK *m=&parent->mdl[slot];
    if(!m->obj_num||m->obj_num>128||e->lkono<0||(unsigned int)e->lkono>=m->obj_num||
       !effect_arena(m->objP,m->obj_num*sizeof(*m->objP))||
       !effect_arena(m->owP,m->obj_num*sizeof(*m->owP))||
       !effect_arena(m->objP[e->lkono].model,sizeof(NJS_CNK_MODEL)))
        recvxGameBootUnsupported("invalid afterimage model joint");
    (void)recvxGameBootEffectVlistBytes(m->objP[e->lkono].model->vlist);
    return m;
}
static void snow_context(O_WRK *e)
{
    context(e->exp0,sizeof(EFF5SNOWRECT));
    EFF5SNOWRECT *s=(EFF5SNOWRECT *)e->exp0;
    if(s->lSnowTotalMax<1||s->lSnowTotalMax>2000||s->lSnowGrpMax<1||s->lSnowGrpMax>20||
       s->lSnowExistCrnt<0||s->lSnowExistCrnt>s->lSnowTotalMax||
       s->lSnowExistNext<0||s->lSnowExistNext>s->lSnowTotalMax||
       s->lSnowExistPrev<0||s->lSnowExistPrev>s->lSnowTotalMax||
       (s->lSnowExistCrnt!=s->lSnowExistNext&&(s->lSnowTimerMax<=0||s->lSnowTimerCnt<0||s->lSnowTimerCnt>=s->lSnowTimerMax)))
        recvxGameBootUnsupported("invalid snow particle counts/timer");
    if(!isfinite(s->fAreaSizeX)||!isfinite(s->fAreaSizeY)||!isfinite(s->fAreaSizeZ)||
       s->fAreaSizeX<=0||s->fAreaSizeY<=0||s->fAreaSizeZ<=0)
        recvxGameBootUnsupported("invalid snow particle area");
    int total=0;
    for(int i=0;i<s->lSnowGrpMax;++i){
        EFF5SNOWGRP *g=&s->SnowGrp[i];
        if(g->lPointMax<1||g->lPointMax>100||!g->pPointTop)
            recvxGameBootUnsupported("invalid snow particle group");
        context((const unsigned char *)g->pPointTop-4,4+g->lPointMax*sizeof(NJS_POINT3));
        total+=g->lPointMax;
    }
    if(total!=s->lSnowTotalMax||!s->pSnowVector)recvxGameBootUnsupported("invalid snow particle capacity");
    context((const unsigned char *)s->pSnowVector-4,4+100*sizeof(NJS_POINT3));
}
static int effect_span(const void *pointer,size_t bytes)
{
    uintptr_t p=(uintptr_t)pointer,b=(uintptr_t)eff;
    if(pointer&&!(p&3u)&&p>=b&&p-b<sizeof(eff)&&bytes<=sizeof(O_WRK)-(p-b)%sizeof(O_WRK))return 1;
    b=(uintptr_t)ene;
    if(pointer&&!(p&3u)&&p>=b&&p-b<sizeof(ene)&&bytes<=sizeof(BH_PWORK)-(p-b)%sizeof(BH_PWORK))return 1;
    b=(uintptr_t)plp;
    return (plp&&p>=b&&p-b<=sizeof(*plp)&&bytes<=sizeof(*plp)-(p-b))||effect_arena(pointer,bytes);
}
static void remaining_parent(O_WRK *e,int joint)
{
    O_WRK copy=*e;copy.flg|=0x80;
    recvxGameBootEffectValidate(&copy);
    O_WRK *p=(O_WRK *)e->lkwkp;
    if(joint>=0&&(!p->mlwP||!effect_span(p->mlwP,sizeof(ML_WORK))||
       (unsigned int)joint>=p->mlwP->obj_num||!effect_arena(p->mlwP->owP,p->mlwP->obj_num*sizeof(O_WORK))))
        recvxGameBootUnsupported("invalid remaining effect parent pose");
}
static void remaining_effect_prepare(unsigned int kind,O_WRK *e)
{
    unsigned int max_mode;
    switch(kind){
    case 222:max_mode=1;break;
    case 223:max_mode=1;break;
    case 224:max_mode=255;break;
    case 225:max_mode=1;break;
    case 226:max_mode=1;break;
    case 227:max_mode=2;break;
    case 228:max_mode=1;break;
    case 229:max_mode=2;break;
    case 230:max_mode=1;break;
    case 231:max_mode=255;break;
    case 232:max_mode=1;break;
    case 233:max_mode=1;break;
    case 234:max_mode=2;break;
    case 235:max_mode=1;break;
    case 236:max_mode=1;break;
    case 237:max_mode=1;break;
    case 238:max_mode=2;break;
    case 239:max_mode=1;break;
    case 240:max_mode=255;break;
    case 241:max_mode=255;break;
    case 242:max_mode=1;break;
    case 243:max_mode=1;break;
    case 244:max_mode=1;break;
    case 245:max_mode=1;break;
    case 246:max_mode=2;break;
    case 247:max_mode=255;break;
    case 248:max_mode=1;break;
    case 249:max_mode=1;break;
    case 303:max_mode=3;break;
    case 304:max_mode=255;break;
    case 305:max_mode=1;break;
    case 306:max_mode=4;break;
    case 307:max_mode=2;break;
    case 308:max_mode=1;break;
    case 309:max_mode=1;break;
    case 310:max_mode=2;break;
    case 311:max_mode=1;break;
    case 312:max_mode=1;break;
    case 349:max_mode=255;break;
    case 99:max_mode=1;break;
    case 250:max_mode=2;break;
    case 251:max_mode=2;break;
    case 252:max_mode=3;break;
    case 257:max_mode=1;break;
    case 298:max_mode=2;break;
    case 297:max_mode=2;break;
    case 296:max_mode=1;break;
    case 272:max_mode=1;break;
    case 256:max_mode=3;break;
    case 258:max_mode=1;break;
    case 259:max_mode=1;break;
    case 260:max_mode=1;break;
    case 261:max_mode=255;break;
    case 270:max_mode=1;break;
    case 263:max_mode=6;break;
    case 265:max_mode=4;break;
    case 266:max_mode=3;break;
    case 262:max_mode=1;break;
    case 274:max_mode=3;break;
    case 275:max_mode=1;break;
    case 276:max_mode=3;break;
    case 267:max_mode=2;break;
    case 268:max_mode=1;break;
    case 269:max_mode=4;break;
    case 271:max_mode=1;break;
    case 273:max_mode=3;break;
    case 299:max_mode=2;break;
    case 350:max_mode=3;break;
    case 351:max_mode=2;break;
    case 352:max_mode=3;break;
    case 353:max_mode=4;break;
    case 354:max_mode=2;break;
    case 355:max_mode=2;break;
    case 356:max_mode=5;break;
    case 360:max_mode=2;break;
    case 361:max_mode=2;break;
    case 362:max_mode=3;break;
    case 363:max_mode=2;break;
    case 364:max_mode=1;break;
    case 365:max_mode=2;break;
    case 366:max_mode=2;break;
    case 367:max_mode=1;break;
    case 368:max_mode=3;break;
    case 369:max_mode=2;break;
    case 370:max_mode=1;break;
    case 371:max_mode=2;break;
    case 372:max_mode=2;break;
    case 373:max_mode=2;break;
    case 398:max_mode=1;break;
    default:return;
    }
    if(kind>=307&&kind<=312){
        OR_WORK *o=(OR_WORK *)e;
        if(kind==307){R07_WORK *r=(R07_WORK *)o->free4;
            if(r->mode0<0||r->mode0>2||r->vtx_num<0||r->vtx_num>17||(!r->mode0&&r->eff_prm.tim<=0)||!effect_span(r->eff_prm.mtxP,sizeof(NJS_MATRIX))||
               !isfinite(r->eff_prm.frm_inc)||r->eff_prm.frm_inc<=0||r->eff_prm.frm_inc>1)
                recvxGameBootUnsupported("invalid spline trail state");
        }else if(kind==308){R08_WORK *r=(R08_WORK *)o->free4;
            if(r->mode<0||r->mode>1||r->prm_a.vtx_num<1||r->prm_a.vtx_num>16||r->drw_num<0||r->drw_num>16)
                recvxGameBootUnsupported("invalid spark particle state");
        }else if(kind==309){R09_WORK *r=(R09_WORK *)o->free4;
            if(r->mode<0||r->mode>1)recvxGameBootUnsupported("invalid blood ribbon mode");
        }else if(kind==310){R0A_WORK *r=(R0A_WORK *)o->free4;
            if(r->mode<0||r->mode>2||r->prm_a.type<0||r->prm_a.type>2)
                recvxGameBootUnsupported("invalid camera shake state");
        }else if(kind==311){R0B_WORK *r=(R0B_WORK *)o->free4;
            if(r->mode<0||r->mode>1||r->prm_a.vtx_num<1||r->prm_a.vtx_num>512*28||
               !effect_span(r->prm_b.vtx_mtxP,sizeof(NJS_MATRIX)))
                recvxGameBootUnsupported("invalid blood mist state");
            if(r->mode){eff30b_vtx_buf_typ *p=r->vtx_bufP;unsigned int visited=0;
                for(;p;p=p->nextP){if(++visited>512)recvxGameBootUnsupported("cyclic blood mist buffers");context(p,sizeof(*p));}}
        }else{R0_WK *r=(R0_WK *)o->free4;
            if(r->mode<0||r->mode>1||!effect_span(r->prm.mtxP,sizeof(NJS_MATRIX))||
               !effect_arena(r->prm.mdlP,sizeof(NJS_CNK_MODEL)))
                recvxGameBootUnsupported("invalid attached effect model");
        }
        return;
    }
    if(kind==349)return; /* Authored registered function callbacks in free4. */
    if(e->mode0>max_mode)recvxGameBootUnsupported("invalid remaining effect mode");
    const float values[]={e->px,e->py,e->pz,e->sx,e->sy,e->sz,e->sxb,e->syb,e->szb,e->spd};
    for(unsigned int i=0;i<sizeof(values)/sizeof(*values);++i)
        if(!isfinite(values[i]))recvxGameBootUnsupported("non-finite remaining effect state");
    if(e->lkwkp)remaining_parent(e,-1);
    if(kind==304){
        if(!effect_span((void *)(uintptr_t)(unsigned int)e->ct0,sizeof(NJS_POINT3))||
           !effect_span((void *)(uintptr_t)(unsigned int)e->ct1,sizeof(NJS_POINT3))||
           !effect_span((void *)(uintptr_t)(unsigned int)e->ct2,sizeof(NJS_POINT3)))
            recvxGameBootUnsupported("invalid shadow contact points");
        remaining_parent(e,-1);return;
    }
    if(e->ct0>INT_MAX/1024||e->ct0<INT_MIN/1024||(kind!=305&&(e->ct1>INT_MAX/1024||e->ct1<INT_MIN/1024)))
        recvxGameBootUnsupported("invalid remaining effect counter");
    if((kind==227||kind==248)&&!actor_array(e->exp0,eff,sizeof(*eff),512))
        recvxGameBootUnsupported("invalid remaining effect emitter link");
    if((kind==223)&&(e->sz<=0||7.5/e->sz>INT_MAX||e->sz>1e7f||(e->mode0&&e->ct1<1)))
        recvxGameBootUnsupported("invalid moving sprite lifetime");
    if((kind==233||kind==234||kind==235||kind==244)&&fabsf(e->sz)>INT_MAX/32.0f)
        recvxGameBootUnsupported("invalid sprite animation duration");
    if(kind==236){
        if(e->lkono<=0||e->lkono>INT_MAX/32||e->ax<0||(e->ax!=65535&&(rom->ene_n<=0||
           !effect_arena(rom->enep,rom->ene_n*sizeof(*rom->enep)))))
            recvxGameBootUnsupported("invalid enemy-attached sprite duration/placement");
        unsigned int n=e->ax==65535?0:rom->enep[e->ax%rom->ene_n].wrk_no;
        if(n>=128)recvxGameBootUnsupported("invalid enemy-attached sprite actor");
    }
    if((kind==240)&&(e->type||e->mode1)&&(e->lkono<1||e->lkono>512))
        recvxGameBootUnsupported("invalid debris burst count");
    if(kind==249&&(e->lkono<=0||e->lkono>INT_MAX/32))
        recvxGameBootUnsupported("invalid oriented sprite duration");
    if(kind==261){remaining_parent(e,4);if(e->mode0||e->exp0)context(e->exp0,4+16*sizeof(NJS_POINT3));}
    if(kind==270)remaining_parent(e,e->lkono);
    if(kind==260&&e->mode0){context(e->exp0,sizeof(P_WRK));if(((P_WRK *)e->exp0)->num<1||((P_WRK *)e->exp0)->num>48)recvxGameBootUnsupported("invalid pollen particle count");}
    if(kind==262&&(e->mode0||e->exp0))context(e->exp0,sizeof(O_WRK));
    if(kind==263&&e->type&&e->mode0<5){
        if(!actor_array(e->exp1,eff,sizeof(*eff),512))recvxGameBootUnsupported("invalid fire manager link");
        O_WRK *manager=(O_WRK *)e->exp1;context(manager->exp0,sizeof(O_WRK));
        if(manager->ct0<0||manager->ct0>300)recvxGameBootUnsupported("invalid fire manager count");
    }
    if((kind==263||kind==269)&&e->type==1&&!effect_arena(rom->lgtp,2*sizeof(LGT_WORK)))
        recvxGameBootUnsupported("invalid fire light work");
    if(kind==306&&(e->mode0==2||e->mode0==3))remaining_parent(e,e->lkono);
    if(kind==355){O_WRK copy=*e;copy.lkwkp=e->exp0;copy.flg|=0x80;recvxGameBootEffectValidate(&copy);}
    if(kind==356||kind==370||kind==297||kind==354||(kind==352&&(sys->pt_flg&2u)))remaining_parent(e,e->lkono);
    if(kind==365||kind==366||kind==367){
        context(e->exp0,kind==365?sizeof(EFFSUB365_WORK):kind==366?sizeof(EFFSUB366_WORK):sizeof(EFFSUB367_WORK));
        D_WORK *d=(D_WORK *)e->exp0;
        if(d->num>(kind==365?40:kind==366?16:32))recvxGameBootUnsupported("invalid effect particle count");
    }
}
void recvxGameBootEffectAnimationPrepare(O_WRK *e,const EFF_ANIM *animation,unsigned int frames,unsigned int uv_count)
{
    if(e->ct0<0||e->ct0==INT_MAX||e->ct1<0||(unsigned int)e->ct1>=frames)
        recvxGameBootUnsupported("invalid particle sprite animation frame");
    const EFF_ANIM *frame=&animation[e->ct1];
    if(frame->ptrn==-1)return;
    if(frame->ptrn==-2){
        if(frame->timer<0||(unsigned int)frame->timer>=frames)
            recvxGameBootUnsupported("invalid particle sprite animation loop");
        frame=&animation[frame->timer];
    }
    if(frame->ptrn<0||(frame->ptrn<100&&(unsigned int)frame->ptrn>=uv_count)||frame->timer<=0)
        recvxGameBootUnsupported("invalid particle sprite animation pattern");
}
void recvxGameBootEffectRemainingDrawPrepare(O_WRK *e)
{
    if(!sys||!actor_array(e,eff,sizeof(*eff),512))recvxGameBootUnsupported("invalid remaining effect draw actor");
    remaining_effect_prepare(e->id,e);
}
void recvxGameBootEffectCustomDrawPrepare(unsigned int kind,O_WRK *e)
{
    if(!sys||!actor_array(e,eff,sizeof(*eff),512))
        recvxGameBootUnsupported("invalid custom effect draw actor");
    if(kind==134||kind==136||kind==137||kind==138||kind==143||kind==219||kind==221){
        recvxGameBootEffectCallbackPrepare(kind,e);
        if(kind==136){
            if(!effect_arena(e->exp0,recvxGameBootEffectVlistBytes(e->exp0?(int *)e->exp0:NULL)))
                recvxGameBootUnsupported("invalid afterimage draw snapshot");
        }
        if(kind==143){context(e->exp0,sizeof(P_WRK));if(((P_WRK *)e->exp0)->num<0||((P_WRK *)e->exp0)->num>48)recvxGameBootUnsupported("invalid dust particle count");}
        if(kind==219)snow_context(e);
        if(kind==221 && e->tvp!=(NJS_TEXTURE_VTX *)e->pv)
            recvxGameBootUnsupported("invalid debris draw vertices");
        return;
    }
    if(kind==114){
        if(e->tvp!=(NJS_TEXTURE_VTX *)e->pv &&
           !effect_arena(e->tvp,4*sizeof(NJS_TEXTUREH_VTX)))
            recvxGameBootUnsupported("invalid debris draw vertices");
        return;
    }
    if(kind==3){
        if(e->type>5 || (e->tvp!=e->tv&&!effect_arena(e->tvp,sizeof(e->tv))) ||
           (e->mtx!=(NJS_MATRIX *)e->mtxbuf&&!effect_arena(e->mtx,sizeof(NJS_MATRIX))))
            recvxGameBootUnsupported("invalid weapon effect draw buffers");
        return;
    }
    context(e->exp0,sizeof(O_WRK));
    if(kind==22||kind==24){
        if(e->ct0<1)recvxGameBootUnsupported("invalid explosion trail emission index");
        EXP_DRAW022_WORK *w=(EXP_DRAW022_WORK *)(e->exp0+4);
        for(int i=0;i<(kind==22?77:64);++i)
            if(w[i].flg&&(w[i].flg>(kind==22?1:2)||w[i].ct<0||w[i].ct>(kind==22?14:(w[i].flg==1?7:15))))
                recvxGameBootUnsupported("invalid explosion trail frame");
    }else if(kind==25||kind==27){
        EXP_EFF025_WORK *w=(EXP_EFF025_WORK *)(e->exp0+4);
        for(int i=0;i<(kind==25?8:16);++i)
            if(w[i].flg&&(w[i].ct<0||w[i].ct>10))
                recvxGameBootUnsupported("invalid explosion particle frame");
    }else if(kind==107){
        ER_WORK *w=(ER_WORK *)(e->exp0+4);
        for(int i=0;i<8;++i)if(w[i].ay<0||w[i].ay>=6)
            recvxGameBootUnsupported("invalid rain splash frame");
    }
}
void recvxGameBootEffectCallbackPrepare(unsigned int kind,O_WRK *e)
{
    if(!sys||!rom||!actor_array(e,eff,sizeof(*eff),512))
        recvxGameBootUnsupported("invalid effect callback actor");
    remaining_effect_prepare(kind,e);
    switch(kind){
    case 130:
        if(e->mode0>2||e->type>6||e->type==5||e->ct2 < -1||e->ct2>=128||
           e->ct0<0||e->ct0>=INT_MAX/2048||e->ct3==INT_MIN)
            recvxGameBootUnsupported("invalid bow projectile state");
        if(e->pv!=e->pvp||e->tv!=e->tvp)recvxGameBootUnsupported("invalid bow projectile vertices");
        recvxPlayerEnemiesPrepare();
        break;
    case 133:
        if(e->mode0>2||sys->ond_cx<0||sys->ond_cx>3)
            recvxGameBootUnsupported("invalid puzzle selector state");
        for(int i=0;i<3;++i)if(sys->ond_n[i]<0||sys->ond_n[i]>9)
            recvxGameBootUnsupported("invalid puzzle selector digit");
        break;
    case 134:
        if(e->mode0>20||e->lkono<0||!effect_arena(sys->obwp,(size_t)(e->lkono+1)*sizeof(O_WRK))||
           e->lkono>=(int)(39936u/sizeof(O_WRK)))
            recvxGameBootUnsupported("invalid crane puzzle object");
        break;
    case 135:
        if(e->mode0>1||e->ct0<0||e->ct0>5)
            recvxGameBootUnsupported("invalid afterimage emitter mode/frame");
        if(e->mode1&&(e->mode0||(e->flg&0x80)))afterimage_model(e,e->type);
        if(e->mode0&&e->mode1&&!effect_arena(e->exp0,1))
            recvxGameBootUnsupported("invalid afterimage emitter snapshot");
        break;
    case 136:
        if(e->mode0>1||e->ct0<0||e->ct0>5||e->flr_no<0||e->flr_no>5)
            recvxGameBootUnsupported("invalid afterimage mode/frame");
        afterimage_model(e,e->mdlver);
        if(!e->mode0&&(e->type>=512||
           eff[e->type].id!=135||!effect_arena(eff[e->type].exp0,1)))
            recvxGameBootUnsupported("invalid afterimage emitter link");
        break;
    case 137:
        if(e->mode0>10||!isfinite(e->xn)||!isfinite(e->yn)||e->ct0<0||e->ct0>=INT_MAX/2)
            recvxGameBootUnsupported("invalid monitor puzzle state");
        break;
    case 138:
        if(e->mode0>11||(e->mode0&& (e->ct0<0||e->ct0>2||e->ct1<0||e->ct1==INT_MAX)))
            recvxGameBootUnsupported("invalid oil puzzle state");
        for(int i=0;i<4;++i)if(e->hide[i]>3)recvxGameBootUnsupported("invalid oil puzzle LED");
        break;
    case 143:
        if(e->mode0>2||e->mode1>1)recvxGameBootUnsupported("invalid dust particle mode");
        if(e->mode0){context(e->exp0,sizeof(P_WRK));if(((P_WRK *)e->exp0)->num<0||((P_WRK *)e->exp0)->num>48)recvxGameBootUnsupported("invalid dust particle count");}
        break;
    case 131:case 144:case 184:case 185:case 186:case 188:case 189:case 190:
    case 208:case 209:case 210:case 211:case 212:case 213:case 216:case 217:case 218:case 219:case 221:
        if(e->mode0>1||!isfinite(e->sx)||!isfinite(e->sy)||!isfinite(e->sz)||
           (e->mode0&&(e->ct0<0||e->ct0>=INT_MAX/2)))
            recvxGameBootUnsupported("invalid environmental effect mode/counter");
        if(e->mode0&&((kind==185&&e->ct0>=4)||(kind==189&&e->ct0>=10)||
           (kind==209&&e->ct0>=21)||(kind==210&&e->ct0>=20)||(kind==211&&e->ct0>=10)||
           (kind==212&&e->ct0>=16)||(kind==213&&e->ct0>=3)||
           (kind==216&&e->ct0>=(e->type%3==2?11:15))))
            recvxGameBootUnsupported("invalid environmental animation frame");
        if(kind==217){if(e->type>=3)recvxGameBootUnsupported("invalid dust color variant");if(e->mode0)context(e->exp0,sizeof(P_WRK));}
        if(kind==219){
            if((e->type||e->mode1)&&((unsigned int)((e->type?e->type:e->mode1)-1)%3==2)&&e->sx<=0)recvxGameBootUnsupported("invalid snow density/area");
            if(e->mode0&&(e->type||e->mode1))snow_context(e);
        }
        if(kind==221&&(e->mdlver>=55||((sys->st_flg&0x2000000u)&&sys->ef_slow<=0)))
            recvxGameBootUnsupported("invalid debris frame/slowdown");
        break;
    case 220:
        if((e->type||e->mode1)&&(e->lkono<1||e->lkono>512))
            recvxGameBootUnsupported("invalid debris burst count");
        break;
    case 191:case 193:case 195:case 196:case 198:case 200:case 201:case 202:case 203:case 204:case 205:case 206:case 207:
        if(e->mode0>(kind==205?2:1))recvxGameBootUnsupported("invalid late sprite mode");
        if(!isfinite(e->sx)||!isfinite(e->sy)||!isfinite(e->sz))
            recvxGameBootUnsupported("invalid late sprite scale");
        if(e->mode0&&(e->ct0<0||e->ct0>INT_MAX/24-1))
            recvxGameBootUnsupported("invalid late sprite frame counter");
        if((kind==191&&e->mode0&&e->ct0>=20)||
           (kind==193&&(e->ct1<0||e->ct1>=15))||
           (kind==200&&e->mode0&&e->ct0>=8)||
           (kind==201&&e->mode0&&e->ct0>=16)||
           (kind==205&&e->mode0&&e->ct0>=(e->mode0==1?14:2))||
           (kind==207&&e->mode0&&e->ct0>=24))
            recvxGameBootUnsupported("invalid late sprite animation frame");
        if(kind==195&&e->type>=3)recvxGameBootUnsupported("invalid late sprite variant");
        if(kind==193&&(!isfinite(e->spd)||e->sy>=2147483520.0f||e->sy<0))
            recvxGameBootUnsupported("invalid late sprite duration");
        if(kind==196&&e->mode0&&e->ct0>=8)
            recvxGameBootUnsupported("invalid late sprite animation frame");
        if(kind==203){
            if(!actor_array(e->exp0,eff,sizeof(*eff),512))
                recvxGameBootUnsupported("invalid linked particle emitter");
            if(e->type<1||e->type>2||e->mdlver>=4||e->sz<=0||e->sz>429496704.0f||
               60.0/e->sz>2147483520.0||(e->mode0&&e->ct1<1))
                recvxGameBootUnsupported("invalid linked particle lifetime/variant");
        }
        if(kind==205&&(e->sz<0||e->sz>=2147483520.0f||e->ct1<0||e->ct1==INT_MAX))
            recvxGameBootUnsupported("invalid late sprite interval");
        break;
    case 192:
        if(e->lkono<0||e->lkono>512)recvxGameBootUnsupported("invalid burst particle count");
        break;
    case 145:case 1460:case 148:
        if((kind!=1460&&e->mode0>1)||(kind==148&&e->mode1>2))
            recvxGameBootUnsupported("invalid linked eye/drop effect mode");
        if((kind==1460 || (kind==148&&!e->mode1)) &&
           e->mtx!=(NJS_MATRIX *)e->mtxbuf&&!effect_arena(e->mtx,sizeof(NJS_MATRIX)))
            recvxGameBootUnsupported("invalid linked eye/drop matrix");
        if(e->lkwkp){O_WRK link=*e;link.flg|=0x80;recvxGameBootEffectValidate(&link);}
        break;
    case 146:
        if(e->mode0>2||e->ct3==INT_MIN)
            recvxGameBootUnsupported("invalid eye particle state");
        break;
    case 149:
        if(e->mode0>6 || (e->mode0>=1&&e->mode0<=4&&(e->frm_no<0||e->frm_no>=9)) ||
           (e->mode0==2&&e->ct3==INT_MIN))
            recvxGameBootUnsupported("invalid fire sprite state");
        break;
    case 151:if(e->mode0>1)recvxGameBootUnsupported("invalid sprite animation mode");break;
    case 152:if(e->ct1<0||e->ct1>=12)recvxGameBootUnsupported("invalid impact emitter phase");break;
    case 153:if(e->mode0>2)recvxGameBootUnsupported("invalid impact sprite mode");break;
    case 264:case 1001:
        if(e->mode0>1)recvxGameBootUnsupported("invalid fire spark mode");
        if(e->mode0||kind==1001){
            context(e->exp0,sizeof(P_WRK));P_WRK *p=(P_WRK *)e->exp0;
            if(p->num<0||p->num>48)recvxGameBootUnsupported("invalid fire spark particle count");
        }
        break;
    case 171:case 173:case 176:case 178:case 180:case 182:
        if(e->mode0>1)recvxGameBootUnsupported("invalid sprite animation mode");
        if(kind==173&&e->type>=100)
            recvxGameBootUnsupported("invalid sprite blend selector");
        if(kind==182&&(e->type>=100||!isfinite(e->sz)))
            recvxGameBootUnsupported("invalid smoke color or velocity selector");
        if(kind==171&&e->ct1==INT_MIN)
            recvxGameBootUnsupported("invalid sprite repeat counter");
        if(kind==178&&(e->ct1<0||e->ct1==INT_MAX))
            recvxGameBootUnsupported("invalid timed sprite lifetime");
        break;
    case 179:case 181:
        if(!isfinite(e->sz)||e->sz<0||e->sz>2147483520.0f)
            recvxGameBootUnsupported("invalid effect emitter interval");
        break;
    case 155:case 157:case 159:case 161:case 163:case 167:case 169:
        if(e->mode0>1)recvxGameBootUnsupported("invalid sprite animation mode");
        if(kind==163 && (!isfinite(e->sz)||e->sz < -32767 || e->sz>32767 ||
           e->ct1 < -32767 || e->ct1>32767))
            recvxGameBootUnsupported("invalid bubble animation repeat count");
        break;
    case 164:case 165:case 170:
        if(e->ct0<0 || e->ct0>65535)
            recvxGameBootUnsupported("invalid effect emitter countdown");
        break;
    case 111:
        if(sys->ef_flg&1u)break;
        if(e->mode0>3||sys->ren_n<0||sys->ren_n>4||sys->ef_ct<0||sys->ef_ct>=450||
           e->id>=450||e->idx>=512||
           (e->pvp!=e->pv&&!effect_arena(e->pvp,4*sizeof(NJS_TEXTUREH_VTX))))
            recvxGameBootUnsupported("invalid monitor effect state");
        break;
    case 113:if(e->type>2)recvxGameBootUnsupported("invalid debris emitter type");break;
    case 114:
        if(e->mode0>1||e->mdlver>=55||
           (e->pvp!=e->pv&&!effect_arena(e->pvp,4*sizeof(NJS_TEXTUREH_VTX)))||
           ((sys->st_flg&0x2000000u)&&sys->ef_slow<=0))
            recvxGameBootUnsupported("invalid debris effect state");break;
    case 117:if(e->mode0>2)recvxGameBootUnsupported("invalid linked flame mode");break;
    case 118:
        if(e->mode0>1 || (e->mode0&&(e->ct3<0||e->ct3>8)))
            recvxGameBootUnsupported("invalid linked flame emitter state");
        break;
    case 120:{
        O_WRK *p=(O_WRK *)e->lkwkp;
        if(!p || (p!=(O_WRK *)plp && !actor_array(p,ene,sizeof(*ene),128) &&
           !actor_array(p,eff,sizeof(*eff),512) &&
           !actor_array(p,sys->obwp,sizeof(O_WRK),39936u/sizeof(O_WRK)) &&
           !actor_array(p,sys->itwp,sizeof(O_WRK),39936u/sizeof(O_WRK))))
            recvxGameBootUnsupported("effect links an invalid actor");
        if(!p->mlwP || (!actor_array(p->mlwP,p->mdl,sizeof(ML_WORK),16)&&
           !effect_arena(p->mlwP,sizeof(ML_WORK))) || p->mlwP->obj_num<12 ||
           p->mlwP->obj_num>512 ||
           !effect_arena(p->mlwP->objP,p->mlwP->obj_num*sizeof(NJS_CNK_OBJECT)))
            recvxGameBootUnsupported("invalid UV scroll actor nodes");
        break;}
    case 122:if(e->mode1>=3)recvxGameBootUnsupported("invalid selector effect UV frame");break;
    case 127:
        if(e->type==1&&e->mode0&&e->mode1)recvxPlayerPose(plp,5);
        break;
    case 128:if(e->mdlver>1)recvxGameBootUnsupported("invalid gas particle color selector");break;
    case 3:if(e->type>5||e->mode0>2||(e->tvp!=e->tv&&!effect_arena(e->tvp,sizeof(e->tv))))
        recvxGameBootUnsupported("invalid weapon effect state");break;
    case 5:{
        static const unsigned char frames[]={14,12,13};
        if(e->type>2||(e->mode0&&(e->ct0<0||e->ct0>=frames[e->type])))
            recvxGameBootUnsupported("invalid explosion animation state");break;}
    case 6:if(!e->mode0&&e->type&&(e->flr_no<0||e->flr_no>=8))
        recvxGameBootUnsupported("invalid flame UV selector");break;
    case 13:if(e->mode0&& (e->ct1 < -1||e->ct1>=128))
        recvxGameBootUnsupported("invalid projectile target");break;
    case 17:
        if(!effect_arena(e->lkwkp,sizeof(O_WRK)) &&
           !actor_array(e->lkwkp,ene,sizeof(*ene),128) && e->lkwkp!=(unsigned char *)plp)
            recvxGameBootUnsupported("invalid linked wall effect actor");
        if((((O_WRK *)e->lkwkp)->stflg&0x1000000u) &&
           (((O_WRK *)e->lkwkp)->ct0<0||((O_WRK *)e->lkwkp)->ct0>=sys->mwal_n))
            recvxGameBootUnsupported("invalid linked wall effect index");break;
    case 22:case 24:case 25:case 27:case 100:case 107:
        if(e->mode0)context(e->exp0,sizeof(O_WRK));break;
    case 29:if(!effect_arena(e->lkwkp,2*sizeof(int)))
        recvxGameBootUnsupported("invalid effect completion link");break;
    case 85:
        if(e->mode0 && (e->mode1==1||e->mode1==2)){
            if(e->type>=rom->eff_n||!rom->effp||e->id>=450||sys->ef_tn[e->id]<0||
               (unsigned int)sys->ef_tn[e->id]>=sys->ef_tlist.nbTexture)
                recvxGameBootUnsupported("invalid texture swap effect placement");
            EF_WRK *p=&rom->effp[e->type];
            if(p->lkflg==2){
                if(p->lkno>=128||!ene[p->lkno].mlwP||!ene[p->lkno].mlwP->texP||
                   ene[p->lkno].mlwP->texP->nbTexture<4)
                    recvxGameBootUnsupported("invalid texture swap actor list");
                recvxGameBootTextureGet(ene[p->lkno].mlwP->texP,3);
            }
        }break;
    case 108:
        if((sys->st_flg&0x40u)&&!(sys->gm_flg&1u)){
            if(sys->wt_maxx<=0||sys->wt_maxz<=0||sys->wt_zp<sys->wt_maxz||
               sys->wt_maxx>4096||sys->wt_zp>4096||
               !effect_arena(sys->wt_wvp,(size_t)sys->wt_maxx*sys->wt_zp*sizeof(float))||
               !isfinite(e->car)||e->car<0||e->car>32767)
                recvxGameBootUnsupported("invalid water ripple buffer");
        }break;
    case 119:if(e->type>1)recvxGameBootUnsupported("invalid smoke animation type");break;
    case 132:if(e->type>3)recvxGameBootUnsupported("invalid spark animation type");break;
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
    case 26:
        if(e->mode0>1||e->ct0<0||e->ct0>=32768||e->ct1<0||e->ct1>32||
            (e->mode0&&(!isfinite(e->sz)||e->sz<=0)))recvxGameBootUnsupported("invalid timer expiry effect state");
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
