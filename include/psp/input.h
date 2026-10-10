#ifndef RECVX_PSP_INPUT_H
#define RECVX_PSP_INPUT_H

#include <stdint.h>

#include "psp/platform.h"

#define RECVX_INPUT_REPEAT_FIRST_TICKS 10u
#define RECVX_INPUT_REPEAT_NEXT_TICKS 4u
#define RECVX_INPUT_TICKS_PER_SECOND 30u

typedef struct RecvxInputRepeat {
    uint64_t next_repeat_us[4];
    uint64_t previous_time_us;
    uint32_t previous_buttons;
} RecvxInputRepeat;

void recvxInputRepeatReset(RecvxInputRepeat *repeat);
void recvxInputRepeatAdvance(RecvxInputRepeat *repeat, RecvxInputState *input,
                             uint64_t now_us);

#endif
