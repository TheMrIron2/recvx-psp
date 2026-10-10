#ifndef RECVX_PSP_TIMING_H
#define RECVX_PSP_TIMING_H

#include <stdint.h>

#define RECVX_GAME_TICKS_PER_SECOND 30u
#define RECVX_GAME_MAX_CATCHUP_TICKS 4u

typedef struct RecvxGameClock {
    uint64_t last_time_us;
    uint64_t total_ticks;
    uint64_t dropped_ticks;
    uint32_t phase;
} RecvxGameClock;

void recvxGameClockReset(RecvxGameClock *clock, uint64_t now_us);
uint32_t recvxGameClockAdvance(RecvxGameClock *clock, uint64_t now_us);

#endif
