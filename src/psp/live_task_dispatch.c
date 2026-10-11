#include "psp/live_task_dispatch.h"

uint32_t recvxRunLiveTaskSlots(const volatile unsigned int *active_mask,
                               const volatile unsigned int *paused_mask,
                               const RecvxLiveTaskProc *jump_table,
                               unsigned int slot_count)
{
    uint32_t unsupported = 0;
    unsigned int slot;

    if (active_mask == 0 || paused_mask == 0 || jump_table == 0 ||
        slot_count > 32)
        return UINT32_MAX;

    for (slot = 0; slot < slot_count; ++slot) {
        uint32_t bit = UINT32_C(1) << slot;
        if ((*active_mask & bit) == 0 || (*paused_mask & bit) != 0)
            continue;
        if (jump_table[slot] == 0)
            return unsupported | bit;
        jump_table[slot]();
    }
    return unsupported;
}
