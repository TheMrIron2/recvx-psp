#include <limits.h>
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
void recvxGameBootEffectCustomDrawPrepare(unsigned int kind,O_WRK *e)
{
    if(!sys||!actor_array(e,eff,sizeof(*eff),512))
        recvxGameBootUnsupported("invalid custom effect draw actor");
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
    switch(kind){
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
