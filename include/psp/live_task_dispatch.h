#ifndef RECVX_PSP_LIVE_TASK_DISPATCH_H
#define RECVX_PSP_LIVE_TASK_DISPATCH_H

#include <stdint.h>

typedef void (*RecvxLiveTaskProc)();
typedef struct RecvxLiveTaskStats { uint32_t us[32],game_us[7]; } RecvxLiveTaskStats;
RecvxLiveTaskStats recvxLiveTaskStats(void);
void recvxProfileGameStage(unsigned int stage, uint32_t elapsed_us);

#if defined(RECVX_GAME_BOOT) && defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
#include <pspkernel.h>
#define RECVX_PROFILE_GAME(stage, call) do { \
    uint32_t recvx_stage_started=sceKernelGetSystemTimeLow(); \
    call; recvxProfileGameStage(stage,sceKernelGetSystemTimeLow()-recvx_stage_started); \
} while(0)
#else
#define RECVX_PROFILE_GAME(stage, call) call
#endif

uint32_t recvxRunLiveTaskSlots(const volatile unsigned int *active_mask,
                               const volatile unsigned int *paused_mask,
                               const RecvxLiveTaskProc *jump_table,
                               unsigned int slot_count);

#endif
