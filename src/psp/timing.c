#include <stddef.h>

#include "psp/timing.h"

#define GAME_CLOCK_PHASES_PER_SECOND 1000000u

void recvxGameClockReset(RecvxGameClock *clock, uint64_t now_us)
{
    if (clock == NULL)
        return;
    clock->last_time_us = now_us;
    clock->total_ticks = 0;
    clock->dropped_ticks = 0;
    clock->phase = 0;
}

uint32_t recvxGameClockAdvance(RecvxGameClock *clock, uint64_t now_us)
{
    uint64_t elapsed, phases, due_ticks;
    uint32_t ticks;
    if (clock == NULL)
        return 0;
    if (now_us < clock->last_time_us) {
        clock->last_time_us = now_us;
        clock->phase = 0;
        return 0;
    }
    elapsed = now_us - clock->last_time_us;
    clock->last_time_us = now_us;
    phases = (uint64_t)clock->phase + elapsed * RECVX_GAME_TICKS_PER_SECOND;
    due_ticks = phases / GAME_CLOCK_PHASES_PER_SECOND;
    clock->phase = (uint32_t)(phases % GAME_CLOCK_PHASES_PER_SECOND);
    ticks = due_ticks > RECVX_GAME_MAX_CATCHUP_TICKS ?
            RECVX_GAME_MAX_CATCHUP_TICKS : (uint32_t)due_ticks;
    clock->total_ticks += ticks;
    clock->dropped_ticks += due_ticks - ticks;
    return ticks;
}
