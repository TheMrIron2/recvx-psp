#include <limits.h>
#include <math.h>
#include <stdint.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/main.h"

static int span(const void *pointer, size_t bytes)
{
    uintptr_t p = (uintptr_t)pointer, first = (uintptr_t)freemem, end = (uintptr_t)sys->endp;
    return pointer && !(p & 3u) && p >= first && p <= end && bytes <= end - p;
}

static int point(const NJS_POINT3 *p)
{
    return isfinite(p->x) && isfinite(p->y) && isfinite(p->z);
}

void recvxGameBootVisibilityPrepare(const NJS_CAPSULE *capsule, unsigned int flags)
{
    float x, y, z, length_squared;
    int i;
    if (!sys || !rom || !capsule || ((uintptr_t)capsule & 3u) ||
        !point(&capsule->c1) || !point(&capsule->c2) ||
        !isfinite(capsule->r) || capsule->r < 0)
        recvxGameBootUnsupported("invalid visibility capsule");
    x = capsule->c2.x - capsule->c1.x;
    y = capsule->c2.y - capsule->c1.y;
    z = capsule->c2.z - capsule->c1.z;
    length_squared = x*x + y*y + z*z;
    if (!(length_squared > 0) || !isfinite(length_squared))
        recvxGameBootUnsupported("invalid visibility capsule direction");
    if (rom->wal_n < 0 || rom->wal_n > INT_MAX - 64 || sys->mwal_n < 0 || sys->mwal_n > 64 ||
        (size_t)rom->wal_n > SIZE_MAX / sizeof(ATR_WORK) ||
        (rom->wal_n && !span(rom->walp, (size_t)rom->wal_n * sizeof(ATR_WORK))) ||
        !isfinite(rom->h))
        recvxGameBootUnsupported("invalid visibility wall work");
    for (i = 0; i < rom->wal_n + sys->mwal_n; ++i) {
        const ATR_WORK *wall = i < rom->wal_n ? &rom->walp[i] : &sys->mwalp[i - rom->wal_n];
        float height;
        if (!(wall->flg & 1u) || ((wall->attr & 0x2000000u) && (flags & 0x40000000u))) continue;
        if ((wall->type == 1 || wall->type == 3 || wall->type == 5) && (flags & 0x400u)) continue;
        if (wall->type != 0 && wall->type != 1 && wall->type != 2 && wall->type != 3 &&
            wall->type != 4 && wall->type != 5 && wall->type != 7) continue;
        height = wall->h ? wall->h : rom->h;
        if (!isfinite(wall->px) || !isfinite(wall->py) || !isfinite(wall->pz) ||
            !isfinite(wall->w) || !isfinite(wall->d) || !isfinite(height) ||
            !isfinite(wall->px + wall->w) || !isfinite(wall->py + height) ||
            !isfinite(wall->pz + wall->d))
            recvxGameBootUnsupported("invalid visibility wall geometry");
        if ((wall->type == 4 || wall->type == 5) &&
            (wall->id > 3 || !wall->w || !wall->d || !height))
            recvxGameBootUnsupported("invalid visibility wall triangle");
        if ((wall->type == 2 || wall->type == 3) && (!height || wall->w < 0))
            recvxGameBootUnsupported("invalid visibility wall cylinder");
    }
}
