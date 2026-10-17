#include <math.h>
#include <stdint.h>
#include "psp/game_boot_effect_thl.h"
#include "psp/game_boot_render.h"
#include "psp/engine_original_fault.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/ps2_NaDraw.h"

void recvxGameBootRenderHalfFogTriangle(const RecvxGameChunkVertex input[3], unsigned int offset_argb, int trans);

static int span(const void *pointer, const void *base, size_t bytes, size_t needed)
{
    uintptr_t p=(uintptr_t)pointer, first=(uintptr_t)base;
    return pointer && base && !(p&3u) && p>=first && p-first<=bytes && needed<=bytes-(p-first);
}
static int arena(const void *pointer, size_t size)
{
    uintptr_t first=(uintptr_t)freemem, end=sys?(uintptr_t)sys->endp:0;
    return end>=first && span(pointer,freemem,end-first,size);
}
static int member(const void *pointer,const void *base,size_t count,size_t stride)
{
    uintptr_t p=(uintptr_t)pointer, first=(uintptr_t)base;
    return pointer && base && p>=first && p-first<count*stride && !((p-first)%stride);
}
static int actor(const O_WRK *object)
{
    return object && (object==(O_WRK *)plp || member(object,ene,128,sizeof(*ene)) ||
        member(object,eff,512,sizeof(*eff)) ||
        (member(object,sys->obwp,39936u/sizeof(*object),sizeof(*object)) && arena(object,sizeof(*object))) ||
        (member(object,sys->itwp,39936u/sizeof(*object),sizeof(*object)) && arena(object,sizeof(*object))));
}
static void vertices(const NJS_TEXTUREH_VTX *v,int count)
{
    if(!v || ((uintptr_t)v&3u)) recvxGameBootUnsupported("invalid half-lit effect vertices");
    for(int i=0;i<count;++i)
        if(!isfinite(v[i].x)||!isfinite(v[i].y)||!isfinite(v[i].z)||!isfinite(v[i].u)||!isfinite(v[i].v))
            recvxGameBootUnsupported("invalid half-lit effect coordinate");
}
void recvxGameBootEffectThlCheckMath(void)
{
    if(recvxEngineOriginalFault())recvxGameBootUnsupported(recvxEngineOriginalFault());
}
void recvxGameBootEffectThlPrepare(O_WRK *object)
{
    if(!sys||!actor(object))recvxGameBootUnsupported("invalid half-lit effect actor");
    if((object->flg&0x1000000u)||(object->stflg&0x1000000u)||object->tex_id<0||
       ((sys->gm_flg&0x4000u)&&(object->mdflg&0x40u)))return;
    if(object->pn<3||object->pn>4)recvxGameBootUnsupported("invalid half-lit effect vertex count");
    size_t bytes=(size_t)((object->flg&0x2000000u)?4:object->pn)*sizeof(NJS_TEXTUREH_VTX);
    /* Retail effects deliberately use the contiguous pv+tv storage as one
       larger buffer; a four-vertex HDR stream does not fit tv alone. */
    if(!span(object->tvp,object->pv,sizeof(object->pv)+sizeof(object->tv),bytes) &&
       !span(object->tvp,sys->rpb,sizeof(sys->rpb),bytes) && !arena(object->tvp,bytes))
        recvxGameBootUnsupported("invalid half-lit effect vertex span");
    vertices((const NJS_TEXTUREH_VTX *)object->tvp,object->pn);
    const float values[]={object->px,object->py,object->pz,object->sx,object->sy,object->sz};
    for(unsigned int i=0;i<sizeof(values)/sizeof(values[0]);++i)
        if(!isfinite(values[i]))recvxGameBootUnsupported("invalid half-lit effect transform");
    if(object->mtx!=(NJS_MATRIX *)object->mtxbuf&&!arena(object->mtx,sizeof(NJS_MATRIX)))
        recvxGameBootUnsupported("invalid half-lit effect matrix");
    if(!(object->stflg&0x20u)){
        if(object->tex_id>=450||object->ani_ct<0||sys->ef_tn[object->tex_id]<0||
           (unsigned int)sys->ef_tn[object->tex_id]>=sys->ef_tlist.nbTexture||
           (unsigned int)object->ani_ct>=sys->ef_tlist.nbTexture-(unsigned int)sys->ef_tn[object->tex_id])
            recvxGameBootUnsupported("invalid half-lit effect texture animation");
    }else{
        const NJS_TEXLIST *list=object->txp[0];
        if(!arena(list,sizeof(*list))||object->ani_ct<0||(unsigned int)object->tex_id>=list->nbTexture||
           (unsigned int)object->ani_ct>=list->nbTexture-(unsigned int)object->tex_id)
            recvxGameBootUnsupported("invalid half-lit effect private texture animation");
    }
    if((object->bl_src||object->bl_dst)&&
       (object->bl_src<2||object->bl_src>11||object->bl_dst<2||object->bl_dst>11))
        recvxGameBootUnsupported("invalid half-lit effect blend");
    if((object->flg&0x80u)&&!(object->flg&0x200000u)){
        const O_WRK *parent=(const O_WRK *)object->lkwkp;
        if(!actor(parent)||(!member(parent->mlwP,parent->mdl,16,sizeof(ML_WORK))&&
           !member(parent->mlwP,sys->efm,450,sizeof(ML_WORK))))
            recvxGameBootUnsupported("invalid half-lit effect parent model");
        if(!parent->mlwP->obj_num||parent->mlwP->obj_num>512||object->lkono<0||
           (unsigned int)object->lkono>=parent->mlwP->obj_num||
           !arena(parent->mlwP->owP,parent->mlwP->obj_num*sizeof(O_WORK))||
           !isfinite(object->lox)||!isfinite(object->loy)||!isfinite(object->loz))
            recvxGameBootUnsupported("invalid half-lit effect parent joint");
    }
    if(object->flg&0x2000000u){
        const float axis[]={cam.vx,cam.vy,cam.vz};
        for(unsigned int i=0;i<3;++i)
            if(!isfinite(axis[i])||fabsf(axis[i])>2)
                recvxGameBootUnsupported("invalid half-lit effect camera direction");
    }
}

void njDrawTexture3DHEx(NJS_TEXTUREH_VTX *input,Int count,Int trans)
{
    RecvxGameChunkVertex v[4];
    if(count<3||count>4||(trans!=0&&trans!=1))
        recvxGameBootUnsupported("invalid half-lit texture submission");
    vertices(input,count);
    for(int i=0;i<count;++i)
        v[i]=(RecvxGameChunkVertex){.x=input[i].x,.y=input[i].y,.z=input[i].z,
            .u=input[i].u,.v=input[i].v,.color=input[i].bcol};
    /* Retail uses the first ocol RGB for the GS fog color and a fixed FOG=128
       for every vertex: mix(offset RGB, texture*base RGB, 128/256).
       Alpha comes from texture*base alpha and is unaffected by offset alpha. */
    for(int i=2;i<count;++i){
        int a=i-2,b=i-1;if(i&1){int swap=a;a=b;b=swap;}
        const RecvxGameChunkVertex triangle[3]={v[a],v[b],v[i]};
        recvxGameBootRenderHalfFogTriangle(triangle,input[0].ocol,trans);
    }
}
