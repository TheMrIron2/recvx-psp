#include <math.h>
#include <stdint.h>
#include "ps2/veronica/prog/ps2_NaTextureFunction.h"
#include "psp/game_boot_effect_sprites.h"
#include "psp/game_boot_render.h"
#include "psp/engine_original_fault.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/ps2_NaDraw.h"
#include "ps2/veronica/prog/ps2_NaMatrix.h"

static int polygon_trans, texture_trans;

static int arena(const void *pointer, size_t size)
{
    uintptr_t p = (uintptr_t)pointer, first = (uintptr_t)freemem;
    uintptr_t end = sys ? (uintptr_t)sys->endp : 0;
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
        member(object, sys->obwp, 39936u / sizeof(O_WRK), sizeof(O_WRK)) ||
        member(object, sys->itwp, 39936u / sizeof(O_WRK), sizeof(O_WRK)));
}
void recvxGameBootEffectSpriteCheckMath(void)
{
    if (recvxEngineOriginalFault()) recvxGameBootUnsupported(recvxEngineOriginalFault());
}
void recvxGameBootEffectSpritePrepare(O_WRK *object, int textured)
{
    if (!sys || !actor(object)) recvxGameBootUnsupported("invalid 3D effect actor");
    if ((object->flg & 0x1000000u) || (object->stflg & 0x1000000u) ||
        ((sys->gm_flg & 0x4000u) && (object->mdflg & 0x40u)) ||
        (textured && object->tex_id < 0)) return;
    if (textured == 1 && object->id == 9 && object->type == 2 && !(sys->sp_flg & 8u)) {
        if (!plp) recvxGameBootUnsupported("missing 3D effect player visibility state");
        if ((plp->stflg & 0x1000000u) || (plp->mdflg & 1u)) return;
    }
    const float values[] = {object->px, object->py, object->pz, object->sx, object->sy, object->sz};
    for (unsigned int i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
        if (!isfinite(values[i])) recvxGameBootUnsupported("invalid 3D effect transform");
    if (object->mtx != (NJS_MATRIX *)object->mtxbuf && !arena(object->mtx, sizeof(NJS_MATRIX)))
        recvxGameBootUnsupported("invalid 3D effect matrix");
    int special = (textured == 1 && (object->id == 128 || object->id == 353 || object->id == 123)) ||
        (textured == 2 && object->id == 204);
    int capacity = special ? 256 : 21;
    if (object->pn < 3 || object->pn > capacity)
        recvxGameBootUnsupported("invalid 3D effect vertex count");
    if (textured) {
        if (!object->tvp || (object->tvp == object->tv ? object->pn > 4 :
            !arena(object->tvp, (size_t)object->pn * sizeof(*object->tvp))))
            recvxGameBootUnsupported("invalid textured 3D effect vertices");
        if (!(object->flg & 0x800000u)) {
            if (!(object->stflg & 0x20u)) {
                if (object->tex_id >= 450 || object->ani_ct < 0 || sys->ef_tn[object->tex_id] < 0 ||
                    (unsigned int)sys->ef_tn[object->tex_id] >= sys->ef_tlist.nbTexture ||
                    (unsigned int)object->ani_ct >= sys->ef_tlist.nbTexture - (unsigned int)sys->ef_tn[object->tex_id])
                    recvxGameBootUnsupported("invalid 3D effect texture animation");
            } else {
                NJS_TEXLIST *list = object->txp[0];
                if ((list != &sys->ef_tlist && list != &sys->ren_tlist && !arena(list, sizeof(*list))) ||
                    object->ani_ct < 0 || (unsigned int)object->tex_id >= list->nbTexture ||
                    (unsigned int)object->ani_ct >= list->nbTexture - (unsigned int)object->tex_id)
                    recvxGameBootUnsupported("invalid 3D effect private texture animation");
            }
        }
    } else if (!object->pvp || (object->pvp == object->pv ? object->pn > 4 :
        !arena(object->pvp, (size_t)object->pn * sizeof(*object->pvp))))
        recvxGameBootUnsupported("invalid untextured 3D effect vertices");
    if ((object->flg & 0x80u) && !(object->flg & 0x200000u)) {
        const O_WRK *parent = (const O_WRK *)object->lkwkp;
        if (!actor(parent) || (!member(parent->mlwP, parent->mdl, 16, sizeof(ML_WORK)) &&
            !member(parent->mlwP, sys->efm, 450, sizeof(ML_WORK))))
            recvxGameBootUnsupported("invalid 3D effect parent model");
        if (!parent->mlwP->obj_num || parent->mlwP->obj_num > 512 || object->lkono < 0 ||
            (unsigned int)object->lkono >= parent->mlwP->obj_num ||
            !arena(parent->mlwP->owP, parent->mlwP->obj_num * sizeof(O_WORK)) ||
            !isfinite(object->lox) || !isfinite(object->loy) || !isfinite(object->loz))
            recvxGameBootUnsupported("invalid 3D effect parent joint");
    }
}

/* Scalar form of the original njGetPlaneNormal2, preserving winding. */
void recvxGameBootEffectSpriteNormal(NJS_POINT3 *a, NJS_POINT3 *b, NJS_POINT3 *c, NJS_VECTOR *normal)
{
    normal->x = (b->y-a->y)*(c->z-b->z) - (b->z-a->z)*(c->y-b->y);
    normal->y = (b->z-a->z)*(c->x-b->x) - (b->x-a->x)*(c->z-b->z);
    normal->z = (b->x-a->x)*(c->y-b->y) - (b->y-a->y)*(c->x-b->x);
}

static void submit(const void *data, int count, int textured, int trans, int clipping)
{
    RecvxGameChunkVertex vertices[256];
    if (!data || ((uintptr_t)data & 3u) || count < 3 || count > (clipping ? 256 : 21) ||
        (trans != 0 && trans != 1)) recvxGameBootUnsupported("invalid original 3D sprite submission");
    int behind = 0;
    for (int i = 0; i < count; ++i) {
        if (textured) {
            const NJS_TEXTURE_VTX *t = &((const NJS_TEXTURE_VTX *)data)[i];
            vertices[i] = (RecvxGameChunkVertex){.x=t->x, .y=t->y, .z=t->z,
                .color=t->col, .u=t->u, .v=t->v};
        } else {
            const NJS_POLYGON_VTX *p = &((const NJS_POLYGON_VTX *)data)[i];
            vertices[i] = (RecvxGameChunkVertex){.x=p->x, .y=p->y, .z=p->z, .color=p->col};
        }
        if (!isfinite(vertices[i].x) || !isfinite(vertices[i].y) || !isfinite(vertices[i].z) ||
            !isfinite(vertices[i].u) || !isfinite(vertices[i].v))
            recvxGameBootUnsupported("non-finite original 3D sprite vertex");
        if (textured && !clipping) {
            NJS_POINT3 input = {vertices[i].x, vertices[i].y, vertices[i].z}, point;
            njCalcPoint(NULL, &input, &point); recvxGameBootEffectSpriteCheckMath();
            if (point.z < 0) behind = 1;
        }
    }
    if (behind) return;
    for (int i = 2; i < count; ++i) {
        int a = i-2, b = i-1;
        if (i & 1) { int swap = a; a = b; b = swap; }
        RecvxGameChunkVertex triangle[3] = {vertices[a], vertices[b], vertices[i]};
        recvxGameBootRenderSpriteTriangle(triangle, textured, trans);
    }
}
void njDrawPolygonSub3D(NJS_POLYGON_VTX *p, int count, int flag) { submit(p, count, 0, flag, 0); }
void njDrawPolygon3DExStart(Int trans) { polygon_trans = trans; }
void njDrawPolygon3DExSetData(NJS_POLYGON_VTX *p, Int count) { njDrawPolygonSub3D(p, count, polygon_trans); }
void njDrawPolygon3DExEnd(void) { }
void njDrawPolygon3DEx(NJS_POLYGON_VTX *p, Int count, Int trans)
{ njDrawPolygon3DExStart(trans); njDrawPolygon3DExSetData(p, count); }
void njDrawTextureSub3D(NJS_TEXTURE_VTX *p, int count, int flag) { submit(p, count, 1, flag, 0); }
void njDrawTextureSub3D1P(NJS_TEXTURE_VTX *p, int count, int flag) { submit(p, count, 1, flag, 1); }
void njDrawTexture3DExStart(Int trans) { texture_trans = trans; }
void njDrawTexture3DExSetData(NJS_TEXTURE_VTX *p, Int count) { njDrawTextureSub3D(p, count, texture_trans); }
void njDrawTexture3DExSetData1P(NJS_TEXTURE_VTX *p, int count) { njDrawTextureSub3D1P(p, count, texture_trans); }
void njDrawTexture3DExEnd(void) { }
void njDrawTexture3DEx(NJS_TEXTURE_VTX *p, Int count, Int trans)
{ njDrawTexture3DExStart(trans); njDrawTexture3DExSetData(p, count); }
void njDrawTexture3DEx1P(NJS_TEXTURE_VTX *p, int count, int trans)
{ njDrawTexture3DExStart(trans); njDrawTexture3DExSetData1P(p, count); }

/* Native GU billboards replacing the PS2 particle packet writer. */
static unsigned int particle_color;
static int particle_trans,particle_active;
void njPtclSpriteStart(Sint32 texture,Uint32 color,Sint32 flag)
{
    if(flag<0||flag>1||particle_active)recvxGameBootUnsupported("invalid particle sprite begin");
    njSetTextureNum((Uint32)texture);
    particle_color=color;particle_trans=flag;particle_active=1;
}
void njPtclSpriteEnd(void){particle_active=0;}
void njPtclDrawSprite(NJS_POINT3 *points,Sint32 count,Float width,Float height)
{
    if(!particle_active||!points||count<0||count>256||!isfinite(width)||!isfinite(height))
        recvxGameBootUnsupported("invalid particle sprite data");
    NJS_MATRIX saved;
    njGetMatrix(&saved);
    for(int i=0;i<count;++i){
        NJS_POINT3 center;njCalcPoint(&saved,&points[i],&center);
        recvxGameBootEffectSpriteCheckMath();
        if(center.z<=0)continue;
        RecvxGameChunkVertex q[4]={
            {.x=center.x-width,.y=center.y-height,.z=center.z,.u=0,.v=0,.color=particle_color},
            {.x=center.x+width,.y=center.y-height,.z=center.z,.u=1,.v=0,.color=particle_color},
            {.x=center.x-width,.y=center.y+height,.z=center.z,.u=0,.v=1,.color=particle_color},
            {.x=center.x+width,.y=center.y+height,.z=center.z,.u=1,.v=1,.color=particle_color}};
        njUnitMatrix(NULL);
        RecvxGameChunkVertex a[3]={q[0],q[1],q[2]},b[3]={q[2],q[1],q[3]};
        recvxGameBootRenderSpriteTriangle(a,1,particle_trans);
        recvxGameBootRenderSpriteTriangle(b,1,particle_trans);
        njSetMatrix(NULL,&saved);
    }
}

#include "psp/engine_original_view.h"
/* PS2 polygon particles have a fixed size in projected screen pixels. */
static unsigned int particle_polygon_color;
static int particle_polygon_active;
void njPtclPolygonStart(Uint32 color){particle_polygon_color=color;particle_polygon_active=1;}
void njPtclPolygonEnd(void){particle_polygon_active=0;}
void njPtclDrawPolygon(NJS_POINT3 *points,Sint32 count,Float half_size)
{
    float distance=recvxEngineOriginalViewProjectionDistance(),aspect_x,aspect_y;
    recvxEngineOriginalViewAspect(&aspect_x,&aspect_y);
    if(!particle_polygon_active||!points||count<0||count>256||!isfinite(half_size)||
        distance<=0||aspect_x<=0||aspect_y<=0)
        recvxGameBootUnsupported("invalid polygon particle data/projection");
    NJS_MATRIX saved;njGetMatrix(&saved);
    for(int i=0;i<count;++i){
        NJS_POINT3 center;njCalcPoint(&saved,&points[i],&center);recvxGameBootEffectSpriteCheckMath();
        if(center.z<=0)continue;
        float w=half_size*center.z/(distance*aspect_x),h=half_size*center.z/(distance*aspect_y);
        RecvxGameChunkVertex q[4]={
            {.x=center.x-w,.y=center.y-h,.z=center.z,.color=particle_polygon_color},
            {.x=center.x+w,.y=center.y-h,.z=center.z,.color=particle_polygon_color},
            {.x=center.x-w,.y=center.y+h,.z=center.z,.color=particle_polygon_color},
            {.x=center.x+w,.y=center.y+h,.z=center.z,.color=particle_polygon_color}};
        njUnitMatrix(NULL);
        RecvxGameChunkVertex a[3]={q[0],q[1],q[2]},b[3]={q[2],q[1],q[3]};
        recvxGameBootRenderSpriteTriangle(a,0,1);recvxGameBootRenderSpriteTriangle(b,0,1);
        njSetMatrix(NULL,&saved);
    }
}
