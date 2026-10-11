#include <assert.h>
#include <string.h>

#include "ps2/veronica/prog/types.h"
#include <ninja.h>

#include "psp/engine_pacer.h"
#include "psp/engine_pad_adapter.h"
#include "psp/original_clock.h"

SYS_WORK engine_test_sys;
SYS_WORK *sys = &engine_test_sys;

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
