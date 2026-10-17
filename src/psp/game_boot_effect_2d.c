#include <math.h>
#include <stdint.h>
#include "psp/game_boot_effect_2d.h"
#include "psp/game_boot_render.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/ps2_NaDraw.h"
#include "ps2/veronica/prog/ps2_NaTextureFunction.h"

void recvxGameBootRenderScreenTriangle(const RecvxGameChunkVertex input[3],int trans);

static int span(const void *pointer,const void *base,size_t size,size_t needed)
{
    uintptr_t p=(uintptr_t)pointer,first=(uintptr_t)base;
    return pointer&&base&&!(p&3u)&&p>=first&&p-first<=size&&needed<=size-(p-first);
}
static void coordinates(const NJS_TEXTURE_VTX *v,int count)
{
    if(!v||((uintptr_t)v&3u))recvxGameBootUnsupported("invalid screen effect vertices");
    for(int i=0;i<count;++i)
        if(!isfinite(v[i].x)||!isfinite(v[i].y)||!isfinite(v[i].z)||!isfinite(v[i].u)||!isfinite(v[i].v))
            recvxGameBootUnsupported("invalid screen effect coordinate");
}
void recvxGameBootEffect2DPrepare(O_WRK *object)
{
    /* Actor identity was established by the common original queue guard. */
    if(!sys||!object)recvxGameBootUnsupported("invalid screen effect actor");
    if((object->flg&0x1000000u)||(object->stflg&0x1000000u)||object->tex_id<0||
       ((sys->gm_flg&0x4000u)&&(object->mdflg&0x40u)))return;
    if(object->pn<3||object->pn>42)recvxGameBootUnsupported("invalid screen effect vertex count");
    size_t bytes=(size_t)object->pn*sizeof(*object->tvp);
    uintptr_t first=(uintptr_t)freemem,end=(uintptr_t)sys->endp;
    if(!span(object->tvp,object->pv,sizeof(object->pv)+sizeof(object->tv),bytes)&&
       !span(object->tvp,sys->rpb,sizeof(sys->rpb),bytes)&&
       (!freemem||end<first||!span(object->tvp,freemem,end-first,bytes)))
        recvxGameBootUnsupported("invalid screen effect vertex span");
    coordinates(object->tvp,object->pn);
    if(object->bl_src<2||object->bl_src>11||object->bl_dst<2||object->bl_dst>11)
        recvxGameBootUnsupported("invalid screen effect blend");
}
void njDrawTextureSub(NJS_TEXTURE_VTX *input,Int count,Int trans)
{
    RecvxGameChunkVertex vertices[42];
    if(count<3||count>42||(trans!=0&&trans!=1))recvxGameBootUnsupported("invalid screen texture submission");
    coordinates(input,count);
    for(int i=0;i<count;++i){
        /* This mutation is present in retail njDrawTextureSub, before -1/z is
           mapped into the GS depth buffer. Preserve it for subsequent frames. */
        if(input[i].z<1)input[i].z=1;
        vertices[i]=(RecvxGameChunkVertex){.x=input[i].x,.y=input[i].y,.z=input[i].z,
            .u=input[i].u,.v=input[i].v,.color=input[i].col};
    }
    for(int i=2;i<count;++i){
        int a=i-2,b=i-1;if(i&1){int swap=a;a=b;b=swap;}
        const RecvxGameChunkVertex triangle[3]={vertices[a],vertices[b],vertices[i]};
        recvxGameBootRenderScreenTriangle(triangle,trans);
    }
}
void njDrawTexture(NJS_TEXTURE_VTX *input,Int count,Int global_index,Int trans)
{
    njSetTextureNumG((Uint32)global_index);
    njDrawTextureSub(input,count,trans);
}
