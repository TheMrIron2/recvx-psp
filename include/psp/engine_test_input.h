#ifndef RECVX_PSP_ENGINE_TEST_INPUT_H
#define RECVX_PSP_ENGINE_TEST_INPUT_H

#include "psp/platform.h"

enum {
    RECVX_ENGINE_TEST_INPUT_START_TICK = 12,
    RECVX_ENGINE_TEST_INPUT_PHASE_TICKS = 12,
    RECVX_ENGINE_TEST_INPUT_PHASE_COUNT = 4,
    RECVX_ENGINE_TEST_INPUT_TOTAL_TICKS =
        RECVX_ENGINE_TEST_INPUT_PHASE_TICKS *
        RECVX_ENGINE_TEST_INPUT_PHASE_COUNT
};

enum RecvxEngineTestInputResult {
    RECVX_ENGINE_TEST_INPUT_ERROR = -1,
    RECVX_ENGINE_TEST_INPUT_BAD_PHASE = -2,
    RECVX_ENGINE_TEST_INPUT_NEUTRAL = 0,
    RECVX_ENGINE_TEST_INPUT_ACTIVE = 1
};

int recvxEngineTestInputSample(uint32_t game_tick, RecvxInputState *input,
                               uint32_t *phase_out);

#endif
