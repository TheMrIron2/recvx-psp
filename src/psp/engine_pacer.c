#include <stddef.h>
#include <string.h>

#include "psp/engine_pacer.h"

#define RECVX_ENGINE_VBLANKS_PER_TICK 2u

void recvxEnginePacerInit(RecvxEnginePacer *pacer, uint32_t vblank_count)
{
    if (pacer == NULL)
        return;
    memset(pacer, 0, sizeof(*pacer));
    pacer->last_vblank = vblank_count;
}

void recvxEnginePacerRebase(RecvxEnginePacer *pacer, uint32_t vblank_count)
{
    if (pacer == NULL)
        return;
    pacer->last_vblank = vblank_count;
    pacer->phase = 0;
}

enum RecvxEnginePacerResult recvxEnginePacerAdvance(
    RecvxEnginePacer *pacer, uint32_t vblank_count, uint32_t *elapsed_vblanks)
{
    uint32_t elapsed;

    if (elapsed_vblanks != NULL)
        *elapsed_vblanks = 0;
    if (pacer == NULL)
        return RECVX_ENGINE_PACER_STALL;

    elapsed = vblank_count - pacer->last_vblank;
    pacer->last_vblank = vblank_count;
    if (elapsed_vblanks != NULL)
        *elapsed_vblanks = elapsed;
    if (elapsed == 0)
        return RECVX_ENGINE_PACER_WAIT;

    pacer->total_vblanks += elapsed;
    if (elapsed > RECVX_ENGINE_VBLANKS_PER_TICK) {
        pacer->phase = 0;
        pacer->stalls++;
        return RECVX_ENGINE_PACER_STALL;
    }

    pacer->phase += elapsed;
    if (pacer->phase < RECVX_ENGINE_VBLANKS_PER_TICK)
        return RECVX_ENGINE_PACER_WAIT;

    pacer->phase -= RECVX_ENGINE_VBLANKS_PER_TICK;
    pacer->ticks++;
    return RECVX_ENGINE_PACER_TICK;
}
