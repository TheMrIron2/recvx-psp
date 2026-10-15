#include <limits.h>
#include <math.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/main.h"

static int span(const void *pointer, size_t size)
{
    uintptr_t first = (uintptr_t)freemem, end = (uintptr_t)sys->endp;
    uintptr_t address = (uintptr_t)pointer;
    return pointer && !(address & 3u) && address >= first && address <= end && size <= end - address;
}

static void actor_prepare(BH_PWORK *actor)
{
    uintptr_t address = (uintptr_t)actor, first = (uintptr_t)ene;
    if (!actor || (actor != plp && (address < first || address - first >= sizeof(ene) ||
        (address - first) % sizeof(*ene))))
        recvxGameBootUnsupported("invalid original wall actor");
    if (!isfinite(actor->px) || !isfinite(actor->py) || !isfinite(actor->pz) ||
        !isfinite(actor->ar) || actor->ar < 0)
        recvxGameBootUnsupported("invalid original wall actor dimensions");
}

static int point_valid(const NJS_POINT3 *point)
{
    return point && !((uintptr_t)point & 3u) &&
        isfinite(point->x) && isfinite(point->y) && isfinite(point->z);
}

static void table_prepare(const ATR_WORK *table, int count)
{
    if (count < 0 || count > INT_MAX - 64 ||
        (size_t)count > SIZE_MAX / sizeof(*table) ||
        (count && !span(table, (size_t)count * sizeof(*table))))
        recvxGameBootUnsupported("invalid original wall collision table");
}

static void record_prepare(const ATR_WORK *record, float radius, int triangle)
{
    if (!(record->flg & 1u)) return;
    if (!isfinite(record->px) || !isfinite(record->py) || !isfinite(record->pz) ||
        !isfinite(record->w) || !isfinite(record->h) || !isfinite(record->d) ||
        !isfinite(record->px + record->w + radius) ||
        !isfinite(record->py + (record->h ? record->h : rom->h)) ||
        !isfinite(record->pz + record->d + radius))
        recvxGameBootUnsupported("invalid original wall collision geometry");
    if (triangle && (record->type == 4 || record->type == 5) &&
        (record->id > 3 || !record->w || !record->d))
        recvxGameBootUnsupported("invalid original triangle wall");
}

static void tables_prepare(void)
{
    if (!sys || !rom || !plp || sys->mwal_n < 0 || sys->mwal_n > 64 ||
        sys->metc_n < 0 || sys->metc_n > 64 || !isfinite(rom->h))
        recvxGameBootUnsupported("invalid original dynamic wall count or room");
    table_prepare(rom->walp, rom->wal_n);
    table_prepare(rom->etcp, rom->etc_n);
}

void recvxGameBootWallPrepare(BH_PWORK *actor, NJS_POINT3 *current,
                             NJS_POINT3 *previous, float radius, float height)
{
    int i;
    tables_prepare();
    actor_prepare(actor);
    if (!point_valid(current) || !point_valid(previous) || !isfinite(radius) || radius < 0 ||
        !isfinite(height) || height < 0 || !isfinite(2.0f * radius) ||
        !isfinite(current->y + height))
        recvxGameBootUnsupported("invalid original wall collision query");
    if ((actor->flg & 0x100u) && (actor->flr_no < -2 || actor->flr_no > 29))
        recvxGameBootUnsupported("invalid original wall ground floor");
    if ((actor->flg & 0x100u) && !isfinite(rom->grand[actor->flr_no + 2]))
        recvxGameBootUnsupported("invalid original wall ground height");
    for (i = 0; i < rom->wal_n; ++i) record_prepare(&rom->walp[i], radius, 1);
    for (i = 0; i < sys->mwal_n; ++i) record_prepare(&sys->mwalp[i], radius, 1);
}

void recvxGameBootWallLedgePrepare(BH_PWORK *actor)
{
    int i, valid = 0;
    tables_prepare(); actor_prepare(actor);
    if (!isfinite(4.0f * actor->ar) ||
        !isfinite(actor->px - actor->ar) || !isfinite(actor->px + actor->ar) ||
        !isfinite(actor->pz - actor->ar) || !isfinite(actor->pz + actor->ar))
        recvxGameBootUnsupported("invalid original ledge collision radius");
    for (i = 0; i < rom->etc_n; ++i) {
        record_prepare(&rom->etcp[i], actor->ar, 0);
        if (actor->dan_ap == &rom->etcp[i]) valid = 1;
    }
    for (i = 0; i < sys->metc_n; ++i) {
        record_prepare(&sys->metcp[i], actor->ar, 0);
        if (actor->dan_ap == &sys->metcp[i]) valid = 1;
    }
    if (actor->dan_ap && !valid)
        recvxGameBootUnsupported("invalid original ledge collision reference");
}

ATR_WORK *recvxGameBootWallLedgeAllocate(void)
{
    if (sys->mwal_n >= 64)
        recvxGameBootUnsupported("original temporary ledge walls exceed capacity");
    ++sys->dla_n;
    return &sys->mwalp[sys->mwal_n++];
}

void recvxGameBootWallLedgeGeometryPrepare(const ATR_WORK *ledge, float radius)
{
    if (!isfinite(ledge->px - 2.5f * radius) || !isfinite(ledge->pz - 2.5f * radius) ||
        !isfinite(ledge->px + ledge->w + 0.5f * radius) ||
        !isfinite(ledge->pz + ledge->d + 0.5f * radius) ||
        !isfinite(ledge->w + 4.0f * radius) || !isfinite(ledge->d + 4.0f * radius))
        recvxGameBootUnsupported("invalid original temporary ledge geometry");
}
