#include <math.h>
#include <stdint.h>
#include "psp/game_boot_effect_lines.h"
#include "psp/game_boot_render.h"
#include "psp/engine_original_fault.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/ps2_NaGraphics3D.h"

void recvxGameBootRenderLine(const RecvxGameChunkVertex input[2], int trans);

void recvxGameBootEffectLineCheckMath(void)
{
    if (recvxEngineOriginalFault()) recvxGameBootUnsupported(recvxEngineOriginalFault());
}

void recvxGameBootEffectLinePrepare(O_WRK *object)
{
    /* The common queue guard has already established actor ownership. */
    if (!sys || !object) recvxGameBootUnsupported("invalid original line effect actor");
    if ((object->flg & 0x1000000u) || (object->stflg & 0x1000000u) ||
        ((sys->gm_flg & 0x4000u) && (object->mdflg & 0x40u))) return;
    if (object->pn != 1) recvxGameBootUnsupported("invalid original line effect segment count");
    uintptr_t p = (uintptr_t)object->mtx, first = (uintptr_t)freemem, end = (uintptr_t)sys->endp;
    if (object->mtx != (NJS_MATRIX *)object->mtxbuf &&
        (!object->mtx || (p & 3u) || !freemem || end < first || p < first || p > end || sizeof(NJS_MATRIX) > end-p))
        recvxGameBootUnsupported("invalid original line effect matrix");
    const float values[] = {object->px,object->py,object->pz,object->sx,object->sy,object->sz,
        object->pv[0].x,object->pv[0].y,object->pv[0].z,object->pv[1].x,object->pv[1].y,object->pv[1].z};
    for (unsigned int i=0;i<sizeof(values)/sizeof(values[0]);++i)
        if (!isfinite(values[i])) recvxGameBootUnsupported("invalid original line effect coordinate");
    if (object->bl_src < 2 || object->bl_src > 11 || object->bl_dst < 2 || object->bl_dst > 11)
        recvxGameBootUnsupported("invalid original line effect blend");
}

void njDrawLine3D(NJS_POINT3COL *points, Int count, Uint32 attributes)
{
    RecvxGameChunkVertex vertices[10];
    if (count < 0 || count > ((attributes & 2u) ? 9 : 5))
        recvxGameBootUnsupported("invalid original 3D line segment count");
    if (attributes & 0x80000000u) recvxGameBootUnsupported("unported textured original 3D line");
    if (attributes & ~(0x40u | 2u)) recvxGameBootUnsupported("unported original 3D line attributes");
    if (!count) return;
    if (!points || !points->p || !points->col || ((uintptr_t)points->p & 3u) || ((uintptr_t)points->col & 3u))
        recvxGameBootUnsupported("invalid original 3D line vertices");
    int vertex_count = (attributes & 2u) ? count+1 : count*2;
    for (int i=0;i<vertex_count;++i) {
        NJS_POINT3 p = points->p[i];
        if (!isfinite(p.x) || !isfinite(p.y) || !isfinite(p.z))
            recvxGameBootUnsupported("invalid original 3D line coordinate");
        vertices[i] = (RecvxGameChunkVertex){.x=p.x,.y=p.y,.z=p.z,.color=points->col[i].color};
    }
    for (int i=0;i<count;++i) {
        int first = (attributes & 2u) ? i : i*2;
        RecvxGameChunkVertex pair[2] = {vertices[first],vertices[first+1]};
        recvxGameBootRenderLine(pair, (attributes & 0x40u) != 0);
    }
}

/* Ex uses the same segment count and per-endpoint colors as the public API. */
void njDrawLine3DEx(NJS_POINT3COL *points,Int count,Uint32 attributes)
{ njDrawLine3D(points,count,attributes); }
