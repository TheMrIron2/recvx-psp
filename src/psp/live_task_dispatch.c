#include "psp/live_task_dispatch.h"
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
#include <pspkernel.h>
#endif

static RecvxLiveTaskStats task_stats;
RecvxLiveTaskStats recvxLiveTaskStats(void) { return task_stats; }
void recvxProfileGameStage(unsigned int stage, uint32_t elapsed_us)
{ if(stage<7)task_stats.game_us[stage]+=elapsed_us; }

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
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
        unsigned int started=sceKernelGetSystemTimeLow();
#endif
        jump_table[slot]();
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
        task_stats.us[slot]+=sceKernelGetSystemTimeLow()-started;
#endif
    }
    return unsupported;
}
