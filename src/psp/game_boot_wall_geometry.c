#include <math.h>
#include <stdint.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/hitchk.h"
#include "ps2/veronica/prog/main.h"

static int aligned(const void *pointer)
{
    return pointer && !((uintptr_t)pointer & 3u);
}

static int point3(const NJS_POINT3 *point)
{
    return aligned(point) && isfinite(point->x) && isfinite(point->y) && isfinite(point->z);
}

static int point2(const NJS_POINT2 *point)
{
    return aligned(point) && isfinite(point->x) && isfinite(point->y);
}

void recvxGameBootWallBoxPrepare(const ATR_WORK *wall, const NJS_POINT3 *position,
                               float radius, float height)
{
    /* Original triangle fallback creates a partial ATR_WORK on its stack.
       Only these six geometry fields are initialized or consumed by bhCheckBox. */
    if (!aligned(wall) || !point3(position) || !rom || !isfinite(rom->h) ||
        !isfinite(wall->px) || !isfinite(wall->py) || !isfinite(wall->pz) ||
        !isfinite(wall->w) || !isfinite(wall->h) || !isfinite(wall->d) ||
        !isfinite(radius) || !isfinite(height) || radius < 0 || height < 0 ||
        !isfinite(wall->px + wall->w + radius) || !isfinite(wall->pz + wall->d + radius) ||
        !isfinite(wall->py + (wall->h ? wall->h : rom->h)) ||
        !isfinite(position->y + height))
        recvxGameBootUnsupported("invalid original wall box geometry");
}

void recvxGameBootWallTrianglePrepare(const ATR_WORK *wall, const NJS_POINT3 *position,
                                    float radius, float height)
{
    recvxGameBootWallBoxPrepare(wall, position, radius, height);
    if (wall->id > 3 || !wall->w || !wall->d)
        recvxGameBootUnsupported("invalid original wall triangle geometry");
}

void recvxGameBootWallQuadPrepare(const NJS_POINT2 *position, const NJS_POINT2 *a,
                                const NJS_POINT2 *b, const NJS_POINT2 *c, const NJS_POINT2 *d)
{
    if (!point2(position) || !point2(a) || !point2(b) || !point2(c) || !point2(d))
        recvxGameBootUnsupported("invalid original wall quadrilateral geometry");
}

void recvxGameBootWallLinePrepare(const NJS_POINT3 *point, const NJS_LINE *line,
                                const NJS_POINT3 *closest)
{
    float length_squared;
    if (!point3(point) || !aligned(line) || (closest && !aligned(closest)) ||
        !isfinite(line->px) || !isfinite(line->py) || !isfinite(line->pz) ||
        !isfinite(line->vx) || !isfinite(line->vy) || !isfinite(line->vz))
        recvxGameBootUnsupported("invalid original wall projection geometry");
    length_squared = line->vx * line->vx + line->vy * line->vy + line->vz * line->vz;
    if (!(length_squared > 0) || !isfinite(length_squared))
        recvxGameBootUnsupported("invalid original wall projection direction");
}
