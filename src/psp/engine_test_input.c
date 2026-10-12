#include <string.h>

#include "psp/engine_test_input.h"

int recvxEngineTestInputSample(uint32_t game_tick, RecvxInputState *input,
                               uint32_t *phase_out)
{
    uint32_t phase;

    if (input == NULL)
        return RECVX_ENGINE_TEST_INPUT_ERROR;

    memset(input, 0, sizeof(*input));
    input->analog_x = 128;
    input->analog_y = 128;
    if (phase_out != NULL)
        *phase_out = UINT32_MAX;

    if (game_tick < RECVX_ENGINE_TEST_INPUT_START_TICK ||
        game_tick >= RECVX_ENGINE_TEST_INPUT_START_TICK +
            RECVX_ENGINE_TEST_INPUT_TOTAL_TICKS)
        return RECVX_ENGINE_TEST_INPUT_NEUTRAL;

    phase = (game_tick - RECVX_ENGINE_TEST_INPUT_START_TICK) /
            RECVX_ENGINE_TEST_INPUT_PHASE_TICKS;
    if (phase_out != NULL)
        *phase_out = phase;

    switch (phase) {
    case 0:
        input->analog_y = 0;
        break;
    case 1:
        input->analog_x = 0;
        break;
    case 2:
        input->analog_y = 255;
        break;
    case 3:
        input->analog_x = 255;
        break;
    default:
        return RECVX_ENGINE_TEST_INPUT_BAD_PHASE;
    }

    return RECVX_ENGINE_TEST_INPUT_ACTIVE;
}
