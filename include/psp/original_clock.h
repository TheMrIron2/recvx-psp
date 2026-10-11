#ifndef RECVX_PSP_ORIGINAL_CLOCK_H
#define RECVX_PSP_ORIGINAL_CLOCK_H

#include <stdint.h>

void recvxOriginalVsyncTimeAdvance(uint32_t vblank_count);
void recvxOriginalVsyncCallbackCounters(void);
void recvxOriginalEngineClockAdvance(uint32_t elapsed_vblanks,
                                     int engine_frame_presented);

#endif
