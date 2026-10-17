#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot_effect_shadow.h"
#include "psp/game_boot_render.h"
#include "psp/game_boot_render_texture.h"
#include "psp/engine_original_view.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/ps2_NaMatrix.h"
#include "ps2/veronica/prog/ps2_NaFog.h"
#include "ps2/veronica/prog/ps2_dummy.h"

int lCnkModClipFace;
static float bounds[4], last_fog;
static int near_crossing, active, pass;
void recvxGameBootEffectShadowReset(void)
{ active=near_crossing=pass=0; last_fog=0; memset(bounds,0,sizeof(bounds)); lCnkModClipFace=0; }

static unsigned int u16(const unsigned char *p) { return p[0] | ((unsigned int)p[1]<<8); }
static float f32(const unsigned char *p) { float value; memcpy(&value,p,sizeof(value)); return value; }
static size_t remaining(const void *pointer, unsigned int alignment)
{
    uintptr_t p=(uintptr_t)pointer, first=(uintptr_t)freemem, end=sys?(uintptr_t)sys->endp:0;
    if (!pointer || !freemem || (p & (alignment-1u)) || p<first || p>end) return 0;
    size_t size=end-p, source=recvxGameBootSourceRemaining(pointer);
    return source && source<size ? source:size;
}
static int member(const void *pointer, const void *base, size_t count, size_t stride)
{
    uintptr_t p=(uintptr_t)pointer,b=(uintptr_t)base;
    return pointer && base && p>=b && p-b<count*stride && !((p-b)%stride);
}
static int actor(const O_WRK *object)
{
    return object && (object==(O_WRK *)plp || member(object,ene,128,sizeof(*ene)) ||
        member(object,eff,512,sizeof(*eff)) ||
        (member(object,sys->obwp,39936u/sizeof(O_WRK),sizeof(O_WRK)) && remaining(object,4)>=sizeof(*object)) ||
        (member(object,sys->itwp,39936u/sizeof(O_WRK),sizeof(O_WRK)) && remaining(object,4)>=sizeof(*object)));
}
static void work(const O_WRK *object)
{
    const ML_WORK *model=object->mlwP;
    if (!member(model,object->mdl,16,sizeof(*model)) && !member(model,sys->efm,450,sizeof(*model)))
        recvxGameBootUnsupported("invalid shadow effect model work");
    if (!model->obj_num || model->obj_num>512 || object->mdlver>=model->obj_num ||
        remaining(model->objP,4)<model->obj_num*sizeof(NJS_CNK_OBJECT))
        recvxGameBootUnsupported("invalid shadow effect model object");
}
void recvxGameBootEffectShadowPrepare(O_WRK *object)
{
    if (!sys || !actor(object)) recvxGameBootUnsupported("invalid shadow effect actor");
    if ((object->flg & 0x1000000u) || (object->stflg & 0x1000000u) ||
        ((sys->gm_flg&0x4000u) && (object->mdflg&0x40u))) return;
    work(object);
    const float values[]={object->px,object->py,object->pz,object->sx,object->sy,object->sz};
    for (unsigned int i=0;i<sizeof(values)/sizeof(values[0]);++i)
        if (!isfinite(values[i])) recvxGameBootUnsupported("invalid shadow effect transform");
    if (object->mtx!=(NJS_MATRIX *)object->mtxbuf && remaining(object->mtx,4)<sizeof(NJS_MATRIX))
        recvxGameBootUnsupported("invalid shadow effect matrix");
    if ((object->flg&0x80u) && !(object->flg&0x200000u)) {
        const O_WRK *parent=(const O_WRK *)object->lkwkp;
        if (!actor(parent)) recvxGameBootUnsupported("invalid shadow effect parent");
        work(parent);
        if (object->lkono<0 || (unsigned int)object->lkono>=parent->mlwP->obj_num ||
            remaining(parent->mlwP->owP,4)<parent->mlwP->obj_num*sizeof(O_WORK) ||
            !isfinite(object->lox) || !isfinite(object->loy) || !isfinite(object->loz))
            recvxGameBootUnsupported("invalid shadow effect joint");
    }
}
void Ps2ShadowStart(void)
{
    if (active) recvxGameBootUnsupported("nested original shadow pass");
    active=1; recvxGameBootRenderShadowStart();
}
void Ps2ShadowMain0(void)
{
    if (!active) recvxGameBootUnsupported("missing original shadow pass");
    bounds[0]=bounds[1]=65536; bounds[2]=bounds[3]=-65536; pass=0;
}
void Ps2ShadowMain1(void) { pass=1; }
void Ps2ShadowEnd(void) { active=0; recvxGameBootRenderShadowEnd(); }

typedef struct ShadowVertex {
    RecvxGameChunkVertex local;
    float screen_x,screen_y,fog;
} ShadowVertex;

int njCnkDrawModelLocalMod(NJS_CNK_MODEL *model)
{
    if (!active || !model || remaining(model,4)<sizeof(*model))
        recvxGameBootUnsupported("invalid shadow modifier model");
    if (!isfinite(model->r) || !isfinite(model->center.x) || !isfinite(model->center.y) || !isfinite(model->center.z))
        recvxGameBootUnsupported("invalid shadow modifier bounds");
    if ((recvxGameBootRenderStateGet()->control_flags & 0x100u) && model->r>0) {
        NJS_POINT3 center; float near,far,ax,ay;
        njCalcPoint(NULL,&model->center,&center); recvxGameBootEffectSpriteCheckMath();
        recvxEngineOriginalViewClipPlanes(&near,&far);
        if (center.z+model->r < -near || center.z-model->r > -far) return 0;
        float distance=fabsf(recvxEngineOriginalViewProjectionDistance()/center.z);
        recvxEngineOriginalViewAspect(&ax,&ay);
        if (center.x*distance+model->r*distance*ax < -320 || center.x*distance-model->r*distance*ax > 320 ||
            center.y*distance+model->r*distance*ay < -240 || center.y*distance-model->r*distance*ay > 240) return 0;
    }
    if (!model->plist) return 1;
    if (!model->vlist) recvxGameBootUnsupported("unported shared shadow vertex cache");
    const unsigned char *v=(const unsigned char *)model->vlist, *p=(const unsigned char *)model->plist;
    size_t vs=remaining(v,4), ps=remaining(p,2);
    if (vs<8 || ps<6) recvxGameBootUnsupported("invalid shadow modifier header");
    /* NJD_CV (34) stores packed XYZ triples. Type32 is SH4 XYZ/W,
       and cannot be read with the modifier routine's 12-byte stride. */
    if (v[0]!=34 || p[0]!=56) {
        recvxGameBootLog("shadow modifier header vertex=%02x:%02x size=%u first=%u count=%u polygon=%02x:%02x size=%u count=%u",
            v[0],v[1],u16(v+2),u16(v+4),u16(v+6),p[0],p[1],u16(p+2),u16(p+4));
        recvxGameBootUnsupported("unported shadow modifier command");
    }
    unsigned int first=u16(v+4), count=u16(v+6), polygons=u16(p+4)&0x3fffu, user=u16(p+4)>>14;
    size_t vertex_bytes=8u+(size_t)count*12u, polygon_bytes=6u+(size_t)polygons*(3u+user)*2u;
    if (!count || first+count>65536u || vertex_bytes>vs || (size_t)u16(v+2)*4u+4u!=vertex_bytes ||
        polygon_bytes>ps || (size_t)u16(p+2)*2u+4u < polygon_bytes)
        recvxGameBootUnsupported("invalid shadow modifier extent");
    /* Validate the complete primitive stream before submitting either pass. */
    for (unsigned int i=0;i<count;++i)
        for (unsigned int a=0;a<3;++a)
            if (!isfinite(f32(v+8+i*12+a*4))) recvxGameBootUnsupported("non-finite shadow vertex");
    for (unsigned int i=0;i<polygons;++i)
        for (unsigned int a=0;a<3;++a) {
            unsigned int index=u16(p+6+(i*(3u+user)+a)*2u);
            if (index<first || index-first>=count) recvxGameBootUnsupported("shadow triangle index exceeds vertex chunk");
        }
    ShadowVertex *vertices=malloc((size_t)count*sizeof(*vertices));
    if (!vertices) recvxGameBootUnsupported("shadow modifier allocation failed");
    float ax,ay,distance=recvxEngineOriginalViewProjectionDistance();
    recvxEngineOriginalViewAspect(&ax,&ay); near_crossing=0;
    for (unsigned int i=0;i<count;++i) {
        NJS_POINT3 local={f32(v+8+i*12),f32(v+12+i*12),f32(v+16+i*12)}, point;
        njCalcPoint(NULL,&local,&point); recvxGameBootEffectSpriteCheckMath();
        if (point.z<1) near_crossing=1;
        vertices[i].local=(RecvxGameChunkVertex){.x=local.x,.y=local.y,.z=local.z,.color=0x80202020u};
        vertices[i].screen_x=2048+ax*(point.x*(distance/point.z));
        vertices[i].screen_y=2048+ay*(point.y*(distance/point.z));
        vertices[i].fog=njCalcFogPower(point.z);
    }
    for (unsigned int i=0;i<polygons;++i) {
        ShadowVertex *t[3];
        for (unsigned int a=0;a<3;++a) {
            t[a]=&vertices[u16(p+6+(i*(3u+user)+a)*2u)-first];
            if (t[a]->screen_x<bounds[0]) bounds[0]=t[a]->screen_x;
            if (t[a]->screen_y<bounds[1]) bounds[1]=t[a]->screen_y;
            if (t[a]->screen_x>bounds[2]) bounds[2]=t[a]->screen_x;
            if (t[a]->screen_y>bounds[3]) bounds[3]=t[a]->screen_y;
        }
        float normal=(t[1]->screen_x-t[2]->screen_x)*(t[0]->screen_y-t[2]->screen_y)-
            (t[0]->screen_x-t[2]->screen_x)*(t[1]->screen_y-t[2]->screen_y);
        if ((!pass && normal>0) || (pass && normal<0)) {
            RecvxGameChunkVertex triangle[3]={t[0]->local,t[1]->local,t[2]->local};
            recvxGameBootRenderShadowTriangle(triangle,(unsigned int)pass);
        }
        last_fog=t[2]->fog;
    }
    free(vertices); return 1;
}
void njCnkModDrawModel(NJS_CNK_MODEL *model) { njCnkDrawModelLocalMod(model); }
void Ps2ShadowDraw(void)
{
    for (unsigned int i=0;i<4;++i) if (bounds[i]<1536 || bounds[i]>=2560) return;
    float rectangle[4];
    for (unsigned int i=0;i<4;++i) {
        float low=(i&1)?1808:1728, high=(i&1)?2288:2368;
        bounds[i]=fmaxf(low,fminf(high,bounds[i]));
        rectangle[i]=near_crossing?0:bounds[i]-low;
    }
    if (!isfinite(last_fog) || last_fog<0 || last_fog>255)
        recvxGameBootUnsupported("invalid original modifier fog");
    recvxGameBootRenderShadowComposite(rectangle,(unsigned int)last_fog);
}
