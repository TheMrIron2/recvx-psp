#include <stddef.h>
#include <string.h>

#include "psp/input.h"

static const uint32_t repeat_buttons[4] = {
    RECVX_BUTTON_UP, RECVX_BUTTON_RIGHT,
    RECVX_BUTTON_DOWN, RECVX_BUTTON_LEFT
};

static uint64_t repeat_period_us(uint32_t ticks)
{
    return ((uint64_t)ticks * 1000000u +
            RECVX_INPUT_TICKS_PER_SECOND - 1u) /
           RECVX_INPUT_TICKS_PER_SECOND;
}

void recvxInputRepeatReset(RecvxInputRepeat *repeat)
{
    if (repeat != NULL)
        memset(repeat, 0, sizeof(*repeat));
}

void recvxInputRepeatAdvance(RecvxInputRepeat *repeat, RecvxInputState *input,
                             uint64_t now_us)
{
    uint64_t first_delay, next_delay;
    unsigned int i;
    if (repeat == NULL || input == NULL)
        return;

    input->repeat = input->pressed;
    first_delay = repeat_period_us(RECVX_INPUT_REPEAT_FIRST_TICKS);
    next_delay = repeat_period_us(RECVX_INPUT_REPEAT_NEXT_TICKS);
    for (i = 0; i < 4; ++i) {
        uint32_t button = repeat_buttons[i];
        uint64_t *deadline = &repeat->next_repeat_us[i];
        if (!(input->buttons & button)) {
            *deadline = 0;
        } else if ((input->pressed & button) ||
                   !(repeat->previous_buttons & button)) {
            *deadline = now_us + first_delay;
        } else if (now_us < repeat->previous_time_us) {
            *deadline = now_us + first_delay;
        } else if (*deadline != 0 && now_us >= *deadline) {
            uint64_t remainder = (now_us - *deadline) % next_delay;
            uint64_t advance = next_delay - remainder;
            input->repeat |= button;
            *deadline = now_us + advance;
            if (*deadline < now_us)
                *deadline = UINT64_MAX;
        }
    }
    repeat->previous_buttons = input->buttons;
    repeat->previous_time_us = now_us;
}
