#ifndef RECVX_PSP_ENGINE_PACER_H
#define RECVX_PSP_ENGINE_PACER_H

#include <stdint.h>

enum RecvxEnginePacerResult {
    RECVX_ENGINE_PACER_WAIT,
    RECVX_ENGINE_PACER_TICK,
    RECVX_ENGINE_PACER_STALL
};

typedef struct RecvxEnginePacer {
    uint32_t last_vblank;
    uint32_t total_vblanks;
    uint64_t ticks;
    uint32_t phase;
    uint32_t stalls;
} RecvxEnginePacer;

void recvxEnginePacerInit(RecvxEnginePacer *pacer, uint32_t vblank_count);
void recvxEnginePacerRebase(RecvxEnginePacer *pacer, uint32_t vblank_count);
enum RecvxEnginePacerResult recvxEnginePacerAdvance(
    RecvxEnginePacer *pacer, uint32_t vblank_count, uint32_t *elapsed_vblanks);

#endif
