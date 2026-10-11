#ifndef RECVX_PSP_LIVE_TASK_DISPATCH_H
#define RECVX_PSP_LIVE_TASK_DISPATCH_H

#include <stdint.h>

typedef void (*RecvxLiveTaskProc)();

uint32_t recvxRunLiveTaskSlots(const volatile unsigned int *active_mask,
                               const volatile unsigned int *paused_mask,
                               const RecvxLiveTaskProc *jump_table,
                               unsigned int slot_count);

#endif
