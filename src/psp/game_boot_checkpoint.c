#include <stddef.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/main.h"

#define CHECKPOINT_BYTES (offsetof(SYS_WORK, save_end) - offsetof(SYS_WORK, version))
_Static_assert(CHECKPOINT_BYTES == 0x834, "original save-game payload layout changed");
static uintptr_t checkpoint_start, arena_limit;

void recvxGameBootCheckpointReset(void)
{
    checkpoint_start = (uintptr_t)freemem;
    arena_limit = (uintptr_t)sys->endp;
    keepmem = NULL; /* bhInitSystem reserves the checkpoint before installing keepmem. */
}

void recvxGameBootCheckpointPrepare(void)
{
    uintptr_t limit = (uintptr_t)sys->endp, cursor = (uintptr_t)sys->memp;
    uintptr_t retained = (uintptr_t)keepmem;
    if (!checkpoint_start || (uintptr_t)freemem != checkpoint_start ||
        limit < checkpoint_start || limit > arena_limit || CHECKPOINT_BYTES > limit - checkpoint_start ||
        retained < checkpoint_start + CHECKPOINT_BYTES || retained > limit || cursor < retained || cursor > limit)
        recvxGameBootUnsupported("invalid original checkpoint buffer reservation");
    if (plp != &ply || sys->ply_id < 0 || sys->ply_id >= 4)
        recvxGameBootUnsupported("invalid original checkpoint player");
}

void recvxGameBootCheckpointSaved(void)
{
    recvxGameBootLog("checkpoint stored: bytes=%u version=%u player=%d room=%u/%u/%u hp=%d",
        (unsigned int)CHECKPOINT_BYTES, sys->version, sys->ply_id,
        sys->stg_no, sys->rom_no, sys->rcase, sys->ply_hp[sys->ply_id]);
}
