#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ps2/veronica/prog/types.h"
#include <ninja.h>

#include "ps2/veronica/prog/pad.h"
#include "psp/engine_pacer.h"
#include "psp/engine_pad_adapter.h"
#include "psp/engine_test_input.h"
#include "psp/original_clock.h"

SYS_WORK engine_test_sys;
SYS_WORK *sys = &engine_test_sys;
int pd_port;
static int unexpected_original_pad_call;

void njFogDisable(void) { unexpected_original_pad_call = 1; }
void njSetBackColor(Uint32 col1, Uint32 col2, Uint32 col3)
{
    unexpected_original_pad_call = 1;
    (void)col1;
    (void)col2;
    (void)col3;
}
void bhReturnTitle(void) { unexpected_original_pad_call = 1; }
void bhSetScreenFade(unsigned int argb, float ct)
{
    unexpected_original_pad_call = 1;
    (void)argb;
    (void)ct;
}
void RequestAllStopSoundEx(int adx_flag, int sound_flag, int fade_count)
{
    unexpected_original_pad_call = 1;
    (void)adx_flag;
    (void)sound_flag;
    (void)fade_count;
}
int bhDispMessageEx(float px, float py, float pri, int mes_type, int mes_index,
                    unsigned int argb, int count)
{
    unexpected_original_pad_call = 1;
    (void)px;
    (void)py;
    (void)pri;
    (void)mes_type;
    (void)mes_index;
    (void)argb;
    (void)count;
    return -1;
}
float bhMesLen(unsigned short *text)
{
    unexpected_original_pad_call = 1;
    (void)text;
    return 0.0f;
}
Float njSin(Angle angle)
{
    unexpected_original_pad_call = 1;
    (void)angle;
    return 0.0f;
}

static void check_original_analog_directions(void)
{
    static const struct {
        unsigned char x, y;
        uint32_t pad;
    } phases[RECVX_ENGINE_TEST_INPUT_PHASE_COUNT] = {
        {128, 0, 0x42},
        {0, 128, 0x08},
        {128, 255, 0x21},
        {255, 128, 0x04}
    };
    RecvxInputState input;
    uint32_t phase_counts[RECVX_ENGINE_TEST_INPUT_PHASE_COUNT] = {0};
    uint32_t tick;
    uint32_t previous_pad = 0;
    uint32_t phase = 0;

    memset(&engine_test_sys, 0, sizeof(engine_test_sys));
    sys->keytype = 0;
    pd_port = 0;
    recvxEnginePadAdapterReset();
    assert(recvxEngineTestInputSample(0, NULL, NULL) ==
           RECVX_ENGINE_TEST_INPUT_ERROR);
    assert(recvxEngineTestInputSample(UINT32_MAX, &input, &phase) ==
           RECVX_ENGINE_TEST_INPUT_NEUTRAL);
    assert(phase == UINT32_MAX);
    for (tick = 0;
         tick <= RECVX_ENGINE_TEST_INPUT_START_TICK +
             RECVX_ENGINE_TEST_INPUT_TOTAL_TICKS;
         ++tick) {
        uint32_t phase = UINT32_MAX;
        uint32_t expected_pad = 0;
        int active = recvxEngineTestInputSample(tick, &input, &phase);

        assert(active >= 0);
        if (active) {
            assert(phase < RECVX_ENGINE_TEST_INPUT_PHASE_COUNT);
            assert(input.analog_x == phases[phase].x);
            assert(input.analog_y == phases[phase].y);
            expected_pad = phases[phase].pad;
            ++phase_counts[phase];
        } else {
            assert(phase == UINT32_MAX);
            assert(input.analog_x == 128 && input.analog_y == 128);
        }
        assert(input.buttons == 0 && input.pressed == 0 &&
               input.released == 0 && input.repeat == 0);
        assert(recvxEnginePadAdapterUpdate(&input) == 0);
        bhSetPad();
        assert(sys->pad_on == expected_pad);
        assert(sys->pad_ps == (expected_pad & ~previous_pad));
        previous_pad = expected_pad;
    }
    for (tick = 0; tick < RECVX_ENGINE_TEST_INPUT_PHASE_COUNT; ++tick)
        assert(phase_counts[tick] == RECVX_ENGINE_TEST_INPUT_PHASE_TICKS);
    assert(unexpected_original_pad_call == 0);
    puts("Original pad axis schedule: PASS (48 active ticks, four directions)");
}

int main(void)
{
    static const uint32_t vblanks[] = {101, 102, 103, 104, 120, 121, 122};
    RecvxInputState input;
    const NJS_PERIPHERAL *peripheral;
    const uint32_t all_psp_buttons = (1u << 12) - 1u;

    memset(&input, 0, sizeof(input));
    recvxEnginePadAdapterReset();
    input.buttons = all_psp_buttons;
    input.analog_x = 255;
    input.analog_y = 0;
    assert(recvxEnginePadAdapterUpdate(&input) == 0);
    peripheral = njGetPeripheral(0);
    assert(peripheral != NULL);
    assert(peripheral->on == 0xfcf9);
    assert(peripheral->press == 0xfcf9 && peripheral->release == 0);
    assert(peripheral->x1 == 127 && peripheral->y1 == -128);
    assert(peripheral->l == 255 && peripheral->r == 255);
    assert(njGetPeripheral(1) == NULL);

    assert(recvxEnginePadAdapterUpdate(&input) == 0);
    assert(peripheral->on == 0xfcf9 && peripheral->press == 0 &&
           peripheral->release == 0);

    input.buttons = RECVX_BUTTON_CROSS;
    input.analog_x = 128;
    input.analog_y = 128;
    assert(recvxEnginePadAdapterUpdate(&input) == 0);
    assert(peripheral->on == 0x4000 && peripheral->press == 0 &&
           peripheral->release == 0xbcf9);
    assert(peripheral->x1 == 0 && peripheral->y1 == 0);

    input.buttons |= RECVX_BUTTON_TRIANGLE;
    assert(recvxEnginePadAdapterUpdate(&input) == 0);
    assert(peripheral->on == 0x5000 && peripheral->press == 0x1000 &&
           peripheral->release == 0);

    input.buttons = 0;
    assert(recvxEnginePadAdapterUpdate(&input) == 0);
    assert(peripheral->on == 0 && peripheral->release == 0x5000);
    assert(recvxEnginePadAdapterUpdate(NULL) == -1);

    check_original_analog_directions();

    {
        RecvxEnginePacer pacer;
        uint32_t on[3], pressed[3], released[3], elapsed, i;
        unsigned int samples = 0;

        memset(&engine_test_sys, 0, sizeof(engine_test_sys));
        sys->tk_flg = 0x80;
        sys->gm_flg = 0x80000000;
        sys->sp_flg = 0x200;
        sys->evt_tim = 10;
        recvxEnginePadAdapterReset();
        recvxEnginePacerInit(&pacer, 100);
        for (i = 0; i < sizeof(vblanks) / sizeof(vblanks[0]); ++i) {
            enum RecvxEnginePacerResult result = recvxEnginePacerAdvance(
                &pacer, vblanks[i], &elapsed);
            recvxOriginalEngineClockAdvance(
                elapsed, result == RECVX_ENGINE_PACER_TICK);
            if (result == RECVX_ENGINE_PACER_TICK) {
                input.buttons = samples < 2 ? RECVX_BUTTON_CROSS : 0;
                assert(recvxEnginePadAdapterUpdate(&input) == 0);
                peripheral = njGetPeripheral(0);
                on[samples] = peripheral->on;
                pressed[samples] = peripheral->press;
                released[samples] = peripheral->release;
                ++samples;
            }
        }
        assert(pacer.ticks == 3 && pacer.stalls == 1 && samples == 3);
        assert(sys->time == 22 && sys->vsyc_flg == 3 && sys->evt_tim == 4);
        assert(on[0] == 0x4000 && pressed[0] == 0x4000 && released[0] == 0);
        assert(on[1] == 0x4000 && pressed[1] == 0 && released[1] == 0);
        assert(on[2] == 0 && pressed[2] == 0 && released[2] == 0x4000);
    }
    return 0;
}
