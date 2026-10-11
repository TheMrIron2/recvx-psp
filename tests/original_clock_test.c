#include <assert.h>
#include <string.h>

#include "ps2/veronica/prog/types.h"
#include "ps2/veronica/prog/flag.h"
#include "ps2/veronica/prog/main.h"
#include "psp/original_clock.h"

SYS_WORK test_sys;
SYS_WORK *sys = &test_sys;

int main(void)
{
    memset(&test_sys, 0, sizeof(test_sys));
    recvxOriginalVsyncTimeAdvance(10);
    assert(sys->time == 0);

    sys->tk_flg = 0x80;
    sys->gm_flg = 0x80000000;
    sys->time = 40;
    recvxOriginalVsyncTimeAdvance(3);
    assert(sys->time == 43);
    sys->time = 216000000 - 2;
    recvxOriginalVsyncTimeAdvance(16);
    assert(sys->time == 216000000);

    sys->vsyc_ct = 59;
    sys->gfrm_ct = 31;
    recvxOriginalVsyncCallbackCounters();
    assert(sys->vsyc_flg == 1 && sys->vsyc_ct == 0);
    assert(sys->gframe == 31 && sys->gfrm_ct == 0);

    sys->sp_flg = 0x200;
    sys->evt_tim = 10;
    recvxOriginalVsyncCallbackCounters();
    assert(sys->evt_tim == 8);

    sys->evt_tim = 4;
    sys->evt_tmd = 1;
    recvxOriginalVsyncCallbackCounters();
    assert(sys->evt_tmd == 0 && sys->evt_tim == 0);
    return 0;
}
