#include "../../../ps2/veronica/prog/types.h"
#include "../../../ps2/veronica/prog/pad.h"
#include "../../../ps2/veronica/prog/main.h"
#include "../../../ps2/veronica/prog/system.h"

void bhSysCallPad()
{
    if (sys->ss_flg & 0x40000000) {
        sys->sp_flg = ~0x0;
        sys->ss_flg &= ~0x40000000;
    }

    if (sys->sp_flg & 0x20)
        bhSetPad();
    else
        sys->pad_on = 0;

    if ((sys->cb_flg & 0x4) ||
        ((sys->tk_flg & 0x1000) && !(sys->ts_flg & 0x1000))) {
        sys->pad_on &= 0x1188F;
        sys->pad_ps &= 0x1188F;
    }
}
