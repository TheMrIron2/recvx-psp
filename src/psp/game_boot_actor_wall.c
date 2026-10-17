#include <float.h>
#include <limits.h>
#include <math.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/main.h"

void recvxGameBootWallPrepare(BH_PWORK *, NJS_POINT3 *, NJS_POINT3 *, float, float);

static int quantized_height(float value)
{
    float scaled = 100.0f * (0.001f + value);
    return isfinite(value) && (double)scaled >= INT_MIN && (double)scaled <= INT_MAX;
}

void recvxGameBootActorWallPrepare(BH_PWORK *actor)
{
    uintptr_t address=(uintptr_t)actor, first=(uintptr_t)ene;
    if (!actor || (actor != plp && (address < first || address-first >= sizeof(ene) ||
        (address-first) % sizeof(*ene))))
        recvxGameBootUnsupported("invalid original actor wall actor");
    NJS_POINT3 *position = (NJS_POINT3 *)&actor->px;
    recvxGameBootWallPrepare(actor, position, position, actor->ar, actor->ah);
    if (!quantized_height(actor->py))
        recvxGameBootUnsupported("invalid original actor wall quantized height");
    for (int i=0; i<32; ++i)
        if (!isfinite(rom->grand[i]))
            recvxGameBootUnsupported("invalid original actor wall floor height");
    for (int i=0; i<rom->wal_n + sys->mwal_n; ++i) {
        const ATR_WORK *record = i<rom->wal_n ? &rom->walp[i] : &sys->mwalp[i-rom->wal_n];
        if (!(record->flg & 1u)) continue;
        if (!quantized_height(record->py))
            recvxGameBootUnsupported("invalid original actor wall quantized height");
        if (record->type==6 && (record->id>3 || !record->w || !record->d ||
            !(record->h ? record->h : rom->h)))
            recvxGameBootUnsupported("invalid original actor wall slope");
        double dx = fabs((double)actor->px - record->px) + fabs((double)record->w);
        double dz = fabs((double)actor->pz - record->pz) + fabs((double)record->d);
        if (dx*dx + dz*dz > FLT_MAX)
            recvxGameBootUnsupported("invalid original actor wall distance");
    }
}

void recvxGameBootActorWallEffect(int index)
{
    if (index < 0 || index >= 512)
        recvxGameBootUnsupported("invalid original actor wall effect slot");
}
