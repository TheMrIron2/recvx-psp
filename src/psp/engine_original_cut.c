#include "ps2/veronica/prog/types.h"
#include "ps2/veronica/prog/main.h"
#include "psp/engine_original_fault.h"

// Keeps original initialization state changes and faults when camera work is requested
void bhCheckCut(int flag)
{
    sys->gm_flg &= ~0x20;
    if (sys->sp_flg & 0x40)
        recvxEngineOriginalFaultSet("original cut and camera work is unsupported in 4a");
    (void)flag;
}
