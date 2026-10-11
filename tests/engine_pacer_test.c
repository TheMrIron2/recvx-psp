#include <assert.h>
#include <stdint.h>

#include "psp/engine_pacer.h"

int main(void)
{
    RecvxEnginePacer pacer;
    uint32_t elapsed;

    recvxEnginePacerInit(&pacer, 100);
    assert(recvxEnginePacerAdvance(&pacer, 100, &elapsed) ==
           RECVX_ENGINE_PACER_WAIT && elapsed == 0);
    assert(recvxEnginePacerAdvance(&pacer, 101, &elapsed) ==
           RECVX_ENGINE_PACER_WAIT && elapsed == 1);
    assert(recvxEnginePacerAdvance(&pacer, 102, &elapsed) ==
           RECVX_ENGINE_PACER_TICK && elapsed == 1);
    assert(recvxEnginePacerAdvance(&pacer, 103, &elapsed) ==
           RECVX_ENGINE_PACER_WAIT && elapsed == 1);
    assert(recvxEnginePacerAdvance(&pacer, 104, &elapsed) ==
           RECVX_ENGINE_PACER_TICK && elapsed == 1);
    assert(pacer.ticks == 2 && pacer.total_vblanks == 4);

    assert(recvxEnginePacerAdvance(&pacer, 120, &elapsed) ==
           RECVX_ENGINE_PACER_STALL && elapsed == 16);
    assert(pacer.ticks == 2 && pacer.stalls == 1 && pacer.phase == 0);
    assert(recvxEnginePacerAdvance(&pacer, 121, &elapsed) ==
           RECVX_ENGINE_PACER_WAIT && elapsed == 1);
    assert(recvxEnginePacerAdvance(&pacer, 122, &elapsed) ==
           RECVX_ENGINE_PACER_TICK && elapsed == 1);
    assert(pacer.ticks == 3 && pacer.total_vblanks == 22);

    recvxEnginePacerRebase(&pacer, UINT32_MAX);
    assert(recvxEnginePacerAdvance(&pacer, 0, &elapsed) ==
           RECVX_ENGINE_PACER_WAIT && elapsed == 1);
    assert(recvxEnginePacerAdvance(&pacer, 1, &elapsed) ==
           RECVX_ENGINE_PACER_TICK && elapsed == 1);

    recvxEnginePacerRebase(&pacer, 500);
    assert(recvxEnginePacerAdvance(&pacer, 501, &elapsed) ==
           RECVX_ENGINE_PACER_WAIT);
    recvxEnginePacerRebase(&pacer, 900);
    assert(recvxEnginePacerAdvance(&pacer, 901, &elapsed) ==
           RECVX_ENGINE_PACER_WAIT);
    assert(recvxEnginePacerAdvance(&pacer, 902, &elapsed) ==
           RECVX_ENGINE_PACER_TICK);
    assert(pacer.ticks == 5 && pacer.phase == 0);
    return 0;
}
