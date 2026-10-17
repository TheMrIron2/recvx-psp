#include <math.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/game_boot_save_controllers.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/ranking.h"
#include "ps2/veronica/prog/message.h"
#include "ps2/veronica/prog/bup_00.h"
#include "ps2/veronica/prog/ps2_SaveScreen.h"
#include "ps2/veronica/prog/ps2_LoadScreen.h"

extern RANK_WORK rkw;
extern SAVE_SCREEN Save, *pSave;
extern LOAD_SCREEN Load, *pLoad;

static int arena(const void *pointer, size_t bytes)
{
    uintptr_t first = (uintptr_t)freemem, last = (uintptr_t)sys->endp;
    uintptr_t address = (uintptr_t)pointer;
    return pointer && first <= last && address >= first && address <= last && bytes <= last-address;
}

void recvxGameBootRankingScorePrepare(void)
{
    if (!sys || (sys->gm_mode != 0 && sys->gm_mode != 3) ||
        (!sys->costume && (sys->ply_id < 0 || sys->ply_id >= 4)))
        recvxGameBootUnsupported("invalid original ranking score selectors");
}

void recvxGameBootRankingPrepare(int stage)
{
    if (!sys || sys->typ_md0 >= 6 || stage < 0 || stage > 3)
        recvxGameBootUnsupported("invalid original ranking dispatch");
    if (stage == 2 && (rkw.mode_01 < 0 || rkw.mode_01 > 3 || rkw.mesloop > 4))
        recvxGameBootUnsupported("invalid original ranking presentation state");
    if (stage == 3 && (rkw.mode_00 < 0 || rkw.mode_00 > 8 ||
            !arena(sys->memp, 0) || (!sys->costume && (sys->ply_id < 0 || sys->ply_id >= 4))))
        recvxGameBootUnsupported("invalid original ranking texture state");
}

void recvxGameBootRankingNumberPrepare(const NJS_POINT2 *position, int number)
{
    if (!position || !isfinite(position->x) || !isfinite(position->y) || number < 0 || number > 99)
        recvxGameBootUnsupported("invalid original ranking two-digit number");
}

void recvxGameBootTypewriterPrepare(int stage)
{
    if (!sys || sys->typ_md0 >= 3 || stage < 0 || stage > 4 ||
        !arena(sys->typ_exp, sizeof(TYP_WORK)) || ((uintptr_t)sys->typ_exp & 3u))
        recvxGameBootUnsupported("invalid original typewriter dispatch/work");
    const TYP_WORK *work = sys->typ_exp;
    if (work->mode_00 < 0 || work->mode_00 > 9 || sys->typ_md1 > 2)
        recvxGameBootUnsupported("invalid original typewriter phase");
    if ((stage == 1 || stage == 2 || stage == 4) && (!rom || !arena(sys->memp, 0)))
        recvxGameBootUnsupported("invalid original typewriter texture arena");
    if (stage == 2 && !(sys->ss_flg & 0x200u) &&
            (!plp || sys->ply_id < 0 || sys->ply_id >= 4))
        recvxGameBootUnsupported("invalid original typewriter player snapshot");
    if (stage == 4 && !arena(sys->sbs_sp, 0))
        recvxGameBootUnsupported("invalid original typewriter saved arena");
    if (stage == 3 && ((sys->ss_flg & 0x200u) ? pLoad != &Load : pSave != &Save))
        recvxGameBootUnsupported("invalid original typewriter save/load screen");
}

void recvxGameBootSaveMessagePrepare(void)
{
    size_t bytes = recvxGameBootSourceRemaining(sys->mes_ip);
    uint32_t offset;
    if (!sys->mes_ip || ((uintptr_t)sys->mes_ip & 3u) || bytes < 8)
        recvxGameBootUnsupported("invalid original save-screen message header");
    memcpy(&offset, sys->mes_ip, sizeof(offset));
    if ((offset & 3u) || offset > bytes - 8)
        recvxGameBootUnsupported("invalid original save-screen message tables");
}

/* The checkpoint is the retained original save payload at the arena base. */
void recvxGameBootRankingRestorePrepare(void)
{
    recvxGameBootCheckpointPrepare();
    unsigned int version;
    memcpy(&version, freemem, sizeof(version));
    size_t player_offset = offsetof(SYS_WORK, ply_id) - offsetof(SYS_WORK, version);
    if (version != 10 || freemem[player_offset] >= 4)
        recvxGameBootUnsupported("invalid original ranking checkpoint payload");
}

void recvxGameBootRankingTimePrepare(const NJS_POINT2 *position, unsigned int flags, int time, float priority)
{
    if (!sys || !position || !isfinite(position->x) || !isfinite(position->y) ||
        !isfinite(priority) || !isfinite(FontScaleX) || FontScaleX < 0 ||
        (flags & ~15u) || time < 0)
        recvxGameBootUnsupported("invalid original ranking time display");
}
