#include "psp/original_clock.h"

#include "ps2/veronica/prog/types.h"
#include "ps2/veronica/prog/flag.h"
#include "ps2/veronica/prog/main.h"

#define RECVX_ORIGINAL_TIME_LIMIT 216000000

void recvxOriginalVsyncTimeAdvance(uint32_t vblank_count)
{
    int64_t advanced;

    if ((sys->tk_flg & 0x80) == 0 ||
        (sys->gm_flg & 0x80000000) == 0 ||
        sys->time >= RECVX_ORIGINAL_TIME_LIMIT)
        return;

    advanced = (int64_t)sys->time + vblank_count;
    if (advanced >= RECVX_ORIGINAL_TIME_LIMIT)
        sys->time = RECVX_ORIGINAL_TIME_LIMIT;
    else
        sys->time = (int)advanced;
}

void recvxOriginalVsyncCallbackCounters(void)
{
    sys->vsyc_flg++;
    sys->vsyc_ct++;

    if (sys->vsyc_ct >= 60) {
        sys->vsyc_ct = 0;
        sys->gframe = sys->gfrm_ct;
        sys->gfrm_ct = 0;
    }

    if (sys->tk_flg & 0x80) {
        if (bhCkFlg(sys->ev_flg, 3) != 0 &&
            sys->stv_tm < RECVX_ORIGINAL_TIME_LIMIT)
            sys->stv_tm++;

        if ((sys->sp_flg & 0x200) && sys->evt_tim != 0 &&
            !(sys->ss_flg & 0x80000000)) {
            sys->evt_tim -= 2;

            switch (sys->evt_tmd) {
            case 1:
                if (bhCkFlg(sys->ev_flg, 69) == 0) {
                    sys->evt_tmd = 0;
                    sys->evt_tim = 0;
                }
                break;
            case 2:
                if (bhCkFlg(sys->ev_flg, 67) == 0) {
                    sys->evt_tmd = 0;
                    sys->evt_tim = 0;
                }
                break;
            case 3:
                if (bhCkFlg(sys->ev_flg, 70) == 0) {
                    sys->evt_tmd = 0;
                    sys->evt_tim = 0;
                }
                break;
            }
        }
    }
}

void recvxOriginalEngineClockAdvance(uint32_t elapsed_vblanks,
                                     int engine_frame_presented)
{
    if (elapsed_vblanks != 0)
        recvxOriginalVsyncTimeAdvance(elapsed_vblanks);
    if (engine_frame_presented)
        recvxOriginalVsyncCallbackCounters();
}
