#include <math.h>
#include <stdint.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/hitchk.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/player.h"

static int array_member(const void *pointer, const void *base, size_t count, size_t stride)
{
    uintptr_t p = (uintptr_t)pointer, b = (uintptr_t)base;
    return p >= b && p - b < count * stride && !((p - b) % stride);
}

static int arena_span(const void *pointer, size_t bytes)
{
    uintptr_t p = (uintptr_t)pointer, first = (uintptr_t)freemem, end = (uintptr_t)sys->endp;
    return pointer && !(p & 3u) && p >= first && p <= end && bytes <= end - p;
}

static void actor_prepare(BH_PWORK *actor)
{
    if (!actor || (actor != plp && !array_member(actor, ene, 128, sizeof(*ene))))
        recvxGameBootUnsupported("invalid collision actor work");
    if (!isfinite(actor->px) || !isfinite(actor->py) || !isfinite(actor->pz) ||
        !isfinite(actor->aox) || !isfinite(actor->aoy) || !isfinite(actor->aoz) ||
        !isfinite(actor->car) || !isfinite(actor->cah) || actor->car < 0 || actor->cah < 0)
        recvxGameBootUnsupported("invalid collision actor dimensions");
}

uintptr_t recvxGameBootCollisionActorAddress(const void *actor)
{
    uintptr_t address = (uintptr_t)actor;
#if UINTPTR_MAX == UINT32_MAX
    /* Retail code compares cached and uncached aliases of the same work. */
    address &= ~(uintptr_t)0x80000000u;
#endif
    return address;
}

void recvxGameBootCollisionActorsPrepare(BH_PWORK *actor, int player_contact)
{
    int i;
    if (!sys || !plp || sys->ewk_n < 0 || sys->ewk_n > 128)
        recvxGameBootUnsupported("invalid enemy collision work count");
    actor_prepare(actor);
    actor_prepare(plp);
    if (player_contact) {
        if (!(plp->flg & 8u) && !arena_span(plp->exp0, sizeof(EXP_WORK)))
            recvxGameBootUnsupported("invalid player collision expression work");
        return;
    }
    if (actor->flg2 & 1u) return;
    for (i = 0; i < sys->ewk_n; ++i) {
        BH_PWORK *other = &ene[i];
        if ((other->flg & 9u) != 9u || (other->stflg & 0x1000000u)) continue;
        if (recvxGameBootCollisionActorAddress(actor) == recvxGameBootCollisionActorAddress(other) ||
            (recvxGameBootCollisionActorAddress(actor) == recvxGameBootCollisionActorAddress(plp) &&
             !(other->flg & 0x40u))) continue;
        actor_prepare(other);
        if (other->flg & 0x80u) {
            const void *link = other->lkwkp;
            if (!link || (link != plp && !array_member(link, ene, 128, sizeof(*ene)) &&
                !array_member(link, eff, 512, sizeof(*eff)) && !arena_span(link, sizeof(O_WRK))))
                recvxGameBootUnsupported("invalid enemy collision linked work");
        }
    }
}

static void table_prepare(const ATR_WORK *table, int count)
{
    if (count < 0 || (size_t)count > SIZE_MAX / sizeof(*table) ||
        (count && !arena_span(table, (size_t)count * sizeof(*table))))
        recvxGameBootUnsupported("invalid original room collision table");
}

void recvxGameBootCollisionFloorsPrepare(BH_PWORK *actor, int floor_query)
{
    if (!sys || !rom || !plp || sys->metc_n < 0 || sys->metc_n > 64 ||
        sys->mflr_n < 0 || sys->mflr_n > 64)
        recvxGameBootUnsupported("invalid dynamic floor collision count");
    if (actor) actor_prepare(actor);
    table_prepare(rom->etcp, rom->etc_n);
    table_prepare(rom->flrp, rom->flr_n);
    if (floor_query == 1 && !arena_span(plp->exp0, sizeof(EXP_WORK)))
        recvxGameBootUnsupported("invalid floor collision expression work");
}
